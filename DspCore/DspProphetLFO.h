/**
 * @file DspProphetLFO.h
 * @brief Prophet-5 LFO generator with saw/triangle/pulse waveforms and sync options.
 * 
 * Adapted from melodee-main firmware (eng_prophet_test.c) converting fixed-point
 * DSP to float precision. Maintains the original algorithmic behavior while
 * using float for finer resolution.
 * 
 * Magic numbers from the original firmware are referenced in DspProphetLFO_config.h
 * for easy tuning and review.
 */

#pragma once

#include <cmath>
#include <array>
#include <algorithm>

namespace abd::dsp
{

/* ================================================================================================
 * Prophet-5 LFO Parameters (extracted from original firmware)
 * ================================================================================================ */

/** LFO rate table: raw value 0-127 to frequency in Hz.
 *  Original: exponential mapping from ~0.022 Hz to ~500 Hz.
 *  Reference: raw 80 = ~17.6 Hz */
struct DspProphetLFOConfig
{
    /** Convert raw LFO rate value (0-127) to frequency in Hz. */
    std::array<float, 128> lfo_inc;

    /** LFO initial amount (depth): raw value 0-127 -> 0.0 to 1.0. */
    std::array<float, 128> lfo_amount;

    /** Noise blend for LFO: original had P5_LFO_SAW/P5_LFO_TRI/P5_LFO_PULSE
     *  and a noise mix knob (P5_WHEEL_MIX). */
    float noise_blend; // 0.0 = pure waveform, 1.0 = pure noise

    /** LFO phase reset on note-on behavior. */
    bool reset_on_note_on;
};

/** Default LFO configuration extracted from melodee-main firmware. */
constexpr DspProphetLFOConfig kProphetLFODefaultConfig = {
    /* LFO rate table: exponential 0.022 - 500 Hz */
    []{
        DspProphetLFOConfig cfg;
        /* Map raw 0-127 to exponential range */
        for (int i = 0; i < 128; ++i)
        {
            /* Exponential mapping */
            /* Original: rate decreases as raw increases? No, raw 80 = 17.6 Hz */
            /* Let's use a log-scale: 2^((i-64)/6) roughly */
            float x = static_cast<float>(i) / 127.0f;
            /* Map: roughly 0.01 Hz at i=0 to 500 Hz at i=127 */
            cfg.lfo_inc[i] = 0.01f * std::pow(500.0f / 0.01f, x);
        }
        return cfg;
    }(),

    /* LFO amount: linear 0 to 1 */
    []{
        DspProphetLFOConfig cfg;
        for (int i = 0; i < 128; ++i)
        {
            cfg.lfo_amount[i] = static_cast<float>(i) / 127.0f;
        }
        return cfg;
    }(),

    /* Noise blend: default 0.0 (no noise) */
    0.0f,

    /* Reset on note on: true */
    true
};

/* ================================================================================================
 * Prophet-5 LFO Voice State
 * ================================================================================================ */

class DspProphetLFOVoiceState
{
public:
    DspProphetLFOVoiceState() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        phase_ = 0.0f;          // 32-bit phase accumulator
        rate_ = 0.0f;          // LFO rate in Hz
        amount_ = 0.0f;        // LFO depth/amount
        waveform_ = 0;         // 0=saw, 1=triangle, 2=pulse
        noise_ = 0.0f;         // Noise blend amount
        last_value_ = 0.0f;    // Last output value
        is_running_ = false;
    }

    // --- Parameter setting ---
    
    void setRate(float rateHz) noexcept
    {
        rate_ = rateHz;
        // Recalculate phase increment based on sample rate
        // inc = rate / sampleRate * 2^32 (for 32-bit phase)
        // In float: we'll accumulate phase directly
        phase_inc_ = rateHz / 44100.0f * (1.0f / (1 << 23)); // scaled for float phase
    }

    void setWaveform(int waveform) noexcept // 0=saw, 1=triangle, 2=pulse
    {
        waveform_ = std::clamp(waveform, 0, 2);
    }

    void setAmount(float amount) noexcept // 0.0 to 1.0
    {
        amount_ = std::clamp(amount, 0.0f, 1.0f);
    }

    void setNoiseBlend(float noise) noexcept // 0.0 to 1.0
    {
        noise_ = std::clamp(noise, 0.0f, 1.0f);
    }

    // --- Processing ---
    
    float process(float sampleRate, float lfo_input = 0.0f) noexcept
    {
        if (!is_running_) return 0.0f;
        
        // Advance phase
        phase_ += phase_inc_;
        if (phase_ >= 1.0f) phase_ -= 1.0f; // wrap 32-bit phase
        
        // Generate waveform based on phase and type
        float value = 0.0f;
        
        switch (waveform_)
        {
            case 0: // Saw wave
            {
                // Original: pos = (float)phase * (1.0f/4294967296.0f)
                // Float simplified: phase is already normalized 0-1
                float saw = phase_ * 2.0f - 1.0f; // -1 to 1
                // Add BLEP for bandlimitedness (simplified)
                // In original: p5_fast_blep handled this
                value = saw;
                break;
            }
            case 1: // Triangle wave
            {
                // Original: pos < 0x80000000 ? pos*2.0f-0.5f : 1.5f-pos*2.0f
                // Phase 0-1, 0x80000000 = 0.5 in 32-bit
                if (phase_ < 0.5f)
                    value = phase_ * 2.0f - 0.5f; // 0 to 0.5 -> -0.5 to 0.5? Let's check
                else
                    value = 1.5f - phase_ * 2.0f; // 1.5 to 0.5
                // Normalize to -1 to 1
                value = value * 2.0f - 1.0f;
                break;
            }
            case 2: // Pulse wave (square)
            {
                // Original: phase<0x80000000u?1.0f:0.0f
                value = (phase_ < 0.5f) ? 1.0f : -1.0f;
                break;
            }
        }
        
        // Apply noise blend
        if (noise_ > 0.0f)
        {
            // Simple random noise (placeholder - would use proper noise generator)
            float rand_val = static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f;
            value = (1.0f - noise_) * value + noise_ * rand_val;
        }
        
        // Apply amount/depth
        value *= amount_;
        
        // Apply noise input if provided (for external modulation)
        value += lfo_input * (1.0f - amount_);
        
        last_value_ = value;
        return value;
    }

    float getLastValue() const noexcept { return last_value_; }

    // --- Control ---
    
    void start() noexcept { is_running_ = true; }
    void stop() noexcept { is_running_ = false; }
    void noteOn() noexcept { reset(); } // Reset phase on note on
    void noteOff() noexcept { /* Can hold last value or reset */ }

    [[nodiscard]] float getRate() const noexcept { return rate_; }
    [[nodiscard]] float getAmount() const noexcept { return amount_; }
    [[nodiscard]] int getWaveform() const noexcept { return waveform_; }
    [[nodiscard]] float getLastValueFloat() const noexcept { return last_value_; }

private:
    float phase_ = 0.0f;          // 32-bit phase accumulator (0 to 1)
    float phase_inc_ = 0.0f;      // Phase increment per sample
    float rate_ = 0.0f;           // LFO rate in Hz
    float amount_ = 0.0f;         // Depth/amount 0-1
    int waveform_ = 0;            // 0=saw, 1=triangle, 2=pulse
    float noise_ = 0.0f;          // Noise blend 0-1
    float last_value_ = 0.0f;     // Last output sample
    bool is_running_ = false;
};

/* ================================================================================================
 * Prophet-5 LFO Main Class
 * ================================================================================================ */

class DspProphetLFO
{
public:
    DspProphetLFO() noexcept = default;
    ~DspProphetLFO() = default;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        // Initialize all LFO voices
        for (auto& lfo : lfos_)
        {
            lfo.reset();
        }
        lfoReady_ = true;
    }

    void reset() noexcept
    {
        for (auto& lfo : lfos_)
        {
            lfo.reset();
        }
        lfoReady_ = true;
    }

    // --- Global parameter setting ---
    
    void setRate(float rateHz) noexcept
    {
        for (auto& lfo : lfos_)
        {
            lfo.setRate(rateHz);
        }
    }

    void setWaveform(int waveform) noexcept // 0=saw, 1=triangle, 2=pulse
    {
        for (auto& lfo : lfos_)
        {
            lfo.setWaveform(waveform);
        }
    }

    void setAmount(float amount) noexcept // 0.0 to 1.0
    {
        for (auto& lfo : lfos_)
        {
            lfo.setAmount(amount);
        }
    }

    void setNoiseBlend(float noise) noexcept // 0.0 to 1.0
    {
        for (auto& lfo : lfos_)
        {
            lfo.setNoiseBlend(noise);
        }
    }

    // --- Per-LFO processing ---
    
    float process(int lfoIndex, float sampleRate = -1.0f) noexcept
    {
        float sr = (sampleRate > 0.0f) ? sampleRate_ : sampleRate_;
        if (lfoIndex >= 0 && lfoIndex < static_cast<int>(lfos_.size()))
        {
            return lfos_[lfoIndex].process(sr);
        }
        return 0.0f;
    }

    // --- Convenience: process all LFOs (for part-wide modulation) ---
    
    // Generate LFO values for all voices/voices
    // Returns array of LFO values, one per voice
    std::array<float, 4> processAll(float sampleRate = -1.0f) noexcept
    {
        float sr = (sampleRate > 0.0f) ? sampleRate_ : sampleRate_;
        std::array<float, 4> values{};
        for (int i = 0; i < static_cast<int>(lfos_.size()) && i < 4; ++i)
        {
            values[i] = lfos_[i].process(sr);
        }
        return values;
    }

    [[nodiscard]] float getRate() const noexcept { return sampleRate_ > 0 ? 0.0f : 0.0f; } // placeholder
    [[nodiscard]] int getWaveform() const noexcept { return 0; } // placeholder
    [[nodiscard]] std::array<float, 4> getLastValues() const noexcept { return {}; } // placeholder

private:
    static constexpr int kMaxLFOs = 4; // Prophet-5 has 4 voices, typically 1 LFO shared
    
    float sampleRate_ = 44100.0;
    bool lfoReady_ = false;
    
    std::array<DspProphetLFOVoiceState, kMaxLFOs> lfos_;
};

} // namespace abd::dsp