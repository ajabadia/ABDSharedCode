/**
 * @file JuceMidiHardwareBackend.cpp
 * @brief Implementation of JuceMidiHardwareBackend for ABDSharedCode.
 * @author ABDSynths
 * @date 2026
 */

#include "JuceMidiHardwareBackend.h"

namespace abd::hwid
{

JuceMidiHardwareBackend::JuceMidiHardwareBackend(std::shared_ptr<IMidiDeviceProvider> provider)
    : deviceProvider(provider ? std::move(provider) : std::make_shared<DefaultJuceMidiDeviceProvider>())
{
}

JuceMidiHardwareBackend::~JuceMidiHardwareBackend()
{
    stopListening();
    closeOpenedOutput();
}

void JuceMidiHardwareBackend::setTargetDevice(const juce::String& identifierOrName)
{
    targetDevice = identifierOrName;
    activeOutIdentifier.clear();
    if (deviceProvider)
    {
        deviceProvider->closeOutput();
    }
}

bool JuceMidiHardwareBackend::isOutputOpen() const noexcept
{
    return deviceProvider ? deviceProvider->isOutputOpen() : false;
}

std::string JuceMidiHardwareBackend::getOpenedOutputIdentifier() const
{
    return deviceProvider ? deviceProvider->getOpenedOutputIdentifier() : std::string();
}

juce::MidiOutput* JuceMidiHardwareBackend::getOpenedOutput() noexcept
{
    return deviceProvider ? deviceProvider->getRawOutput() : nullptr;
}

void JuceMidiHardwareBackend::closeOpenedOutput() noexcept
{
    activeOutIdentifier.clear();
    if (deviceProvider)
    {
        deviceProvider->closeOutput();
    }
}

MidiEndpointOpenResult JuceMidiHardwareBackend::openStrictOutput(const std::string& identifier, const std::string& displayName)
{
    MidiEndpointSelectionRequest req;
    req.requestedStableDeviceId = identifier;
    req.requestedDisplayName    = displayName;
    return openStrictOutput(req);
}

MidiEndpointOpenResult JuceMidiHardwareBackend::openStrictOutput(const MidiEndpointSelectionRequest& request)
{
    // Explicit Model B lifecycle: Always close any prior active handle before processing
    closeOpenedOutput();

    const std::string& reqId   = request.requestedStableDeviceId;
    const std::string& reqName = request.requestedDisplayName;

    // Rule 9: If both fields are empty -> InvalidSelection
    if (reqId.empty() && reqName.empty())
    {
        lastOpenResult = MidiEndpointOpenResult::fail(
            MidiEndpointOpenOutcome::InvalidSelection,
            reqId, reqName,
            "ERR_MIDI_ENDPOINT_INVALID_SELECTION",
            "Both requestedStableDeviceId and requestedDisplayName are empty.");
        return lastOpenResult;
    }

    const auto available = deviceProvider->getAvailableOutputs();
    if (available.empty())
    {
        lastOpenResult = MidiEndpointOpenResult::fail(
            MidiEndpointOpenOutcome::RequestedEndpointUnavailable,
            reqId, reqName,
            "ERR_MIDI_ENDPOINT_UNAVAILABLE",
            "No MIDI output endpoints are available on the host.");
        return lastOpenResult;
    }

    juce::MidiDeviceInfo matchedDevice;
    MidiEndpointResolutionMethod resolutionMethod = MidiEndpointResolutionMethod::None;

    // Rule 1: StableDeviceId exact match precedence
    if (!reqId.empty())
    {
        int matchCount = 0;
        for (const auto& dev : available)
        {
            if (dev.identifier == juce::String(reqId))
            {
                matchedDevice = dev;
                matchCount++;
            }
        }

        if (matchCount == 1)
        {
            // If requestedDisplayName was also provided, verify consistency (Rule 8)
            if (!reqName.empty() && matchedDevice.name != juce::String(reqName))
            {
                lastOpenResult = MidiEndpointOpenResult::fail(
                    MidiEndpointOpenOutcome::InvalidSelection,
                    reqId, reqName,
                    "ERR_MIDI_ENDPOINT_INVALID_SELECTION",
                    "Requested display name contradicts stable device ID resolution.");
                return lastOpenResult;
            }
            resolutionMethod = MidiEndpointResolutionMethod::StableDeviceIdExact;
        }
        else if (matchCount > 1)
        {
            lastOpenResult = MidiEndpointOpenResult::fail(
                MidiEndpointOpenOutcome::RequestedEndpointAmbiguous,
                reqId, reqName,
                "ERR_MIDI_ENDPOINT_AMBIGUOUS",
                "Multiple MIDI output endpoints share the requested stable device ID: " + reqId);
            return lastOpenResult;
        }
        else
        {
            lastOpenResult = MidiEndpointOpenResult::fail(
                MidiEndpointOpenOutcome::RequestedEndpointNotFound,
                reqId, reqName,
                "ERR_MIDI_ENDPOINT_NOT_FOUND",
                "Requested stable device ID was not found: " + reqId);
            return lastOpenResult;
        }
    }
    // Rule 5: Only if stable ID is empty and display name is not empty -> DisplayName match
    else if (!reqName.empty())
    {
        int matchCount = 0;
        for (const auto& dev : available)
        {
            if (dev.name == juce::String(reqName))
            {
                matchedDevice = dev;
                matchCount++;
            }
        }

        if (matchCount == 1)
        {
            resolutionMethod = MidiEndpointResolutionMethod::DisplayNameExact;
        }
        else if (matchCount > 1)
        {
            lastOpenResult = MidiEndpointOpenResult::fail(
                MidiEndpointOpenOutcome::RequestedEndpointAmbiguous,
                reqId, reqName,
                "ERR_MIDI_ENDPOINT_AMBIGUOUS",
                "Multiple MIDI output endpoints share the requested display name: " + reqName);
            return lastOpenResult;
        }
        else
        {
            lastOpenResult = MidiEndpointOpenResult::fail(
                MidiEndpointOpenOutcome::RequestedEndpointNotFound,
                reqId, reqName,
                "ERR_MIDI_ENDPOINT_NOT_FOUND",
                "Requested display name was not found: " + reqName);
            return lastOpenResult;
        }
    }

    // Check availability prior to attempting open (SS2.1 outcome distinction)
    if (!deviceProvider->isEndpointAvailable(matchedDevice.identifier))
    {
        lastOpenResult = MidiEndpointOpenResult::fail(
            MidiEndpointOpenOutcome::RequestedEndpointUnavailable,
            reqId, reqName,
            "ERR_MIDI_ENDPOINT_UNAVAILABLE",
            "Requested endpoint is currently unavailable to open: " + matchedDevice.identifier.toStdString());
        return lastOpenResult;
    }

    // Now attempt exact opening exclusively on the matched device
    const bool opened = deviceProvider->openExactOutput(matchedDevice.identifier);
    if (!opened)
    {
        closeOpenedOutput();
        lastOpenResult = MidiEndpointOpenResult::fail(
            MidiEndpointOpenOutcome::BackendOpenFailed,
            reqId, reqName,
            "ERR_MIDI_ENDPOINT_OPEN_FAILED",
            "Backend failed to open the exactly matched endpoint: " + matchedDevice.identifier.toStdString());
        return lastOpenResult;
    }

    activeOutIdentifier = matchedDevice.identifier;

    lastOpenResult = MidiEndpointOpenResult::ok(
        resolutionMethod,
        reqId, reqName,
        matchedDevice.identifier.toStdString(),
        matchedDevice.name.toStdString());

    return lastOpenResult;
}

juce::MidiDeviceInfo JuceMidiHardwareBackend::findMatchingOutput() const
{
    const auto available = deviceProvider->getAvailableOutputs();
    if (available.empty())
        return {};

    if (targetDevice.isNotEmpty())
    {
        for (const auto& d : available)
        {
            if (d.identifier == targetDevice || d.name == targetDevice)
                return d;
        }
    }

    // Fail-closed: do NOT return outputs[0] as fallback
    return {};
}

juce::MidiDeviceInfo JuceMidiHardwareBackend::findMatchingInput() const
{
    const auto available = deviceProvider->getAvailableInputs();
    if (available.empty())
        return {};

    if (targetDevice.isNotEmpty())
    {
        for (const auto& d : available)
        {
            if (d.identifier == targetDevice || d.name == targetDevice)
                return d;
        }
    }

    // Fail-closed: do NOT return inputs[0] as fallback
    return {};
}

std::string JuceMidiHardwareBackend::getOutputPortName() const
{
    auto dev = findMatchingOutput();
    return dev.name.toStdString();
}

void JuceMidiHardwareBackend::sendBytes(const std::vector<uint8_t>& bytes)
{
    if (bytes.empty())
        return;

    auto dev = findMatchingOutput();
    if (dev.identifier.isEmpty())
        return;

    if (activeOutIdentifier != dev.identifier)
    {
        if (deviceProvider->openExactOutput(dev.identifier))
        {
            activeOutIdentifier = dev.identifier;
        }
    }

    deviceProvider->sendSysExMessage(bytes);
}

void JuceMidiHardwareBackend::setReceiveCallback(std::function<void(const std::vector<uint8_t>&)> cb)
{
    receiveCallback = std::move(cb);
}

void JuceMidiHardwareBackend::startListening()
{
    stopListening();

    auto dev = findMatchingInput();
    if (dev.identifier.isNotEmpty())
    {
        const bool inputOk = deviceProvider->openExactInput(dev.identifier, this);
        juce::ignoreUnused(inputOk);
    }
}

void JuceMidiHardwareBackend::stopListening()
{
    if (deviceProvider)
    {
        deviceProvider->closeInput();
    }
}

void JuceMidiHardwareBackend::refreshPorts()
{
}

void JuceMidiHardwareBackend::handleIncomingMidiMessage(juce::MidiInput* /*source*/, const juce::MidiMessage& message)
{
    if (message.isSysEx() && receiveCallback)
    {
        const auto* data = message.getSysExData();
        const int size   = message.getSysExDataSize();
        if (data != nullptr && size > 0)
        {
            std::vector<uint8_t> bytes(data, data + size);
            juce::MessageManager::callAsync([cb = receiveCallback, b = std::move(bytes)]() {
                if (cb)
                {
                    cb(b);
                }
            });
        }
    }
}

} // namespace abd::hwid
