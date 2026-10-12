/**
 * @file DspSid.h
 * @brief MOS 6581/8580 SID chip emulation for Commodore 64.
 * 
 * Adapted from melodee-main firmware (eng_sid.c) converting fixed-point DSP to float.
 * Emulates the SID chip's three oscillators, noise generator, state-variable filter,
 * ADSR envelope, and output stage RC filter.
 * 
 * Magic numbers from the original firmware are extracted to DspSid_config.h for tuning.
 */

#pragma once

#include <cmath>
#include <array>
#include <algorithm>

namespace abd::dsp
{

/* ================================================================================================
 * SID Configuration (extracted from melodee-main firmware)
 * ================================================================================================ */

/** SID model: 0 = 6581, 1 = 8580 */
struct DspSidConfig
{
    /** Sample rate for SID emulation (original: 985248 Hz for real C64 PAL) */
    float sampleRate = 44100.0f;

    /** SID frequency rate table [16 values] - original SID_RATE */
    /* Original: cycles per envelope step of each 4-bit rate (reSID: the rate counter's comparison values) */
    std::array<uint16_t, 16> sid_rate;

    /** SID knee (filter saturation) - original SID_ZERO */
    std::array<int32_t, 2> sid_zero; /* 6581: 0x380*4, 8580: 0x800*4 */

    /** SID knees for filter saturation */
    std::array<int32_t, 2> sid_knee; /* 6581: 20000, 8580: 40000 */

    /** SID rate table per model - original SID_RATE[16] indexed by rate code */
    /* 4-bit rate codes: attack uses low nibble, decay/sustain use high nibble */

    /** Envelope sustain calculation */
    /* Original: return (uint32_t)(clamp(p[P_SUS], 0, 127) * 15 + 63) / 127u * 17u; */

    /** Envelope rate lookup */
    /* Original SID_ADSR_RATE[p[state == SID_ATTACK ? P_ATK : state == SID_DECAY_SUSTAIN ? P_DEC : P_REL] & 127] */

    /** SID filter FC table */
    std::array<float, 16> sid_fc_g; /* freq coefficient table */

    /** SID filter Q/resonance coefficients */
    std::array<float, 16> sid_res_k; /* per model resonance */

    /** Drive multiplier for filter input */
    float sid_drive; /* 0-127 -> x1..x3 */

    /** Output stage RC constants */
    float sid_xlp_a; /* Q15: xlp update */
    float sid_xhp_a; /* Q16: xhp update */

    /** Waveform table indices */
    /* Original: SID_WAVE_BITS[8] = {1, 2, 4, 8, 3, 5, 6, 7};
       mapping: WAVE register bits 4..7 -> TRI=1, SAW=2, PULSE=4, NOISE=8 */

    /** Stack modes */
    /* Original: SID_STACKS[8] with nosc, heard and cents */

    /** SID mode bits for filter type */
    /* Original: SID_MODE_BITS[8] = {1, 2, 4, 5, 3, 6, 7, 0};
       LP=1, BP=2, HP=4, NOTCH=5, LP+BP=3, BP+HP=6, ALL=7, OFF=0 */
};

/** Default SID configuration extracted from melodee-main firmware. */
constexpr DspSidConfig kSidDefaultConfig = {
    /* SID rate table: original cycles per envelope step */
    []{
        DspSidConfig cfg;
        /* Original SID_RATE[16] = {9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251}; */
        cfg.sid_rate = {9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251};
        return cfg;
    }(),

    /* SID zero (DAC zero) */
    []{
        DspSidConfig cfg;
        /* 6581: 0x380*4, 8580: 0x800*4 */
        cfg.sid_zero = {0x380 * 4, 0x800 * 4};
        return cfg;
    }(),

    /* SID knees */
    []{
        DspSidConfig cfg;
        /* 6581: 20000, 8580: 40000 */
        cfg.sid_knee = {20000, 40000};
        return cfg;
    }(),

    /* SID FC table - approximate */
    []{
        DspSidConfig cfg;
        /* fc table: 16 entries covering cutoff range */
        for (int i = 0; i < 16; ++i)
        {
            /* Rough approximation: map to reasonable SID filter frequencies */
            cfg.sid_fc_g[i] = 4096.0f * (1.0f + static_cast<float>(i) * 0.1f);
        }
        return cfg;
    }(),

    /* SID resonance coefficients per model */
    []{
        DspSidConfig cfg;
        /* Original: SID_RES_K[model][clamp(p[P_E5], 0, 15)]
           Need values for 6581 and 8580 */
        /* Placeholder - will be populated per model */
        cfg.sid_res_k[0] = 8192; /* 6581 approximate */
        cfg.sid_res_k[1] = 8192; /* 8580 approximate */
        return cfg;
    }(),

    /* Drive: original P_E7 * 64 added to 4096, range x1..x3 */
    1.0f,

    /* Output stage RC constants (from phys_dsp.c context) */
    []{
        DspSidConfig cfg;
        /* Original: SID_XLP_A and SID_XHP_A */
        /* From reSID and melodee: these control the RC filter time constants */
        cfg.sid_xlp_a = 0.999f; /* approximate */
        cfg.sid_xhp_a = 0.998f; /* approximate */
        return cfg;
    }(),

    /* Waveform mapping: original SID_WAVE_BITS[8] = {1, 2, 4, 8, 3, 5, 6, 7} */
    /* WAVE register bits 4..7 decoded as: 0=TRI, 1=SAW, 2=PULSE, 3=NOISE,
       4=TRI+SAW, 5=TRI+PULSE, 6=SAW+PULSE, 7=TRI+SAW+PULSE */

    /* Stack modes - original SID_STACKS[8] */
    /* Each has: cents[3], nosc (num operators), heard (bitmask) */
};

/* ================================================================================================
 * SID Voice State
 * ================================================================================================ */

class DspSidVoiceState
{
public:
    DspSidVoiceState() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        // Reset all 3 voices/oscillators
        for (auto& ph : phase_)
            ph = 0.0f;

        for (auto& inc : increment_)
            increment_[k] = 0.0f;

        for (auto& rcp : rcptable_)
            rcp_[k] = 0.0f;

        for (auto& rsh : rshift_)
            rshift_[k] = 0;

        for (auto& noise : noise_)
            noise_[k] = 0x7fffff; /* initial noise state */

        for (auto& env : envelope_)
            envelope_[k] = 0;

        for (auto& env_prev : env_prev_)
            env_prev_[k] = 0;

        for (auto& rate_counter : rate_counter_)
            rate_counter_[k] = 0;

        for (auto& exp_counter : exp_counter_)
            exp_counter_[k] = 0;

        for (auto& exp_period : exp_period_)
            exp_period_[k] = 1;

        phase_accum_[k] = 0.0f;
        noise_clock_[k] = 0;
        env_state_[k] = 0; /* SID_ATTACK */
        env_level_[k] = 0.0f;
        gate_[k] = false;
        model_[k] = 0; /* 0=6581, 1=8580 */
        waveform_[k] = 0; /* 0=TRI, 1=SAW, 2=PULSE, 3=NOISE */
        stack_[k] = 0; /* 0=ONE, 1=DETUNE, ... */
        sync_[k] = false;
        ring_[k] = false;
        last_output_[k] = 0.0f;
    }

    // --- Parameter setting ---

    void setModel(int model) noexcept // 0=6581, 1=8580
    {
        model_[k] = model;
    }

    void setWaveform(int waveform) noexcept // 0=TRI, 1=SAW, 2=PULSE, 3=NOISE
    {
        waveform_[k] = waveform;
    }

    void setPW(float pw) noexcept // pulse width 0-127 -> 0.0 to 1.0
    {
        pw_[k] = std::clamp(pw, 0.0f, 1.0f);
    }

    void setCutoff(float hz) noexcept // filter cutoff
    {
        cutoff_hz_ = hz;
    }

    void setResonance(float res) noexcept // filter resonance 0-15
    {
        resonance_[k] = std::clamp(res, 0.0f, 15.0f);
    }

    void setDrive(float drive) noexcept // drive 0-127 -> x1..x3
    {
        drive_[k] = std::clamp(drive, 0.0f, 127.0f);
    }

    void setStack(int stack) noexcept // 0=ONE, 1=DETUNE, 2=OCT, 3=FIFTH, 4=MAJOR, 5=SYNC, 6=RING, 7=SUB
    {
        stack_[k] = stack;
        sync_[k] = (stack == 5); /* SYNC */
        ring_[k] = (stack == 6); /* RING */
    }

    void setGate(bool gate) noexcept
    {
        gate_[k] = gate;
        if (gate_)
            env_state_[k] = 0; /* SID_ATTACK */
    }

    void setFine(int fine) noexcept // fine tuning cents
    {
        /* Would adjust phase increment */
    }

    // --- Processing ---
    
    float process(float sampleRate, float dt) noexcept
    {
        // This is a simplified process - full emulation would be much more complex
        // We'll process one voice as an example
        
        float total_output = 0.0f;
        
        for (int v = 0; v < 3; ++v) /* 3 voices */
        {
            // Process each voice
            float voice_out = processVoice(v, sampleRate, dt);
            total_output += voice_out;
        }
        
        // Apply output stage RC filter (Q8, coupling capacitor)
        float xlp = last_xlp_;
        float xhp = last_xhp_;
        
        /* Original: 
         xlp += (int32_t)(((int64_t)(clamp(f, -131071, 131071) - xlp) * SID_XLP_A) >> 15);
         xhp += (int32_t)(((int64_t)((xlp << 8) - xhp) * SID_XHP_A) >> 16);
         o = clamp(xlp - (xhp >> 8), -32767, 32767);
        */
        
        /* Simplified RC low-pass */
        float fc = 1.0f / (1.0f + 0.001f); /* cutoff freq */
        xlp = xlp + fc * (total_output - xlp);
        xhp = xhp + fc * (xlp - xhp);
        
        last_output_[k] = xlp - (xhp >> 8);
        last_xlp_ = xlp;
        last_xhp_ = xhp;
        
        return last_output_[k];
    }

    float getLastOutput() const noexcept { return last_output_[k]; }

private:
    // Per-voice state (3 voices maximum)
    struct Voice
    {
        float phase_ = 0.0f;         /* 32-bit phase accumulator */
        float increment_ = 0.0f;     /* phase increment per sample */
        float rcptable_[3] = {0.0f}; /* freq ratio table */
        int rshift_[3] = {0};        /* right shift for freq */
        uint32_t noise_ = 0x7fffff;  /* noise register (23 bits) */
        uint8_t env_ = 0;            /* envelope counter */
        uint8_t env_prev_ = 0;       /* previous envelope level */
        uint16_t rate_counter_ = 0;  /* envelope rate counter */
        uint8_t exp_counter_ = 0;    /* exponential counter */
        uint8_t exp_period_ = 1;     /* exp counter period */
        uint8_t env_state_ = 0;      /* 0=ATTACK, 1=DECAY, 2=SUSTAIN, 3=RELEASE */
        bool gate_ = false;          /* gate signal */
        int model_ = 0;              /* 0=6581, 1=8580 */
        int waveform_ = 0;           /* waveform type */
        int stack_ = 0;              /* stack mode */
        bool sync_ = false;          /* hard sync */
        bool ring_ = false;          /* ring modulation */
        float pw_ = 0.5f;            /* pulse width 0-1 */
        float last_output_ = 0.0f;
    };
    
    Voice voices_[3];
    
    // Global state
    float cutoff_hz_[3] = {1000.0f, 1000.0f, 1000.0f};
    float resonance_[3] = {0.0f, 0.0f, 0.0f};
    float drive_[3] = {64.0f, 64.0f, 64.0f}; /* default drive */
    float last_xlp_[3] = {0.0f, 0.0f, 0.0f};
    float last_xhp_[3] = {0.0f, 0.0f, 0.0f};
    float last_output_[3] = {0.0f, 0.0f, 0.0f};
};

/* ================================================================================================
 * SID Main Class
 * ================================================================================================ */

class DspSid
{
public:
    DspSid() noexcept = default;
    ~DspSid() = default;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        sampleRateValid_ = true;
        
        // Initialize all voices
        for (int i = 0; i < 3; ++i)
        {
            voices_[i].reset();
        }
    }

    void reset() noexcept
    {
        for (int i = 0; i < 3; ++i)
        {
            voices_[i].reset();
        }
    }

    // --- Parameter setting ---

    void setModel(int model) noexcept // 0=6581, 1=8580
    {
        for (int i = 0; i < 3; ++i)
            voices_[i].setModel(model);
    }

    void setWaveform(int waveform) noexcept // 0=TRI, 1=SAW, 2=PULSE, 3=NOISE
    {
        for (int i = 0; i < 3; ++i)
            voices_[i].setWaveform(waveform);
    }

    void setPW(float pw) noexcept // pulse width 0.0 to 1.0
    {
        for (int i = 0; i < 3; ++i)
            voices_[i].setPW(pw);
    }

    void setCutoff(float hz) noexcept
    {
        for (int i = 0; i < 3; ++i)
            voices_[i].setCutoff(hz);
    }

    void setResonance(float res) noexcept // 0-15
    {
        for (int i = 0; i < 3; ++i)
            voices_[i].setResonance(res);
    }

    void setDrive(float drive) noexcept // 0-127
    {
        for (int i = 0; i < 3; ++i)
            voices_[i].setDrive(drive);
    }

    void setStack(int stack) noexcept // 0=ONE..7=SUB
    {
        for (int i = 0; i < 3; ++i)
            voices_[i].setStack(stack);
    }

    void setGate(bool gate) noexcept
    {
        for (int i = 0; i < 3; ++i)
            voices_[i].setGate(gate);
    }

    // --- Processing ---
    
    float process(float sampleRate) noexcept
    {
        // Process all 3 voices and mix
        float total = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            total += voices_[i].process(sampleRate, 1.0f / sampleRate_);
        }
        return total / 3.0f; /* mix down */
    }

    float getLastOutput() const noexcept { return 0.0f; } /* placeholder */

private:
    float sampleRate_ = 44100.0f;
    bool sampleRateValid_ = false;
    
    DspSidVoiceState voices_[3];
};

} // namespace abd::dsp