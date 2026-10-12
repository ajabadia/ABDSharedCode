/**
 * @file DspCz1.h
 * @brief Casio CZ-1 phase distortion synthesizer core interface.
 */

#pragma once

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#include <cmath>
#include <array>
#include <algorithm>
#include <cstdint>
#include "DspCz101Filter.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace abd::dsp
{

/* ================================================================================================
 * CZ-1 Parameter Wire <-> Domain Metas & Conversions
 * ============================================================================================== */
namespace Cz101ParameterMeta
{
    struct Cutoff {};
    struct Resonance {};
    struct LFO1_Rate {};
    struct LFO1_Amount {};
    struct DcoFineTune {};
    struct PitchBend {};
    struct Velocity {};
}

inline float wireToDomain(Cz101ParameterMeta::Cutoff, int raw) noexcept {
    return 20.0f * std::pow(1000.0f, std::clamp(raw, 0, 127) / 127.0f);
}
inline int domainToWire(Cz101ParameterMeta::Cutoff, float hz) noexcept {
    return std::clamp(static_cast<int>(std::round((std::log(std::clamp(hz, 20.0f, 20000.0f) / 20.0f) / std::log(1000.0f)) * 127.0f)), 0, 127);
}
inline float wireToDomain(Cz101ParameterMeta::Resonance, int raw) noexcept {
    return std::clamp(raw, 0, 127) / 127.0f;
}
inline int domainToWire(Cz101ParameterMeta::Resonance, float val) noexcept {
    return std::clamp(static_cast<int>(std::round(std::clamp(val, 0.0f, 1.0f) * 127.0f)), 0, 127);
}

struct DspCz1AmpTable {
    std::array<float, 128> amplitudes;
    DspCz1AmpTable() {
        for (int i = 0; i < 128; ++i) amplitudes[i] = (i < 32) ? static_cast<float>(i) / 127.0f : 1.0f;
    }
};

/* ================================================================================================
 * CZ-1 Voice State
 * ============================================================================================== */
class DspCz1VoiceState
{
public:
    DspCz1VoiceState() noexcept { reset(); }

    void reset() noexcept {
        phase_ = 0.0f; phase_inc_ = 0.0f; dcw_ = 0.0f; pivot_ = 1024.0f;
        k0_ = 1.0f; k1_ = 1.0f; window_ = 0; envelope_active_ = false;
        gate_ = false; sustain_ = 0; end_ = 0; level_ = 0.0f; last_output_ = 0.0f;
        
        // Reset del estado del filtro embebido
        filterState_.filter_z1 = 0.0f;
        filterState_.filter_z2 = 0.0f;
        filterState_.coefficients.valid = false;
    }

    void setDcw(float dcw) noexcept { dcw_ = std::clamp(dcw, 0.0f, 1023.0f); }
    void setPivot(float pivot) noexcept { pivot_ = pivot; recalcK(); }
    void setWindow(int window) noexcept { window_ = std::clamp(window, 0, 7); }
    void setGate(bool gate) noexcept { gate_ = gate; if (gate) envelope_active_ = true; }
    void setSustain(int sustain) noexcept { sustain_ = sustain; }
    void setEnd(int end) noexcept { end_ = end; }

    float process(float sampleRate, float dt) noexcept;
    float getLastOutput() const noexcept { return last_output_; }
    
    // Getter para que el filtro externo acceda al sub-estado de esta voz
    Cz101FilterVoiceState& getFilterState() noexcept { return filterState_; }

private:
    void recalcK() noexcept {
        k0_ = (pivot_ > 0.0f) ? (1024.0f / pivot_) : 1.0f;
        float denom = 2048.0f - pivot_;
        k1_ = (denom > 0.0f) ? (1024.0f / denom) : 1.0f;
    }
    float processWave(float phase) noexcept;
    float processEnvelope(float dt) noexcept;

private:
    // Estado del filtro encapsulado por voz
    Cz101FilterVoiceState filterState_;

    float phase_;
    float phase_inc_;
    float dcw_;
    float pivot_;
    float k0_;
    float k1_;
    int window_;
    bool envelope_active_;
    bool gate_;
    int sustain_;
    int end_;
    float level_;
    float last_output_;
};

class DspCz1 {
public:
    DspCz1() noexcept = default;
    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        voice_.reset();
        voice_.getFilterState().sampleRate = sampleRate;
    }
    void reset() noexcept { voice_.reset(); }
    void setDcw(float dcw) noexcept { voice_.setDcw(dcw); }
    void setPivot(float pivot) noexcept { voice_.setPivot(pivot); }
    void setWindow(int window) noexcept { voice_.setWindow(window); }
    void setGate(bool gate) noexcept { voice_.setGate(gate); }
    float process(float sampleRate) noexcept {
        return voice_.process(sampleRate, 1.0f / (sampleRate_ > 0.0f ? sampleRate_ : 44100.0f));
    }
private:
    float sampleRate_ = 44100.0f;
    DspCz1VoiceState voice_;
};

} // namespace abd::dsp