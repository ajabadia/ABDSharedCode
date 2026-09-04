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
