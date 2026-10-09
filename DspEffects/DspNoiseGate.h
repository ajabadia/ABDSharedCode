/*
  ==============================================================================

    DspNoiseGate.h
    Puerta de ruido estéreo con control de transitorios (Punch) y modo Ducker.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspNoiseGate.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Emulación de puerta de ruido y procesador dinámico tipo Drawmer DS201 /
      Klark Teknik DN6000 (DeepMind 12 FX Type 33).
      Incluye:
        1. Tres modos de operación:
           - Gate clásico (0): atenúa la señal por debajo del umbral hasta el valor Range.
           - Transient Enhancer (1): refuerza los picos de ataque por encima de la envolvente (Punch).
           - Ducker (2): atenúa la señal cuando supera el umbral (ducking/voice-over invertido).
        2. Tiempos dinámicos independientes: Attack (0.5 a 20 ms), Release (2 a 2000 ms),
           y tiempo de retención Hold (2 a 2000 ms).
        3. Umbral de -50 dB a 0 dB y atenuación de suelo Range de -100 dB a 0 dB.
        4. Suavizado exponencial de ganancias para evitar chasquidos de apertura/cierre.

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

class DspNoiseGate
{
public:
    enum Mode
    {
        kGate      = 0,
        kTransient = 1,
        kDucker    = 2
    };

    DspNoiseGate() noexcept
    {
        reset();
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        updateTimeCoeffs();
        reset();
    }

    void reset() noexcept
    {
        envL_ = envR_ = 0.0f;
        gainL_ = gainR_ = 1.0f;
        holdTimerL_ = holdTimerR_ = 0.0f;
    }

    void setThresholdNorm(float norm) noexcept
    {
        threshold_ = std::clamp(norm, 0.0f, 1.0f);
    }

    void setRangeNorm(float norm) noexcept
    {
        range_ = std::clamp(norm, 0.0f, 1.0f);
    }

    void setAttackNorm(float norm) noexcept
    {
        const float v = std::clamp(norm, 0.0f, 1.0f);
        attackMs_     = 0.5f + 19.5f * v;
        updateTimeCoeffs();
    }

    void setReleaseNorm(float norm) noexcept
    {
        const float v = std::clamp(norm, 0.0f, 1.0f);
        releaseMs_    = 2.0f + 1998.0f * v;
        updateTimeCoeffs();
    }

    void setHoldNorm(float norm) noexcept
    {
        const float v = std::clamp(norm, 0.0f, 1.0f);
        holdMs_       = 2.0f + 1998.0f * v;
        updateTimeCoeffs();
    }

    void setPunchNorm(float norm) noexcept
    {
        punch_ = std::clamp(norm, 0.0f, 1.0f);
    }

    void setMode(int modeIdx) noexcept
    {
        gateMode_ = std::clamp(modeIdx, 0, 2);
    }

    void setPower(bool on) noexcept
    {
        power_ = on;
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        if (!power_)
        {
            for (int s = 0; s < numSamples; ++s)
            {
                outL[s] = inL[s];
                outR[s] = inR[s];
            }
            return;
        }

        const float threshLin = std::pow(10.0f, (threshold_ * -50.0f) / 20.0f);
        const float rangeLin  = std::pow(10.0f, (range_ * -100.0f) / 20.0f);
        const float punchGain = std::pow(10.0f, (punch_ * 12.0f - 6.0f) / 20.0f);

        for (int s = 0; s < numSamples; ++s)
        {
            const float l    = inL[s];
            const float r    = inR[s];
            const float absL = std::abs(l);
            const float absR = std::abs(r);

            // 1. Seguidores de envolvente
            envL_ = (absL > envL_) ? envL_ + atkCoeff_ * (absL - envL_)
                                   : envL_ + relCoeff_ * (absL - envL_);
            envR_ = (absR > envR_) ? envR_ + atkCoeff_ * (absR - envR_)
                                   : envR_ + relCoeff_ * (absR - envR_);

            // 2. Reducción de ganancia por canal
            processChannelGain(envL_, gainL_, holdTimerL_, threshLin, rangeLin);
            processChannelGain(envR_, gainR_, holdTimerR_, threshLin, rangeLin);

            float outL_s = l * gainL_;
            float outR_s = r * gainR_;

            // 3. Modos especiales
            if (gateMode_ == kTransient)
            {
                const float transientL = std::max(0.0f, absL - envL_) * punchGain;
                const float transientR = std::max(0.0f, absR - envR_) * punchGain;
                outL_s += (l > 0 ? transientL : -transientL) * 0.5f;
                outR_s += (r > 0 ? transientR : -transientR) * 0.5f;
            }
            else if (gateMode_ == kDucker)
            {
                const float duckGainL = 1.0f - gainL_ * (1.0f - rangeLin);
                const float duckGainR = 1.0f - gainR_ * (1.0f - rangeLin);
                outL_s                = l * duckGainL;
                outR_s                = r * duckGainR;
            }

            outL[s] = outL_s;
            outR[s] = outR_s;
        }
    }

private:
    void updateTimeCoeffs() noexcept
    {
        atkCoeff_    = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * attackMs_ / 1000.0f));
        relCoeff_    = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * releaseMs_ / 1000.0f));
        holdSamples_ = static_cast<float>(sampleRate_ * holdMs_ / 1000.0f);
    }

    inline void processChannelGain(float env, float& gain, float& timer,
                                   float threshLin, float rangeLin) noexcept
    {
        if (env > threshLin)
        {
            // Puerta abierta: suavizado hacia 1.0
            const float atkSmooth = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * attackMs_ / 1000.0f));
            gain += atkSmooth * (1.0f - gain);
            timer = holdSamples_;
        }
        else
        {
            if (timer > 0.0f)
            {
                timer -= 1.0f; // Tiempo de retención Hold
            }
            else
            {
                // Puerta cerrando: suavizado hacia rangeLin
                const float relSmooth = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * releaseMs_ / 1000.0f));
                gain += relSmooth * (rangeLin - gain);
            }
        }
    }

    double sampleRate_ = 44100.0;
    float threshold_   = 0.5f;
    float range_       = 0.5f;
    float attackMs_    = 1.0f;
    float releaseMs_   = 100.0f;
    float holdMs_      = 50.0f;
    float punch_       = 0.5f;
    int gateMode_      = kGate;
    bool power_        = true;

    float envL_ = 0.0f, envR_ = 0.0f;
    float gainL_ = 1.0f, gainR_ = 1.0f;
    float atkCoeff_ = 0.01f, relCoeff_ = 0.001f;
    float holdTimerL_ = 0.0f, holdTimerR_ = 0.0f;
    float holdSamples_ = 0.0f;
};

} // namespace abd::dsp
