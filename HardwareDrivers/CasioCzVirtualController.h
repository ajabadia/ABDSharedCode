#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>
#include <memory>
#include "HardwareController.h"
#include "CasioNibbleCodec.h"
#include <juce_audio_devices/juce_audio_devices.h>

namespace abd::hw
{

/**
 * @class CasioCzVirtualController
 * @brief Autonomous hardware controller for interrogating Casio CZ Phase Distortion (PD) synths
 *        emulated in Vintage Emulator Studio (VES / MAME Core) or physical hardware via loopMIDI.
 * @details Implements IHardwareController with standard 4-bit nibble packing (Casio Manufacturer ID: 0x44).
 *          Supports a canonical 1984 NZ-1 opcode table plus dynamic override via JSON mapping.
 */
class CasioCzVirtualController : public IHardwareController
{
public:
    CasioCzVirtualController();
    ~CasioCzVirtualController() override;

    // MÃ©todos obligatorios de la interfaz IHardwareController
    bool connect() override;
    void disconnect() override;
    [[nodiscard]] bool isConnected() const noexcept override;
    [[nodiscard]] bool isAutomatic() const noexcept override { return true; }
    [[nodiscard]] juce::String getHardwareName() const override { return "Casio CZ (MAME Core / VES)"; }

    bool setParameter(int paramIndex, float normalizedValue) override;
    bool setParameterRaw(int paramIndex, int rawValue) override;
    bool sendMidiMessage(const juce::MidiMessage& msg) override;

    bool setupSubmodule(int /*slotIndex*/, uint8_t /*typeId*/) override { return true; }
    bool setPatchCable(uint8_t /*sourceId*/, uint8_t /*destId*/, bool /*isConnected*/) override { return true; }
    void requestStateDump() override {}

    // ConfiguraciÃ³n contextual del entorno virtual loopMIDI
    void setTargetDeviceIdentifier(const juce::String& portSubstring);
    void setMidiChannel(int channel) noexcept;
    [[nodiscard]] int getMidiChannel() const noexcept { return midiChannel; }

    struct OpcodePair
    {
        uint8_t msb { 0 };
        uint8_t lsb { 0 };
        int maxRawValue { 99 };
    };

    void registerCustomOpcode(int paramIndex, uint8_t opcodeMSB, uint8_t opcodeLSB, int maxRaw = 99);
    bool loadParameterMappingJson(const juce::String& jsonString);

    [[nodiscard]] const std::unordered_map<int, OpcodePair>& getOpcodeRegistry() const noexcept { return opcodeRegistry; }

    // Funciones nativas del procesador PD original (Casio NZ-1, 1984)
    bool sendCzParameter(uint8_t opcodeMSB, uint8_t opcodeLSB, int value);
    bool sendCzEnvelopeStep(int envType /*0=Pitch, 1=DCW, 2=DCA*/, int line /*1 o 2*/,
                            int stepIndex /*1..8*/, int rate /*0..99*/, int level /*0..99*/);

    [[nodiscard]] const std::vector<juce::MidiMessage>& getSentMessages() const noexcept { return sentMessages; }
    void clearSentMessages() noexcept { sentMessages.clear(); }

    void setSettlingDelayMs(int delayMs) noexcept { settlingDelayMs = delayMs; }
    [[nodiscard]] int getSettlingDelayMs() const noexcept { return settlingDelayMs; }

private:
    std::unique_ptr<juce::MidiOutput> midiDevice;
    juce::String targetPortName { "ABDAudioLab_MIDI_Out" };
    int midiChannel { 1 };
    bool isCurrentlyConnected { false };
    int settlingDelayMs { 5 };

    std::unordered_map<int, OpcodePair> opcodeRegistry;
    std::vector<juce::MidiMessage> sentMessages;

    void initializeCanonicalCzOpcodes();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CasioCzVirtualController)
};

} // namespace abd::hw
