/**
 * @file DspPhys.cpp
 * @brief Physical modeling engines implementation.
 * 
 * Adapted from melodee-main firmware (eng_phys.c, phys_dsp.c) converting fixed-point
 * DSP to float. Includes Modal resonator, String resonator, Membrane drum, and
 * Sympathetic resonance models.
 * 
 * Magic numbers from the original firmware are referenced in config for tuning.
 */

#include "DspPhys.h"
#include <cmath>
#include <algorithm>

namespace abd::dsp
{

/* ================================================================================================
 * DspPhysModalVoiceState implementation
 * ================================================================================================ */

DspPhysModalVoiceState::DspPhysModalVoiceState() noexcept
{
    reset();
}

void DspPhysModalVoiceState::reset() noexcept
{
    for (int i = 0; i < 12; ++i)
    {
        mode_active_[i] = false;
        mode_level_[i] = 0.0f;
        mode_freq_[i] = 0.0f;
        mode_q_[i] = 0.0f;
        mode_brightness_[i] = 0.5f;
        mode_damp_[i] = 0.01f;
    }
    
    strike_pos_ = 0.5f;
    pitch_bend_ = 0.0f;
    sample_rate_ = 44100.0f;
}

float DspPhysModalVoiceState::process(float dt) noexcept
{
    float total = 0.0f;
    
    for (int i = 0; i < 12; ++i)
    {
        if (!mode_active_[i]) continue;
        
        /* Each mode is a band-pass filter resonating at mode_freq_[i] */
        float freq = mode_freq_[i];
        float q = mode_q_[i];
        float brightness = mode_brightness_[i];
        float damp = mode_damp_[i];
        
        /* Phase increment from frequency */
        float phase_inc = freq / sample_rate_ * 2.0f * M_PI;
        
        /* Simple accumulation - in full implementation would track phase per mode */
        /* Apply brightness: emphasize harmonics */
        float emphasized = freq * (0.5f + brightness * 0.5f);
        
        /* Apply damping - exponential decay per mode */
        /* Original Q20: modes at or above 0.499 cycles clamped */
        /* g h folded into one coefficient: t = g h (x - (r + g) s1 - s2) */
        
        /* Add sine wave at mode frequency (placeholder - real impl would have persistence) */
        total += std::sin(emphasized * dt);
    }
    
    /* Apply pitch bend */
    /* Would shift all frequencies by pitch_bend_ semitones */
    
    /* Decay - modes decay at different rates based on Q */
    /* Q20: every 32 samples, divisions 64 / 32 and only per block */
    
    return total / 12.0f;
}

/* ================================================================================================
 * DspPhysStringVoiceState implementation
 * ================================================================================================ */

DspPhysStringVoiceState::DspPhysStringVoiceState() noexcept
{
    reset();
}

void DspPhysStringVoiceState::reset() noexcept
{
    delay_line_.reset();
    dispersion_coeff_ = 0.5f;
    brighteness_ = 0.5f;
    pluck_pos_ = 0.5f;
    damping_ = 0.01f;
    loop_gain_ = 0.996f;
    is_plucked_ = false;
    decay_counter_ = 0;
    sample_rate_ = 44100.0f;
}

float DspPhysStringVoiceState::process(float dt) noexcept
{
    if (!is_plucked_ && decay_counter_ <= 0) return 0.0f;
    
    /* Pluck: inject noise burst at start */
    if (is_plucked_ && decay_counter_ == 0)
    {
        /* Noise burst - original: "a noise burst one period long (or Dust) through Svf" */
        float noise = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
        /* Write to delay line at pluck position */
        float pos_sample = noise * (0.5f + pluck_pos_ * 0.5f);
        delay_line_.write(pos_sample);
        decay_counter_ = 1;
    }
    
    /* Read from delay line */
    float sample = delay_line_.read();
    
    /* Apply dispersion (curved bridge) */
    /* Original: "dispersion all-pass or curved bridge, DcBlock" */
    /* Simplified: all-pass filter for dispersion */
    float dispersed = sample; /* placeholder - real all-pass */
    
    /* Apply brightness */
    dispersed *= brighteness_;
    
    /* Apply damping (exponential decay) */
    float damped = sample * loop_gain_; /* feedback */
    
    /* Write back (feedback loop) */
    delay_line_.write(damped);
    
    /* Decay counter */
    if (decay_counter_ > 0) decay_counter_++;
    
    /* Check decay - original: per block divisions */
    if (decay_counter_ > 2000) /* rough timeout at 44100sr ~45ms */
    {
        is_plucked_ = false;
        decay_counter_ = 0;
    }
    
    return damped;
}

/* ================================================================================================
 * DspPhysMembVoiceState implementation
 * ================================================================================================ */

DspPhysMembVoiceState::DspPhysMembVoiceState() noexcept
{
    reset();
}

void DspPhysMembVoiceState::reset() noexcept
{
    for (int i = 0; i < 12; ++i)
    {
        mode_active_[i] = false;
        mode_freq_[i] = 0.0f;
        mode_q_[i] = 0.0f;
    }
    
    harm_blend_ = 0.5f;
    pos_strike_ = 0.5f;
    bend_ = 0.0f;
    
    is_excited_ = false;
    decay_counter_ = 0;
    sample_rate_ = 44100.0f;
}

float DspPhysMembVoiceState::process(float dt) noexcept
{
    if (!is_excited_ && decay_counter_ <= 0) return 0.0f;
    
    /* Excite: impulse into low-pass */
    if (is_excited_ && decay_counter_ == 0)
    {
        /* Modal modes follow cos(2 pi pos i) */
        for (int i = 0; i < 12; ++i)
        {
            /* Mode frequency depends on strike position */
            float base_freq = 100.0f * (i + 1);
            float freq_mod = std::cos(2.0f * M_PI * strike_pos_ * (i + 1) / 12.0f);
            mode_freq_[i] = base_freq * freq_mod;
            mode_active_[i] = true;
        }
        decay_counter_ = 1;
    }
    
    /* Process each mode */
    float total = 0.0f;
    for (int i = 0; i < 12; ++i)
    {
        if (!mode_active_[i]) continue;
        float freq = mode_freq_[i] * (1.0f + bend_ / 12.0f);
        total += std::sin(freq * dt); /* placeholder */
    }
    
    /* HARM blend: mix ideal head with loaded membrane */
    float mixed = total * harm_blend_;
    
    /* Decay */
    if (decay_counter_ > 0) decay_counter_++;
    if (decay_counter_ > 500) { is_excited_ = false; decay_counter_ = 0; }
    
    return mixed;
}

/* ================================================================================================
 * DspPhysSympVoiceState implementation
 * ================================================================================================ */

DspPhysSympVoiceState::DspPhysSympVoiceState() noexcept
{
    reset();
}

void DspPhysSympVoiceState::reset() noexcept
{
    for (int i = 0; i < 3; ++i)
    {
        symp_freq_[i] = 0.0f;
        symp_active_[i] = false;
        symp_amount_[i] = 0.0f;
    }
    
    chord_root_ = 0.0f;
    buzz_ = 0.0f;
    is_ringing_ = false;
    decay_counter_ = 0;
    sample_rate_ = 44100.0f;
}

float DspPhysSympVoiceState::process(float dt) noexcept
{
    if (!is_ringing_ && decay_counter_ <= 0) return 0.0f;
    
    /* Set sympathetic frequencies based on chord root */
    float root_hz = 440.0f * std::pow(2.0f, chord_root_ / 12.0f);
    
    /* 3 strings: root, fifth, fourth */
    float intervals[3] = {0.0f, 7.0f, 5.0f};
    
    for (int i = 0; i < 3; ++i)
    {
        symp_freq_[i] = root_hz * std::pow(2.0f, intervals[i] / 12.0f);
        symp_active_[i] = is_ringing_;
        symp_amount_[i] = 1.0f / (i + 1);
    }
    
    /* Process each sympathetic string */
    float total = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (!symp_active_[i]) continue;
        /* Simple resonator */
        total += std::sin(symp_freq_[i] * dt * 2.0f * M_PI) * symp_amount_[i];
    }
    
    /* Decay */
    if (decay_counter_ > 0) decay_counter_++;
    if (decay_counter_ > 1000) { is_ringing_ = false; decay_counter_ = 0; }
    
    return total;
}

/* ================================================================================================
 * DspPhys implementation
 * ================================================================================================ */

DspPhys::DspPhys() noexcept
{
}

void DspPhys::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    sampleRateValid_ = true;
}

void DspPhys::reset() noexcept
{
    modal_.reset();
    string_.reset();
    membran_.reset();
    symp_.reset();
}

void DspPhys::setModalModeActive(int mode, bool active) noexcept /* 0-11 */ { modal_.setModeActive(mode, active); }
void DspPhys::setModalFreq(int mode, float freq) noexcept /* 0-11 */ { modal_.setModeFreq(mode, freq); }
void DspPhys::setModalQ(int mode, float q) noexcept /* 0-11 */ { modal_.setModeQ(mode, q); }
void DspPhys::setModalBrightness(int mode, float bright) noexcept /* 0-11 */ { modal_.setModeBrightness(mode, bright); }
void DspPhys::setModalDamp(int mode, float damp) noexcept /* 0-11 */ { modal_.setModeDamp(mode, damp); }
void DspPhys::setModalStrikePos(float pos) noexcept /* 0-1 */ { modal_.setStrikePos(pos); }
void DspPhys::setModalPitchBend(float bend) noexcept /* semitones */ { modal_.setPitchBend(bend); }

void DspPhys::setStringPluckPos(float pos) noexcept /* 0-1 */ { string_.setPluckPos(pos); }
void DspPhys::setStringBrightness(float bright) noexcept { string_.setBrightness(bright); }
void DspPhys::setStringDamp(float damp) noexcept { string_.setDamp(damp); }
void DspPhys::setStringLoopGain(float gain) noexcept { string_.setLoopGain(gain); }
void DspPhys::stringPluck() noexcept { string_.pluck(); }

void DspPhys::setMembHarmBlend(float blend) noexcept /* 0-1 */ { membran_.setHarmBlend(blend); }
void DspPhys::setMembStrikePos(float pos) noexcept /* 0-1 */ { membran_.setStrikePos(pos); }
void DspPhys::setMembPitchBend(float bend) noexcept /* semitones */ { membran_.setPitchBend(bend); }
void DspPhys::membExcite() noexcept { membran_.excite(); }

void DspPhys::setSympRoot(float root) noexcept /* MIDI */ { symp_.setChordRoot(root); }
void DspPhys::setSympAmount(float amount) noexcept /* 0-1 */ { symp_.setSympAmount(amount); }
void DspPhys::setSympBuzz(float buzz) noexcept /* 0-1 */ { symp_.setBuzz(buzz); }
void DspPhys::sympRing() noexcept { symp_.ring(); }

float DspPhys::processModal(float dt) noexcept { return modal_.process(dt); }
float DspPhys::processString(float dt) noexcept { return string_.process(dt); }
float DspPhys::processMemb(float dt) noexcept { return membran_.process(dt); }
float DspPhys::processSymp(float dt) noexcept { return symp_.process(dt); }

} // namespace abd::dsp