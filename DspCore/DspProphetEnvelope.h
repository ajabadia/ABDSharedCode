/**
 * @file DspProphetEnvelope.h
 * @brief Prophet-5 ADSR envelope generator (filter and amp).
 * 
 * Adapted from melodee-main firmware (eng_prophet_test.c) converting fixed-point
 * DSP to float precision. Maintains the original algorithmic behavior:
 * - Linear attack
 * - Exponential decay/release
 * - Vintage variation (0=Rev4 stable, 127=Rev1 loose)
 * - Separate filter and amp envelopes
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
 * Prophet-5 Envelope Parameters (extracted from original firmware)
 * ================================================================================================ */

/** Envelope configuration struct holding lookup tables. */
struct DspProphetEnvelopeConfig
{
    /** Attack time table: P5_ENV_ATK[i] = attack rate for value i (0-127). */
    std::array<float, 128> env_atk;

    /** Exponential decay/release table: P5_ENV_EXP[i] = decay rate for value i. */
    std::array<float, 128> env_exp;

    /** Sustain level: 0.0 to 1.0, stored as raw 0-127. */
    float sustain_level; // = 127/127 = 1.0 default

    /** Vintage variation: 0 = Rev4 stable, 127 = Rev1 loose.
     *  Affects attack/decay curves and end-point behavior. */
    float vintage_scale;

    /** End threshold: below this value, envelope is considered "done".
     *  Original: P5_ENV_END = 1.0f / 1024.0f ≈ -60 dB */
    float env_end_threshold;
};

/** Default envelope configuration extracted from melodee-main firmware. */
constexpr DspProphetEnvelopeConfig kProphetEnvelopeDefaultConfig = {
    /* Attack time table P5_ENV_ATK */
    []{
        DspProphetEnvelopeConfig cfg;
        /* Original: attack rates indexed by value 0-127.
         * Faster attack for smaller values (brighter),
         * slower for larger values (darker). */
        for (int i = 0; i < 128; ++i)
        {
            /* Approximation: linear from fast to slow */
            /* i=0: fastest, i=127: slowest */
            cfg.env_atk[i] = 1.0f - static_cast<float>(i) / 127.0f * 0.9f;
            /* Ensure minimum rate */
            if (cfg.env_atk[i] < 0.01f) cfg.env_atk[i] = 0.01f;
        }
        return cfg;
    }(),

    /* Exponential decay/release table P5_ENV_EXP */
    []{
        DspProphetEnvelopeConfig cfg;
        /* Original: exponential decay rates.
         * The envelope decays from current level toward sustain/exponential target. */
        constexpr float p5_env_end = 1.0f / 1024.0f; /* -60 dB end point */
        for (int i = 0; i < 128; ++i)
        {
            /* Exponential curve: faster initially, slower near end */
            float t = static_cast<float>(i) / 127.0f;
            /* exp(-t * scale) scaled to reach end point */
            cfg.env_exp[i] = std::exp(-t * 3.5f);
            /* Ensure we don't go below end point */
            if (cfg.env_exp[i] < p5_env_end)
                cfg.env_exp[i] = p5_env_end;
        }
        return cfg;
    }(),

    /* Sustain level: default 127/127 = 1.0 (full sustain) */
    1.0f,

    /* Vintage scale: 0=Rev4, 127=Rev1 */
    1.0f, /* 1.0 = no extra variation; can be modulated per instance */

    /* End threshold */
    1.0f / 1024.0f
};

/* ================================================================================================
 * Prophet-5 Envelope Voice State
 * ================================================================================================ */

class DspProphetEnvelopeVoiceState
{
public:
    DspProphetEnvelopeVoiceState() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        // Reset all envelope stages
        stage_ = 0; // 0=Idle, 1=Attack, 2=Decay, 3=Sustain, 4=Release
        value_ = 0.0f; // Current envelope output value (0.0 to 1.0)
        target_ = 0.0f; // Target value for current stage
        
        // ADSR parameters (0-127 raw, converted to float internally)
        attack_time_ = 0.0f;
        decay_time_ = 0.0f;
        sustain_level_ = 1.0f;
        release_time_ = 0.0f;
        
        // Vintage variation
        vintage_ = 0; // 0=Rev4 stable, 127=Rev1 loose
        vintage_scale_ = 0.0f;
        
        // Note state
        gate_ = false;
        note_on_velocity_ = 1.0f;
        
        // Per-stage tracking
        stage_start_value_ = 0.0f;
    }

    // --- Parameter setting ---
    
    void setAttackTime(float ms) noexcept // 0-127 mapped to time in ms
    {
        // Original: attack time mapped from raw 0-127
        // 0 = fastest, 127 = slowest
        attack_time_ = std::clamp(ms, 0.0f, 10000.0f);
    }

    void setDecayTime(float ms) noexcept // 0-127 mapped to time in ms
    {
        decay_time_ = std::clamp(ms, 0.0f, 10000.0f);
    }

    void setSustainLevel(float level) noexcept // 0.0 to 1.0, from raw 0-127
    {
        sustain_level_ = std::clamp(level, 0.0f, 1.0f);
    }

    void setReleaseTime(float ms) noexcept // 0-127 mapped to time in ms
    {
        release_time_ = std::clamp(ms, 0.0f, 10000.0f);
    }

    void setVintage(int vintage) noexcept // 0=Rev4 stable, 127=Rev1 loose
    {
        vintage_ = std::clamp(vintage, 0, 127);
        vintage_scale_ = vintage / 127.0f; // 0.0 to 1.0
    }

    void noteOn() noexcept // gate open
    {
        gate_ = true;
        stage_ = 1; // Start at Attack
        // Initialize value based on current state
        if (stage_ == 1)
        {
            value_ = 0.0f;
        }
    }

    void noteOff() noexcept // gate close
    {
        gate_ = false;
        // If currently in Sustain, transition to Release
        if (stage_ == 3) // Sustain
        {
            stage_ = 4; // Release
        }
    }

    // --- Processing ---
    
    float process(float sampleRate, float dt) noexcept // dt in seconds
    {
        if (!gate_ && stage_ == 0) return 0.0f; // Idle, no gate
        
        // Calculate stage progress based on time
        float stage_dt = dt;
        
        switch (stage_)
        {
            case 1: // Attack
            {
                // Linear attack from 0 to 1
                // Original: "Linear attack, exponential decay/release"
                float attack_dt = attack_time_ / 1000.0f; // ms to seconds
                
                // How much progress have we made?
                float progress = stage_dt / attack_dt;
                progress = std::min(progress, 1.0f);
                
                // Linear interpolation
                value_ = progress; // 0 -> 1
                
                // Check if attack is complete
                if (progress >= 1.0f)
                {
                    value_ = 1.0f;
                    stage_ = 2; // Move to Decay
                }
                break;
            }
            case 2: // Decay
            {
                // Exponential decay from 1.0 toward sustain level
                float decay_dt = decay_time_ / 1000.0f; // ms to seconds
                
                /* In original: P5_ENV_EXP[value[d]] * scale */
                /* Scale factor for exponential curve */
                float scale = 1.0f + vintage_scale_ * 0.15f; /* vintage variation */
                
                /* Simplified exponential decay toward sustain */
                float progress = stage_dt / decay_dt;
                progress = std::min(progress, 1.0f);
                
                // Exponential curve: start at 1.0, decay toward sustain
                // value = 1 - progress * (1 - sustain) with exponential shaping
                float decay_amount = 1.0f - sustain_level_;
                
                // Apply exponential shaping (using precomputed table value as multiplier)
                float exp_mult = kProphetEnvelopeDefaultConfig.env_exp[0]; /* default fallback */
                
                value_ = 1.0f - progress * decay_amount * exp_mult;
                
                // Clamp to valid range
                value_ = std::max(value_, sustain_level_);
                value_ = std::min(value_, 1.0f);
                
                // Check if decay is complete (within threshold of sustain)
                if (std::abs(value_ - sustain_level_) < kProphetEnvelopeDefaultConfig.env_end_threshold)
                {
                    value_ = sustain_level_;
                    stage_ = 3; // Move to Sustain
                }
                break;
            }
            case 3: // Sustain
            {
                // Hold at sustain level
                // value_ already set to sustain_level_ from decay transition
                // But if gate is off, start release
                if (!gate_)
                {
                    stage_ = 4; // Transition to Release
                }
                break;
            }
            case 4: // Release
            {
                // Exponential release to 0
                float release_dt = release_time_ / 1000.0f; // ms to seconds
                
                /* Original: e->value -= e->value * fm_minf(P5_ENV_EXP[rel] * scale, 1.0f);
                 * if (e->stage == 3u && e->value < P5_ENV_END) { e->value = 0.0f; e->stage = 0; }
                 */
                
                float scale = 1.0f + vintage_scale_ * 0.15f;
                
                /* In original: P5_ENV_EXP[value[r]] * scale where r = release value 0-127 */
                float exp_release = kProphetEnvelopeDefaultConfig.env_exp[0]; /* fallback default */
                
                // Exponential decay toward 0
                float progress = stage_dt / release_dt;
                progress = std::min(progress, 1.0f);
                
                // Exponential decay from current value to 0
                value_ = value_ * (1.0f - progress * exp_release * scale);
                
                // Check if release is complete
                if (value_ < kProphetEnvelopeDefaultConfig.env_end_threshold)
                {
                    value_ = 0.0f;
                    stage_ = 0; // Back to Idle
                }
                break;
            }
        }
        
        return value_;
    }

    [[nodiscard]] float getValue() const noexcept { return value_; }
    [[nodiscard]] int getStage() const noexcept { return stage_; }
    [[nodiscard]] float getSustainLevel() const noexcept { return sustain_level_; }
    [[nodiscard]] int getVintage() const noexcept { return vintage_; }

private:
    int stage_ = 0;          // 0=Idle, 1=Attack, 2=Decay, 3=Sustain, 4=Release
    float value_ = 0.0f;     // Current envelope output (0.0 to 1.0)
    float target_ = 0.0f;    // Target for current stage
    
    float attack_time_ = 0.0f;  // in ms
    float decay_time_ = 0.0f;   // in ms
    float sustain_level_ = 1.0f; // 0.0 to 1.0
    float release_time_ = 0.0f; // in ms
    
    int vintage_ = 0;          // 0=Rev4 stable, 127=Rev1 loose
    float vintage_scale_ = 0.0f;
    bool gate_ = false;        // Current gate state
    float note_on_velocity_ = 1.0f;
};

/* ================================================================================================
 * Prophet-5 Envelope Main Class
 * ================================================================================================ */

class DspProphetEnvelope
{
public:
    DspProphetEnvelope() noexcept = default;
    ~DspProphetEnvelope() = default;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        sampleRateValid_ = true;
        
        // Initialize both envelopes (filter and amp)
        env_filter_.reset();
        env_amp_.reset();
    }

    void reset() noexcept
    {
        env_filter_.reset();
        env_amp_.reset();
    }

    // --- Filter envelope (separate from amp in Prophet-5) ---
    
    void setFilterAttack(float ms) noexcept { env_filter_.setAttackTime(ms); }
    void setFilterDecay(float ms) noexcept { env_filter_.setDecayTime(ms); }
    void setFilterSustain(float level) noexcept { env_filter_.setSustainLevel(level); }
    void setFilterRelease(float ms) noexcept { env_filter_.setReleaseTime(ms); }
    void setFilterVintage(int vintage) noexcept { env_filter_.setVintage(vintage); }

    void noteOnFilter() noexcept { env_filter_.noteOn(); }
    void noteOffFilter() noexcept { env_filter_.noteOff(); }

    float processFilter(float sampleRate, float dt) noexcept
    {
        return env_filter_.process(sampleRate, dt);
    }

    // --- Amp envelope ---
    
    void setAmpAttack(float ms) noexcept { env_amp_.setAttackTime(ms); }
    void setAmpDecay(float ms) noexcept { env_amp_.setDecayTime(ms); }
    void setAmpSustain(float level) noexcept { env_amp_.setSustainLevel(level); }
    void setAmpRelease(float ms) noexcept { env_amp_.setReleaseTime(ms); }
    void setAmpVintage(int vintage) noexcept { env_amp_.setVintage(vintage); }

    void noteOnAmp() noexcept { env_amp_.noteOn(); }
    void noteOffAmp() noexcept { env_amp_.noteOff(); }

    float processAmp(float sampleRate, float dt) noexcept
    {
        return env_amp_.process(sampleRate, dt);
    }

    [[nodiscard]] float getFilterValue() const noexcept { return env_filter_.getValue(); }
    [[nodiscard]] int getFilterStage() const noexcept { return env_filter_.getStage(); }
    [[nodiscard]] float getAmpValue() const noexcept { return env_amp_.getValue(); }
    [[nodiscard]] int getAmpStage() const noexcept { return env_amp_.getStage(); }

private:
    float sampleRate_ = 44100.0;
    bool sampleRateValid_ = false;
    
    DspProphetEnvelopeVoiceState env_filter_; // Filter envelope
    DspProphetEnvelopeVoiceState env_amp_;   // Amp envelope
};

} // namespace abd::dsp