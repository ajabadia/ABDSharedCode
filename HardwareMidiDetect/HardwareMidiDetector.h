/**
 * @file HardwareMidiDetector.h
 * @brief Automatic MIDI hardware detection driven by shared hardware contracts.
 * @details Fully contract-driven detection engine. Decodes hardware according to
 *          the identity claims declared in JSON hardware contracts, without
 *          hardcoding manufacturer/model specifics. Every SysEx query sent is
 *          derived from the loaded contracts (universal identity inquiry always,
 *          plus each contract's declared autoDetectSysEx).
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <string>
#include <vector>
#include <optional>
#include <map>
#include "HardwareContract.h"

namespace abd::hwid
{

/**
 * @struct DiscoveredDevice
 * @brief Metadata for a hardware device identified via MIDI SysEx inquiry.
 */
struct DiscoveredDevice
{
    std::string hardwareId;             /**< Registry ID (from matching contract id). */
    std::string displayName;            /**< Human readable name (from contract displayName). */
    std::string manufacturer;           /**< Manufacturer name from contract. */
    std::string model;                  /**< Model designation from contract. */
    std::string firmwareVersion;        /**< Firmware / software revision string if available. */

    juce::MidiDeviceInfo inDevice;      /**< JUCE MIDI input port info. */
    juce::MidiDeviceInfo outDevice;     /**< JUCE MIDI output port info. */
    uint8_t deviceId { 0x10 };          /**< Device ID or MIDI channel. */
    bool isSysExVerified { false };     /**< True if verified via SysEx response; false if name heuristic. */
};

/**
 * @class HardwareMidiDetector
 * @brief Contract-driven scanner for MIDI ports and SysEx identity messages.
 */
class HardwareMidiDetector : private juce::MidiInputCallback
{
public:
    explicit HardwareMidiDetector(std::vector<HardwareContract> contracts = {});
    ~HardwareMidiDetector() override;

    /**
     * @brief Set the active hardware identity contracts used to recognize devices.
     */
    void setContracts(std::vector<HardwareContract> contracts);

    /**
     * @brief Scans all physical MIDI ports using registered contracts.
     * @param timeoutMs Timeout in ms to wait for SysEx replies per port.
     * @return List of discovered and identified hardware devices matching contracts.
     */
    std::vector<DiscoveredDevice> scanAllPorts(int timeoutMs = 350);

    /**
     * @brief Parses an incoming MIDI SysEx message against a set of contracts.
     * @param msg Incoming MIDI message.
     * @param outDevice Output structure to populate if matched.
     * @param contracts List of contracts to match against.
     * @return True if the message matched a contract.
     */
    static bool parseIdentityReply(const juce::MidiMessage& msg,
                                   DiscoveredDevice& outDevice,
                                   const std::vector<HardwareContract>& contracts);

    /**
     * @brief Heuristic name matcher for fallback detection against contract port matches.
     */
    static std::optional<DiscoveredDevice> matchFromPortNames(const juce::MidiDeviceInfo& inDev,
                                                              const juce::MidiDeviceInfo& outDev,
                                                              const std::vector<HardwareContract>& contracts);

    /**
     * @brief Build a standard Universal Non-Real Time Identity Request message.
     * @param deviceId Target device ID (0x7F = broadcast).
     */
    static juce::MidiMessage makeIdentityRequest(uint8_t deviceId = 0x7F);

    /**
     * @brief Build the list of detection queries to send for a set of contracts.
     * @details Always includes the Universal broadcast Identity Inquiry, then each
     *          unique autoDetectSysEx declared by the contracts (deduplicated).
     * @return SysEx messages to send, in order.
     */
    static std::vector<juce::MidiMessage> buildDetectionQueries(const std::vector<HardwareContract>& contracts);

    /**
     * @brief Helper to convert a whitespace-delimited hex string into a byte vector.
     */
    static std::vector<uint8_t> parseHexBytes(const std::string& hexStr);

private:
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    std::vector<HardwareContract> registeredContracts;
    std::vector<DiscoveredDevice> currentScanResults;
    juce::CriticalSection scanLock;
};

} // namespace abd::hwid