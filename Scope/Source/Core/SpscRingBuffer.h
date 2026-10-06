#pragma once

#include <atomic>
#include <cstddef>
#include <vector>
#include <algorithm>

namespace abd::scope {

/**
 * Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 *
 * Guarantees:
 * - Real-time safe: Zero heap allocation, zero blocking locks.
 * - Memory Ordering: Release on write, Acquire on read.
 * - Power-of-two capacity for fast bitwise wrapping.
 */
template <typename T>
class SpscRingBuffer final {
public:
    explicit SpscRingBuffer(size_t capacity = 4096) {
        // Round up capacity to next power of two
        size_t cap = 16;
        while (cap < capacity) cap <<= 1;
        
        m_mask = cap - 1;
        m_buffer.assign(cap, T{});
        m_writeIndex.store(0, std::memory_order_relaxed);
        m_readIndex.store(0, std::memory_order_relaxed);
    }

    ~SpscRingBuffer() = default;
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    /**
     * Push samples into buffer from audio thread (Single Producer).
     * Returns number of samples successfully written.
     */
    size_t write(const T* data, size_t count) noexcept {
        if (data == nullptr || count == 0) return 0;

        const size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
        const size_t readIdx = m_readIndex.load(std::memory_order_acquire);
        const size_t capacity = m_buffer.size();
        const size_t available = capacity - (writeIdx - readIdx);
        const size_t toWrite = std::min(count, available);

        if (toWrite == 0) return 0;

        const size_t offset = writeIdx & m_mask;
        const size_t firstPart = std::min(toWrite, capacity - offset);
        const size_t secondPart = toWrite - firstPart;

        std::copy_n(data, firstPart, m_buffer.data() + offset);
        if (secondPart > 0) {
            std::copy_n(data + firstPart, secondPart, m_buffer.data());
        }

        m_writeIndex.store(writeIdx + toWrite, std::memory_order_release);
        return toWrite;
    }

    /**
     * Read samples from UI / message thread (Single Consumer).
     * Returns number of samples successfully read.
     */
    size_t read(T* destination, size_t count) noexcept {
        if (destination == nullptr || count == 0) return 0;

        const size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
        const size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
        const size_t available = writeIdx - readIdx;
        const size_t toRead = std::min(count, available);

        if (toRead == 0) return 0;

        const size_t capacity = m_buffer.size();
        const size_t offset = readIdx & m_mask;
        const size_t firstPart = std::min(toRead, capacity - offset);
        const size_t secondPart = toRead - firstPart;

        std::copy_n(m_buffer.data() + offset, firstPart, destination);
        if (secondPart > 0) {
            std::copy_n(m_buffer.data(), secondPart, destination + firstPart);
        }

        m_readIndex.store(readIdx + toRead, std::memory_order_release);
        return toRead;
    }

    /**
     * Get number of samples available for reading.
     */
    [[nodiscard]] size_t getAvailableRead() const noexcept {
        const size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
        const size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
        return (writeIdx >= readIdx) ? (writeIdx - readIdx) : 0;
    }

    /**
     * Reset read/write pointers. Safe only when no concurrent read/write is occurring.
     */
    void reset() noexcept {
        m_writeIndex.store(0, std::memory_order_relaxed);
        m_readIndex.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<T> m_buffer;
    size_t m_mask { 0 };
    alignas(64) std::atomic<size_t> m_writeIndex { 0 };
    alignas(64) std::atomic<size_t> m_readIndex { 0 };
};

} // namespace abd::scope
