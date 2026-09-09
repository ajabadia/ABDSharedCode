#include "ADSREnvelope.h"
#include <cmath>

namespace abd::synth {

void ADSREnvelope::prepare(double sampleRate) noexcept
{
    sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
    updateRates();
    reset();
}

void ADSREnvelope::reset() noexcept
{
    stage_ = EnvelopeStage::Idle;
    currentLevel_ = 0.0f;
    targetLevel_ = 0.0f;
}

void ADSREnvelope::setAttack(float attack0to1) noexcept
{
    attackParam_ = attack0to1;
    updateRates();
}

void ADSREnvelope::setDecay(float decay0to1) noexcept
{
    decayParam_ = decay0to1;
    updateRates();
}

void ADSREnvelope::setSustain(float sustain0to1) noexcept
{
    sustainParam_ = std::max(0.0f, std::min(1.0f, sustain0to1));
}

void ADSREnvelope::setRelease(float release0to1) noexcept
{
    releaseParam_ = release0to1;
    updateRates();
}

void ADSREnvelope::updateRates() noexcept
{
    float attackSec = EnvelopeCurves::getAttackTimeSeconds(attackParam_);
    attackRate_ = 1.0 / (attackSec * sampleRate_);

    float decaySec = EnvelopeCurves::getDecayReleaseTimeSeconds(decayParam_);
    decayMult_ = EnvelopeCurves::getDecayMultiplier(decaySec, sampleRate_);

    float releaseSec = EnvelopeCurves::getDecayReleaseTimeSeconds(releaseParam_);
    releaseMult_ = EnvelopeCurves::getDecayMultiplier(releaseSec, sampleRate_);
}

void ADSREnvelope::noteOn(float velocity) noexcept
{
    velocityGain_ = std::max(0.1f, std::min(1.0f, velocity));
    stage_ = EnvelopeStage::Attack;
    targetLevel_ = 1.0f;
}

void ADSREnvelope::noteOff() noexcept
{
    if (stage_ != EnvelopeStage::Idle)
    {
        stage_ = EnvelopeStage::Release;
        targetLevel_ = 0.0f;
    }
}

float ADSREnvelope::getNextSample() noexcept
{
    switch (stage_)
    {
        case EnvelopeStage::Attack:
        {
            // Exponential RC-charge curve: fast start, easing into peak
            // Target overshoots to 1.582 so that level crosses 1.0 at the
            // specified attack time (1 time constant = 63.2% of target).
            constexpr float kOvershootTarget = 1.582f;
            float coeff = static_cast<float>(attackRate_) * 6.0f;
            currentLevel_ += (kOvershootTarget - currentLevel_) * coeff;

            if (currentLevel_ >= 1.0f)
            {
                currentLevel_ = 1.0f;
                stage_ = EnvelopeStage::Decay;
            }
            break;
        }

        case EnvelopeStage::Decay:
        {
            // Exponential discharge towards sustain level
            float diff = currentLevel_ - sustainParam_;
            currentLevel_ = sustainParam_ + diff * static_cast<float>(decayMult_);

            if (std::abs(currentLevel_ - sustainParam_) < 0.0001f)
            {
                currentLevel_ = sustainParam_;
                stage_ = EnvelopeStage::Sustain;
            }
            break;
        }

        case EnvelopeStage::Sustain:
        {
            currentLevel_ = sustainParam_;
            break;
        }

        case EnvelopeStage::Release:
        {
            // Exponential discharge towards zero
            currentLevel_ *= static_cast<float>(releaseMult_);

            if (currentLevel_ < 0.00001f)
            {
                currentLevel_ = 0.0f;
                stage_ = EnvelopeStage::Idle;
            }
            break;
        }

        case EnvelopeStage::Idle:
        default:
            currentLevel_ = 0.0f;
            break;
    }

    return currentLevel_ * velocityGain_;
}

} // namespace abd::synth
