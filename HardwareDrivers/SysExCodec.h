#pragma once
#include <cstdint>
#include <vector>
#include <cstddef>

namespace abd::hw {

/**
 * @brief Korg MIDI System Exclusive 7-to-8 bit Unpacker and 8-to-7 bit Packer.
 * 
 * Standard MIDI limits data bytes to 7 bits (0x00..0x7F).
 * Korg hardware groups data in chunks of 7 bytes of 8-bit memory,
 * encoding them into 8 bytes of 7-bit MIDI data with an initial MSB collector byte:
 * 
 * Encoding:
 * [MSB Collector] [Byte0 & 0x7F] [Byte1 & 0x7F] ... [Byte6 & 0x7F]
 * MSB Collector bit i (0..6) = (Byte i bit 7).
 */
class SysExCodec {
public:
    SysExCodec() = default;

    /**
     * Unpacks 7-bit encoded MIDI SysEx body to raw 8-bit memory buffer.
     * @param src Pointer to 7-bit encoded data (after header).
     * @param srcLen Number of encoded bytes.
     * @param dest Output vector for unpacked 8-bit bytes.
     * @return true on success.
     */
    static bool unpack7to8(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept;

    /**
     * Packs raw 8-bit memory buffer into 7-bit MIDI SysEx body.
     * @param src Pointer to raw 8-bit bytes.
     * @param srcLen Number of raw 8-bit bytes.
     * @param dest Output vector for packed 7-bit bytes.
     * @return true on success.
     */
    static bool pack8to7(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept;
};

} // namespace abd::hw
