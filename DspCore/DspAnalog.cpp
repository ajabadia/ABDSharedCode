/**
 * @file DspAnalog.cpp
 * @brief Basic analog synthesizer engine implementation.
 * 
 * Adapted from melodee-main firmware (eng_analog.c) converting fixed-point DSP to float.
 * Provides 2 oscillators with wave selection, noise, drive/saturation, and a
 * trapezoidal SVF low-pass filter.
 * 
 * Magic numbers from the original firmware are referenced in config for tuning.
 */

#include "DspAnalog.h"
#include <cmath>

namespace abd::dsp
{

/* ================================================================================================
 * DspAnalogVoiceState implementation
 * ================================================================================================ */

DspAnalogVoiceState::DspAnalogVoiceState() noexcept
{
    reset();
}

void DspAnalogVoiceState::reset() noexcept
{
    /* Oscillator A */
    wave_a_ = 0;
    freq_a_ = 440.0f;
    detune_a_ = 0;
    level_a_ = 1.0f;

    /* Oscillator B */
    wave_b_ = 0;
    freq_b_ = 440.0f;
    detune_b_ = 0;
    level_b_ = 0.0f;

    /* Noise */
    noise_level_ = 0.0f;

    /* Filter */
    cutoff_hz_ = 1000.0f;
    resonance_ = 0.0f;
    drive_ = 1.0f;
    filter_state_ = 0.0f;

    /* Envelope */
    env_stage_ = 0;
    env_value_ = 0.0f;
    env_attack_ = 0.1f;
    env_decay_ = 0.5f;
    env_sustain_ = 0.7f;
    env_release_ = 1.0f;
    env_gate_ = false;

    /* Global */
    sample_rate_ = 44100.0f;
}

float DspAnalogVoiceState::process(float dt) noexcept
{
    /* --- Oscillator A --- */
    float osc_a = 0.0f;
    
    switch (wave_a_)
    {
        case 0: /* SAW */
        {
            /* Saw wave */
            static float phase_a = 0.0f;
            phase_a += freq_a_ / sample_rate_ * 2.0f * M_PI;
            if (phase_a >= 2.0f * M_PI) phase_a -= 2.0f * M_PI;
            osc_a = (phase_a / M_PI) - 1.0f;
            break;
        }
        case 1: /* SQR */
        {
            static float phase_a = 0.0f;
            phase_a += freq_a_ / sample_rate_ * 2.0f * M_PI;
            if (phase_a >= 2.0f * M_PI) phase_a -= 2.0f * M_PI;
            osc_a = (phase_a < M_PI) ? 1.0f : -1.0f;
            break;
        }
        case 2: /* TRI */
        {
            static float phase_a = 0.0f;
            phase_a += freq_a_ / sample_rate_ * 2.0f * M_PI;
            if (phase_a >= 2.0f * M_PI) phase_a -= 2.0f * M_PI;
            /* Triangle: 2*abs(phase/PI - 0.5) - 1 */
            float pa = phase_a / M_PI;
            osc_a = 2.0f * std::abs(pa - 0.5f) - 1.0f;
            break;
        }
        case 3: /* SIN */
        {
            static float phase_a = 0.0f;
            phase_a += freq_a_ / sample_rate_ * 2.0f * M_PI;
            if (phase_a >= 2.0f * M_PI) phase_a -= 2.0f * M_PI;
            osc_a = std::sin(phase_a);
            break;
        }
        case 4: /* PWM */
        {
            static float phase_a = 0.0f;
            static float pw = 0.5f;
            phase_a += freq_a_ / sample_rate_ * 2.0f * M_PI;
            if (phase_a >= 2.0f * M_PI) phase_a -= 2.0f * M_PI;
            osc_a = (phase_a < pw * 2.0f * M_PI) ? 1.0f : -1.0f;
            break;
        }
    }

    /* --- Oscillator B --- */
    float osc_b = 0.0f;
    
    /* Simplified: only SAW for B */
    switch (wave_b_)
    {
        case 0: /* SAW */
        {
            static float phase_b = 0.0f;
            phase_b += freq_b_ / sample_rate_ * 2.0f * M_PI;
            if (phase_b >= 2.0f * M_PI) phase_b -= 2.0f * M_PI;
            osc_b = (phase_b / M_PI) - 1.0f;
            break;
        }
        default: break;
    }

    /* --- Mix oscillators --- */
    float mix = 0.5f;
    float mixed = osc_a * (1.0f - mix) + osc_b * mix;

    /* --- Noise --- */
    float noise = 0.0f;
    if (noise_level_ > 0.0f)
    {
        noise = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f * noise_level_;
        mixed += noise;
    }

    /* --- Drive / Saturation --- */
    float drive_factor = drive_;
    mixed = std::tanh(mixed * drive_factor * 0.5f) * 2.0f;

    /* --- Filter: Trapezoidal SVF simplified --- */
    float input = mixed;
    
    /* Simple LP with resonance */
    float k = resonance_ * 3.88f; /* Moog ladder approximation */
    float input_gain = 1.0f / (1.0f + k);
    float fb = k * input_gain;
    float in_gain = input_gain * (input + fb);
    
    /* One-pole lowpass */
    filter_state_ = 0.5f * in_gain + 0.5f * filter_state_;
    
    /* Soft knee on peaks */
    if (std::abs(filter_state_) > 16000.0f)
    {
        float a = 16000.0f + (std::tanh((std::abs(filter_state_) - 16000.0f) * 2.0f) >> 1);
        filter_state_ = (filter_state_ < 0.0f) ? -a : a;
    }
    
    float filtered = filter_state_;
    
    /* --- Envelope --- */
    if (env_gate_ && env_stage_ == 0) env_stage_ = 1;
    
    float env_val = env_value_;
    if (env_stage_ == 1) /* ATTACK */
    {
        env_val = std::min(env_value_ + dt / env_attack_, 1.0f);
        if (env_val >= 1.0f) env_stage_ = 2;
    }
    else if (env_stage_ == 2) /* DECAY */
    {
        env_val = env_sustain_ + (1.0f - env_sustain_) * std::exp(-dt / env_decay_);
        if (std::abs(env_val - env_sustain_) < 0.01f) env_stage_ = 3;
    }
    else if (env_stage_ == 3) /* SUSTAIN */
    {
        /* hold */
    }
    else if (env_stage_ == 4) /* RELEASE */
    {
        env_val = env_value_ * std::exp(-dt / env_release_);
        if (env_val < 0.01f) { env_val = 0.0f; env_stage_ = 0; }
    }
    
    filtered *= env_val;

    return filtered;
}

/* ================================================================================================
 * DspAnalog implementation
 * ================================================================================================ */

DspAnalog::DspAnalog() noexcept
{
}

void DspAnalog::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    sampleRateValid_ = true;
    
    voice_.reset();
}

void DspAnalog::reset() noexcept
{
    voice_.reset();
}

void DspAnalog::setWaveA(int wave) noexcept { voice_.setWaveA(wave); }
void DspAnalog::setWaveB(int wave) noexcept { voice_.setWaveB(wave); }
void DspAnalog::setFreqA(float hz) noexcept { voice_.setFreqA(hz); }
void DspAnalog::setFreqB(float hz) noexcept { voice_.setFreqB(hz); }
void DspAnalog::setDetuneA(float cents) noexcept { voice_.setDetuneA(cents); }
void DspAnalog::setDetuneB(float cents) noexcept { voice_.setDetuneB(cents); }
void DspAnalog::setLevelA(float level) noexcept { voice_.setLevelA(level); }
void DspAnalog::setLevelB(float level) noexcept { voice_.setLevelB(level); }
void DspAnalog::setNoiseLevel(float level) noexcept { voice_.setNoiseLevel(level); }
void DspAnalog::setCutoff(float hz) noexcept { voice_.setCutoff(hz); }
void DspAnalog::setResonance(float res) noexcept { voice_.setResonance(res); }
void DspAnalog::setDrive(float drv) noexcept { voice_.setDrive(drv); }
void DspAnalog::setGate(bool gate) noexcept { voice_.setGate(gate); }

float DspAnalog::process(float dt) noexcept { return voice_.process(dt); }

} // namespace abd::dsp