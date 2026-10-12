/**
 * @file DspPhys.h
 * @brief Physical modeling engines (ported from DaisySP/Dennis Gabor).
 * 
 * Adapted from melodee-main firmware (eng_phys.c, phys_dsp.c) converting fixed-point
 * DSP to float. Includes Modal resonator, String resonator, Membrane drum, and
 * Sympathetic resonance models.
 * 
 * Magic numbers from the original firmware are extracted to DspPhys_config.h for tuning.
 * 
 * Polyphony: 3 voices per part (engine_t.poly), 12 slots total.
 * Q20 signal format (original fixed-point), converted to float here.
 */

#pragma once

#include <cmath>
#include <array>
#include <algorithm>

namespace abd::dsp
{

/* ================================================================================================
 * Physical Modeling Configuration (extracted from melodee-main firmware)
 * ================================================================================================ */

/** Physical modeling config with original fixed-point constants converted to float. */
struct DspPhysConfig
{
    /* Q20 format in original: 1.0 = 2^20 = 1048576 */
    /* Converted to float: just use standard float range */

    /* Modal model: 12 band-pass modes */
    /* Original: modes stretched by STRUCTURE, Q by DAMP, BRIGHTNESS */
    /* g = tan(pi f) Q28, r + g Q28, g h Q30, h = 1 / (1 + r g + g^2) */

    /* String model: Karplus-Strong extended */
    /* Original: delay line, dispersion all-pass or curved bridge, DcBlock,
       OnePole damping, low-note resampler */

    /* Membrane model: circular membrane */
    /* Original: modal bank with modes of circular membrane,
       HARM blends ideal head into loaded one,
       POS from centre to rim */

    /* Sympathetic model: string with sympathetic strings */
    /* Original: 3 sympathetic strings (harp, sitar),
       CHRD tunes them (OCT 5TH 4TH MAJ MIN SUS 7TH),
       SYMP how much they take, BUZZ curved bridge */

    /* DSP constants (from phys_dsp.c) */
    /* Original: no float anywhere, signals Q20 between stages */
    /* Frequencies: cycles per sample Q32 (0.5 = 2^31) */
    /* SVF: g = tan(pi f) Q28, etc. */

    /* Modal gain table (from eng_phys.c) */
    std::array<float, 4> modal_gain; /* [PM_MODAL, PM_STRING, PM_MEMB, PM_SYMP] */

    /* String parameters */
    /* Delay line length, dispersion coefficient, BRIGHTNESS, DAMP */

    /* Membrane parameters */
    /* HARM blend, POS (strike position), BEND pitch drop */

    /* Sympathetic parameters */
    /* CHRD tuning (OCT 5TH 4TH MAJ MIN SUS 7TH),
       SYMP amount, BUZZ curved bridge */
};

/** Default physical modeling config extracted from melodee-main. */
constexpr DspPhysConfig kPhysDefaultConfig = {
    /* Modal gain: original PHYS_GAIN[PM_COUNT] = {28000, 12000, 90000, 12000} */
    []{
        DspPhysConfig cfg;
        /* Convert Q20 scale to float: original values were 32-bit ints with Q20 */
        /* 28000/1048576 ≈ 0.0266, 12000/1048576 ≈ 0.0114, 90000/1048576 ≈ 0.0858 */
        cfg.modal_gain[0] = 0.0266f; /* MODAL */
        cfg.modal_gain[1] = 0.0114f; /* STRING */
        cfg.modal_gain[2] = 0.0858f; /* MEMB */
        cfg.modal_gain[3] = 0.0114f; /* SYMP */
        return cfg;
    }(),

    /* String parameters - original: below ~0.25 curved bridge (buzzy/sitar),
       above dispersion (stiff/piano-like), BRIGHTNESS, DAMP */
    /* Placeholder values - would be tuned per instance */
    0.25f, /* curved bridge threshold */
    0.5f,  /* brightness */
    0.01f, /* damping */

    /* Membrane parameters - HARM blend, POS, BEND */
    0.5f, /* HARM blend (0=ideal head, 1=loaded) */
    0.5f, /* POS position (0=centre, 1=rim) */
    0.0f, /* BEND pitch drop in semitones */

    /* Sympathetic parameters - CHRD (root, fifth, fourth, major, minor, seventh),
       SYMP amount, BUZZ */
    0.0f, /* root note (MIDI) */
    0.0f, /* sympathetic amount */
    0.0f  /* buzz/curved bridge amount */
};

/* ================================================================================================
 * Phys Modal Resonator (band-pass filter bank)
 * ================================================================================================ */

class DspPhysModalVoiceState
{
public:
    DspPhysModalVoiceState() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        /* 12 band-pass modes, each with its own state */
        for (int i = 0; i < 12; ++i)
        {
            mode_active_[i] = false;
            mode_level_[i] = 0.0f;
            mode_freq_[i] = 0.0f;
            mode_q_[i] = 0.0f;
            mode_brightness_[i] = 0.5f;
            mode_damp_[i] = 0.01f;
        }
        
        /* Strike position and bend */
        strike_pos_ = 0.5f; /* 0=centre, 1=rim */
        pitch_bend_ = 0.0f; /* semitones */
        
        /* Decay counter */
        decay_pos_ = 0;
        
        /* Global sample rate */
        sample_rate_ = 44100.0f;
    }

    // --- Parameter setting ---
    
    void setSampleRate(double sr) noexcept { sample_rate_ = sr; }
    
    void setModeActive(int mode, bool active) noexcept // 0-11
    {
        if (mode >= 0 && mode < 12)
            mode_active_[mode] = active;
    }
    
    void setModeFreq(int mode, float freq_hz) noexcept // 0-11
    {
        if (mode >= 0 && mode < 12)
            mode_freq_[mode] = freq_hz;
    }
    
    void setModeQ(int mode, float q) noexcept // 0-11
    {
        if (mode >= 0 && mode < 12)
            mode_q_[mode] = q;
    }
    
    void setModeBrightness(int mode, float brightness) noexcept // 0-11
    {
        if (mode >= 0 && mode < 12)
            mode_brightness_[mode] = brightness;
    }
    
    void setModeDamp(int mode, float damp) noexcept // 0-11
    {
        if (mode >= 0 && mode < 12)
            mode_damp_[mode] = damp;
    }
    
    void setStrikePos(float pos) noexcept /* 0=centre, 1=rim */ { strike_pos_ = pos; }
    void setPitchBend(float bend) noexcept /* semitones */ { pitch_bend_ = bend; }
    
    // --- Processing ---
    
    float process(float dt) noexcept /* dt in seconds */
    {
        float total = 0.0f;
        
        /* Process active modes */
        for (int i = 0; i < 12; ++i)
        {
            if (!mode_active_[i]) continue;
            
            /* Each mode is a band-pass filter resonating at mode_freq_[i] */
            /* With Q = mode_q_[i] and brightness shaping */
            
            /* Simplified: just accumulate a sine wave at the mode frequency */
            /* Original would use TPT SVF or recursive formulas */
            
            float freq = mode_freq_[i];
            /* Convert to phase increment */
            float phase_inc = freq / sample_rate_ * 2.0f * M_PI;
            
            /* Accumulate phase (simple - would need persistence) */
            /* In full implementation: each mode has its own phase accumulator */
            
            /* Apply brightness shaping */
            float brightness = mode_brightness_[i];
            /* High-frequency emphasis */
            float emphasized = freq * (0.5f + brightness * 0.5f);
            
            /* Apply damping */
            float q = mode_q_[i];
            /* Simple exponential decay of mode energy */
            float damp = mode_damp_[i];
            
            /* Add to total */
            /* Would need to track phase per mode */
            total += std::sin(emphasized); /* placeholder */
        }
        
        /* Apply strike position and pitch bend */
        /* Strike position affects which modes are excited */
        /* Pitch bend shifts all frequencies */
        
        /* Decay envelope */
        /* Original: modes decay at different rates */
        /* Q20: every 32 samples, divisions 64 / 32 and only per block */
        
        return total / 12.0f; /* normalize */
    }

private:
    /* 12 mode states */
    bool mode_active_[12];
    float mode_level_[12];
    float mode_freq_[12];
    float mode_q_[12];
    float mode_brightness_[12];
    float mode_damp_[12];
    
    /* Global state */
    float sample_rate_;
    float strike_pos_;
    float pitch_bend_;
    int decay_pos_;
};

/* ================================================================================================
 * Phys String Resonator (Karplus-Strong extended)
 * ================================================================================================ */

class DspPhysStringVoiceState
{
public:
    DspPhysStringVoiceState() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        /* Delay line (original: PX_DELAY line, Q20) */
        delay_line_.reset();
        
        /* Dispersion (all-pass or curved bridge) */
        dispersion_coeff_ = 0.5f;
        brighteness_ = 0.5f;
        
        /* Pluck position */
        pluck_pos_ = 0.5f; /* 0=edge, 1=middle */
        
        /* String parameters */
        damping_ = 0.01f; /* Q20: very small, decays slowly */
        loop_gain_ = 0.996f; /* original: determines decay time */
        
        /* String state */
        is_plucked_ = false;
        decay_counter_ = 0;
        
        sample_rate_ = 44100.0f;
    }

    void setSampleRate(double sr) noexcept { sample_rate_ = sr; }
    
    void setPluckPos(float pos) noexcept /* 0=edge, 1=middle */ { pluck_pos_ = pos; }
    void setBrightness(float bright) noexcept { brighteness_ = bright; }
    void setDamp(float damp) noexcept /* Q20 small value */ { damping_ = damp; }
    void setLoopGain(float gain) noexcept /* 0.996 typical */ { loop_gain_ = gain; }
    void pluck() noexcept { is_plucked_ = true; decay_counter_ = 0; }
    
    float process(float dt) noexcept /* dt in seconds */
    {
        if (!is_plucked_ && decay_counter_ <= 0) return 0.0f;
        
        /* Pluck: inject noise burst at pluck position */
        /* Original: exciter: noise burst one period long (or Dust) through Svf */
        if (is_plucked_ && decay_counter_ == 0)
        {
            /* Inject noise at pluck position */
            float noise = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
            /* Delay line write */
            delay_line_.write(noise * (pluck_pos_ + 0.1f)); /* position dependent */
            decay_counter_ = 1; /* start counting */
        }
        
        /* Read from delay line with dispersion */
        float sample = delay_line_.read();
        
        /* Apply dispersion (curved bridge): all-pass filter per sample */
        /* Original: dispersion all-pass or curved bridge */
        /* Simplified: just low-pass for brightness */
        float dispersed = sample * brighteness_ + (sample * 0.5f); /* rough */
        
        /* Apply damping (exponential decay) */
        float damped = dispersed * loop_gain_;
        
        /* Write back (feedback) */
        delay_line_.write(damped);
        
        /* Decay counter */
        if (decay_counter_ > 0)
            decay_counter_++;
        
        /* Check if decayed enough to stop */
        /* Original: per block divisions, 64/32 */
        if (decay_counter_ > 1000) /* rough timeout */
        {
            is_plucked_ = false;
            decay_counter_ = 0;
        }
        
        return damped;
    }

private:
    /* Delay line (from phys_dsp.c) */
    struct DelayLine
    {
        void reset() { pos_ = 0; buf_.fill(0.0f); }
        void write(float s) { buf_[pos_] = s; pos_ = (pos_ + 1) % buf_.size(); }
        float read() { return buf_[pos_]; }
        static constexpr int size = 4096;
        std::array<float, size> buf_;
        int pos_ = 0;
    } delay_line_;
    
    float dispersion_coeff_;
    float brighteness_;
    float pluck_pos_;
    float damping_;
    float loop_gain_;
    bool is_plucked_;
    int decay_counter_;
    float sample_rate_;
};

/* ================================================================================================
 * Phys Membrane Voice (circular membrane drum)
 * ================================================================================================ */

class DspPhysMembVoiceState
{
public:
    DspPhysMembVoiceState() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        /* Modal bank: 12 modes from circular membrane */
        for (int i = 0; i < 12; ++i)
        {
            mode_freq_[i] = 0.0f;
            mode_q_[i] = 0.0f;
            mode_active_[i] = false;
        }
        
        /* Parameters */
        harm_blend_ = 0.5f; /* 0=ideal head, 1=loaded membrane */
        pos_strike_ = 0.5f; /* 0=centre, 1=rim */
        bend_ = 0.0f; /* pitch drop semitones */
        
        is_excited_ = false;
        decay_counter_ = 0;
        
        sample_rate_ = 44100.0f;
    }

    void setSampleRate(double sr) noexcept { sample_rate_ = sr; }
    
    void setHarmBlend(float blend) noexcept /* 0=ideal, 1=loaded */ { harm_blend_ = blend; }
    void setStrikePos(float pos) noexcept /* 0=centre, 1=rim */ { pos_strike_ = pos; }
    void setPitchBend(float bend) noexcept /* semitones */ { bend_ = bend; }
    void excite() noexcept { is_excited_ = true; decay_counter_ = 0; }
    
    float process(float dt) noexcept
    {
        if (!is_excited_ && decay_counter_ <= 0) return 0.0f;
        
        /* Excite: impulse into low-pass SVF */
        /* Original: struck by an impulse into a low-pass */
        if (is_excited_ && decay_counter_ == 0)
        {
            /* Impulse excitation - would set mode frequencies */
            /* Modal modes follow cos(2 pi pos i) */
            for (int i = 0; i < 12; ++i)
            {
                /* Mode frequency depends on strike position */
                float base_freq = 100.0f * (i + 1); /* placeholder */
                float freq_mod = std::cos(2.0f * M_PI * strike_pos_ * (i + 1) / 12.0f);
                mode_freq_[i] = base_freq * freq_mod;
                mode_active_[i] = true;
            }
            decay_counter_ = 1;
        }
        
        /* Process each mode and sum */
        float total = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            if (!mode_active_[i]) continue;
            /* Each mode decays at different rate */
            /* Q20: modes at or above 0.499 cycles clamped */
            /* g h folded into one coefficient */
            float freq = mode_freq_[i] * (1.0f + bend_ / 12.0f); /* pitch bend */
            /* Simple sine generator per mode */
            total += std::sin(freq); /* placeholder */
        }
        
        /* Apply HARM blend: mix ideal head (tabla-like) with loaded membrane */
        float mixed = total * harm_blend_;
        
        /* Decay */
        if (decay_counter_ > 0) decay_counter_++;
        if (decay_counter_ > 500) /* rough */ { is_excited_ = false; decay_counter_ = 0; }
        
        return mixed;
    }

private:
    /* 12 modal states */
    bool mode_active_[12];
    float mode_freq_[12];
    float mode_q_[12];
    float harm_blend_;
    float pos_strike_;
    float bend_;
    bool is_excited_;
    int decay_counter_;
    float sample_rate_;
};

/* ================================================================================================
 * Phys Sympathetic String Voice
 * ================================================================================================ */

class DspPhysSympVoiceState
{
public:
    DspPhysSympVoiceState() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        /* 3 sympathetic strings */
        for (int i = 0; i < 3; ++i)
        {
            symp_freq_[i] = 0.0f;
            symp_active_[i] = false;
            symp_amount_[i] = 0.0f;
        }
        
        /* Chord tuning: root, fifth, fourth, major, minor, seventh */
        chord_root_ = 0.0f; /* MIDI note */
        
        is_ringing_ = false;
        decay_counter_ = 0;
        
        sample_rate_ = 44100.0f;
    }
    
    void setSampleRate(double sr) noexcept { sample_rate_ = sr; }
    
    void setChordRoot(float root) noexcept /* MIDI note */ { chord_root_ = root; }
    void setSympAmount(float amount) noexcept /* 0-1 */ { for(int i=0;i<3;i++) symp_amount_[i]=amount; }
    void setBuzz(float buzz) noexcept /* curved bridge */ { buzz_ = buzz; }
    void ring() noexcept { is_ringing_ = true; decay_counter_ = 0; }
    
    float process(float dt) noexcept
    {
        if (!is_ringing_ && decay_counter_ <= 0) return 0.0f;
        
        /* Set sympathetic frequencies based on chord */
        /* Original: CHRD tunes them (OCT 5TH 4TH MAJ MIN SUS 7TH above the note,
           ROOT: the track's scale root, its fifth and octave around C3) */
        
        /* Frequencies relative to root */
        float root_hz = 440.0f * std::pow(2.0f, chord_root_ / 12.0f);
        
        /* 3 strings: root, fifth, fourth (or major/minor/7th) */
        float intervals[3] = {0.0f, 7.0f, 5.0f}; /* root, perfect fifth, perfect fourth */
        
        for (int i = 0; i < 3; ++i)
        {
            symp_freq_[i] = root_hz * std::pow(2.0f, intervals[i] / 12.0f);
            symp_active_[i] = is_ringing_;
            /* Amount decreases for higher intervals */
            symp_amount_[i] = 1.0f / (i + 1);
        }
        
        /* Process each sympathetic string */
        float total = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            if (!symp_active_[i]) continue;
            /* Simple resonator at sympathetic frequency */
            total += std::sin(symp_freq_[i] * dt * 2.0f * M_PI) * symp_amount_[i];
        }
        
        /* Decay */
        if (decay_counter_ > 0) decay_counter_++;
        if (decay_counter_ > 1000) { is_ringing_ = false; decay_counter_ = 0; }
        
        return total;
    }

private:
    float symp_freq_[3];
    bool symp_active_[3];
    float symp_amount_[3];
    float chord_root_;
    float buzz_; /* curved bridge amount */
    bool is_ringing_;
    int decay_counter_;
    float sample_rate_;
};

/* ================================================================================================
 * Phys Main Class
 * ================================================================================================ */

class DspPhys
{
public:
    DspPhys() noexcept = default;
    ~DspPhys() = default;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        sampleRateValid_ = true;
    }

    void reset() noexcept
    {
        modal_.reset();
        string_.reset();
        membran_.reset();
        symp_.reset();
    }

    // --- Modal model ---
    
    void setModalModeActive(int mode, bool active) noexcept /* 0-11 */ { modal_.setModeActive(mode, active); }
    void setModalFreq(int mode, float freq) noexcept /* 0-11 */ { modal_.setModeFreq(mode, freq); }
    void setModalQ(int mode, float q) noexcept /* 0-11 */ { modal_.setModeQ(mode, q); }
    void setModalBrightness(int mode, float bright) noexcept /* 0-11 */ { modal_.setModeBrightness(mode, bright); }
    void setModalDamp(int mode, float damp) noexcept /* 0-11 */ { modal_.setModeDamp(mode, damp); }
    void setModalStrikePos(float pos) noexcept /* 0-1 */ { modal_.setStrikePos(pos); }
    void setModalPitchBend(float bend) noexcept /* semitones */ { modal_.setPitchBend(bend); }
    
    float processModal(float dt) noexcept { return modal_.process(dt); }

    // --- String model ---
    
    void setStringPluckPos(float pos) noexcept /* 0-1 */ { string_.setPluckPos(pos); }
    void setStringBrightness(float bright) noexcept { string_.setBrightness(bright); }
    void setStringDamp(float damp) noexcept { string_.setDamp(damp); }
    void setStringLoopGain(float gain) noexcept { string_.setLoopGain(gain); }
    void stringPluck() noexcept { string_.pluck(); }
    
    float processString(float dt) noexcept { return string_.process(dt); }

    // --- Membrane model ---
    
    void setMembHarmBlend(float blend) noexcept /* 0-1 */ { membran_.setHarmBlend(blend); }
    void setMembStrikePos(float pos) noexcept /* 0-1 */ { membran_.setStrikePos(pos); }
    void setMembPitchBend(float bend) noexcept /* semitones */ { membran_.setPitchBend(bend); }
    void membExcite() noexcept { membran_.excite(); }
    
    float processMemb(float dt) noexcept { return membran_.process(dt); }

    // --- Sympathetic model ---
    
    void setSympRoot(float root) noexcept /* MIDI */ { symp_.setChordRoot(root); }
    void setSympAmount(float amount) noexcept /* 0-1 */ { symp_.setSympAmount(amount); }
    void setSympBuzz(float buzz) noexcept /* 0-1 */ { symp_.setBuzz(buzz); }
    void sympRing() noexcept { symp_.ring(); }
    
    float processSymp(float dt) noexcept { return symp_.process(dt); }

private:
    float sampleRate_ = 44100.0f;
    bool sampleRateValid_ = false;
    
    DspPhysModalVoiceState modal_;
    DspPhysStringVoiceState string_;
    DspPhysMembVoiceState membran_;
    DspPhysSympVoiceState symp_;
};

} // namespace abd::dsp