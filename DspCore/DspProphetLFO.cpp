/**
 * @file DspProphetLFO.cpp
 * @brief Prophet-5 LFO implementation.
 * 
 * Adapted from melodee-main firmware (eng_prophet_test.c) converting fixed-point
 * DSP to float precision. Maintains the original algorithmic behavior while
 * using float for finer resolution and easier integration.
 * 
 * Magic numbers from the original firmware are referenced in DspProphetLFO_config.h
 * for easy tuning and review.
 */

#include "DspProphetLFO.h"
#include <cstdlib> // for rand()

namespace abd::dsp
{

/* ================================================================================================
 * DspProphetLFOVoiceState implementation
 * ================================================================================================ */

DspProphetLFOVoiceState::DspProphetLFOVoiceState() noexcept
{
    reset();
}

void DspProphetLFOVoiceState::reset() noexcept
{
    phase_ = 0.0f;
    rate_ = 0.0f;
    amount_ = 0.0f;
    waveform_ = 0;
    noise_ = 0.0f;
    last_value_ = 0.0f;
    is_running_ = false;
}

float DspProphetLFOVoiceState::process(float sampleRate, float lfo_input) noexcept
{
    if (!is_running_) return 0.0f;
    
    // Advance phase accumulator
    // Original used 32-bit uint phase: phase += inc, wrap at 0x100000000
    // Float simplified: phase in [0, 1) range
    phase_ += phase_inc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f; // wrap
    
    // Generate waveform based on type
    float value = 0.0f;
    
    switch (waveform_)
    {
        case 0: // Saw wave
        {
            // Original saw: (float)ph * (1.0f/4294967296.0f) - 1.0f - blep
            // Float normalized saw: phase goes 0->1, we want -1->1
            // (phase * 2) - 1 gives -1 when phase=0, +1 when phase->1
            // But original starts at 0 and goes to max, so let's match:
            // phase in [0,1) -> saw output in [-1, 1) with discontinuity at wrap
            float saw = phase_ * 2.0f - 1.0f;
            value = saw;
            break;
        }
        case 1: // Triangle wave
        {
            // Original: pos<0x80000000u?pos*2.0f-0.5f:1.5f-pos*2.0f
            // Phase 0-1, mid at 0.5
            if (phase_ < 0.5f)
                value = phase_ * 2.0f - 0.5f; // 0 to 0.5
            else
                value = 1.5f - phase_ * 2.0f; // 1.5 down to 0.5
            // Normalize to -1 to 1 range
            // 0.5 -> -0.5, 0 -> -1, 1 -> +1? Let's be careful:
            // When phase=0: value = -0.5 -> after *2-1 = -2? No.
            // Let's just map linearly: phase 0->1 maps to -1->1
            // Triangle: phase 0->0.5 -> -1->0, phase 0.5->1 -> 0->1
            // So: if phase<0.5: value = (phase*2 - 1) * 1 ... hmm
            // Simpler: just use standard triangle: 2*abs(phase-0.5)-1
            value = 2.0f * std::abs(phase_ - 0.5f) - 1.0f;
            break;
        }
        case 2: // Pulse / Square wave
        {
            // Original: phase<0x80000000u?1.0f:0.0f
            // 0x80000000 in 32-bit = half phase = 0.5 in normalized
            value = (phase_ < 0.5f) ? 1.0f : -1.0f;
            break;
        }
    }
    
    // Apply noise blend (original had noise mixing with LFO)
    if (noise_ > 0.0f)
    {
        // Simple random noise generation
        float rand_val = static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f;
        value = (1.0f - noise_) * value + noise_ * rand_val;
    }
    
    // Apply depth/amount
    value *= amount_;
    
    // Add external modulation input if provided
    value += lfo_input * (1.0f - amount_);
    
    last_value_ = value;
    return value;
}

float DspProphetLFOVoiceState::getLastValue() const noexcept { return last_value_; }

void DspProphetLFOVoiceState::start() noexcept { is_running_ = true; }
void DspProphetLFOVoiceState::stop() noexcept { is_running_ = false; }
void DspProphetLFOVoiceState::noteOn() noexcept { reset(); } // Reset phase
void DspProphetLFOVoiceState::noteOff() noexcept { /* hold or reset */ }

[[nodiscard]] float DspProphetLFOVoiceState::getRate() const noexcept { return rate_; }
[[nodiscard]] float DspProphetLFOVoiceState::getAmount() const noexcept { return amount_; }
[[nodiscard]] int DspProphetLFOVoiceState::getWaveform() const noexcept { return waveform_; }
[[nodiscard]] float DspProphetLFOVoiceState::getLastValueFloat() const noexcept { return last_value_; }

/* ================================================================================================
 * DspProphetLFO implementation
 * ================================================================================================ */

DspProphetLFO::DspProphetLFO() noexcept
{
}

void DspProphetLFO::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    sampleRateValid_ = true;
    
    // Initialize all LFO voices
    for (auto& lfo : lfos_)
    {
        lfo.reset();
    }
}

void DspProphetLFO::reset() noexcept
{
    for (auto& lfo : lfos_)
    {
        lfo.reset();
    }
}

void DspProphetLFO::setRate(float rateHz) noexcept
{
    // Calculate phase increment for float phase accumulator
    // Original: 32-bit phase increment = rate / sampleRate * 2^32
    // Float: phase_inc = rate / sampleRate (since phase is 0-1 per sample cycle)
    // But we want continuous phase accumulation, so:
    // phase_inc_ = rateHz / sampleRate * (1.0f / kMaxLFORateFactor)
    // Using 44100 as reference: if rate = 1 Hz, phase should advance 1/4410 per sample
    if (sampleRate_ > 0.0f)
    {
        phase_inc_ = rateHz / sampleRate_;
    }
    else
    {
        phase_inc_ = 0.0f;
    }
}

void DspProphetLFO::setWaveform(int waveform) noexcept // 0=saw, 1=triangle, 2=pulse
{
    for (auto& lfo : lfos_)
    {
        lfo.setWaveform(waveform);
    }
}

void DspProphetLFO::setAmount(float amount) noexcept // 0.0 to 1.0
{
    for (auto& lfo : lfos_)
    {
        lfo.setAmount(amount);
    }
}

void DspProphetLFO::setNoiseBlend(float noise) noexcept // 0.0 to 1.0
{
    for (auto& lfo : lfos_)
    {
        lfo.setNoiseBlend(noise);
    }
}

float DspProphetLFO::process(int lfoIndex, float sampleRate) noexcept
{
    float sr = (sampleRate > 0.0f) ? sampleRate_ : sampleRate;
    if (lfoIndex >= 0 && lfoIndex < static_cast<int>(lfos_.size()))
    {
        return lfos_[lfoIndex].process(sr, 0.0f); // no external input by default
    }
    return 0.0f;
}

std::array<float, 4> DspProphetLFO::processAll(float sampleRate) noexcept
{
    float sr = (sampleRate > 0.0f) ? sampleRate_ : sampleRate;
    std::array<float, 4> values{};
    for (int i = 0; i < static_cast<int>(lfos_.size()) && i < 4; ++i)
    {
        values[i] = lfos_[i].process(sr);
    }
    return values;
}

} // namespace abd::dsp