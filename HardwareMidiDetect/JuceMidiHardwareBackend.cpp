/**
 * @file JuceMidiHardwareBackend.cpp
 * @brief Implementation of JuceMidiHardwareBackend for ABDSharedCode.
 * @author ABDSynths
 * @date 2026
 */

#include "JuceMidiHardwareBackend.h"
#include <juce_core/juce_core.h>

namespace abd::hwid
{

void JuceMidiHardwareBackend::setTargetDevice(const juce::String& identifierOrName)
{
    targetDevice = identifierOrName;
}

juce::MidiDeviceInfo JuceMidiHardwareBackend::findMatchingOutput() const
{
    auto outputs = juce::MidiOutput::getAvailableDevices();
    if (outputs.isEmpty())
        return {};

    if (targetDevice.isNotEmpty())
    {
        for (const auto& d : outputs)
        {
            if (d.name.containsIgnoreCase(targetDevice) || d.identifier.containsIgnoreCase(targetDevice))
                return d;
        }
    }

    return outputs[0];
}

juce::MidiDeviceInfo JuceMidiHardwareBackend::findMatchingInput() const
{
    auto inputs = juce::MidiInput::getAvailableDevices();
    if (inputs.isEmpty())
        return {};

    if (targetDevice.isNotEmpty())
    {
        for (const auto& d : inputs)
        {
            if (d.name.containsIgnoreCase(targetDevice) || d.identifier.containsIgnoreCase(targetDevice))
                return d;
        }
    }

    return inputs[0];
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

    if (activeOutput == nullptr || activeOutIdentifier != dev.identifier)
    {
        activeOutput = juce::MidiOutput::openDevice(dev.identifier);
        activeOutIdentifier = dev.identifier;
    }

    if (activeOutput != nullptr)
    {
        auto msg = juce::MidiMessage::createSysExMessage(bytes.data(), static_cast<int>(bytes.size()));
        activeOutput->sendMessageNow(msg);
    }
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
        activeInput = juce::MidiInput::openDevice(dev.identifier, this);
        if (activeInput != nullptr)
        {
            activeInput->start();
        }
    }
}

void JuceMidiHardwareBackend::stopListening()
{
    if (activeInput != nullptr)
    {
        activeInput->stop();
        activeInput.reset();
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
        const int size = message.getSysExDataSize();
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
