/**
 * @file CasioNibbleCodec.cpp
 * @brief Implementation of Casio CZ 4-bit nibble encoder/decoder and checksum.
 * @author ABDSynths
 * @date 2026
 */

#include "CasioNibbleCodec.h"

namespace abd::hw
{

bool CasioNibbleCodec::encode(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest, NibbleOrder order) noexcept
{
    if (srcLen > 0 && src == nullptr)
        return false;

    dest.clear();
    dest.reserve(srcLen * 2);

    for (size_t i = 0; i < srcLen; ++i)
    {
        uint8_t byte = src[i];
        uint8_t high = (byte >> 4) & 0x0F;
        uint8_t low  = byte & 0x0F;

        if (order == NibbleOrder::HighFirst)
        {
            dest.push_back(high);
            dest.push_back(low);
        }
        else
        {
            dest.push_back(low);
            dest.push_back(high);
        }
    }

    return true;
}

bool CasioNibbleCodec::decode(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest, NibbleOrder order) noexcept
{
    if (srcLen == 0)
    {
        dest.clear();
        return true;
    }

    if (src == nullptr || (srcLen % 2) != 0)
        return false;

    dest.clear();
    dest.reserve(srcLen / 2);

    for (size_t i = 0; i < srcLen; i += 2)
    {
        uint8_t first  = src[i] & 0x0F;
        uint8_t second = src[i + 1] & 0x0F;

        uint8_t byte = 0;
        if (order == NibbleOrder::HighFirst)
        {
            byte = static_cast<uint8_t>((first << 4) | second);
        }
        else
        {
            byte = static_cast<uint8_t>((second << 4) | first);
        }

        dest.push_back(byte);
    }

    return true;
}

uint8_t CasioNibbleCodec::calculateChecksum(const uint8_t* data, size_t len) noexcept
{
    if (data == nullptr || len == 0)
        return 0;

    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i)
    {
        sum += data[i];
    }

    return static_cast<uint8_t>(sum & 0x7F);
}

} // namespace abd::hw
