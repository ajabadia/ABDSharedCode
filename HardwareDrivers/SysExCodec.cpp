#include "SysExCodec.h"

namespace abd::hw {

bool SysExCodec::unpack7to8(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept
{
    if (src == nullptr || srcLen == 0) return false;

    dest.clear();
    dest.reserve((srcLen * 7) / 8);

    size_t srcIndex = 0;
    while (srcIndex < srcLen)
    {
        uint8_t msbCollector = src[srcIndex++];

        for (int i = 0; i < 7; ++i)
        {
            if (srcIndex >= srcLen) break;

            uint8_t current7Bit = src[srcIndex++];
            uint8_t bit7 = (msbCollector >> i) & 0x01;
            uint8_t original8Bit = current7Bit | static_cast<uint8_t>(bit7 << 7);

            dest.push_back(original8Bit);
        }
    }

    return true;
}

bool SysExCodec::pack8to7(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept
{
    if (src == nullptr || srcLen == 0) return false;

    dest.clear();
    dest.reserve(((srcLen + 6) / 7) * 8);

    size_t srcIndex = 0;
    while (srcIndex < srcLen)
    {
        size_t chunkSize = std::min(static_cast<size_t>(7), srcLen - srcIndex);
        uint8_t msbCollector = 0;

        // First pass: collect MSBs (bit 7) of up to 7 bytes
        for (size_t i = 0; i < chunkSize; ++i)
        {
            uint8_t b = src[srcIndex + i];
            if ((b & 0x80) != 0)
            {
                msbCollector |= static_cast<uint8_t>(1 << i);
            }
        }

        // Push MSB collector byte
        dest.push_back(msbCollector);

        // Push the 7-bit masked data bytes
        for (size_t i = 0; i < chunkSize; ++i)
        {
            dest.push_back(src[srcIndex + i] & 0x7F);
        }

        srcIndex += chunkSize;
    }

    return true;
}

} // namespace abd::hw
