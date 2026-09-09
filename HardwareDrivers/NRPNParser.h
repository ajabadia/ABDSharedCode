#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>

namespace abd::hw {

struct NRPNMessage {
    int channel{ 1 };
    int nrpnMSB{ -1 };
    int nrpnLSB{ -1 };
    int dataMSB{ -1 };
    int dataLSB{ -1 };

    bool isComplete() const noexcept {
        return (nrpnMSB >= 0 && nrpnLSB >= 0 && dataMSB >= 0);
    }

    int getValue14Bit() const noexcept {
        int lsb = (dataLSB >= 0) ? dataLSB : 0;
        return (dataMSB << 7) | lsb;
    }
};

/**
 * @brief State Machine parser and encoder for MIDI NRPN (Non-Registered Parameter Numbers).
 */
class NRPNParser {
public:
    NRPNParser() = default;

    void reset() noexcept;

    // Process a CC message. Returns true if a full NRPN message was completed.
    bool processCC(int channel, int ccNumber, int ccValue, NRPNMessage& outMsg) noexcept;

    // Encode an NRPN into a MidiBuffer.
    // If use14Bit is true: dataValue is treated as 14-bit (MSB | LSB).
    // If use14Bit is false: dataValue is treated as 7-bit and only CC6 is sent.
    static void appendNRPNToBuffer(juce::MidiBuffer& buffer, int channel, int nrpnMSB, int nrpnLSB,
                                    int dataValue, bool use14Bit, int samplePosition = 0) noexcept;

private:
    int currentChannel_{ 1 };
    int currentNRPN_MSB_{ -1 };
    int currentNRPN_LSB_{ -1 };
    int currentDataMSB_{ -1 };
    int currentDataLSB_{ -1 };
};

} // namespace abd::hw
