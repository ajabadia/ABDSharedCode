#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace abd::synth {

namespace DSPUtils {

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float HALF_PI = 1.57079632679489661923f;

inline float clamp(float value, float minVal, float maxVal) noexcept
{
    return std::max(minVal, std::min(maxVal, value));
}

inline float midiNoteToFrequency(float midiNote) noexcept
{
    return 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
}

inline float semitonesAndCentsToRatio(float semitones, float cents) noexcept
{
    return std::pow(2.0f, (semitones + (cents / 100.0f)) / 12.0f);
}

inline float convertSysExToCutoffHz(float normalized0to1) noexcept
{
    const float minHz = 20.0f;
    const float maxHz = 20000.0f;
    return minHz * std::pow(maxHz / minHz, clamp(normalized0to1, 0.0f, 1.0f));
}

inline float decibelsToLinear(float dB) noexcept
{
    return std::pow(10.0f, dB * 0.05f);
}

inline float linearToDecibels(float linear) noexcept
{
    return (linear > 0.00001f) ? (20.0f * std::log10(linear)) : -100.0f;
}

// Analog saturation soft clipper (MS2000 style hyperbolic tangent)
inline float softClip(float x) noexcept
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    return std::tanh(x);
}

// Asymmetric distortion for MS2000 Amp Distortion
// Pre-gains the signal heavily, applies asymmetric tanh waveshaping,
// then applies makeup gain. The MS2000 distortion is deliberately gritty.
inline float ampDistortion(float x, float drive) noexcept
{
    float preGain = 1.0f + drive * 8.0f;  // Up to 7.8x pre-gain
    float in = x * preGain;
    float shaped;
    // Asymmetric clipping curve (even harmonics from asymmetry)
    if (in > 0.0f)
        shaped = std::tanh(in * 1.5f);
    else
        shaped = std::tanh(in * 2.0f) * 0.75f;
    // Makeup gain to restore perceived loudness
    return shaped * (1.0f / (0.5f + drive * 0.5f));
}

// Shared fast LCG-based random float in [-1.0, +1.0] range.
// All DSP modules should use this single RNG source for consistency.
inline float randomBipolar(uint32_t& state) noexcept
{
    state = state * 1664525u + 1013904223u;
    return (static_cast<float>(state) * (2.0f / 4294967295.0f)) - 1.0f;
}

template <typename T = float>
class LinearSmoother {
public:
    void reset(double sampleRate, double rampLengthSeconds) noexcept {
        steps_ = static_cast<int>(std::max(1.0, sampleRate * rampLengthSeconds));
        step_ = steps_;
    }
    void setCurrentAndTargetValue(T val) noexcept {
        current_ = target_ = val;
        step_ = steps_;
        stepSize_ = 0;
    }
    void setTargetValue(T target) noexcept {
        target_ = target;
        step_ = 0;
        stepSize_ = (steps_ > 0) ? ((target_ - current_) / static_cast<T>(steps_)) : 0;
    }
    T getNextValue() noexcept {
        if (step_ < steps_) {
            current_ += stepSize_;
            ++step_;
        } else {
            current_ = target_;
        }
        return current_;
    }
    T getCurrentValue() const noexcept { return current_; }
    bool isSmoothing() const noexcept { return step_ < steps_; }
private:
    T current_{ 0 };
    T target_{ 0 };
    T stepSize_{ 0 };
    int steps_{ 1 };
    int step_{ 1 };
};

} // namespace DSPUtils

} // namespace abd::synth
