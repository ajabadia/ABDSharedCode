#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace abd::hw
{

/**
 * @brief 7-to-8 bit packer/unpacker for SysEx payloads, in the two orders the
 *        ABDSynths family actually uses.
 *
 * Standard MIDI data bytes carry 7 bits (0x00..0x7F), so a device that must
 * transfer raw 8-bit memory packs 7 bytes into 8: one collector byte holding
 * the MSBs, followed by the 7 low bytes.
 *
 * The family uses TWO collector-bit orders and they are NOT interchangeable:
 *
 *  - Canonical order (`pack8to7` / `unpack7to8`): collector bit i is the MSB
 *    of byte i. Used by the ABDSynths Pro-800 (F0 7D ...), by the Behringer
 *    DeepMind family (`pack8to7Dm` on the TS side) and by ABDMS2000's
 *    MS2000HardwareProgram packer.
 *
 *  - Korg order (`pack8to7Korg` / `unpack7to8Korg`): collector bit 6-j is the
 *    MSB of byte j, i.e. the MSB of the first byte sits in the collector's
 *    high bit. This is the order real Korg MS2000 / microKORG factory dumps
 *    use: the fixtures in ABDBankManager (`fixtures/sysex/korg-*`) decode only
 *    with it, and that repo's TS codec (`pack8to7NoPad` / `unpack7to8` in
 *    Source/Contracts/SysEx/codec.ts) mirrors this order.
 *
 * HISTORICAL NOTE: this header used to document the canonical order as
 * "Korg MIDI ...". The naming above follows the factory-dump evidence
 * collected in ABDBankManager/DOCS/ABDSYNTHS_SYSEX_GUIDE.md. The behavior of
 * `pack8to7` / `unpack7to8` is byte-for-byte unchanged — only the
 * documentation moved, so existing consumers (ABDMS2000, ABDJUNiO601) keep
 * exactly what they had.
 *
 * Padding policy: a partial final group can be emitted as-is (the NoPad
 * variants — what real hardware transmits: a 254-byte MS2000 program arrives
 * as 291 payload bytes, not 296) or zero-padded to a full 7-byte group (what
 * some device firmware and the TS `pack8to7` / `pack8to7Dm` emit). The unpack
 * side decodes both — a padded frame just yields trailing zero bytes that the
 * caller slices off.
 */
class SysExCodec
{
public:
    SysExCodec() = default;

    /**
     * Unpacks 7-bit encoded MIDI SysEx body to raw 8-bit memory buffer
     * (canonical order). Partial trailing groups are decoded, not dropped:
     * real devices end the payload mid-group.
     * @param src Pointer to 7-bit encoded data (after header).
     * @param srcLen Number of encoded bytes.
     * @param dest Output vector for unpacked 8-bit bytes.
     * @return true on success (false for null src or empty input).
     */
    static bool unpack7to8(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept;

    /**
     * Packs raw 8-bit memory buffer into 7-bit MIDI SysEx body (canonical
     * order, no padding of the final partial group).
     * @param src Pointer to raw 8-bit bytes.
     * @param srcLen Number of raw 8-bit bytes.
     * @param dest Output vector for packed 7-bit bytes.
     * @return true on success.
     */
    static bool pack8to7(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept;

    /**
     * Canonical order with the final partial group zero-padded to 7 bytes, so
     * the output length is always a multiple of 8 (mirrors TS `pack8to7Dm`).
     */
    static bool pack8to7Padded(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept;

    /**
     * Korg order (collector bit 6-j = MSB of byte j), no padding of the final
     * partial group: wire-exact for real Korg MS2000 / microKORG frames
     * (254-byte program -> 291 payload bytes). Mirrors TS `pack8to7NoPad`.
     */
    static bool pack8to7Korg(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept;

    /**
     * Korg order with the final partial group zero-padded to 7 bytes.
     * Mirrors TS `pack8to7`.
     */
    static bool pack8to7KorgPadded(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept;

    /**
     * Unpacks Korg-order payload (collector bit 6-j). Partial trailing groups
     * are decoded, not dropped. Mirrors TS `unpack7to8` / `unpack7to8Tolerant`.
     */
    static bool unpack7to8Korg(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept;
};

} // namespace abd::hw
