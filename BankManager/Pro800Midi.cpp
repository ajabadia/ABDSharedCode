#include "Pro800Midi.h"

#include <HardwareDrivers/SysExCodec.h>

#include <vector>

namespace ABD::BankManager
{

namespace
{
constexpr std::array<uint8_t, 7> header{0x00, 0x20, 0x32, 0x00, 0x01, 0x24, 0x00};
constexpr uint8_t responseCommand = 0x78;

} // namespace

juce::MemoryBlock Pro800MidiTransport::buildDumpRequest(int slot)
{
    slot = juce::jlimit(0, 399, slot);
    const uint8_t message[]{0xf0, 0x00, 0x20, 0x32, 0x00, 0x01, 0x24, 0x00, 0x77,
                            static_cast<uint8_t>(slot % 128), static_cast<uint8_t>(slot / 128), 0xf7};
    return {message, sizeof(message)};
}

int Pro800MidiTransport::parseResponse(const juce::MemoryBlock& message, juce::MemoryBlock& rawData)
{
    if (message.getSize() < 13) return -1;
    const auto* bytes = static_cast<const uint8_t*>(message.getData());
    if (bytes[0] != 0xf0 || bytes[1] != 0x00 || bytes[2] != 0x20 || bytes[3] != 0x32 ||
        bytes[4] != 0x00 || bytes[5] != 0x01 || bytes[6] != 0x24 || bytes[7] != 0x00 ||
        bytes[8] != responseCommand || bytes[message.getSize() - 1] != 0xf7) return -1;
    rawData.reset();
    // El unpack lo hace el codec compartido (orden canonico bit i, grupo
    // parcial decodificado): bytes[11..size-2] son el payload, sin el F7 final.
    std::vector<uint8_t> unpacked;
    if (!abd::hw::SysExCodec::unpack7to8(bytes + 11, message.getSize() - 12, unpacked))
        return -1;
    rawData.append(unpacked.data(), unpacked.size());
    return bytes[9] + (bytes[10] << 7);
}

juce::MemoryBlock Pro800MidiTransport::buildPatchDump(const juce::MemoryBlock& rawData, int slot)
{
    slot = juce::jlimit(0, 399, slot);
    // Pack 8->7 delegado en el codec compartido (orden canonico bit i, sin
    // rellenar el grupo parcial): byte identico al que hacia el pack local.
    std::vector<uint8_t> packed;
    abd::hw::SysExCodec::pack8to7(static_cast<const uint8_t*>(rawData.getData()),
                                  rawData.getSize(), packed);
    juce::MemoryBlock message;
    const uint8_t prefix[]{0xf0, 0x00, 0x20, 0x32, 0x00, 0x01, 0x24, 0x00, 0x78,
                           static_cast<uint8_t>(slot % 128), static_cast<uint8_t>(slot / 128)};
    message.append(prefix, sizeof(prefix));
    message.append(packed.data(), packed.size());
    const uint8_t end = 0xf7;
    message.append(&end, 1);
    return message;
}

} // namespace ABD::BankManager
