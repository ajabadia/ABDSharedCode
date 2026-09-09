#include "NRPNParser.h"
#include <algorithm>

namespace abd::hw {

void NRPNParser::reset() noexcept
{
    currentChannel_ = 1;
    currentNRPN_MSB_ = -1;
    currentNRPN_LSB_ = -1;
    currentDataMSB_ = -1;
    currentDataLSB_ = -1;
}

bool NRPNParser::processCC(int channel, int ccNumber, int ccValue, NRPNMessage& outMsg) noexcept
{
    // If channel changes, reset partial state
    if (channel != currentChannel_)
    {
        reset();
        currentChannel_ = channel;
    }

    switch (ccNumber)
    {
        case 99: // NRPN MSB
            currentNRPN_MSB_ = ccValue & 0x7F;
            currentDataMSB_ = -1;
            currentDataLSB_ = -1;
            break;

        case 98: // NRPN LSB
            currentNRPN_LSB_ = ccValue & 0x7F;
            currentDataMSB_ = -1;
            currentDataLSB_ = -1;
            break;

        case 6:  // Data Entry MSB
            if (currentNRPN_MSB_ >= 0 && currentNRPN_LSB_ >= 0)
            {
                currentDataMSB_ = ccValue & 0x7F;
                outMsg.channel = currentChannel_;
                outMsg.nrpnMSB = currentNRPN_MSB_;
                outMsg.nrpnLSB = currentNRPN_LSB_;
                outMsg.dataMSB = currentDataMSB_;
                outMsg.dataLSB = currentDataLSB_;
                return true;
            }
            break;

        case 38: // Data Entry LSB
            if (currentNRPN_MSB_ >= 0 && currentNRPN_LSB_ >= 0 && currentDataMSB_ >= 0)
            {
                currentDataLSB_ = ccValue & 0x7F;
                outMsg.channel = currentChannel_;
                outMsg.nrpnMSB = currentNRPN_MSB_;
                outMsg.nrpnLSB = currentNRPN_LSB_;
                outMsg.dataMSB = currentDataMSB_;
                outMsg.dataLSB = currentDataLSB_;
                return true;
            }
            break;

        default:
            break;
    }

    return false;
}

void NRPNParser::appendNRPNToBuffer(juce::MidiBuffer& buffer, int channel, int nrpnMSB, int nrpnLSB,
                                     int dataValue, bool use14Bit, int samplePosition) noexcept
{
    int ch = std::max(1, std::min(16, channel));

    // CC 99: NRPN MSB
    buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 99, nrpnMSB & 0x7F), samplePosition);
    // CC 98: NRPN LSB
    buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 98, nrpnLSB & 0x7F), samplePosition);

    if (use14Bit)
    {
        // 14-bit value: CC 6 = MSB, CC 38 = LSB
        int msb = (dataValue >> 7) & 0x7F;
        int lsb = dataValue & 0x7F;
        buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 6, msb), samplePosition);
        buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 38, lsb), samplePosition);
    }
    else
    {
        // 7-bit value: CC 6 only
        buffer.addEvent(juce::MidiMessage::controllerEvent(ch, 6, dataValue & 0x7F), samplePosition);
    }
}

} // namespace abd::hw
