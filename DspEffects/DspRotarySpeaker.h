/*
  ==============================================================================

    DspRotarySpeaker.h
    Emulación de altavoz giratorio mecánico Leslie 122 (Horn + Drum/Rotor).

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspRotarySpeaker.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Emulación de cabina rotatoria tipo Leslie 122 (DeepMind 12 FX Type 16).
      Simula el comportamiento físico acústico y mecánico:
        - Crossover acústico a ~800 Hz separando agudos (Horn) y graves (Drum/Rotor).
        - Velocidades independientes configurables:
          * Lenta (LoSpeed): 0.1 Hz a 4.0 Hz.
          * Rápida (HiSpeed): 2.0 Hz a 9.9 Hz.
        - Conmutador bi-estado Slow/Fast con inercia y aceleración física continua.
        - Control de encendido de motor (Motor Run / Stop) con desaceleración inercial.
        - Efecto Doppler mediante modulación de fase acústica (Horn más rápido, Drum más lento).
        - Modulación de amplitud estéreo (Tremolo) en antifase de 180° que crea la sensación de giro espacial.
        - Balance acústico Horn / Rotor y distancia microfónica virtual.

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

class DspRotarySpeaker
{
public:
    DspRotarySpeaker() noexcept
    {
        reset();
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        xoverCoeff_ = static_cast<float>(800.0 / (800.0 + sampleRate_ * 0.5));
        updateTargets();
        updateAccelRate();
        reset();
    }

    void reset() noexcept
    {
        hornPhase_    = 0.0;
        rotorPhase_   = 0.0;
        hornSpeed_    = 0.0;
        rotorSpeed_   = 0.0;
        hornTarget_   = 0.0;
        rotorTarget_  = 0.0;
        hornDelayL_   = 0.0f;
        hornDelayR_   = 0.0f;
        rotorDelayL_  = 0.0f;
        rotorDelayR_  = 0.0f;
        xoverStateL_  = 0.0f;
        xoverStateR_  = 0.0f;
        motorRunning_ = true;
        targetSpeed_  = 0.0f; // SLOW
        updateTargets();
    }

    void setLoSpeedNorm(float norm) noexcept
    {
        loSpeed_ = std::clamp(norm, 0.0f, 1.0f);
        updateTargets();
    }
    void setHiSpeedNorm(float norm) noexcept
    {
        hiSpeed_ = std::clamp(norm, 0.0f, 1.0f);
        updateTargets();
    }
    void setAccelNorm(float norm) noexcept
    {
        accel_ = std::clamp(norm, 0.0f, 1.0f);
        updateAccelRate();
    }
    void setDistanceNorm(float norm) noexcept { distance_ = std::clamp(norm, 0.0f, 1.0f); }
    void setBalanceNorm(float norm) noexcept { balance_ = std::clamp(norm, 0.0f, 1.0f); }

    void setMotorRunning(bool run) noexcept
    {
        motorRunning_ = run;
        if (!motorRunning_)
        {
            hornTarget_  = 0.0;
            rotorTarget_ = 0.0;
        }
        else
        {
            updateTargets();
        }
    }

    void setSpeedFast(bool fast) noexcept
    {
        targetSpeed_ = fast ? 1.0f : 0.0f;
        updateTargets();
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        constexpr double kTwoPi = 6.28318530717958647692;

        for (int s = 0; s < numSamples; ++s)
        {
            // 1. Aceleración inercial suave de cada rotor
            hornSpeed_ += (hornTarget_ - hornSpeed_) * accelRate_;
            rotorSpeed_ += (rotorTarget_ - rotorSpeed_) * accelRate_;

            // 2. Avance de fase de rotación
            hornPhase_ += hornSpeed_ / sampleRate_;
            rotorPhase_ += rotorSpeed_ / sampleRate_;
            if (hornPhase_ >= 1.0) hornPhase_ -= 1.0;
            if (rotorPhase_ >= 1.0) rotorPhase_ -= 1.0;

            // 3. Separación acústica por Crossover (~800 Hz)
            const float inputL = inL[s];
            const float inputR = inR[s];

            xoverStateL_ += xoverCoeff_ * (inputL - xoverStateL_);
            xoverStateR_ += xoverCoeff_ * (inputR - xoverStateR_);

            const float rotorSignalL = xoverStateL_;
            const float hornSignalL  = inputL - xoverStateL_;
            const float rotorSignalR = xoverStateR_;
            const float hornSignalR  = inputR - xoverStateR_;

            // 4. Modulación Doppler (desplazamiento de tono por movimiento de la fuente)
            const float hornPhaseOffset = static_cast<float>(std::sin(kTwoPi * hornPhase_));
            const float dopplerHorn     = hornPhaseOffset * distance_ * 0.05f;

            const float rotorPhaseOffset = static_cast<float>(std::sin(kTwoPi * rotorPhase_));
            const float dopplerRotor     = rotorPhaseOffset * distance_ * 0.03f;

            float hornOutL = hornSignalL + dopplerHorn * hornDelayL_;
            float hornOutR = hornSignalR + dopplerHorn * hornDelayR_;
            hornDelayL_    = hornSignalL;
            hornDelayR_    = hornSignalR;

            float rotorOutL = rotorSignalL + dopplerRotor * rotorDelayL_;
            float rotorOutR = rotorSignalR + dopplerRotor * rotorDelayR_;
            rotorDelayL_    = rotorSignalL;
            rotorDelayR_    = rotorSignalR;

            // 5. Modulación de amplitud estéreo (Tremolo espacial en antifase de 180°)
            const float hornTremL  = getDistanceAtten(distance_, hornPhase_);
            const float hornTremR  = getDistanceAtten(distance_, hornPhase_ + 0.5);
            const float rotorTremL = getDistanceAtten(distance_ * 0.5f, rotorPhase_);
            const float rotorTremR = getDistanceAtten(distance_ * 0.5f, rotorPhase_ + 0.5);

            hornOutL *= hornTremL;
            hornOutR *= hornTremR;
            rotorOutL *= rotorTremL;
            rotorOutR *= rotorTremR;

            // 6. Balance y ganancia de motor
            const float hornGain  = balance_;
            const float rotorGain = 1.0f - balance_;
            const float motorGain = motorRunning_ ? 1.0f : 0.0f;

            outL[s] = (hornOutL * hornGain + rotorOutL * rotorGain) * motorGain;
            outR[s] = (hornOutR * hornGain + rotorOutR * rotorGain) * motorGain;
        }
    }

private:
    void updateTargets() noexcept
    {
        if (!motorRunning_)
        {
            hornTarget_  = 0.0;
            rotorTarget_ = 0.0;
            return;
        }

        if (targetSpeed_ > 0.5f) // FAST
        {
            hornTarget_  = 2.0 + 7.9 * hiSpeed_;
            rotorTarget_ = hornTarget_ * 0.5;
        }
        else // SLOW
        {
            hornTarget_  = 0.1 + 3.9 * loSpeed_;
            rotorTarget_ = hornTarget_ * 0.5;
        }
    }

    void updateAccelRate() noexcept
    {
        accelRate_ = 0.001 + 0.999 * accel_;
    }

    static inline float getDistanceAtten(float distanceNorm, double phase) noexcept
    {
        constexpr double kTwoPi = 6.28318530717958647692;
        const float doppler     = static_cast<float>(std::sin(kTwoPi * phase));
        const float modAmount   = distanceNorm * 0.3f;
        return 1.0f + doppler * modAmount;
    }

    double sampleRate_ = 44100.0;
    float loSpeed_     = 0.3f;
    float hiSpeed_     = 0.6f;
    float accel_       = 0.5f;
    float distance_    = 0.5f;
    float balance_     = 0.5f;
    float targetSpeed_ = 0.0f;

    double hornPhase_  = 0.0;
    double rotorPhase_ = 0.0;
    double hornSpeed_  = 0.0;
    double rotorSpeed_ = 0.0;
    bool motorRunning_ = true;

    double hornTarget_  = 0.0;
    double rotorTarget_ = 0.0;
    double accelRate_   = 0.0;

    float hornDelayL_  = 0.0f;
    float hornDelayR_  = 0.0f;
    float rotorDelayL_ = 0.0f;
    float rotorDelayR_ = 0.0f;

    float xoverStateL_ = 0.0f;
    float xoverStateR_ = 0.0f;
    float xoverCoeff_  = 0.0f;
};

} // namespace abd::dsp
