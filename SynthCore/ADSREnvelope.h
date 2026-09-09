#pragma once
#include "EnvelopeCurves.h"

namespace abd::synth {

enum class EnvelopeStage {
    Idle = 0,
    Attack,
    Decay,
    Sustain,
    Release
};

/**
 * @brief High-performance Exponential ADSR Envelope Generator.
 * Matches the analogue capacitor charging/discharging response of Korg MS2000 EG1/EG2.
 */
class ADSREnvelope {
public:
    ADSREnvelope() = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setAttack(float attack0to1) noexcept;
    void setDecay(float decay0to1) noexcept;
    void setSustain(float sustain0to1) noexcept;
    void setRelease(float release0to1) noexcept;

    void noteOn(float velocity = 1.0f) noexcept;
    void noteOff() noexcept;

    float getNextSample() noexcept;
    float getCurrentLevel() const noexcept { return currentLevel_; }
    bool isIdle() const noexcept { return stage_ == EnvelopeStage::Idle; }
    EnvelopeStage getStage() const noexcept { return stage_; }

private:
    double sampleRate_{ 44100.0 };
    EnvelopeStage stage_{ EnvelopeStage::Idle };

    float attackParam_{ 0.1f };
    float decayParam_{ 0.3f };
    float sustainParam_{ 0.7f };
    float releaseParam_{ 0.3f };

    float currentLevel_{ 0.0f };
    float targetLevel_{ 0.0f };
    float velocityGain_{ 1.0f };

    double attackRate_{ 0.01 };
    double decayMult_{ 0.999 };
    double releaseMult_{ 0.999 };

    void updateRates() noexcept;
};

} // namespace abd::synth
