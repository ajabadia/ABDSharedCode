#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <utility>
#include "ScopeTapType.h"
#include "SpscRingBuffer.h"

namespace abd::scope {

/**
 * Individual Telemetry Tap inserted into any point of the audio/DSP chain.
 *
 * Performance guarantee:
 * - When inactive, check costs < 1 ns (single relaxed atomic load).
 * - When active, writes samples to lock-free ring buffers without heap allocation.
 */
class ScopeTap final {
public:
    ScopeTap(std::string name, ScopeTapType type, size_t bufferCapacity = 4096, std::string id = {})
        : m_name(std::move(name)),
          m_id(std::move(id)),
          m_type(type),
          m_bufferL(bufferCapacity),
          m_bufferR(type == ScopeTapType::StereoAudio ? bufferCapacity : 0)
    {
    }

    ~ScopeTap() = default;
    ScopeTap(const ScopeTap&) = delete;
    ScopeTap& operator=(const ScopeTap&) = delete;

    [[nodiscard]] const std::string& getName() const noexcept { return m_name; }

    /**
     * Stable wire-protocol identifier (slug). If empty, the serializer derives a
     * deterministic slug from the display name via makeSlug().
     */
    [[nodiscard]] const std::string& getId() const noexcept { return m_id; }

    void setId(std::string id) noexcept { m_id = std::move(id); }

    [[nodiscard]] ScopeTapType getType() const noexcept { return m_type; }

    [[nodiscard]] bool isActive() const noexcept {
        return m_isActive.load(std::memory_order_relaxed);
    }

    void setActive(bool active) noexcept {
        m_isActive.store(active, std::memory_order_relaxed);
        if (!active) {
            m_bufferL.reset();
            m_bufferR.reset();
        }
    }

    /**
     * Push mono/control samples from audio thread.
     * Cost: < 1 ns if inactive.
     */
    void write(const float* channelData, size_t numSamples) noexcept {
        if (!m_isActive.load(std::memory_order_relaxed) || channelData == nullptr) return;
        m_bufferL.write(channelData, numSamples);
    }

    /**
     * Push stereo samples from audio thread.
     * Cost: < 1 ns if inactive.
     */
    void writeStereo(const float* leftData, const float* rightData, size_t numSamples) noexcept {
        if (!m_isActive.load(std::memory_order_relaxed)) return;
        if (leftData != nullptr) m_bufferL.write(leftData, numSamples);
        if (rightData != nullptr && m_type == ScopeTapType::StereoAudio) {
            m_bufferR.write(rightData, numSamples);
        }
    }

    /**
     * Read captured samples on message thread.
     */
    size_t read(float* destL, float* destR, size_t numSamples) noexcept {
        const size_t readL = m_bufferL.read(destL, numSamples);
        if (destR != nullptr && m_type == ScopeTapType::StereoAudio) {
            m_bufferR.read(destR, numSamples);
        }
        return readL;
    }

    [[nodiscard]] size_t getAvailableRead() const noexcept {
        return m_bufferL.getAvailableRead();
    }

private:
    std::string m_name;
    std::string m_id;
    ScopeTapType m_type;
    alignas(64) std::atomic<bool> m_isActive { false };
    SpscRingBuffer<float> m_bufferL;
    SpscRingBuffer<float> m_bufferR;
};

} // namespace abd::scope
