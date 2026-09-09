/**
 * @file JuceMidiHardwareBackend.h
 * @brief Turnkey JUCE implementation of MidiHardwareBackend for ABDSharedCode.
 * @details Provides ready-to-use MIDI In/Out port scanning and SysEx message
 *          routing over JUCE devices. Reusable across all ABDSynths products.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include "MidiHardwareBackend.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace abd::hwid
{

class JuceMidiHardwareBackend : public MidiHardwareBackend,
                                private juce::MidiInputCallback
{
public:
    JuceMidiHardwareBackend() = default;
    ~JuceMidiHardwareBackend() override { stopListening(); }

    std::string getOutputPortName() const override;
    void sendBytes(const std::vector<uint8_t>& bytes) override;
    void setReceiveCallback(std::function<void(const std::vector<uint8_t>&)> cb) override;
    void startListening() override;
    void stopListening() override;
    void refreshPorts() override;

    void setTargetDevice(const juce::String& identifierOrName);

private:
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    juce::MidiDeviceInfo findMatchingOutput() const;
    juce::MidiDeviceInfo findMatchingInput() const;

    std::function<void(const std::vector<uint8_t>&)> receiveCallback;
    std::unique_ptr<juce::MidiOutput> activeOutput;
    std::unique_ptr<juce::MidiInput> activeInput;
    juce::String targetDevice;
    juce::String activeOutIdentifier;
};

} // namespace abd::hwid
