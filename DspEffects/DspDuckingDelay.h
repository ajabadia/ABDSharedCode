/*
  ==============================================================================

    DspDuckingDelay.h
    Retardo dinámico inteligente con atenuación sidechain por envolvente (Ducking).

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspDuckingDelay.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Retardo digital dinámico con atenuación lateral (DeepMind 12 FX Type 44),
      inspirado en el legendario TC Electronic 2290 Dynamic Digital Delay:
        - Cuando la señal directa (dry) supera el umbral (Threshold), el volumen de las repeticiones
          se atenúa suavemente (Ducking), dejando espacio y articulación en la mezcla.
        - En los silencios o notas sostenidas, el retardo recupera su nivel nominal (Ratio).
        - Tiempo de retardo ajustable de 50 ms a 1500 ms.
        - Realimentación de hasta 95% con desfasaje estéreo micro-temporal.

    RENDIMIENTO:
      - Búfer circular estático de 65536 muestras (potencia de dos) con bitmasking directo.
      - 100% Real-Time Safe: CERO memoria dinámica en audio thread.
      - CERO dependencias de JUCE (C++20 estándar puro).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace abd::dsp
{

class DspDuckingDelay
{
public:
    static constexpr int kMaxDelaySamples = 65536;
    static constexpr int kDelayMask = kMaxDelaySamples - 1;

    DspDuckingDelay() noexcept
    {
        reset();
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        delayBufL_.fill(0.0f);
        delayBufR_.fill(0.0f);
        writePos_ = 0;
        envL_ = 0.0f;
        envR_ = 0.0f;
        duckGain_ = 1.0f;
    }

    void setMixNorm(float norm) noexcept       { paramMix_ = std::clamp(norm, 0.0f, 1.0f); }
    void setTimeNorm(float norm) noexcept      { paramTime_ = std::clamp(norm, 0.0f, 1.0f); }
    void setFeedbackNorm(float norm) noexcept  { paramFeedback_ = std::clamp(norm, 0.0f, 1.0f); }
    void setThresholdNorm(float norm) noexcept { paramThreshold_ = std::clamp(norm, 0.0f, 1.0f); }
    void setRatioNorm(float norm) noexcept     { paramRatio_ = std::clamp(norm, 0.0f, 1.0f); }

    void setParameter(int index, float value) noexcept
    {
        const float val = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0: setMixNorm(val); break;
            case 1: setTimeNorm(val); break;
            case 2: setFeedbackNorm(val); break;
            case 3: setThresholdNorm(val); break;
            case 4: setRatioNorm(val); break;
            default: break;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        // 50ms - 1500ms
        const float baseDelayMs = 50.0f + paramTime_ * 1450.0f;
        const float baseDelay = static_cast<float>(sampleRate_ * 0.001f * baseDelayMs);
        const float delayClamped = std::clamp(baseDelay, 1.0f, static_cast<float>(kMaxDelaySamples - 4));
        const float fb = paramFeedback_ * 0.95f;
        const float thresh = 0.01f + paramThreshold_ * 0.5f;

        constexpr float kEnvAttack = 0.001f;
        constexpr float kEnvRelease = 0.05f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float inAbsL = std::abs(inL[i]);
            const float inAbsR = std::abs(inR[i]);

            // Seguidor de envolvente
            envL_ += (inAbsL > envL_) ? kEnvAttack * (inAbsL - envL_) : kEnvRelease * (inAbsL - envL_);
            envR_ += (inAbsR > envR_) ? kEnvAttack * (inAbsR - envR_) : kEnvRelease * (inAbsR - envR_);
            const float envMax = std::max(envL_, envR_);

            // Cálculo de atenuación
            float targetDuck = 1.0f;
            if (envMax > thresh)
            {
                const float overshoot = envMax - thresh;
                targetDuck = std::clamp(1.0f - overshoot * paramRatio_ * 4.0f, 0.05f, 1.0f);
            }
            duckGain_ += 0.005f * (targetDuck - duckGain_);

            // Lectura interpolada L
            const float rPosL = static_cast<float>(writePos_) - delayClamped;
            const int iPosL = static_cast<int>(rPosL);
            const float fracL = rPosL - static_cast<float>(iPosL);
            const int idx0L = iPosL & kDelayMask;
            const int idx1L = (iPosL + 1) & kDelayMask;
            const float delL = delayBufL_[idx0L] + fracL * (delayBufL_[idx1L] - delayBufL_[idx0L]);

            // Lectura interpolada R
            const float rPosR = static_cast<float>(writePos_) - (delayClamped * 1.003f);
            const int iPosR = static_cast<int>(rPosR);
            const float fracR = rPosR - static_cast<float>(iPosR);
            const int idx0R = iPosR & kDelayMask;
            const int idx1R = (iPosR + 1) & kDelayMask;
            const float delR = delayBufR_[idx0R] + fracR * (delayBufR_[idx1R] - delayBufR_[idx0R]);

            // Escritura
            delayBufL_[writePos_] = inL[i] + delL * fb;
            delayBufR_[writePos_] = inR[i] + delR * fb;
            writePos_ = (writePos_ + 1) & kDelayMask;

            // Salida atenuada por ducking
            const float wetL = delL * duckGain_;
            const float wetR = delR * duckGain_;

            outL[i] = inL[i] * (1.0f - paramMix_) + wetL * paramMix_;
            outR[i] = inR[i] * (1.0f - paramMix_) + wetR * paramMix_;
        }
    }

private:
    double sampleRate_      = 44100.0;
    float paramMix_         = 0.35f;
    float paramTime_        = 0.35f;
    float paramFeedback_    = 0.45f;
    float paramThreshold_   = 0.5f;
    float paramRatio_       = 0.6f;

    std::array<float, kMaxDelaySamples> delayBufL_{};
    std::array<float, kMaxDelaySamples> delayBufR_{};
    int writePos_           = 0;

    float envL_             = 0.0f;
    float envR_             = 0.0f;
    float duckGain_         = 1.0f;
};

} // namespace abd::dsp
