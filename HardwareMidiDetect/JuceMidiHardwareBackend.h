/**
 * @file JuceMidiHardwareBackend.h
 * @brief Turnkey JUCE implementation of MidiHardwareBackend for ABDSharedCode.
 * @details Provides ready-to-use MIDI In/Out port scanning, strict endpoint opening
 *          without fallback to index 0, explicit Model B ownership lifecycle,
 *          and SysEx message routing over JUCE devices.
 *          Reusable across all ABDSynths products.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include "MidiEndpointTypes.h"
#include "MidiHardwareBackend.h"
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <string>
#include <vector>

namespace abd::hwid
{

class JuceMidiHardwareBackend : public MidiHardwareBackend,
                                private juce::MidiInputCallback
{
public:
    explicit JuceMidiHardwareBackend(std::shared_ptr<IMidiDeviceProvider> provider = nullptr);
    ~JuceMidiHardwareBackend() override;

    std::string getOutputPortName() const override;
    void sendBytes(const std::vector<uint8_t>& bytes) override;
    void setReceiveCallback(std::function<void(const std::vector<uint8_t>&)> cb) override;
    void startListening() override;
    void stopListening() override;
    void refreshPorts() override;

    void setTargetDevice(const juce::String& identifierOrName);

    /**
     * @brief Strict opening of MIDI output endpoint with zero fallback to index 0.
     * @param request The requested stable device ID and/or display name.
     * @return Strongly-typed open result containing outcome, resolution method and handle status.
     */
    MidiEndpointOpenResult openStrictOutput(const MidiEndpointSelectionRequest& request);

    /**
     * @brief Convenience overload for opening by identifier and/or display name.
     */
    MidiEndpointOpenResult openStrictOutput(const std::string& identifier, const std::string& displayName = "");

    /**
     * @brief Check whether an output endpoint is currently open.
     */
    [[nodiscard]] bool isOutputOpen() const noexcept;

    /**
     * @brief Retrieve the identifier of the currently opened output endpoint, if any.
     */
    [[nodiscard]] std::string getOpenedOutputIdentifier() const;

    /**
     * @brief Access the raw juce::MidiOutput handle owned by this backend.
     */
    [[nodiscard]] juce::MidiOutput* getOpenedOutput() noexcept;

    /**
     * @brief Explicitly close the opened output endpoint, resetting ownership.
     */
    void closeOpenedOutput() noexcept;

    [[nodiscard]] const MidiEndpointOpenResult& getLastOpenResult() const noexcept { return lastOpenResult; }

private:
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    juce::MidiDeviceInfo findMatchingOutput() const;
    juce::MidiDeviceInfo findMatchingInput() const;

    std::shared_ptr<IMidiDeviceProvider> deviceProvider;
    std::function<void(const std::vector<uint8_t>&)> receiveCallback;
    juce::String targetDevice;
    juce::String activeOutIdentifier;
    MidiEndpointOpenResult lastOpenResult;
};

} // namespace abd::hwid
