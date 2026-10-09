/*
  ==============================================================================

    DspAutoPan.h
    Efecto Auto Pan / Tremolo con LFO multimórfico y modulación dinámica por envolvente.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspAutoPan.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Efecto de Auto Pan y Tremolo modulado (DeepMind 12 FX Type 20).
      Modula la posición espacial panorámica con curva de potencia constante (Equal Power)
      o modulación rítmica de amplitud.
      Características:
        - Tasa de LFO: 0.05 Hz a 5.0 Hz.
        - Desfase estéreo entre canales: 0° a 180°.
        - Control continuo de forma de onda (Triangular -> Sinusoidal -> Cuadrada suave).
        - Seguidor de envolvente (Envelope Follower) interactivo:
          * La amplitud de la señal entrante puede acelerar o desacelerar el LFO (EnvSpd).
          * La amplitud puede modular la profundidad de efecto (EnvDepth).
          * Parámetros de Attack y Release configurables para balística del seguidor.

    RENDIMIENTO:
      - Cero asignaciones en memoria dinámica (100% Real-Time Safe).
      - CERO dependencias de JUCE (C++20 estándar puro).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include <algorithm>
#include <cmath>

namespace abd::dsp
{

class DspAutoPan
{
public:
    DspAutoPan() noexcept
    {
        reset();
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        updateLFOIncrement();
        updateEnvCoeffs();
        reset();
    }

    void reset() noexcept
    {
        lfoPhaseL_ = 0.0;
        lfoPhaseR_ = 0.0;
        envStateL_ = 0.0f;
        envStateR_ = 0.0f;
    }

    void setSpeedNorm(float norm) noexcept
    {
        speed_ = std::clamp(norm, 0.0f, 1.0f);
        updateLFOIncrement();
    }
    void setPhaseNorm(float norm) noexcept { phase_ = std::clamp(norm, 0.0f, 1.0f); }
    void setWaveNorm(float norm) noexcept { wave_ = std::clamp(norm, 0.0f, 1.0f); }
    void setDepthNorm(float norm) noexcept { depth_ = std::clamp(norm, 0.0f, 1.0f); }
    void setEnvSpdNorm(float norm) noexcept { envSpd_ = std::clamp(norm, 0.0f, 1.0f); }
    void setEnvDepthNorm(float norm) noexcept { envDepth_ = std::clamp(norm, 0.0f, 1.0f); }
    void setAttackNorm(float norm) noexcept
    {
        attackParam_ = std::clamp(norm, 0.0f, 1.0f);
        updateEnvCoeffs();
    }
    void setReleaseNorm(float norm) noexcept
    {
        releaseParam_ = std::clamp(norm, 0.0f, 1.0f);
        updateEnvCoeffs();
    }
    void setHoldNorm(float norm) noexcept { hold_ = std::clamp(norm, 0.0f, 1.0f); }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        for (int s = 0; s < numSamples; ++s)
        {
            // 1. Seguidor de envolvente por canal
            const float absL = std::abs(inL[s]);
            const float absR = std::abs(inR[s]);

            if (absL > envStateL_)
                envStateL_ += envAttack_ * (absL - envStateL_);
            else
                envStateL_ += envRelease_ * (absL - envStateL_);

            if (absR > envStateR_)
                envStateR_ += envAttack_ * (absR - envStateR_);
            else
                envStateR_ += envRelease_ * (absR - envStateR_);

            const float envL = std::clamp(envStateL_ * 4.0f, 0.0f, 1.0f);
            const float envR = std::clamp(envStateR_ * 4.0f, 0.0f, 1.0f);

            // 2. Modulación de velocidad LFO por envolvente
            const float effectiveIncL = lfoInc_ * (1.0f + envSpd_ * envL);
            const float effectiveIncR = lfoInc_ * (1.0f + envSpd_ * envR);

            lfoPhaseL_ += effectiveIncL;
            if (lfoPhaseL_ >= 1.0) lfoPhaseL_ -= 1.0;

            const float phaseOffset = phase_ * 0.5f;
            lfoPhaseR_              = lfoPhaseL_ + phaseOffset;
            if (lfoPhaseR_ >= 1.0) lfoPhaseR_ -= 1.0;

            lfoPhaseR_ += (effectiveIncR - effectiveIncL);
            if (lfoPhaseR_ >= 1.0) lfoPhaseR_ -= 1.0;
            if (lfoPhaseR_ < 0.0) lfoPhaseR_ += 1.0;

            // 3. Forma de onda del LFO
            const float lfoL = getLFOWave(lfoPhaseL_, wave_);
            const float lfoR = getLFOWave(lfoPhaseR_, wave_);

            // 4. Modulación de profundidad por envolvente
            float effectiveDepth = depth_ * (1.0f + envDepth_ * (envL - 0.5f));
            effectiveDepth       = std::clamp(effectiveDepth, 0.0f, 1.0f);

            if (effectiveDepth < 0.01f)
            {
                outL[s] = inL[s];
                outR[s] = inR[s];
            }
            else
            {
                // Auto Pan: equal power pan law
                const float panPos          = lfoL * effectiveDepth;
                constexpr double kPiQuarter = 0.78539816339744830962;
                const float gainL           = static_cast<float>(std::cos(kPiQuarter * (panPos + 1.0f)));
                const float gainR           = static_cast<float>(std::sin(kPiQuarter * (panPos + 1.0f)));

                outL[s] = inL[s] * gainL;
                outR[s] = inR[s] * gainR;
            }
        }
    }

private:
    void updateLFOIncrement() noexcept
    {
        const float freqHz = 0.05f + 4.95f * speed_;
        lfoInc_            = freqHz / static_cast<float>(sampleRate_);
    }

    void updateEnvCoeffs() noexcept
    {
        envAttack_  = 0.005f + 0.5f * attackParam_;
        envRelease_ = 0.0005f + 0.5f * releaseParam_;
    }

    static float getLFOWave(double phase, float waveParam) noexcept
    {
        constexpr double kTwoPi = 6.28318530717958647692;
        const float sinVal      = static_cast<float>(std::sin(kTwoPi * phase));

        if (waveParam < 0.3f)
        {
            const float tri = static_cast<float>(4.0 * std::abs(phase - std::floor(phase + 0.5)) - 1.0);
            const float t   = waveParam / 0.3f;
            return tri * (1.0f - t) + sinVal * t;
        }
        else if (waveParam < 0.7f)
        {
            const float t      = (waveParam - 0.3f) / 0.4f;
            const double skew  = (phase - 0.5) * 0.3 * t;
            double skewedPhase = phase + skew;
            if (skewedPhase > 1.0) skewedPhase -= 1.0;
            if (skewedPhase < 0.0) skewedPhase += 1.0;
            return static_cast<float>(std::sin(kTwoPi * skewedPhase));
        }
        else
        {
            const float sq = (sinVal >= 0.0f) ? 1.0f : -1.0f;
            const float t  = (waveParam - 0.7f) / 0.3f;
            float out      = sinVal * (1.0f - t) + sq * t;
            if (t > 0.5f)
            {
                const float soft = static_cast<float>(std::tanh(out * 0.5f)) * 2.0f;
                out              = out * (1.0f - t) + soft * t;
            }
            return out;
        }
    }

    double sampleRate_  = 44100.0;
    float speed_        = 0.3f;
    float phase_        = 0.0f;
    float wave_         = 0.5f;
    float depth_        = 0.5f;
    float envSpd_       = 0.0f;
    float envDepth_     = 0.0f;
    float hold_         = 0.5f;
    float attackParam_  = 0.02f;
    float releaseParam_ = 0.002f;

    float envStateL_  = 0.0f;
    float envStateR_  = 0.0f;
    float envAttack_  = 0.01f;
    float envRelease_ = 0.001f;

    double lfoPhaseL_ = 0.0;
    double lfoPhaseR_ = 0.0;
    float lfoInc_     = 0.0f;
};

} // namespace abd::dsp
