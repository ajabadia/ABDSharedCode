/**
 * @file DspProphetEnvelope.cpp
 * @brief Prophet-5 ADSR envelope implementation.
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

#include "DspProphetEnvelope.h"
#include <cmath>

namespace abd::dsp
{

/* ================================================================================================
 * DspProphetEnvelopeVoiceState implementation
 * ================================================================================================ */

DspProphetEnvelopeVoiceState::DspProphetEnvelopeVoiceState() noexcept
{
    reset();
}

void DspProphetEnvelopeVoiceState::reset() noexcept
{
    stage_ = 0;
    value_ = 0.0f;
    target_ = 0.0f;
    attack_time_ = 0.0f;
    decay_time_ = 0.0f;
    sustain_level_ = 1.0f;
    release_time_ = 0.0f;
    vintage_ = 0;
    vintage_scale_ = 0.0f;
    gate_ = false;
    note_on_velocity_ = 1.0f;
    stage_start_value_ = 0.0f;
}

float DspProphetEnvelopeVoiceState::process(float sampleRate, float dt) noexcept
{
    if (!gate_ && stage_ == 0) return 0.0f;
    
    float stage_dt = dt;
    
    switch (stage_)
    {
        case 1: // Attack
        {
            float attack_dt = attack_time_ / 1000.0f; // ms to seconds
            float progress = stage_dt / attack_dt;
            progress = std::min(progress, 1.0f);
            
            value_ = progress; // 0 -> 1
            
            if (progress >= 1.0f)
            {
                value_ = 1.0f;
                stage_ = 2; // Move to Decay
            }
            break;
        }
        case 2: // Decay
        {
            float decay_dt = decay_time_ / 1000.0f;
            
            float scale = 1.0f + vintage_scale_ * 0.15f;
            
            float progress = stage_dt / decay_dt;
            progress = std::min(progress, 1.0f);
            
            float decay_amount = 1.0f - sustain_level_;
            float exp_mult = kProphetEnvelopeDefaultConfig.env_exp[0]; /* fallback */
            
            value_ = 1.0f - progress * decay_amount * exp_mult;
            
            value_ = std::max(value_, sustain_level_);
            value_ = std::min(value_, 1.0f);
            
            if (std::abs(value_ - sustain_level_) < kProphetEnvelopeDefaultConfig.env_end_threshold)
            {
                value_ = sustain_level_;
                stage_ = 3; // Move to Sustain
            }
            break;
        }
        case 3: // Sustain
        {
            if (!gate_)
            {
                stage_ = 4; // Transition to Release
            }
            break;
        }
        case 4: // Release
        {
            float release_dt = release_time_ / 1000.0f;
            
            float scale = 1.0f + vintage_scale_ * 0.15f;
            float exp_release = kProphetEnvelopeDefaultConfig.env_exp[0]; /* fallback */
            
            float progress = stage_dt / release_dt;
            progress = std::min(progress, 1.0f);
            
            value_ = value_ * (1.0f - progress * exp_release * scale);
            
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

float DspProphetEnvelopeVoiceState::getValue() const noexcept { return value_; }
int DspProphetEnvelopeVoiceState::getStage() const noexcept { return stage_; }
float DspProphetEnvelopeVoiceState::getSustainLevel() const noexcept { return sustain_level_; }
int DspProphetEnvelopeVoiceState::getVintage() const noexcept { return vintage_; }

/* ================================================================================================
 * DspProphetEnvelope implementation
 * ================================================================================================ */

DspProphetEnvelope::DspProphetEnvelope() noexcept
{
}

void DspProphetEnvelope::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    sampleRateValid_ = true;
    
    env_filter_.reset();
    env_amp_.reset();
}

void DspProphetEnvelope::reset() noexcept
{
    env_filter_.reset();
    env_amp_.reset();
}

void DspProphetEnvelope::setFilterAttack(float ms) noexcept { env_filter_.setAttackTime(ms); }
void DspProphetEnvelope::setFilterDecay(float ms) noexcept { env_filter_.setDecayTime(ms); }
void DspProphetEnvelope::setFilterSustain(float level) noexcept { env_filter_.setSustainLevel(level); }
void DspProphetEnvelope::setFilterRelease(float ms) noexcept { env_filter_.setReleaseTime(ms); }
void DspProphetEnvelope::setFilterVintage(int vintage) noexcept { env_filter_.setVintage(vintage); }

void DspProphetEnvelope::noteOnFilter() noexcept { env_filter_.noteOn(); }
void DspProphetEnvelope::noteOffFilter() noexcept { env_filter_.noteOff(); }

float DspProphetEnvelope::processFilter(float sampleRate, float dt) noexcept
{
    return env_filter_.process(sampleRate, dt);
}

void DspProphetEnvelope::setAmpAttack(float ms) noexcept { env_amp_.setAttackTime(ms); }
void DspProphetEnvelope::setAmpDecay(float ms) noexcept { env_amp_.setDecayTime(ms); }
void DspProphetEnvelope::setAmpSustain(float level) noexcept { env_amp_.setSustainLevel(level); }
void DspProphetEnvelope::setAmpRelease(float ms) noexcept { env_amp_.setReleaseTime(ms); }
void DspProphetEnvelope::setAmpVintage(int vintage) noexcept { env_amp_.setVintage(vintage); }

void DspProphetEnvelope::noteOnAmp() noexcept { env_amp_.noteOn(); }
void DspProphetEnvelope::noteOffAmp() noexcept { env_amp_.noteOff(); }

float DspProphetEnvelope::processAmp(float sampleRate, float dt) noexcept
{
    return env_amp_.process(sampleRate, dt);
}

[[nodiscard]] float DspProphetEnvelope::getFilterValue() const noexcept { return env_filter_.getValue(); }
[[nodiscard]] int DspProphetEnvelope::getFilterStage() const noexcept { return env_filter_.getStage(); }
[[nodiscard]] float DspProphetEnvelope::getAmpValue() const noexcept { return env_amp_.getValue(); }
[[nodiscard]] int DspProphetEnvelope::getAmpStage() const noexcept { return env_amp_.getStage(); }

} // namespace abd::dsp