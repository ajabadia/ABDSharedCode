/**
 * @file CasioNibbleCodec.h
 * @brief Universal 4-bit Nibble Packer/Unpacker and Checksum for Casio CZ Series.
 * @details Implements byte splitting and reconstruction into 4-bit nibbles
 *          (0x00..0x0F) and Casio 7-bit checksum verification.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <cstdint>
#include <vector>
#include <cstddef>

namespace abd::hw
{

/**
 * @enum NibbleOrder
 * @brief Order of nibbles in the transmission stream.
 */
enum class NibbleOrder
{
    HighFirst, /**< Most Significant Nibble (bits 7..4) followed by Least Significant Nibble (bits 3..0). Standard Casio CZ format. */
    LowFirst   /**< Least Significant Nibble (bits 3..0) followed by Most Significant Nibble (bits 7..4). */
};

/**
 * @class CasioNibbleCodec
 * @brief Codec for Casio CZ 4-bit nibble encoding, decoding, and checksum calculation.
 */
class CasioNibbleCodec
{
public:
    CasioNibbleCodec() = default;

    /**
     * @brief Encodes raw 8-bit bytes into 4-bit nibbles (each input byte produces two 7-bit MIDI bytes in [0..15]).
     * @param src Pointer to input raw bytes.
     * @param srcLen Number of bytes to encode.
     * @param dest Output vector for encoded nibbles.
     * @param order Nibble transmission order (default: HighFirst).
     * @return true on success, false if input pointer is null and srcLen > 0.
     */
    static bool encode(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest, NibbleOrder order = NibbleOrder::HighFirst) noexcept;

    /**
     * @brief Overload taking std::vector.
     */
    static bool encode(const std::vector<uint8_t>& src, std::vector<uint8_t>& dest, NibbleOrder order = NibbleOrder::HighFirst) noexcept
    {
        return encode(src.data(), src.size(), dest, order);
    }

    /**
     * @brief Decodes 4-bit nibbles back into raw 8-bit bytes (each pair of nibbles produces one byte).
     * @param src Pointer to encoded nibbles.
     * @param srcLen Number of nibbles to decode (must be even).
     * @param dest Output vector for decoded bytes.
     * @param order Nibble transmission order (default: HighFirst).
     * @return true on success, false if srcLen is odd or src is null.
     */
    static bool decode(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest, NibbleOrder order = NibbleOrder::HighFirst) noexcept;

    /**
     * @brief Overload taking std::vector.
     */
    static bool decode(const std::vector<uint8_t>& src, std::vector<uint8_t>& dest, NibbleOrder order = NibbleOrder::HighFirst) noexcept
    {
        return decode(src.data(), src.size(), dest, order);
    }

    /**
     * @brief Calculates Casio 7-bit additive checksum: (sum of all bytes) & 0x7F.
     * @param data Pointer to bytes/nibbles.
     * @param len Number of bytes.
     * @return 7-bit checksum [0..127].
     */
    [[nodiscard]] static uint8_t calculateChecksum(const uint8_t* data, size_t len) noexcept;

    /**
     * @brief Verifies that calculated checksum matches expected checksum.
     */
    [[nodiscard]] static bool verifyChecksum(const uint8_t* data, size_t len, uint8_t expectedChecksum) noexcept
    {
        return calculateChecksum(data, len) == (expectedChecksum & 0x7F);
    }
};

} // namespace abd::hw
