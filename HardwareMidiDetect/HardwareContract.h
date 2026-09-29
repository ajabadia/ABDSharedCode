/**
 * @file HardwareContract.h
 * @brief Shared hardware identity contract for MIDI/SysEx detection.
 * @details The base identity contract consumed by the shared MIDI identity
 *          detector. Intentional subset: it carries everything needed to
 *          recognize a device via SysEx (Universal Identity Reply, proprietary
 *          header) or by port-name heuristics. Product-specific data (e.g.
 *          control/function schemas) is intentionally NOT modelled here;
 *          consumers read it from the raw JSON exposed by
 *          HardwareContractRegistry::getRawContractJson().
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <string>
#include <vector>

namespace abd::hwid
{

/**
 * @enum HardwareMidiIdentityState
 * @brief Five-state preflight identity classification model for hardware MIDI detection.
 *
 * Semantics:
 * - PortAvailable: Endpoint enumerated and available at OS level; no identity assertion made.
 * - IdentityVerified: SysEx identity evidence received and strictly matches contract.
 * - IdentityUnavailable: Insufficient identity evidence: inquiry not requested, unsupported,
 *   timed out, or unparseable.
 * - IdentityMismatch: SysEx identity reply received, but contradicts expected contract.
 * - UserConfirmedUnverified: Reserved exclusively for application layer (ABDAudioLab) with operator.
 *   CRITICAL INVARIANT: ABDSharedCode NEVER constructs, assigns, returns or persists this state.
 */
enum class HardwareMidiIdentityState
{
    PortAvailable = 0,
    IdentityVerified,
    IdentityUnavailable,
    IdentityMismatch,
    UserConfirmedUnverified
};

/**
 * @brief Centralized legacy converter for backward compatibility with isSysExVerified.
 * Invariants:
 * - IdentityVerified <===> isSysExVerified == true
 * - All other states <===> isSysExVerified == false
 */
[[nodiscard]] constexpr bool toLegacyIsSysExVerified(HardwareMidiIdentityState state) noexcept
{
    return state == HardwareMidiIdentityState::IdentityVerified;
}

/**
 * @brief Executable invariant: checks whether a state can be emitted by ABDSharedCode shared discovery.
 * Returns false for UserConfirmedUnverified.
 */
[[nodiscard]] constexpr bool isSharedDiscoveryEmittable(HardwareMidiIdentityState state) noexcept
{
    return state != HardwareMidiIdentityState::UserConfirmedUnverified;
}

/**
 * @brief Strong factory for shared discovery identity outcomes.
 * Guarantees at compile-time and run-time that ABDSharedCode discovery can only ever instantiate
 * the four permissible shared states.
 */
class SharedDiscoveryIdentityResult
{
public:
    static constexpr SharedDiscoveryIdentityResult portAvailable() noexcept
    {
        return SharedDiscoveryIdentityResult(HardwareMidiIdentityState::PortAvailable);
    }

    static constexpr SharedDiscoveryIdentityResult verified() noexcept
    {
        return SharedDiscoveryIdentityResult(HardwareMidiIdentityState::IdentityVerified);
    }

    static constexpr SharedDiscoveryIdentityResult unavailable() noexcept
    {
        return SharedDiscoveryIdentityResult(HardwareMidiIdentityState::IdentityUnavailable);
    }

    static constexpr SharedDiscoveryIdentityResult mismatch() noexcept
    {
        return SharedDiscoveryIdentityResult(HardwareMidiIdentityState::IdentityMismatch);
    }

    [[nodiscard]] constexpr HardwareMidiIdentityState getState() const noexcept { return state_; }
    [[nodiscard]] constexpr bool isSysExVerified() const noexcept { return toLegacyIsSysExVerified(state_); }

private:
    explicit constexpr SharedDiscoveryIdentityResult(HardwareMidiIdentityState s) noexcept : state_(s) {}
    HardwareMidiIdentityState state_;
};

/**
 * @struct MidiIdentityContract
 * @brief The MIDI identity claim of a hardware device.
 * @detail Fields map 1:1 to the "midiIdentification" object in the shared
 *         contract JSON (ABDSharedAssets/contracts/*.json).
 */
struct MidiIdentityContract
{
    std::string manufacturer;            /**< e.g. "Korg" */
    std::string manufacturerIdHex;       /**< e.g. "42", "00 20 32" (MMA ID, space separated) */
    std::string model;                   /**< e.g. "MS2000" */
    std::string modelIdHex;              /**< e.g. "58" (family 2nd byte in Universal reply) */
    std::string familyIdHex;             /**< e.g. "00 00", "32 00" */
    std::string sysexHeaderHex;          /**< proprietary SysEx header, e.g. "42 30 58" */
    std::vector<std::string> portNameMatches; /**< port-name substring keywords for heuristics */
};

/**
 * @struct HardwareContract
 * @brief The base identity contract for a hardware device.
 */
struct HardwareContract
{
    std::string schemaVersion { "2.0" };
    std::string id;                      /**< e.g. "korg_ms2000" */
    std::string displayName;
    std::string description;
    std::string deviceType;              /**< MANUAL_EURORACK / ANALOGUE_PEDAL / AUTOMATED_SYSEX / AUTOMATED_MIDI_CC / MOCK_DSP */
    std::string brand;
    std::string brandLogo;
    std::string modelImage;
    std::string manufacturer;
    std::string model;
    std::string modelIdHex;
    std::string autoDetectSysEx;
    std::string theme { "audiolab-light" };

    MidiIdentityContract midiIdentity;
};

} // namespace abd::hwid
