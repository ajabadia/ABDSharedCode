#include "SysExCodec.h"

namespace abd::hw
{

namespace
{

enum class Order
{
    Canonical,
    Korg
};

// Collector bit that carries the MSB of byte i under the given order:
// canonical puts byte i in bit i; Korg puts byte j in bit 6-j (first byte in
// the collector's high bit).
constexpr size_t collectorBit(Order order, size_t i) noexcept
{
    return order == Order::Korg ? (6 - i) : i;
}

bool packImpl(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest,
              Order order, bool padFinalGroup) noexcept
{
    if (src == nullptr || srcLen == 0) return false;

    dest.clear();
    dest.reserve(((srcLen + 6) / 7) * 8);

    size_t srcIndex = 0;
    while (srcIndex < srcLen)
    {
        const size_t chunkSize = std::min(static_cast<size_t>(7), srcLen - srcIndex);
        uint8_t msbCollector   = 0;

        // First pass: collect MSBs (bit 7) of up to 7 bytes
        for (size_t i = 0; i < chunkSize; ++i)
        {
            uint8_t b = src[srcIndex + i];
            if ((b & 0x80) != 0)
            {
                msbCollector |= static_cast<uint8_t>(1u << collectorBit(order, i));
            }
        }

        // Push MSB collector byte
        dest.push_back(msbCollector);

        // Push the 7-bit masked data bytes (zero-padded when asked to)
        const size_t emitted = padFinalGroup ? static_cast<size_t>(7) : chunkSize;
        for (size_t i = 0; i < emitted; ++i)
        {
            const uint8_t b = i < chunkSize ? src[srcIndex + i] : 0;
            dest.push_back(static_cast<uint8_t>(b & 0x7F));
        }

        srcIndex += chunkSize;
    }

    return true;
}

bool unpackImpl(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest, Order order) noexcept
{
    if (src == nullptr || srcLen == 0) return false;

    dest.clear();
    dest.reserve((srcLen * 7) / 8);

    size_t srcIndex = 0;
    while (srcIndex < srcLen)
    {
        uint8_t msbCollector = src[srcIndex++];

        for (size_t i = 0; i < 7; ++i)
        {
            if (srcIndex >= srcLen) break;

            uint8_t current7Bit  = src[srcIndex++];
            uint8_t bit7         = (msbCollector >> collectorBit(order, i)) & 0x01;
            uint8_t original8Bit = current7Bit | static_cast<uint8_t>(bit7 << 7);

            dest.push_back(original8Bit);
        }
    }

    return true;
}

} // namespace

bool SysExCodec::unpack7to8(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept
{
    return unpackImpl(src, srcLen, dest, Order::Canonical);
}

bool SysExCodec::pack8to7(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept
{
    return packImpl(src, srcLen, dest, Order::Canonical, false);
}

bool SysExCodec::pack8to7Padded(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept
{
    return packImpl(src, srcLen, dest, Order::Canonical, true);
}

bool SysExCodec::pack8to7Korg(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept
{
    return packImpl(src, srcLen, dest, Order::Korg, false);
}

bool SysExCodec::pack8to7KorgPadded(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept
{
    return packImpl(src, srcLen, dest, Order::Korg, true);
}

bool SysExCodec::unpack7to8Korg(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dest) noexcept
{
    return unpackImpl(src, srcLen, dest, Order::Korg);
}

} // namespace abd::hw
