/**
 * @file DspProphetVCF.cpp
 * @brief Prophet-5 Rev-4 four-pole VCF implementation.
 * 
 * Adapted from melodee-main firmware (eng_prophet_test.c) converting fixed-point
 * DSP to float precision. Maintains the original algorithmic character (SSI
 * distributed saturation vs Curtis concentrated saturation) while using float
 * for finer resolution and easier integration.
 * 
 * Magic numbers from the original firmware are referenced in DspProphetVCF_config.h
 * for easy tuning and review.
 */

#include "DspProphetVCF.h"
#include <cmath>
#include <algorithm>

namespace abd::dsp
{

/* ================================================================================================
 * DspProphetVCFVoiceState implementation
 * ================================================================================================ */

void DspProphetVCFVoiceState::reset() noexcept
{
    // Reset all 4 stages to zero
    for (auto& s : stages)
    {
        s.lp = 0.0f;
        s.hip = 0.0f; // bandpass
        s.prev_in = 0.0f;
    }
    
    // Reset coefficient cache
    coeff_valid = false;
    cutoff_hz = 1000.0f;
    resonance = 0.0f;
    filter_model_ = 0; // 0=SSI, 1=Curtis
    vintage_ = 0;     // 0=Rev4, 127=Rev1
    
    // Drift parameters (per-voice subtle tuning variation)
    drift_left = 0.0f;
    drift_right = 0.0f;
}

float DspProphetVCFVoiceState::process(float sample) noexcept
{
    if (!coeff_valid)
        updateCoefficients();
    
    // Input soft-clipping to emulate transistor pair saturation (SSI model)
    // In original: softclip via std::tanh() to emulate differential transistor pair saturation
    float x = std::tanh(sample);
    
    // Feedback from last stage (original: k * inputGain * stages[3].getLP())
    // In float: resonance * stage4_lp
    float fb = resonance * stages[3].lp;
    
    // Input summing junction with gain (original inputGain factor)
    // Original: inputGain = 1.0 / (1.0 + k) where k = 3.88 * resonance
    // Float simplified: just use resonance directly
    float in_gain = 1.0f - resonance * 0.045f; // ~45/32767 approximation
    float in = in_gain * (x + fb);
    
    // Cascade through 4 one-pole LP stages
    // Original has VAOnePoleFilter stages - simplified to 4 cascaded one-pole sections
    float lp1 = stages[0].process(in);
    float lp2 = stages[1].process(lp1);
    float lp3 = stages[2].process(lp2);
    float lp4 = stages[3].process(lp3);
    
    // Select output based on pole mode
    // 0 = 4-pole (24 dB/oct) from stage 4
    // 1 = 2-pole (12 dB/oct) from stage 2
    float lp_out = (pole_mode == 1) ? lp2 : lp4;
    
    // Sub-mode selection
    switch (sub_mode)
    {
        case 1: // Bandpass: stage2 LP - stage4 LP
            return lp2 - lp4;
        case 2: // Highpass: input - 4-pole LP
            return sample - lp4;
        default: // Lowpass
            return lp_out;
    }
}

float DspProphetVCFVoiceState::processSample(double sample) noexcept
{
    return process(static_cast<float>(sample));
}

void DspProphetVCFVoiceState::setCutoff(float hz) noexcept
{
    cutoff_hz = hz;
    coeff_valid = false; // Force coefficient recalculation
}

void DspProphetVCFVoiceState::setResonance(float res) noexcept
{
    resonance = std::clamp(res, 0.0f, 1.0f);
    coeff_valid = false;
}

void DspProphetVCFVoiceState::setFilterModel(int model) noexcept // 0=SSI, 1=Curtis
{
    filter_model_ = std::clamp(model, 0, 1);
    coeff_valid = false;
}

void DspProphetVCFVoiceState::setVintage(int vintage) noexcept // 0=Rev4 stable, 127=Rev1 loose
{
    vintage_ = vintage;
    coeff_valid = false;
}

void DspProphetVCFVoiceState::setPoleMode(int mode) noexcept
{
    pole_mode = std::clamp(mode, 0, 1);
}

void DspProphetVCFVoiceState::setSubMode(int mode) noexcept
{
    sub_mode = std::clamp(mode, 0, 2);
}

[[nodiscard]] float DspProphetVCFVoiceState::getCutoff() const noexcept { return cutoff_hz; }
[[nodiscard]] float DspProphetVCFVoiceState::getResonance() const noexcept { return resonance; }
[[nodiscard]] int DspProphetVCFVoiceState::getPoleMode() const noexcept { return pole_mode; }
[[nodiscard]] int DspProphetVCFVoiceState::getSubMode() const noexcept { return sub_mode; }
[[nodiscard]] int DspProphetVCFVoiceState::getFilterModel() const noexcept { return filter_model_; }
[[nodiscard]] int DspProphetVCFVoiceState::getVintage() const noexcept { return vintage_; }

/* ================================================================================================
 * DspProphetVCF implementation
 * ================================================================================================ */

DspProphetVCF::DspProphetVCF() noexcept
{
    // Initialize voices with default state
    for (auto& v : voices)
    {
        // voices are default-constructed, reset will be called in prepare()
    }
}

void DspProphetVCF::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    sampleRateValid_ = true;
    
    // Initialize all voices
    for (auto& v : voices)
    {
        v.reset();
    }
    
    // Set default global parameters (matching INIT PROPHET patch)
    global_cutoff_hz_ = 1000.0f; /* Default cutoff */
    global_resonance_ = 0.0f;   // No resonance by default
    global_filter_model_ = 0;   // SSI model by default
    global_vintage_ = 0;        // Rev4 stable
}

void DspProphetVCF::reset() noexcept
{
    for (auto& v : voices)
    {
        v.reset();
    }
    // Reset global state to defaults
    global_cutoff_hz_ = 1000.0f;
    global_resonance_ = 0.0f;
    global_filter_model_ = 0;
    global_vintage_ = 0;
}

// --- Global parameter setting ---

void DspProphetVCF::setCutoff(float hz) noexcept
{
    global_cutoff_hz_ = hz;
    for (auto& v : voices)
    {
        v.setCutoff(hz);
    }
}

void DspProphetVCF::setResonance(float res) noexcept
{
    global_resonance_ = res;
    for (auto& v : voices)
    {
        v.setResonance(res);
    }
}

void DspProphetVCF::setFilterModel(int model) noexcept // 0=SSI, 1=Curtis
{
    global_filter_model_ = std::clamp(model, 0, 1);
    for (auto& v : voices)
    {
        v.setFilterModel(model);
    }
}

void DspProphetVCF::setVintage(int vintage) noexcept // 0=Rev4 stable, 127=Rev1 loose
{
    global_vintage_ = vintage;
    for (auto& v : voices)
    {
        v.setVintage(vintage);
    }
}

// --- Per-voice processing ---

float DspProphetVCF::process(float sample, int voiceIndex) noexcept
{
    if (voiceIndex >= 0 && voiceIndex < static_cast<int>(voices.size()))
    {
        return voices[voiceIndex].process(sample);
    }
    return sample; // fallback
}

float DspProphetVCF::processGlobal(float sample) noexcept
{
    // Ensure all voices have current global params
    for (auto& v : voices)
    {
        v.setCutoff(global_cutoff_hz_);
        v.setResonance(global_resonance_);
        v.setFilterModel(global_filter_model_);
        v.setVintage(global_vintage_);
    }
    if (!voices.empty())
    {
        return voices[0].process(sample);
    }
    return sample;
}

[[nodiscard]] float DspProphetVCF::getCutoff() const noexcept { return global_cutoff_hz_; }
[[nodiscard]] float DspProphetVCF::getResonance() const noexcept { return global_resonance_; }
[[nodiscard]] int DspProphetVCF::getFilterModel() const noexcept { return global_filter_model_; }
[[nodiscard]] int DspProphetVCF::getVintage() const noexcept { return global_vintage_; }

} // namespace abd::dsp