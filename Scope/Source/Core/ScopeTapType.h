#pragma once

#include <cstdint>
#include <string_view>

namespace abd::scope {

/**
 * Type of audio / control signal captured by a ScopeTap.
 */
enum class ScopeTapType : uint8_t {
    StereoAudio = 0,    ///< Stereo audio signal (PCM bipolar +/-1.0, trigger active)
    MonoAudio = 1,      ///< Mono audio signal (PCM bipolar +/-1.0, trigger active)
    ControlSignal = 2   ///< Control signal (CV/LFO 0..1 or +/-1.0, trigger bypassed)
};

/**
 * Helper to convert ScopeTapType to wire-protocol string representation.
 */
[[nodiscard]] constexpr std::string_view toString(ScopeTapType type) noexcept {
    switch (type) {
        case ScopeTapType::StereoAudio:   return "audio";
        case ScopeTapType::MonoAudio:     return "audio";
        case ScopeTapType::ControlSignal: return "control";
    }
    return "audio";
}

} // namespace abd::scope
