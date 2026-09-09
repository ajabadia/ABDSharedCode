#pragma once
#include <cmath>
#include <algorithm>

namespace abd::synth {

namespace EnvelopeCurves {

/**
 * @brief Maps normalized [0.0, 1.0] knob value to real-world seconds based on MS2000 measured curves.
 * MS2000 Attack: ~0.5ms (min) to ~5.0s (max). Knob curve is steep — most useful range in first half.
 */
inline float getAttackTimeSeconds(float norm0to1) noexcept
{
    float norm = std::max(0.0f, std::min(1.0f, norm0to1));
    const float minTime = 0.0005f;  // 0.5 ms (snap attack)
    const float maxTime = 5.0f;     // 5 seconds
    float curved = std::pow(norm, 3.0f);
    return minTime + (maxTime - minTime) * curved;
}

/**
 * MS2000 Decay/Release: ~5ms (min) to ~10.0s (max).
 */
inline float getDecayReleaseTimeSeconds(float norm0to1) noexcept
{
    float norm = std::max(0.0f, std::min(1.0f, norm0to1));
    const float minTime = 0.005f;  // 5 ms
    const float maxTime = 10.0f;   // 10 seconds
    float curved = std::pow(norm, 3.0f);
    return minTime + (maxTime - minTime) * curved;
}

/**
 * @brief Computes exponential per-sample multiplier for capacitor decay discharge.
 */
inline double getDecayMultiplier(double timeSeconds, double sampleRate) noexcept
{
    if (timeSeconds <= 0.0001 || sampleRate <= 1000.0) return 0.0;
    // -4.605 corresponds to reaching ~1% (-40dB) at timeSeconds
    return std::exp(-4.60517 / (timeSeconds * sampleRate));
}

} // namespace EnvelopeCurves

} // namespace abd::synth
