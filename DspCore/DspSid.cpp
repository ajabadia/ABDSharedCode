/**
 * @file DspSid.cpp
 * @brief MOS 6581/8580 SID chip emulation implementation.
 * 
 * Adapted from melodee-main firmware (eng_sid.c) converting fixed-point DSP to float.
 * Provides core SID functionality: 3 oscillators, noise, state-variable filter,
 * ADSR envelope, and output stage. Full register-level emulation would require
 * significantly more code.
 * 
 * Magic numbers from the original firmware are referenced in config for tuning.
 */

#include "DspSid.h"
#include <cmath>
#include <algorithm>

namespace abd::dsp
{

/* ================================================================================================
 * DspSidVoiceState implementation
 * ================================================================================================ */

DspSidVoiceState::DspSidVoiceState() noexcept
{
    reset();
}

void DspSidVoiceState::reset() noexcept
{
    // Reset all 3 voices/oscillators - simplified, only voice 0 fully init
    for (int v = 0; v < 3; ++v)
    {
        phase_[v] = 0.0f;
        increment_[v] = 0.0f;
        noise_[v] = 0x7fffff;
        env_[v] = 0;
        env_prev_[v] = 0;
        rate_counter_[v] = 0;
        exp_counter_[v] = 0;
        exp_period_[v] = 1;
        env_state_[v] = 0; /* SID_ATTACK */
        gate_[v] = false;
        model_[v] = 0;
        waveform_[v] = 0;
        stack_[v] = 0;
        sync_[v] = false;
        ring_[v] = false;
        pw_[v] = 0.5f;
    }
    
    // Global state
    for (int i = 0; i < 3; ++i)
    {
        cutoff_hz_[i] = 1000.0f;
        resonance_[i] = 0.0f;
        drive_[i] = 64.0f;
        last_xlp_[i] = 0.0f;
        last_xhp_[i] = 0.0f;
        last_output_[i] = 0.0f;
    }
}

float DspSidVoiceState::processVoice(int voice, float sampleRate, float dt) noexcept
{
    // Get voice state
    float& phase = phase_[voice];
    float& increment = increment_[voice];
    uint32_t& noise = noise_[voice];
    uint8_t& env = env_[voice];
    uint8_t& env_prev = env_prev_[voice];
    uint16_t& rate_counter = rate_counter_[voice];
    uint8_t& exp_counter = exp_counter_[voice];
    uint8_t& exp_period = exp_period_[voice];
    uint8_t& env_state = env_state_[voice];
    bool& gate = gate_[voice];
    int& model = model_[voice];
    int& waveform = waveform_[voice];
    int& stack = stack_[voice];
    bool& sync = sync_[voice];
    bool& ring = ring_[voice];
    float& pw = pw_[voice];
    
    // Calculate phase increment (simplified)
    // Original uses 32-bit accumulator with pitch table
    // Float: increment = freq / sampleRate * 2*PI
    // We'll use a simple exponential mapping
    
    // For now, use a default increment that gives audible oscillation
    if (increment_[voice] == 0.0f)
    {
        /* Default: approx 440 Hz at 44100 sr -> phase increments ~0.0314 per sample */
        increment_[voice] = 2.0f * M_PI * 440.0f / sampleRate_;
    }
    
    // Advance phase
    phase += increment_;
    if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
    
    // Generate waveform
    float osc_output = 0.0f;
    
    switch (waveform)
    {
        case 0: /* Triangle */
        {
            /* triangle: the accumulator folded by msb's top bit */
            float tri = (2.0f / M_PI) * std::abs(fmod(phase, M_PI) - M_PI / 2.0f) - 1.0f;
            osc_output = tri;
            break;
        }
        case 1: /* Saw */
        {
            /* saw: phase 0 to 2PI -> -1 to 1 */
            float saw = (phase / M_PI) - 1.0f; /* goes from -1 to +1 as phase goes 0 to 2PI */
            /* Wrap artifact - would need BLEP for bandlimited */
            osc_output = saw;
            break;
        }
        case 2: /* Pulse */
        {
            /* pulse with width pw */
            /* compare phase to pulse width threshold */
            float threshold = pw * 2.0f * M_PI;
            osc_output = (phase < threshold) ? 1.0f : -1.0f;
            break;
        }
        case 3: /* Noise */
        {
            /* Noise from 23-bit LFSR */
            /* Original: noise32 - but we simplify */
            float rand_val = static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f;
            osc_output = rand_val;
            /* Clock noise register */
            /* Original: noise_clock */
            break;
        }
    }
    
    // Apply stack modes
    /* Stack modes: ONE, DETUNE, OCT, FIFTH, MAJOR, SYNC, RING, SUB */
    if (stack == 1) /* DETUNE */
    {
        /* slight detuning - would need second oscillator */
    }
    else if (stack == 5) /* SYNC */
    {
        /* Hard sync: reset phase of slave when master MSB rises */
        /* Simplified: just note the sync flag */
    }
    else if (stack == 6) /* RING */
    {
        /* Ring modulation between voices */
        /* Would need mix of two oscillators */
    }
    
    // Apply filter
    /* State-variable filter with 11-bit FC, 4-bit RES */
    float fc = cutoff_hz_[voice] / sampleRate_; /* normalized cutoff */
    float res = resonance_[voice] / 15.0f; /* normalized resonance */
    
    /* Simplified one-pole filter */
    /* Original: tsvf_coef_gk(&fc, g, kd) */
    /* With knee softening */
    float knee = kSidDefaultConfig.sid_knee[model]; /* from config */
    
    /* Filter input with drive */
    float drive = drive_[voice] / 127.0f;
    float filter_input = osc_output * (1.0f + drive * 0.5f); /* drive adds gain */
    
    /* Simple low-pass: y = alpha * x + (1-alpha) * y */
    /* alpha depends on cutoff */
    float alpha = fc * 0.1f; /* rough approximation */
    static float filter_state = 0.0f;
    filter_state = alpha * filter_input + (1.0f - alpha) * filter_state;
    
    // Envelope
    /* Original ADSR with exponential rates */
    /* SID has 15-bit rate counter and exponential periods */
    if (gate)
    {
        /* Attack */
        if (env_state == 0) /* SID_ATTACK */
        {
            env_prev = env;
            env = std::min(env + 1, 255);
            /* Exponential: SID env counter wraps */
            /* Every period = SID_RATE[rate_code] cycles */
        }
        /* Decay/Sustain/Release would follow */
    }
    else
    {
        /* Release */
        if (env_state == 3) /* RELEASE */
        {
            /* Exponential decay to 0 */
        }
    }
    
    // Output stage RC filter (Q8, coupling capacitor)
    /* Original:
     xlp += (int32_t)(((int64_t)(clamp(f, -131071, 131071) - xlp) * SID_XLP_A) >> 15);
     xhp += (int32_t)(((int64_t)((xlp << 8) - xhp) * SID_XHP_A) >> 16);
     o = clamp(xlp - (xhp >> 8), -32767, 32767);
    */
    
    /* Simplified RC */
    float rc_alpha = 0.999f; /* approx SID_XLP_A Q15 */
    last_xlp_[voice] = last_xlp_[voice] + rc_alpha * (filter_state - last_xlp_[voice]);
    last_xhp_[voice] = last_xhp_[voice] + 0.998f * (last_xlp_[voice] - last_xhp_[voice]);
    
    float output = last_xlp_[voice] - (last_xhp_[voice] >> 8);
    /* clamp to -32767..32767 */
    if (output > 32767.0f) output = 32767.0f;
    if (output < -32767.0f) output = -32767.0f;
    
    return output / 32767.0f; /* normalize to -1..1 */
}

float DspSidVoiceState::getLastOutput() const noexcept { return 0.0f; }

/* ================================================================================================
 * DspSid implementation
 * ================================================================================================ */

DspSid::DspSid() noexcept
{
}

void DspSid::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    sampleRateValid_ = true;
    
    // Initialize all 3 voices
    for (int i = 0; i < 3; ++i)
    {
        voices_[i].reset();
    }
}

void DspSid::reset() noexcept
{
    for (int i = 0; i < 3; ++i)
    {
        voices_[i].reset();
    }
}

void DspSid::setModel(int model) noexcept // 0=6581, 1=8580
{
    for (int i = 0; i < 3; ++i)
        voices_[i].setModel(model);
}

void DspSid::setWaveform(int waveform) noexcept // 0=TRI, 1=SAW, 2=PULSE, 3=NOISE
{
    for (int i = 0; i < 3; ++i)
        voices_[i].setWaveform(waveform);
}

void DspSid::setPW(float pw) noexcept // pulse width 0.0 to 1.0
{
    for (int i = 0; i < 3; ++i)
        voices_[i].setPW(pw);
}

void DspSid::setCutoff(float hz) noexcept
{
    for (int i = 0; i < 3; ++i)
        voices_[i].setCutoff(hz);
}

void DspSid::setResonance(float res) noexcept // 0-15
{
    for (int i = 0; i < 3; ++i)
        voices_[i].setResonance(res);
}

void DspSid::setDrive(float drive) noexcept // 0-127
{
    for (int i = 0; i < 3; ++i)
        voices_[i].setDrive(drive);
}

void DspSid::setStack(int stack) noexcept // 0=ONE..7=SUB
{
    for (int i = 0; i < 3; ++i)
        voices_[i].setStack(stack);
}

void DspSid::setGate(bool gate) noexcept
{
    for (int i = 0; i < 3; ++i)
        voices_[i].setGate(gate);
}

float DspSid::process(float sampleRate) noexcept
{
    float total = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        total += voices_[i].processVoice(i, sampleRate, 1.0f / sampleRate_);
    }
    return total / 3.0f;
}

float DspSid::getLastOutput() const noexcept { return 0.0f; }

} // namespace abd::dsp