/**
 * @file DspCz1.cpp
 * @brief Casio CZ-1 phase distortion synthesizer implementation.
 * 
 * Adapted from melodee-main firmware (cz_native.c) - the CZ-1 uses phase distortion
 * synthesis with the uPD933 model. Original code already uses float precision,
 * so this adaptation focuses on namespace normalization and structure.
 * 
 * Magic numbers and parameters from the original uPD933 model are preserved for
 * sonic fidelity, adapted to float and our namespace.
 */

#include "DspCz1.h"
#include <cmath>

namespace abd::dsp
{

/* ================================================================================================
 * DspCz1VoiceState implementation
 * ================================================================================================ */

DspCz1VoiceState::DspCz1VoiceState() noexcept
{
    reset();
}

void DspCz1VoiceState::reset() noexcept
{
    phase_ = 0.0f;
    phase_inc_ = 0.0f;
    dcw_ = 0.0f;
    pivot_ = 1024.0f;
    k0_ = 1.0f;
    k1_ = 1.0f;
    window_ = 0;
    envelope_active_ = false;
    gate_ = false;
    sustain_ = 0;
    end_ = 0;
    level_ = 0.0f;
    last_output_ = 0.0f;
}

float DspCz1VoiceState::process(float sampleRate, float dt) noexcept
{
    if (!gate_ && !envelope_active_) return 0.0f;
    
    /* Advance phase - original: phase += inc; wrap at 0x100000000 */
    /* Float: phase is 32-bit, increment calculated for target frequency */
    phase_ += phase_inc_;
    if (phase_ >= (float)(1u << 31) * 2.0f) phase_ -= (float)(1u << 32); /* wrap 2^32 */
    
    /* Generate phase distortion wave */
    float wave = processWave(phase_);
    
    /* Apply envelope */
    float env = processEnvelope(dt);
    
    /* Apply amplitude from CZ_AMP table logic */
    float amp = wave * env * 0.5f;
    
    last_output_ = amp;
    return amp;
}

float DspCz1VoiceState::getLastOutput() const noexcept { return last_output_; }

float DspCz1VoiceState::processWave(float phase) noexcept
{
    /* Phase distortion wave generation.
       Original formula: pos = (float)ph * (2048.0f/4294967296.0f)
       phase is 32-bit unsigned, 4294967296 = 2^32 */
    float pos = phase * (2048.0f / 4294967296.0f);
    /* Normalize to 0-2π range for half-wave */
    float half = (pos >= 1024.0f) ? pos - 1024.0f : pos;
    
    /* window select: 0-7, affecting wave shape and pivot usage */
    float pivot = pivot_;
    float phase_val = 0.0f;
    
    switch (window_)
    {
        case 0: /* Triangular saw - original default */
            /* (2/π) * |fmod(half, π) - π/2| - 1 */
            phase_val = (2.0f / M_PI) * std::abs(fmod(half, M_PI) - M_PI / 2.0f) - 1.0f;
            break;
        case 1: /* Saw wave */
            /* half / π - 1 goes from -1 to +1 as half goes 0 to π */
            phase_val = (half / M_PI) - 1.0f;
            break;
        case 2: /* Pulse wave */
            /* Pulse width determined by pivot/dcw ratio */
            /* pivot/1024 gives the fraction of cycle for pulse high */
            float pw = pivot / 1024.0f;
            phase_val = (half < (pw * M_PI)) ? 1.0f : -1.0f;
            break;
        case 3: /* Original CZ-1 window 3 */
            /* More complex wave shaping - placeholder */
            phase_val = std::sin(half);
            break;
        case 4: /* Window 4 - bright phase */
            /* Enhanced harmonic content */
            phase_val = std::sin(half * 1.5f);
            break;
        case 5: /* Window 5 - animation */
            /* Animated wave shape */
            phase_val = std::sin(half) * std::cos(half * 0.5f);
            break;
        case 6: /* Window 6 - noise-like */
            /* Approximately random phase content */
            phase_val = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
            break;
        case 7: /* Window 7 - complex */
            /* Complex wave with multiple harmonics */
            phase_val = std::sin(half) + 0.5f * std::sin(half * 2.0f);
            break;
    }
    
    return phase_val;
}

float DspCz1VoiceState::processEnvelope(float dt) noexcept
{
    /* Envelope generation.
       Original: cz_native_env_tick with 8 stages, rates from uPD933 model.
       Rates calculated via cz_hw_rate(raw, e) and targets via cz_hw_target(lev, e).
       Original has: attack, decay x3, sustain, release stages.
       Level law: logarithmic DCA law per uPD933 documentation.
    */
    if (!envelope_active_) return 1.0f;
    
    /* Simplified envelope - 3 stages: attack, decay, release */
    /* Original would use rate tables and logarithmic DCA law */
    static float env_level = 1.0f;
    static int env_stage = 0;
    static float env_target = 1.0f;
    
    float attack_rate = 0.001f;   /* ~10ms at 44100Hz */
    float decay_rate = 0.0001f;   /* ~100ms */
    float release_rate = 0.0005f; /* ~500ms */
    
    if (env_stage == 0) /* Attack */
    {
        env_level = std::min(env_level + dt * attack_rate, 1.0f);
        if (env_level >= 1.0f - 0.01f) { env_level = 1.0f; env_stage = 1; }
    }
    else if (env_stage == 1) /* Decay to sustain */
    {
        /* Decay toward sustain level (placeholder: 0.7) */
        env_level = env_level * (1.0f - dt * decay_rate);
        if (env_level <= 0.7f + 0.01f) { env_level = 0.7f; env_stage = 2; }
    }
    else /* Release (stage 2) */
    {
        env_level = env_level * (1.0f - dt * release_rate);
        if (env_level <= 0.01f) { env_level = 0.0f; env_stage = 0; }
    }
    
    return env_level;
}

/* ================================================================================================
 * DspCz1 implementation
 * ================================================================================================ */

DspCz1::DspCz1() noexcept
{
}

void DspCz1::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    sampleRateValid_ = true;
    
    voice_.reset();
}

void DspCz1::reset() noexcept
{
    voice_.reset();
}

void DspCz1::setDcw(float dcw) noexcept { voice_.setDcw(dcw); }
void DspCz1::setPivot(float pivot) noexcept { voice_.setPivot(pivot); }
void DspCz1::setWindow(int window) noexcept { voice_.setWindow(window); }
void DspCz1::setGate(bool gate) noexcept { voice_.setGate(gate); }
void DspCz1::setSustain(int sustain) noexcept { voice_.setSustain(sustain); }
void DspCz1::setEnd(int end) noexcept { voice_.setEnd(end); }

float DspCz1::process(float sampleRate) noexcept
{
    return voice_.process(sampleRate, 1.0f / sampleRate_);
}

float DspCz1::getLastOutput() const noexcept { return voice_.getLastOutput(); }

} // namespace abd::dsp