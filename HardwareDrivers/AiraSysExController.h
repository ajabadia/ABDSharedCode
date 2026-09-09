#pragma once

#include "HardwareController.h"
#include "RoutingValidator.h"
#include "FskAudioModem.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <vector>

namespace abd::hw
{

enum class AiraModel : uint8_t
{
    GenericModular = 0x00,
    Bitrazer       = 0x15,
    Demora         = 0x16,
    Torcido        = 0x17,
    Scooper        = 0x18
};

/**
 * @brief Hardware controller for Roland AIRA Modular units using USB MIDI SysEx & CC.
 */
class AiraSysExController : public IHardwareController,
                            public juce::MidiInputCallback
{
public:
    explicit AiraSysExController(AiraModel model = AiraModel::Bitrazer)
        : targetModel(model)
    {
    }

    ~AiraSysExController() override
    {
        disconnect();
    }

    [[nodiscard]] bool isAutomatic() const noexcept override { return true; }

    [[nodiscard]] static uint8_t mapHardwareIdToAiraModel(const juce::String& hardwareId)
    {
        if (hardwareId.contains("BITRAZER")) return static_cast<uint8_t>(AiraModel::Bitrazer);
        if (hardwareId.contains("DEMORA"))   return static_cast<uint8_t>(AiraModel::Demora);
        if (hardwareId.contains("TORCIDO"))  return static_cast<uint8_t>(AiraModel::Torcido);
        if (hardwareId.contains("SCOOPER"))  return static_cast<uint8_t>(AiraModel::Scooper);
        return static_cast<uint8_t>(AiraModel::GenericModular);
    }

    [[nodiscard]] juce::String getHardwareName() const override
    {
        switch (targetModel)
        {
            case AiraModel::Bitrazer: return "Roland AIRA Bitrazer";
            case AiraModel::Demora:   return "Roland AIRA Demora";
            case AiraModel::Torcido:  return "Roland AIRA Torcido";
            case AiraModel::Scooper:  return "Roland AIRA Scooper";
            default:                  return "Roland AIRA Modular";
        }
    }

    bool connect() override
    {
        auto midiOutputs = juce::MidiOutput::getAvailableDevices();
        auto midiInputs = juce::MidiInput::getAvailableDevices();

        juce::String searchKeyword = "AIRA";
        switch (targetModel)
        {
            case AiraModel::Bitrazer: searchKeyword = "BITRAZER"; break;
            case AiraModel::Demora:   searchKeyword = "DEMORA"; break;
            case AiraModel::Torcido:  searchKeyword = "TORCIDO"; break;
            case AiraModel::Scooper:  searchKeyword = "SCOOPER"; break;
        }

        juce::MidiDeviceInfo targetOutDevice, targetInDevice;
        bool foundOut = false, foundIn = false;

        for (const auto& d : midiOutputs)
        {
            if (d.name.containsIgnoreCase(searchKeyword) || d.name.containsIgnoreCase("AIRA"))
            {
                targetOutDevice = d;
                foundOut = true;
                break;
            }
        }

        for (const auto& d : midiInputs)
        {
            if (d.name.containsIgnoreCase(searchKeyword) || d.name.containsIgnoreCase("AIRA"))
            {
                targetInDevice = d;
                foundIn = true;
                break;
            }
        }

        if (foundOut)
            midiOut = juce::MidiOutput::openDevice(targetOutDevice.identifier);

        if (foundIn)
        {
            midiIn = juce::MidiInput::openDevice(targetInDevice.identifier, this);
            if (midiIn != nullptr)
                midiIn->start();
        }

        return (midiOut != nullptr);
    }

    void disconnect() override
    {
        if (midiIn != nullptr)
        {
            midiIn->stop();
            midiIn.reset();
        }
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

        // GRF knob parameters map directly to CC 11..16 or SysEx 10 00 00 01..08
        if (paramIndex >= 1 && paramIndex <= 8)
        {
            uint8_t addr[4] = { 0x10, 0x00, 0x00, static_cast<uint8_t>(paramIndex) };
            uint8_t data[1] = { val };
            sendDataSet1(addr, data, 1);
            return true;
        }

        // Standard MIDI CC fallback for GRF 1..6 (CC 11 to 16)
        if (paramIndex >= 11 && paramIndex <= 16)
        {
            midiOut->sendMessageNow(juce::MidiMessage::controllerEvent(1, paramIndex, val));
            return true;
        }

        return false;
    }

    bool setupSubmodule(int slotIndex, uint8_t typeId) override
    {
        if (!isConnected() || slotIndex < 0 || slotIndex >= 6)
            return false;

        slotTypes[static_cast<size_t>(slotIndex)] = typeId & 0x1F;

        uint8_t offset = static_cast<uint8_t>(slotIndex * 5);
        uint8_t addr[4] = { 0x10, 0x10, 0x00, offset };
        uint8_t data[1] = { static_cast<uint8_t>(typeId & 0x1F) };
        
        sendDataSet1(addr, data, 1);
        return true;
    }

    bool setSubmoduleParameter(int slotIndex, int paramIndex, uint8_t rawValue)
    {
        if (!isConnected() || slotIndex < 0 || slotIndex >= 6 || paramIndex < 1 || paramIndex > 4)
            return false;

        uint8_t offset = static_cast<uint8_t>(slotIndex * 5 + paramIndex);
        uint8_t addr[4] = { 0x10, 0x10, 0x00, offset };
        uint8_t data[1] = { static_cast<uint8_t>(rawValue & 0x7F) };

        sendDataSet1(addr, data, 1);
        return true;
    }

    bool setPatchCable(uint8_t sourceId, uint8_t destId, bool isConnectedCable) override
    {
        if (!isConnected())
            return false;

        if (isConnectedCable)
        {
            std::string err;
            if (!routingValidator.validateConnection(sourceId, destId, slotTypes, &err))
            {
                juce::Logger::writeToLog("[RoutingValidator Error] " + juce::String(err));
                return false;
            }
        }

        uint8_t addr[4] = { 0x10, 0x20, sourceId, destId };
        uint8_t data[1] = { static_cast<uint8_t>(isConnectedCable ? 0x01 : 0x00) };

        sendDataSet1(addr, data, 1);
        return true;
    }

    void requestStateDump() override
    {
        if (!isConnected())
            return;

        // Request Main Module + Submodules block (Address: 10 00 00 00, Size: 00 00 01 00)
        uint8_t addr[4] = { 0x10, 0x00, 0x00, 0x00 };
        uint8_t size[4] = { 0x00, 0x00, 0x01, 0x00 };
        sendDataRequest1(addr, size);
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override
    {
        if (message.isSysEx())
        {
            const auto* data = message.getSysExData();
            int size = message.getSysExDataSize();
            // Process incoming SysEx response if necessary
            juce::ignoreUnused(data, size);
        }
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

    [[nodiscard]] juce::AudioBuffer<float> exportPatchAsFskAudio(double sampleRate = 48000.0) const
    {
        std::vector<uint8_t> payload;
        payload.reserve(16);
        payload.push_back(static_cast<uint8_t>(targetModel));
        for (uint8_t slotType : slotTypes)
        {
            payload.push_back(slotType);
        }
        payload.push_back(0xFF); // End of patch marker
        return fskModem.modulate(payload, sampleRate);
    }

    [[nodiscard]] bool detectFskAudioResponse(const juce::AudioBuffer<float>& inBuffer, double sampleRate) const
    {
        auto det = fskModem.detectCarrier(inBuffer, sampleRate);
        return det.detected;
    }

    [[nodiscard]] const FskAudioModem& getFskModem() const noexcept { return fskModem; }
    [[nodiscard]] FskAudioModem& getFskModem() noexcept { return fskModem; }

private:
    static uint8_t calculateChecksum(const uint8_t* bytes, size_t count)
    {
        uint32_t sum = 0;
        for (size_t i = 0; i < count; ++i)
            sum += bytes[i];
        return static_cast<uint8_t>((128 - (sum % 128)) & 0x7F);
    }

    void sendDataSet1(const uint8_t addr[4], const uint8_t* data, size_t dataSize)
    {
        std::vector<uint8_t> packet;
        packet.reserve(13 + dataSize);

        packet.push_back(0xF0);
        packet.push_back(0x41); // Roland ID
        packet.push_back(0x10); // Device ID
        packet.push_back(0x00); // Model ID #1
        packet.push_back(0x00); // Model ID #2
        packet.push_back(0x00); // Model ID #3
        packet.push_back(static_cast<uint8_t>(targetModel)); // Model ID #4 (15H..18H)
        packet.push_back(0x12); // Command ID (DT1)

        packet.push_back(addr[0]);
        packet.push_back(addr[1]);
        packet.push_back(addr[2]);
        packet.push_back(addr[3]);

        for (size_t i = 0; i < dataSize; ++i)
            packet.push_back(data[i]);

        // Calculate checksum for addr + data
        std::vector<uint8_t> checkPayload;
        checkPayload.insert(checkPayload.end(), addr, addr + 4);
        checkPayload.insert(checkPayload.end(), data, data + dataSize);
        packet.push_back(calculateChecksum(checkPayload.data(), checkPayload.size()));

        packet.push_back(0xF7);

        midiOut->sendMessageNow(juce::MidiMessage(packet.data(), static_cast<int>(packet.size())));
    }

    void sendDataRequest1(const uint8_t addr[4], const uint8_t size[4])
    {
        std::vector<uint8_t> packet;
        packet.reserve(18);

        packet.push_back(0xF0);
        packet.push_back(0x41); // Roland ID
        packet.push_back(0x10); // Device ID
        packet.push_back(0x00); // Model ID #1
        packet.push_back(0x00); // Model ID #2
        packet.push_back(0x00); // Model ID #3
        packet.push_back(static_cast<uint8_t>(targetModel)); // Model ID #4 (15H..18H)
        packet.push_back(0x11); // Command ID (RQ1)

        packet.push_back(addr[0]);
        packet.push_back(addr[1]);
        packet.push_back(addr[2]);
        packet.push_back(addr[3]);

        packet.push_back(size[0]);
        packet.push_back(size[1]);
        packet.push_back(size[2]);
        packet.push_back(size[3]);

        // Calculate checksum for addr + size
        uint8_t checkPayload[8] = { addr[0], addr[1], addr[2], addr[3], size[0], size[1], size[2], size[3] };
        packet.push_back(calculateChecksum(checkPayload, 8));

        packet.push_back(0xF7);

        midiOut->sendMessageNow(juce::MidiMessage(packet.data(), static_cast<int>(packet.size())));
    }

    AiraModel targetModel;
    std::unique_ptr<juce::MidiOutput> midiOut;
    std::unique_ptr<juce::MidiInput> midiIn;
    RoutingValidator routingValidator;
    std::array<uint8_t, 6> slotTypes { 0 };
    FskAudioModem fskModem;
};

} // namespace abd::hw
