/**
 * @file MidiEndpointTypes.h
 * @brief Strongly typed endpoint selection, resolution and open outcomes for ABDSharedCode.
 * @details Establishes ownership models, outcome semantics, and device abstraction
 *          for strict hardware endpoint discovery and opening without fallbacks.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <juce_audio_devices/juce_audio_devices.h>
#include "MidiEndpointSafetyPolicy.h"

namespace abd::hwid
{

/**
 * @brief Specific outcome of a requested MIDI endpoint opening.
 * 
 * Outcome Semantics:
 * - Opened: Endpoint was uniquely resolved and successfully opened by the backend.
 * - RequestedEndpointNotFound: Requested stable ID or display name does not exist in enumeration.
 * - RequestedEndpointAmbiguous: Display name matches multiple endpoints and no stable ID was supplied.
 * - RequestedEndpointUnavailable: Endpoint exists in enumeration, but provider/OS reports it as
 *   unavailable/locked prior to attempting open.
 * - BackendOpenFailed: Endpoint was uniquely resolved, but low-level backend openDevice() failed.
 * - VirtualEndpointRejected: Reserved for SS4. Triggered when an active MidiEndpointSafetyPolicy
 *   explicitly forbids opening virtual endpoints. (In SS2/SS2.1, virtual endpoints are not rejected).
 * - InvalidSelection: Both ID and name are empty, or provided ID and name contradict each other.
 */
enum class MidiEndpointOpenOutcome
{
    Opened = 0,
    RequestedEndpointNotFound,
    RequestedEndpointAmbiguous,
    RequestedEndpointUnavailable,
    BackendOpenFailed,
    VirtualEndpointRejected,
    InvalidSelection
};

/**
 * @brief Formal method through which an endpoint was matched and resolved.
 */
enum class MidiEndpointResolutionMethod
{
    None = 0,
    StableDeviceIdExact,
    DisplayNameExact
};

/**
 * @brief Input request specification for opening a MIDI endpoint.
 */
struct MidiEndpointSelectionRequest
{
    std::string requestedStableDeviceId;
    std::string requestedDisplayName;
};

/**
 * @brief Strongly-typed result of opening a MIDI endpoint.
 * Invariants:
 * - Opened: isSuccess() is true, handleOpened is true.
 * - Non-Opened: isSuccess() is false, handleOpened is false.
 * - Never falls back to index 0 or alternate endpoints.
 * - resolvedStableDeviceId is populated exclusively if matching succeeded.
 */
struct MidiEndpointOpenResult
{
    MidiEndpointOpenOutcome outcome { MidiEndpointOpenOutcome::InvalidSelection };
    MidiEndpointResolutionMethod resolutionMethod { MidiEndpointResolutionMethod::None };

    std::string requestedStableDeviceId;
    std::string requestedDisplayName;

    std::string resolvedStableDeviceId;
    std::string resolvedDisplayName;

    std::string diagnosticCode;
    std::string diagnosticMessage;

    bool handleOpened { false };

    MidiEndpointOpenResult() = default;
    MidiEndpointOpenResult(const MidiEndpointOpenResult&) = default;
    MidiEndpointOpenResult& operator=(const MidiEndpointOpenResult&) = default;
    MidiEndpointOpenResult(MidiEndpointOpenResult&&) noexcept = default;
    MidiEndpointOpenResult& operator=(MidiEndpointOpenResult&&) noexcept = default;

    [[nodiscard]] bool isSuccess() const noexcept
    {
        return outcome == MidiEndpointOpenOutcome::Opened && handleOpened;
    }

    static MidiEndpointOpenResult fail(
        MidiEndpointOpenOutcome outc,
        std::string reqId,
        std::string reqName,
        std::string diagCode,
        std::string diagMsg)
    {
        MidiEndpointOpenResult r;
        r.outcome = outc;
        r.resolutionMethod = MidiEndpointResolutionMethod::None;
        r.requestedStableDeviceId = std::move(reqId);
        r.requestedDisplayName = std::move(reqName);
        r.resolvedStableDeviceId.clear();
        r.resolvedDisplayName.clear();
        r.diagnosticCode = std::move(diagCode);
        r.diagnosticMessage = std::move(diagMsg);
        r.handleOpened = false;
        return r;
    }

    static MidiEndpointOpenResult ok(
        MidiEndpointResolutionMethod method,
        std::string reqId,
        std::string reqName,
        std::string resId,
        std::string resName)
    {
        MidiEndpointOpenResult r;
        r.outcome = MidiEndpointOpenOutcome::Opened;
        r.resolutionMethod = method;
        r.requestedStableDeviceId = std::move(reqId);
        r.requestedDisplayName = std::move(reqName);
        r.resolvedStableDeviceId = std::move(resId);
        r.resolvedDisplayName = std::move(resName);
        r.diagnosticCode = "OK_MIDI_ENDPOINT_OPENED";
        r.diagnosticMessage = "MIDI endpoint opened successfully.";
        r.handleOpened = true;
        return r;
    }
};

/**
 * @brief Abstract device provider to enable hermetic testing and ownership tracking.
 */
class IMidiDeviceProvider
{
public:
    virtual ~IMidiDeviceProvider() = default;

    [[nodiscard]] virtual std::vector<juce::MidiDeviceInfo> getAvailableOutputs() const = 0;
    [[nodiscard]] virtual std::vector<juce::MidiDeviceInfo> getAvailableInputs() const = 0;

    /**
     * @brief Check whether an enumerated endpoint is available for opening before attempting open.
     */
    [[nodiscard]] virtual bool isEndpointAvailable(const juce::String& /*identifier*/) const { return true; }

    [[nodiscard]] virtual bool openExactOutput(const juce::String& identifier) = 0;
    [[nodiscard]] virtual bool openExactInput(const juce::String& identifier, juce::MidiInputCallback* callback) = 0;
    virtual void closeOutput() noexcept = 0;
    virtual void closeInput() noexcept = 0;
    virtual void sendSysExMessage(const std::vector<uint8_t>& bytes) = 0;

    [[nodiscard]] virtual bool isOutputOpen() const noexcept = 0;
    [[nodiscard]] virtual std::string getOpenedOutputIdentifier() const = 0;
    [[nodiscard]] virtual juce::MidiOutput* getRawOutput() noexcept { return nullptr; }
};

/**
 * @brief Default production device provider using real JUCE system APIs.
 * Follows Model B: Owns the active JUCE handle with explicit lifecycle management.
 */
class DefaultJuceMidiDeviceProvider final : public IMidiDeviceProvider
{
public:
    [[nodiscard]] std::vector<juce::MidiDeviceInfo> getAvailableOutputs() const override
    {
        const auto juceList = juce::MidiOutput::getAvailableDevices();
        return std::vector<juce::MidiDeviceInfo>(juceList.begin(), juceList.end());
    }

    [[nodiscard]] std::vector<juce::MidiDeviceInfo> getAvailableInputs() const override
    {
        const auto juceList = juce::MidiInput::getAvailableDevices();
        return std::vector<juce::MidiDeviceInfo>(juceList.begin(), juceList.end());
    }

    [[nodiscard]] bool isEndpointAvailable(const juce::String& identifier) const override
    {
        if (identifier.isEmpty())
            return false;
        const auto outputs = getAvailableOutputs();
        for (const auto& dev : outputs)
        {
            if (dev.identifier == identifier)
                return true;
        }
        return false;
    }

    bool openExactOutput(const juce::String& identifier) override
    {
        closeOutput();
        outputDevice = juce::MidiOutput::openDevice(identifier);
        if (outputDevice)
        {
            openedOutId = identifier;
            return true;
        }
        return false;
    }

    bool openExactInput(const juce::String& identifier, juce::MidiInputCallback* callback) override
    {
        closeInput();
        inputDevice = juce::MidiInput::openDevice(identifier, callback);
        if (inputDevice)
        {
            inputDevice->start();
            openedInId = identifier;
            return true;
        }
        return false;
    }

    void closeOutput() noexcept override
    {
        outputDevice.reset();
        openedOutId.clear();
    }

    void closeInput() noexcept override
    {
        if (inputDevice)
        {
            inputDevice->stop();
            inputDevice.reset();
        }
        openedInId.clear();
    }

    void sendSysExMessage(const std::vector<uint8_t>& bytes) override
    {
        if (outputDevice && !bytes.empty())
        {
            auto msg = juce::MidiMessage::createSysExMessage(bytes.data(), static_cast<int>(bytes.size()));
            outputDevice->sendMessageNow(msg);
        }
    }

    [[nodiscard]] bool isOutputOpen() const noexcept override
    {
        return outputDevice != nullptr;
    }

    [[nodiscard]] std::string getOpenedOutputIdentifier() const override
    {
        return openedOutId.toStdString();
    }

    [[nodiscard]] juce::MidiOutput* getRawOutput() noexcept override
    {
        return outputDevice.get();
    }

private:
    std::unique_ptr<juce::MidiOutput> outputDevice;
    std::unique_ptr<juce::MidiInput> inputDevice;
    juce::String openedOutId;
    juce::String openedInId;
};

} // namespace abd::hwid
