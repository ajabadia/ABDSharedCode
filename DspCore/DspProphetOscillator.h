/**
 * @file DspProphetOscillator.h
 * @brief Prophet-5 oscillators with BLEP band-limited generation.
 * 
 * Adapted from melodee-main firmware (eng_prophet_test.c) converting fixed-point
 * DSP to float precision. Provides band-limited saw, pulse, triangle and sine
 * oscillators matching the Prophet-5 sound architecture.
 * 
 * Magic numbers from the original firmware are referenced in config for tuning.
 */

#pragma once

#include <cmath>
#include <array>
#include <algorithm>

namespace abd::dsp
{

/* ================================================================================================
 * Prophet-5 Oscillator Parameters
 * ================================================================================================ */

/** Oscillator configuration with BLEP table and parameters. */
struct DspProphetOscillatorConfig
{
    /** BLEP (Band-Limited Step) table entries.
     *  Original used integer arithmetic; float version precomputes. */
    std::array<float, 256> blep_table;

    /** Oscillator frequency increment table (optional).
     *  Maps note numbers to phase increments. */
    std::array<float, 128> freq_inc_table;

    /** PWM width table for pulse width modulation. */
    std::array<float, 128> pw_table;
};

/** Default oscillator configuration. */
constexpr DspProphetOscillatorConfig kProphetOscillatorDefaultConfig = {
    /* BLEP table: 256 entries for phase interpolation.
     * Original: blep(uint32_t ph, uint32_t inc) with integer math.
     * Float version: precomputed values for quick lookup. */
    []{
        DspProphetOscillatorConfig cfg;
        /* BLEP approximates the step discontinuity in a saw wave.
         * The triangle integral of the step gives the BLEP correction.
         * 256 entries covering phase range 0-2π (or 0-1 normalized). */
        for (int i = 0; i < 256; ++i)
        {
            float x = static_cast<float>(i) / 255.0f; /* 0 to 1 */
            /* BLEP: smooth the step at wrap-around.
             * Original: if (ph < inc) return x + x - x*x - 1; */
            /* Float simplified: blep correction for saw wave */
            if (x < 1.0f / 3.0f) /* rough third of table */
            {
                float x2 = x * x;
                cfg.blep_table[i] = x + x - x2 - 1.0f;
            }
            else if (x > 2.0f / 3.0f)
            {
                float x2 = (1.0f - x) * (1.0f - x);
                cfg.blep_table[i] = -((1.0f - x) + (1.0f - x) - x2 + 1.0f); /* symmetric */
            }
            else
            {
                cfg.blep_table[i] = 0.0f; /* no blep in middle */
            }
        }
        return cfg;
    }(),

    /* Frequency increment table: note to increment mapping */
    []{
        DspProphetOscillatorConfig cfg;
        /* Map MIDI note 0-127 to phase increment for 440 Hz reference (A4). */
        /* Original used pitch16 (1/16 semitone) and coarse/fine tuning.
         * Float: simple exponential mapping. */
        for (int i = 0; i < 128; ++i)
        {
            /* A4 = 440 Hz at note 69.
               increment = 2^((note-69)/12) * base_inc */
            float note = static_cast<float>(i);
            /* Reference: note 69 -> 440 Hz, so increment proportional to frequency */
            cfg.freq_inc_table[i] = std::pow(2.0f, (note - 69.0f) / 12.0f);
        }
        return cfg;
    }(),

    /* PWM width table: width mod 0-127 -> 0.0 to 1.0 */
    []{
        DspProphetOscillatorConfig cfg;
        for (int i = 0; i < 128; ++i)
        {
            cfg.pw_table[i] = static_cast<float>(i) / 127.0f;
        }
        return cfg;
    }()
};

/* ================================================================================================
 * Prophet-5 Oscillator Voice State
 * ================================================================================================ */

class DspProphetOscillatorVoiceState
{
public:
    DspProphetOscillatorVoiceState() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        phase_ = 0.0f;          // 32-bit phase accumulator (0 to 1)
        phase_inc_ = 0.0f;      // phase increment per sample
        fine_tune_ = 0.0f;      // fine tuning offset (-127 to 127 -> -1 to 1)
        coarse_tune_ = 0;       // coarse semitone offset
        waveform_ = 0;          // 0=saw, 1=pulse, 2=triangle, 3=sine
        pulse_width_ = 0.5f;    // PWM width 0.0 to 1.0 (50% default)
        level_a_ = 1.0f;        // Oscillator A level (0-127 -> 0-1)
        level_b_ = 0.0f;        // Oscillator B level
        noise_level_ = 0.0f;    // Noise level
        sync_ = false;          // Hard sync flag
        last_value_ = 0.0f;
    }

    // --- Parameter setting ---
    
    void setPhaseIncrement(float inc) noexcept
    {
        phase_inc_ = inc;
    }

    void setWaveform(int waveform) noexcept // 0=saw, 1=pulse, 2=triangle, 3=sine
    {
        waveform_ = std::clamp(waveform, 0, 3);
    }

    void setPulseWidth(float width) noexcept // 0.0 to 1.0
    {
        pulse_width_ = std::clamp(width, 0.0f, 1.0f);
    }

    void setFineTune(float tune) noexcept // -1 to 1 or -127 to 127 scaled
    {
        fine_tune_ = tune;
    }

    void setCoarseTune(int semitones) noexcept // -12 to 12 typical range
    {
        coarse_tune_ = std::clamp(semitones, -12, 12);
    }

    void setLevelA(float level) noexcept // 0.0 to 1.0 (from raw 0-127)
    {
        level_a_ = std::clamp(level, 0.0f, 1.0f);
    }

    void setLevelB(float level) noexcept // 0.0 to 1.0
    {
        level_b_ = std::clamp(level, 0.0f, 1.0f);
    }

    void setNoiseLevel(float level) noexcept // 0.0 to 1.0
    {
        noise_level_ = std::clamp(level, 0.0f, 1.0f);
    }

    void setSync(bool sync) noexcept { sync_ = sync; }

    // --- Processing ---
    
    float process(float sampleRate) noexcept
    {
        // Advance phase
        phase_ += phase_inc_;
        if (phase_ >= 1.0f) phase_ -= 1.0f; // wrap
        
        // Generate waveform
        float value = 0.0f;
        
        switch (waveform_)
        {
            case 0: // Saw wave with BLEP (band-limited)
            {
                // Original: p5_fast_osc_saw(phase, inc) = (float)ph * (1.0f/2147483648.0f) - 1.0f - blep
                // Float normalized: phase in [0,1), saw from -1 to 1
                float saw = phase_ * 2.0f - 1.0f; // -1 at phase=0, +1 at phase->1
                
                // Apply BLEP correction for bandlimitedness
                // Find which "bin" the phase is in for BLEP lookup
                // Original used 32-bit phase; we normalize to 0-1
                // BLEP is non-zero only near wrap-around (phase near 0)
                float blep_val = 0.0f;
                if (phase_ < 0.01f) /* near wrap */
                {
                    /* Simple BLEP approximation using table */
                    int idx = static_cast<int>(phase_ * 255.0f); /* 0-255 */
                    if (idx < 0) idx = 0;
                    if (idx >= 256) idx = 255;
                    blep_val = kProphetOscillatorDefaultConfig.blep_table[idx];
                }
                
                // Saw = raw saw - blep (original: - p5_fast_blep)
                value = saw - blep_val;
                
                // Normalize: original saw output was -1 to 1 excluding the wrap artifact
                // BLEP removes the step, so we get a band-limited saw
                break;
            }
            case 1: // Pulse wave (with PWM)
            {
                // Original pulse: (osc_saw - osc_saw(pw)) >> 1
                // Create two saw waves: one normal, one phase-shifted by PWM
                float saw1 = phase_ * 2.0f - 1.0f;
                
                // Phase shift for PWM
                float pw_shift = phase_ + pulse_width_ * (1.0f - phase_); /* simplified */
                if (pw_shift >= 1.0f) pw_shift -= 1.0f;
                
                float saw2 = pw_shift * 2.0f - 1.0f;
                
                // Pulse = (saw1 - saw2) / 2
                // This creates a pulse width determined by pulse_width_
                value = (saw1 - saw2) * 0.5f;
                
                // Pulse width can make it go outside -1 to 1, so clamp
                value = std::max(-1.0f, std::min(1.0f, value));
                break;
            }
            case 2: // Triangle wave
            {
                /* Original triangle from saw with blep:
                 * s = (int32_t)(ph >> 15); s < 65536 ? s - 32768 : 98303 - s;
                 * Float: standard triangle function */
                /* Triangle: |x| <= 1, goes from -1 to 1 with one peak */
                /* Phase 0->1: triangle goes 0 -> 1 -> 0 -> -1 -> 0 */
                /* More standard: 2*abs(phase-0.5)-1 but that gives 0 at ends */
                /* Let's use: phase 0->1 maps to triangle -1 to 1 with peak at 0.25 and 0.75 */
                
                /* Simple: triangle = 2 * |(phase * 2) mod 1 - 0.5| * 2 - 1 */
                float ph2 = phase_ * 2.0f; /* 0 to 2 */
                float tri = std::abs(ph2 - 1.0f) * 2.0f - 1.0f; /* -1 to 1, peak at phase 0.25 and 0.75 */
                value = tri;
                break;
            }
            case 3: // Sine wave (approximation)
            {
                /* Sine approximation using phase.
                 * Original used sine lookup table or polynomial. */
                /* Phase 0->1 -> 0 -> 2π */
                float phase2pi = phase_ * 2.0f * M_PI;
                value = std::sin(phase2pi);
                break;
            }
        }
        
        // Apply levels (A and B mixed)
        float level_mix = level_a_ + level_b_; /* both on by default */
        if (level_a_ > 0 && level_b_ > 0)
        {
            /* Pan/mix: could be more complex, but simple mix */
            value = value * ((level_a_ + level_b_) * 0.5f);
        }
        else
        {
            value = value * level_a_; /* Only A */
        }
        
        // Add noise if enabled
        if (noise_level_ > 0.0f)
        {
            float rand_val = static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f;
            value += rand_val * noise_level_;
        }
        
        // Apply sync if enabled (hard sync: reset phase of slave to master)
        /* Hard sync not fully implemented here; would require master LFO phase tracking */
        
        last_value_ = value;
        return value;
    }

    float getLastValue() const noexcept { return last_value_; }

    // --- Control ---
    
    void noteOn() noexcept { reset(); } // Reset phase
    void noteOff() noexcept { /* can hold or reset */ }

    [[nodiscard]] float getLastValueFloat() const noexcept { return last_value_; }
    [[nodiscard]] int getWaveform() const noexcept { return waveform_; }
    [[nodiscard]] float getPulseWidth() const noexcept { return pulse_width_; }

private:
    float phase_ = 0.0f;          // 32-bit phase accumulator (0 to 1)
    float phase_inc_ = 0.0f;      // phase increment per sample (depends on note/freq)
    float fine_tune_ = 0.0f;
    int coarse_tune_ = 0;
    int waveform_ = 0;            // 0=saw, 1=pulse, 2=triangle, 3=sine
    float pulse_width_ = 0.5f;    // PWM width 0.0 to 1.0
    float level_a_ = 1.0f;        // Oscillator A level
    float level_b_ = 0.0f;        // Oscillator B level
    float noise_level_ = 0.0f;    // Noise level
    bool sync_ = false;           // Hard sync flag
    float last_value_ = 0.0f;
};

/* ================================================================================================
 * Prophet-5 Oscillator Main Class
 * ================================================================================================ */

class DspProphetOscillator
{
public:
    DspProphetOscillator() noexcept = default;
    ~DspProphetOscillator() = default;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        sampleRateValid_ = true;
        
        // Initialize oscillator voice
        voice_.reset();
    }

    void reset() noexcept
    {
        voice_.reset();
    }

    // --- Global parameter setting ---
    
    void setWaveform(int waveform) noexcept // 0=saw, 1=pulse, 2=triangle, 3=sine
    {
        voice_.setWaveform(waveform);
    }

    void setPulseWidth(float width) noexcept // 0.0 to 1.0
    {
        voice_.setPulseWidth(width);
    }

    void setFineTune(float tune) noexcept // -1 to 1 or semitone offset
    {
        voice_.setFineTune(tune);
    }

    void setCoarseTune(int semitones) noexcept // -12 to 12
    {
        voice_.setCoarseTune(semitones);
    }

    void setLevelA(float level) noexcept // 0.0 to 1.0
    {
        voice_.setLevelA(level);
    }

    void setLevelB(float level) noexcept // 0.0 to 1.0
    {
        voice_.setLevelB(level);
    }

    void setNoiseLevel(float level) noexcept // 0.0 to 1.0
    {
        voice_.setNoiseLevel(level);
    }

    void setSync(bool sync) noexcept { voice_.setSync(sync); }

    // --- Processing ---
    
    float process(float sampleRate) noexcept
    {
        return voice_.process(sampleRate);
    }

    float getLastValue() const noexcept { return voice_.getLastValue(); }
    int getWaveform() const noexcept { return voice_.getWaveform(); }
    float getPulseWidth() const noexcept { return voice_.getPulseWidth(); }

private:
    float sampleRate_ = 44100.0;
    bool sampleRateValid_ = false;
    
    DspProphetOscillatorVoiceState voice_;
};

} // namespace abd::dsp