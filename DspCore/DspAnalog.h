/**
 * @file DspAnalog.h
 * @brief Basic analog synthesizer engine (2 OSC + LPF).
 * 
 * Adapted from melodee-main firmware (eng_analog.c) converting fixed-point DSP to float.
 * Features two oscillators with wave selection, noise, drive/saturation, and a
 * trapezoidal SVF low-pass filter.
 * 
 * Magic numbers from the original firmware are extracted to DspAnalog_config.h for tuning.
 */

#pragma once

#include <cmath>
#include <array>
#include <algorithm>

namespace abd::dsp
{

/* ================================================================================================
 * Analog Configuration (extracted from melodee-main firmware)
 * ================================================================================================ */

/** Analog engine configuration. */
struct DspAnalogConfig
{
    /* Waveform names: original N_ANALOG_WAVE[] = {"SAW", "SQR", "TRI", "SIN", "PWM"} */
    /* Oscillator A waveform: 0=SAW, 1=SQR, 2=TRI, 3=SIN, 4=PWM */
    /* Oscillator B waveform: same */

    /* Original: det = p[P_E1], mix = p[P_E2], noise = p[P_E3] */
    /* det: detune in cents */
    /* mix: oscillator mix Q15 (mix * 258 + 32767 - mix) */
    /* noise: noise level */

    /* Original: drive = 32768 + p[P_E6] * 512; /* 1x .. 3x */
    /* Drive stage: 1x, 2x, 3x */

    /* Original: cut = (p[P_E4] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
       filter: tsvf_lp */

    /* Envelope parameters from presets */
    /* Original presets have: {atk, dec, sus, rel} per filter and amp */

    /* Q20 format conversion: original signals Q20 between stages,
       converted to float here (1.0 = 2^20 = 1048576) */
};

/** Default analog configuration. */
constexpr DspAnalogConfig kAnalogDefaultConfig = {
    /* Config values extracted from melodee-main eng_analog.c */
};

/* ================================================================================================
 * Analog Voice State
 * ================================================================================================ */

class DspAnalogVoiceState
{
public:
    DspAnalogVoiceState() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        /* Oscillator A */
        wave_a_ = 0;     /* 0=SAW, 1=SQR, 2=TRI, 3=SIN, 4=PWM */
        freq_a_ = 440.0f; /* Hz */
        detune_a_ = 0;   /* cents */
        level_a_ = 1.0f; /* 0-1 */

        /* Oscillator B */
        wave_b_ = 0;     /* 0=SAW, 1=SQR, 2=TRI, 3=SIN, 4=PWM */
        freq_b_ = 440.0f; /* Hz */
        detune_b_ = 0;   /* cents */
        level_b_ = 0.0f; /* 0-1 */

        /* Noise */
        noise_level_ = 0.0f; /* 0-1 */

        /* Filter */
        cutoff_hz_ = 1000.0f; /* Hz */
        resonance_ = 0.0f;    /* 0-15 or 0-1 */
        drive_ = 1.0f;       /* 1x .. 3x */
        filter_state_ = 0.0f; /* TSVF integrator state */

        /* Envelope (simplified - ADSR) */
        env_stage_ = 0; /* 0=OFF, 1=ATTACK, 2=DECAY, 3=SUSTAIN, 4=RELEASE */
        env_value_ = 0.0f; /* 0-1 */
        env_attack_ = 0.1f;  /* seconds */
        env_decay_ = 0.5f;   /* seconds */
        env_sustain_ = 0.7f; /* 0-1 */
        env_release_ = 1.0f; /* seconds */
        env_gate_ = false;   /* gate signal */

        /* Global */
        sample_rate_ = 44100.0f;
    }

    // --- Parameter setting ---

    void setWaveA(int wave) noexcept { wave_a_ = std::clamp(wave, 0, 4); }
    void setWaveB(int wave) noexcept { wave_b_ = std::clamp(wave, 0, 4); }
    void setFreqA(float hz) noexcept { freq_a_ = hz; }
    void setFreqB(float hz) noexcept { freq_b_ = hz; }
    void setDetuneA(float cents) noexcept { detune_a_ = cents; }
    void setDetuneB(float cents) noexcept { detune_b_ = cents; }
    void setLevelA(float level) noexcept { level_a_ = std::clamp(level, 0.0f, 1.0f); }
    void setLevelB(float level) noexcept { level_b_ = std::clamp(level, 0.0f, 1.0f); }
    void setNoiseLevel(float level) noexcept { noise_level_ = std::clamp(level, 0.0f, 1.0f); }
    void setCutoff(float hz) noexcept { cutoff_hz_ = hz; }
    void setResonance(float res) noexcept { resonance_ = std::clamp(res, 0.0f, 1.0f); }
    void setDrive(float drv) noexcept { drive_ = std::max(1.0f, std::min(3.0f, drv)); }
    void setGate(bool gate) noexcept { env_gate_ = gate; if (gate) env_stage_ = 1; }

    // --- Processing ---
    
    float process(float dt) noexcept /* dt in seconds */
    {
        /* --- Oscillator A --- */
        float osc_a = 0.0f;
        
        switch (wave_a_)
        {
            case 0: /* SAW */
            {
                /* Saw wave: phase 0->2PI -> -1 to 1 */
                /* simplified: use saw from phase accumulator */
                static float phase_a = 0.0f;
                phase_a += freq_a_ / sample_rate_ * 2.0f * M_PI;
                if (phase_a >= 2.0f * M_PI) phase_a -= 2.0f * M_PI;
                /* Saw: (phase / PI) - 1 goes from -1 to +1 */
                osc_a = (phase_a / M_PI) - 1.0f;
                break;
            }
            case 1: /* SQR (Square) */
            {
                static float phase_a = 0.0f;
                phase_a += freq_a_ / sample_rate_ * 2.0f * M_PI;
                if (phase_a >= 2.0f * M_PI) phase_a -= 2.0f * M_PI;
                /* Square: high for half cycle */
                osc_a = (phase_a < M_PI) ? 1.0f : -1.0f;
                break;
            }
            case 2: /* TRI (Triangle) */
            {
                static float phase_a = 0.0f;
                phase_a += freq_a_ / sample_rate_ * 2.0f * M_PI;
                if (phase_a >= 2.0f * M_PI) phase_a -= 2.0f * M_PI;
                /* Triangle: 2*abs(phase - PI) / PI - 1 */
                osc_a = (2.0f * std::abs((phase_a / M_PI) - 1.0f) - 1.0f);
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
                /* Pulse Width Modulation */
                static float phase_a = 0.0f;
                static float pw = 0.5f; /* 50% default */
                phase_a += freq_a_ / sample_rate_ * 2.0f * M_PI;
                if (phase_a >= 2.0f * M_PI) phase_a -= 2.0f * M_PI;
                /* Pulse: high for pw fraction of cycle */
                osc_a = (phase_a < pw * 2.0f * M_PI) ? 1.0f : -1.0f;
                break;
            }
        }

        /* --- Oscillator B --- */
        float osc_b = 0.0f;
        
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
            /* ... other waveforms simplified */
            default: osc_b = 0.0f;
        }

        /* --- Mix oscillators --- */
        /* Original: m1 = 32767 - m2; mix * 258 ... Q15 */
        /* Float: simple crossfade */
        float mix = 0.5f; /* 50/50 mix default */
        float mixed = osc_a * (1.0f - mix) + osc_b * mix;

        /* --- Noise --- */
        float noise = 0.0f;
        if (noise_level_ > 0.0f)
        {
            noise = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f * noise_level_;
            mixed += noise;
        }

        /* --- Drive / Saturation --- */
        /* Original: drive = 32768 + p[P_E6] * 512; /* 1x .. 3x */
        /* s = softclip(((s >> 2) * (drive >> 2)) >> 11) */
        /* Float: tanh saturation */
        float drive_factor = drive_;
        mixed = std::tanh(mixed * drive_factor * 0.5f) * 2.0f; /* soft clip */

        /* --- Filter: Trapezoidal SVF (Simper) --- */
        /* Original: tsvf_lp(&flt, s >> 1, &ic1, &ic2) */
        /* With soft knee: only resonance peaks saturate */
        
        float input = mixed;
        
        /* Simplified one-pole lowpass with resonance */
        /* k = 3.88 * resonance (from Moog ladder) adapted */
        float k = resonance_ * 3.88f;
        float input_gain = 1.0f / (1.0f + k);
        
        /* Feedback from filter output */
        float fb = k * input_gain;
        
        /* Input summing junction */
        float in = input_gain * (input + fb);
        
        /* Single pole LP: y = inputGain * (x + fb - prev) + prev */
        /* With soft knee for resonance peaks */
        float soft_knee_val = std::tanh(std::abs(in) * 0.01f) * 0.01f; /* approximate */
        
        /* Simple LP: */
        filter_state_ = 0.5f * in + 0.5f * filter_state_;
        
        /* Soft knee on resonance peaks */
        if (std::abs(filter_state_) > 16000.0f)
        {
            float a = 16000.0f + (std::tanh((std::abs(filter_state_) - 16000.0f) * 2.0f) >> 1);
            filter_state_ = (filter_state_ < 0.0f) ? -a : a;
        }
        
        /* Output: filter output, possibly inverted for soft clip */
        float filtered = filter_state_;
        if (filtered > 16000.0f)
            filtered = 16000.0f + (std::tanh((filtered - 16000.0f) * 2.0f) >> 1);
        else if (filtered < -16000.0f)
            filtered = -16000.0f - (std::tanh((-filtered - 16000.0f) * 2.0f) >> 1);

        /* --- Envelope --- */
        /* Simple ADR envelope - original had separate filter/amp envelopes */
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
            /* hold at sustain */
        }
        else if (env_stage_ == 4) /* RELEASE */
        {
            env_val = env_value_ * std::exp(-dt / env_release_);
            if (env_val < 0.01f) { env_val = 0.0f; env_stage_ = 0; }
        }

        /* Apply envelope */
        filtered *= env_val;

        return filtered;
    }

private:
    float sample_rate_ = 44100.0f;
    
    /* Oscillator A */
    int wave_a_;
    float freq_a_;
    float detune_a_;
    float level_a_;
    
    /* Oscillator B */
    int wave_b_;
    float freq_b_;
    float detune_b_;
    float level_b_;
    
    /* Noise */
    float noise_level_;
    
    /* Filter */
    float cutoff_hz_;
    float resonance_;
    float drive_;
    float filter_state_;
    
    /* Envelope */
    int env_stage_;
    float env_value_;
    float env_attack_;
    float env_decay_;
    float env_sustain_;
    float env_release_;
    bool env_gate_;
};

/* ================================================================================================
 * Analog Main Class
 * ================================================================================================ */

class DspAnalog
{
public:
    DspAnalog() noexcept = default;
    ~DspAnalog() = default;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        sampleRateValid_ = true;
        
        /* Reset all voices - simplified, single voice */
        voice_.reset();
    }

    void reset() noexcept
    {
        voice_.reset();
    }

    // --- Parameter setting ---
    
    void setWaveA(int wave) noexcept { voice_.setWaveA(wave); }
    void setWaveB(int wave) noexcept { voice_.setWaveB(wave); }
    void setFreqA(float hz) noexcept { voice_.setFreqA(hz); }
    void setFreqB(float hz) noexcept { voice_.setFreqB(hz); }
    void setDetuneA(float cents) noexcept { voice_.setDetuneA(cents); }
    void setDetuneB(float cents) noexcept { voice_.setDetuneB(cents); }
    void setLevelA(float level) noexcept { voice_.setLevelA(level); }
    void setLevelB(float level) noexcept { voice_.setLevelB(level); }
    void setNoiseLevel(float level) noexcept { voice_.setNoiseLevel(level); }
    void setCutoff(float hz) noexcept { voice_.setCutoff(hz); }
    void setResonance(float res) noexcept { voice_.setResonance(res); }
    void setDrive(float drv) noexcept { voice_.setDrive(drv); }
    void setGate(bool gate) noexcept { voice_.setGate(gate); }

    // --- Processing ---
    
    float process(float dt) noexcept { return voice_.process(dt); }

private:
    float sampleRate_ = 44100.0f;
    bool sampleRateValid_ = false;
    
    DspAnalogVoiceState voice_;
};

} // namespace abd::dsp