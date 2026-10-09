/*
  ==============================================================================

    DspPhaser.h
    Efecto Phaser analógico multietapa (2 a 12 etapas) con seguidor de envolvente y LFO estéreo.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspPhaser.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Emulación del phaser analógico clásico (Electro-Harmonix Small Stone / MXR Phase 90 /
      Mu-Tron Phasor II) con cascada de filtros todo-paso (allpass) de primer orden,
      realimentación resonante, modulador LFO estéreo con deformación de simetría y
      seguidor de envolvente dinámico acoplado a la profundidad de barrido.

    PARÁMETROS:
      - Etapas seleccionables: 2, 4, 6, 8, 10, 12 polos (muescas espectrales).
      - Rango de frecuencia base: 20 Hz a 15000 Hz (escala exponencial).
      - Rango de modulación por LFO: 0 a 6000 Hz.
      - Tasa de LFO: 0.05 Hz a 5.0 Hz.
      - Desfase estéreo de LFO: 0° a 180°.
      - Control de simetría de onda: -50 a +50 (deforma senoidal hacia rampa/sierra).
      - Seguidor de envolvente: modulación bipolar de profundidad (-100% a +100%)
        con constantes de tiempo configurables (ataque y relajación de 10ms a 1000ms).

    RENDIMIENTO:
      - Cero asignaciones en memoria dinámica (100% Real-Time Safe).
      - CERO dependencias de JUCE (C++20 estándar puro).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "DspCore/DspEnvelopeFollower.h"
#include <algorithm>
#include <cmath>

namespace abd::dsp
{

class DspPhaser
{
public:
    static constexpr int kMaxStages = 12;

    DspPhaser() = default;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        envFollowerL_.prepare(sampleRate_);
        envFollowerR_.prepare(sampleRate_);
        updateLFOIncrement();
        updateFreqRange();
        updateEnvTimes();
        reset();
    }

    void reset() noexcept
    {
        std::fill(std::begin(stateL_), std::end(stateL_), 0.0f);
        std::fill(std::begin(stateR_), std::end(stateR_), 0.0f);
        std::fill(std::begin(delayL_), std::end(delayL_), 0.0f);
        std::fill(std::begin(delayR_), std::end(delayR_), 0.0f);
        fbStateL_ = 0.0f;
        fbStateR_ = 0.0f;
        lfoPhaseL_ = 0.0;
        lfoPhaseR_ = 0.0;
        envFollowerL_.reset();
        envFollowerR_.reset();
    }

    void setRateNorm(float rateNorm) noexcept
    {
        rateNorm_ = std::clamp(rateNorm, 0.0f, 1.0f);
        updateLFOIncrement();
    }

    void setDepthNorm(float depthNorm) noexcept
    {
        depthNorm_ = std::clamp(depthNorm, 0.0f, 1.0f);
        updateFreqRange();
    }

    void setFeedbackNorm(float resoNorm) noexcept
    {
        feedback_ = std::clamp(resoNorm, 0.0f, 1.0f) * 0.70f;
    }

    void setBaseFreqNorm(float baseNorm) noexcept
    {
        baseNorm_ = std::clamp(baseNorm, 0.0f, 1.0f);
        updateFreqRange();
    }

    void setStageCount(int stages) noexcept
    {
        stages_ = std::clamp(stages, 2, kMaxStages);
    }

    int getStageCount() const noexcept { return stages_; }

    void setWaveSymmetry(float waveNorm) noexcept
    {
        waveNorm_ = std::clamp(waveNorm, 0.0f, 1.0f);
    }

    void setStereoPhaseNorm(float phaseNorm) noexcept
    {
        stereoPhaseNorm_ = std::clamp(phaseNorm, 0.0f, 1.0f);
    }

    void setEnvModNorm(float envModNorm) noexcept
    {
        envMod_ = (std::clamp(envModNorm, 0.0f, 1.0f) - 0.5f) * 2.0f; // -1..+1
    }

    void setAttackNorm(float attackNorm) noexcept
    {
        attackNorm_ = std::clamp(attackNorm, 0.0f, 1.0f);
        updateEnvTimes();
    }

    void setReleaseNorm(float releaseNorm) noexcept
    {
        releaseNorm_ = std::clamp(releaseNorm, 0.0f, 1.0f);
        updateEnvTimes();
    }

    /**
     * Procesa un bloque de audio estéreo y devuelve la señal húmeda al 100%.
     */
    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        for (int s = 0; s < numSamples; ++s)
        {
            // 1. Seguidor de envolvente (modula profundidad efectiva)
            const float envL = envFollowerL_.process(inL[s]);
            const float envR = envFollowerR_.process(inR[s]);
            const float env  = (envL + envR) * 0.5f;

            float effectiveDepth = depthNorm_ * (1.0f + envMod_ * env);
            effectiveDepth = std::clamp(effectiveDepth, 0.0f, 1.0f);

            // 2. Avance de osciladores LFO
            lfoPhaseL_ += lfoInc_;
            if (lfoPhaseL_ >= 1.0) lfoPhaseL_ -= 1.0;

            const double phaseOffset = static_cast<double>(stereoPhaseNorm_ * 0.5f);
            lfoPhaseR_ = lfoPhaseL_ + phaseOffset;
            if (lfoPhaseR_ >= 1.0) lfoPhaseR_ -= 1.0;

            // 3. Frecuencia de corte modulada
            const float lfoValL = computeLFOWave(lfoPhaseL_);
            const float lfoValR = computeLFOWave(lfoPhaseR_);

            const float effModRange = 6000.0f * effectiveDepth;
            const float freqModL    = baseHz_ + (lfoValL * 0.5f + 0.5f) * effModRange;
            const float freqModR    = baseHz_ + (lfoValR * 0.5f + 0.5f) * effModRange;

            const float coeffL = calcAllpassCoeff(freqModL);
            const float coeffR = calcAllpassCoeff(freqModR);

            // 4. Entrada con realimentación resonante
            float sigL = inL[s] + fbStateL_ * feedback_;
            float sigR = inR[s] + fbStateR_ * feedback_;

            // 5. Cascada de filtros todo-paso
            for (int i = 0; i < stages_; ++i)
            {
                const float newStL = -coeffL * sigL + delayL_[i] + coeffL * stateL_[i];
                const float newStR = -coeffR * sigR + delayR_[i] + coeffR * stateR_[i];

                delayL_[i] = sigL;
                delayR_[i] = sigR;
                stateL_[i] = newStL;
                stateR_[i] = newStR;

                sigL = newStL;
                sigR = newStR;
            }

            fbStateL_ = sigL;
            fbStateR_ = sigR;

            outL[s] = sigL;
            outR[s] = sigR;
        }
    }

private:
    void updateLFOIncrement() noexcept
    {
        const float freqHz = 0.05f + 4.95f * rateNorm_;
        lfoInc_ = freqHz / sampleRate_;
    }

    void updateFreqRange() noexcept
    {
        baseHz_ = 20.0f * std::pow(750.0f, baseNorm_);
    }

    void updateEnvTimes() noexcept
    {
        const float atkS = 0.010f + 0.990f * attackNorm_;
        const float relS = 0.010f + 0.990f * releaseNorm_;
        envFollowerL_.setTimes(atkS, relS);
        envFollowerR_.setTimes(atkS, relS);
    }

    float calcAllpassCoeff(float cutoffHz) const noexcept
    {
        if (cutoffHz <= 0.0f) return 0.0f;
        const float nyq = static_cast<float>(sampleRate_) * 0.49f;
        const float f = std::clamp(cutoffHz, 10.0f, nyq);
        const float wd = std::tan(3.14159265358979323846f * f / static_cast<float>(sampleRate_));
        const float clampedWd = std::clamp(wd, 0.001f, 100.0f);
        return (1.0f - clampedWd) / (1.0f + clampedWd);
    }

    float computeLFOWave(double phase) const noexcept
    {
        float lfo = static_cast<float>(std::sin(6.283185307179586 * phase));
        const float w = (waveNorm_ - 0.5f) * 2.0f;

        if (std::abs(w) > 0.01f)
        {
            double skewed = phase + static_cast<double>(w) * 0.3 * std::sin(6.283185307179586 * phase);
            if (skewed > 1.0) skewed -= 1.0;
            if (skewed < 0.0) skewed += 1.0;
            lfo = static_cast<float>(std::sin(6.283185307179586 * skewed));
        }
        return lfo;
    }

    double sampleRate_ { 44100.0 };

    float rateNorm_        { 0.2f };
    float depthNorm_       { 0.5f };
    float feedback_        { 0.21f }; // 30% * 0.70
    float baseNorm_        { 0.3f };
    int   stages_          { 6 };
    float waveNorm_        { 0.5f };
    float stereoPhaseNorm_ { 0.0f };
    float envMod_          { 0.0f };
    float attackNorm_      { 0.02f };
    float releaseNorm_     { 0.02f };

    float baseHz_          { 200.0f };
    double lfoPhaseL_      { 0.0 };
    double lfoPhaseR_      { 0.0 };
    double lfoInc_         { 0.0 };

    float stateL_[kMaxStages] {};
    float stateR_[kMaxStages] {};
    float delayL_[kMaxStages] {};
    float delayR_[kMaxStages] {};
    float fbStateL_        { 0.0f };
    float fbStateR_        { 0.0f };

    EnvelopeFollower envFollowerL_;
    EnvelopeFollower envFollowerR_;
};

} // namespace abd::dsp
