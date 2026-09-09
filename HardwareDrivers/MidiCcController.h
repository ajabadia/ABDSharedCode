#pragma once

#include "HardwareController.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <map>

namespace abd::hw
{

/**
 * @brief Standard MIDI Continuous Controller (CC) hardware driver.
 */
class MidiCcController : public IHardwareController
{
public:
    explicit MidiCcController(juce::String deviceIdentifier = "", int midiChannel = 1)
        : targetDeviceIdentifier(std::move(deviceIdentifier)), channel(std::clamp(midiChannel, 1, 16))
    {
    }

    ~MidiCcController() override
    {
        disconnect();
    }

    void setTargetDeviceIdentifier(const juce::String& identifier)
    {
        targetDeviceIdentifier = identifier;
    }

    void setChannel(int newChannel)
    {
        channel = std::clamp(newChannel, 1, 16);
    }

    [[nodiscard]] bool isAutomatic() const noexcept override { return true; }

    [[nodiscard]] juce::String getHardwareName() const override
    {
        if (midiOut != nullptr)
            return "MIDI CC: " + midiOut->getName();
        return "Generic MIDI CC Device (Channel " + juce::String(channel) + ")";
    }

    bool connect() override
    {
        auto midiOutputs = juce::MidiOutput::getAvailableDevices();
        if (midiOutputs.isEmpty())
            return false;

        if (targetDeviceIdentifier.isNotEmpty())
        {
            for (const auto& d : midiOutputs)
            {
                if (d.identifier == targetDeviceIdentifier 
                    || d.name == targetDeviceIdentifier
                    || d.name.containsIgnoreCase(targetDeviceIdentifier))
                {
                    midiOut = juce::MidiOutput::openDevice(d.identifier);
                    return midiOut != nullptr;
                }
            }
            return false; // Specific target hardware not found on any MIDI port
        }

        return false; // No target hardware specified
    }

    void disconnect() override
    {
        midiOut.reset();
    }

    [[nodiscard]] bool isConnected() const noexcept override
    {
        return midiOut != nullptr;
    }

    bool setParameter(int paramIndex, float normalizedValue) override
    {
        int raw = std::clamp(static_cast<int>(std::lround(normalizedValue * 127.0f)), 0, 127);
        return setParameterRaw(paramIndex, raw);
    }

    bool setParameterRaw(int paramIndex, int rawValue) override
    {
        if (!isConnected())
            return false;

        uint8_t val = static_cast<uint8_t>(std::clamp(rawValue, 0, 127));
        midiOut->sendMessageNow(juce::MidiMessage::controllerEvent(channel, paramIndex, val));
        cachedValues[paramIndex] = val;
        return true;
    }

    /**
     * @brief Send 14-bit NRPN parameter message (CC 99, 98, 6, 38).
     */
    bool sendNrpn(int nrpnParam, int value14Bit)
    {
        if (!isConnected())
            return false;

        uint8_t paramMsb = static_cast<uint8_t>((nrpnParam >> 7) & 0x7F);
        uint8_t paramLsb = static_cast<uint8_t>(nrpnParam & 0x7F);
        uint8_t valMsb = static_cast<uint8_t>((value14Bit >> 7) & 0x7F);
        uint8_t valLsb = static_cast<uint8_t>(value14Bit & 0x7F);

        midiOut->sendMessageNow(juce::MidiMessage::controllerEvent(channel, 99, paramMsb));
        midiOut->sendMessageNow(juce::MidiMessage::controllerEvent(channel, 98, paramLsb));
        midiOut->sendMessageNow(juce::MidiMessage::controllerEvent(channel, 6, valMsb));
        midiOut->sendMessageNow(juce::MidiMessage::controllerEvent(channel, 38, valLsb));
        return true;
    }

    void mapNrpn(int paramIndex, int nrpnParam)
    {
        nrpnMapping[paramIndex] = nrpnParam;
    }

    bool setupSubmodule(int /*slotIndex*/, uint8_t /*typeId*/) override
    {
        return true; // No-op on generic CC synths
    }

    bool setPatchCable(uint8_t /*sourceId*/, uint8_t /*destId*/, bool /*isConnected*/) override
    {
        return true; // No-op on fixed architecture CC synths
    }

    void requestStateDump() override
    {
    }

    bool sendMidiMessage(const juce::MidiMessage& msg) override
    {
        if (midiOut != nullptr)
        {
            midiOut->sendMessageNow(msg);
            return true;
        }
        return false;
    }

    bool sendNoteOn(int ch, int noteNumber, float velocity) override
    {
        int targetChannel = (ch >= 1 && ch <= 16) ? ch : channel;
        return IHardwareController::sendNoteOn(targetChannel, noteNumber, velocity);
    }

    bool sendNoteOff(int ch, int noteNumber, float velocity = 0.0f) override
    {
        int targetChannel = (ch >= 1 && ch <= 16) ? ch : channel;
        return IHardwareController::sendNoteOff(targetChannel, noteNumber, velocity);
    }

    bool sendAllNotesOff(int ch = 1) override
    {
        int targetChannel = (ch >= 1 && ch <= 16) ? ch : channel;
        return IHardwareController::sendAllNotesOff(targetChannel);
    }

    bool sendPitchBend(int ch, int pitchWheelValue) override
    {
        int targetChannel = (ch >= 1 && ch <= 16) ? ch : channel;
        return IHardwareController::sendPitchBend(targetChannel, pitchWheelValue);
    }

private:
    juce::String targetDeviceIdentifier;
    int channel { 1 };
    std::unique_ptr<juce::MidiOutput> midiOut;
    std::map<int, uint8_t> cachedValues;
    std::map<int, int> nrpnMapping;
};

} // namespace abd::hw
