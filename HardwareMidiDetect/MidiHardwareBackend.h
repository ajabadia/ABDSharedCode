/**
 * @file MidiHardwareBackend.h
 * @brief Host-injected MIDI transport backend for the HardwareMidiPicker.
 * @details This is the "bridge the calling software prepares" seam, modelled on
 *          the ABDScope ScopeDataCollector pattern. A plugin/host that embeds
 *          the picker implements this interface over its own juce::MidiOutput /
 *          juce::MidiInput ports, so the WebUI detects hardware without the host
 *          needing any contract/parser/UI logic.
 *
 *          Contract with the WebUI (see HardwareMidiDetect WebUI):
 *            - picker sends Sysex queries via sendBytes()
 *            - incoming MIDI (esp. Identity Reply) flows through setReceiveCallback()
 *            - the WebUI drives everything else
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <functional>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace abd::hwid
{

/**
 * @class MidiHardwareBackend
 * @brief Abstract transport that a host provides to the shared HardwareMidiPicker.
 *
 * The host is responsible for mapping these calls to real physical MIDI
 * (or a loopback / mock) and for installing a receive callback that forwards
 * inbound bytes. No logic lives here beyond the transport contract.
 */
class MidiHardwareBackend
{
public:
    virtual ~MidiHardwareBackend() = default;

    /** @brief Human display name of the current output port. */
    virtual std::string getOutputPortName() const = 0;

    /** @brief Send raw MIDI bytes (one or more complete SysEx messages). */
    virtual void sendBytes(const std::vector<uint8_t>& bytes) = 0;

    /**
     * @brief Install a callback invoked on the message thread when inbound MIDI
     *        bytes arrive (e.g. a SysEx Identity Reply). Call with nullptr to stop.
     */
    virtual void setReceiveCallback(std::function<void(const std::vector<uint8_t>&)> cb) = 0;

    /** @brief Start listening for inbound MIDI (optional; host may already listen). */
    virtual void startListening() = 0;

    /** @brief Stop listening for inbound MIDI. */
    virtual void stopListening() = 0;

    /** @brief Refresh the port list (e.g. after a device was plugged). */
    virtual void refreshPorts() = 0;

    /** @brief Optional firmware/identity string already known by the host. */
    virtual std::string getKnownIdentity() const { return {}; }
};

} // namespace abd::hwid