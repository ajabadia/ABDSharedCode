/*
  ==============================================================================

    DspAnalogTapeDelay.h
    Retardo de cinta analógica vintage con saturación de cabezal magnético y modulación de arrastre.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspAnalogTapeDelay.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Retardo analógico cálido de cinta de bobina abierta (DeepMind 12 FX Type 40).
      Inspirado en ecos de cinta de estudio como el Roland RE-201 y Watkins Copicat:
        - Tiempo de retardo variable de 50 ms a 1200 ms.
        - Arrastre mecánico (Wobble): micro-variaciones aleatorias continuas de velocidad y tono.
        - Saturación de cabezal magnético no lineal parametrizable por Drive.
        - Filtro Tone paso-bajos cálido en el bucle de realimentación (800 Hz a 18 kHz).
        - Generador de piso de ruido analógico de arrastre sutil y determinista.

    RENDIMIENTO:
      - Búfer circular estático de tamaño potencia de dos (65536 muestras) con bitmasking rápido.
      - 100% Real-Time Safe: CERO asignaciones en memoria heap en tiempo de ejecución.
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

class DspAnalogTapeDelay
{
public:
    static constexpr int kMaxDelaySamples = 65536; // >1.3s a 48kHz, potencia de 2
    static constexpr int kDelayMask       = kMaxDelaySamples - 1;

    DspAnalogTapeDelay() noexcept
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
        writePos_    = 0;
        lpStateL_    = 0.0f;
        lpStateR_    = 0.0f;
        noiseSeed_   = 0xABCD1234u;
        wobblePhase_ = 0.0f;
    }

    void setMixNorm(float norm) noexcept { paramMix_ = std::clamp(norm, 0.0f, 1.0f); }
    void setTimeNorm(float norm) noexcept { paramTime_ = std::clamp(norm, 0.0f, 1.0f); }
    void setFeedbackNorm(float norm) noexcept { paramFeedback_ = std::clamp(norm, 0.0f, 1.0f); }
    void setWobbleNorm(float norm) noexcept { paramWobble_ = std::clamp(norm, 0.0f, 1.0f); }
    void setSaturationNorm(float norm) noexcept { paramSaturation_ = std::clamp(norm, 0.0f, 1.0f); }
    void setToneNorm(float norm) noexcept { paramTone_ = std::clamp(norm, 0.0f, 1.0f); }

    void setParameter(int index, float value) noexcept
    {
        const float val = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0:
                setMixNorm(val);
                break;
            case 1:
                setTimeNorm(val);
                break;
            case 2:
                setFeedbackNorm(val);
                break;
            case 3:
                setWobbleNorm(val);
                break;
            case 4:
                setSaturationNorm(val);
                break;
            case 5:
                setToneNorm(val);
                break;
            default:
                break;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        // 50ms - 1200ms
        const float baseDelayMs = 50.0f + paramTime_ * 1150.0f;
        const float baseDelay   = static_cast<float>(sampleRate_ * 0.001f * baseDelayMs);
        const float fb          = paramFeedback_ * 0.95f;
        const float satDrive    = 1.0f + paramSaturation_ * 4.0f;

        // Tone LP coeff (800Hz - 18000Hz)
        const float cutoff  = 800.0f + paramTone_ * 17200.0f;
        const float lpAlpha = 1.0f - static_cast<float>(std::exp(-6.28318530717958647692 * cutoff / sampleRate_));

        const float wobbleRate  = 0.5f / static_cast<float>(sampleRate_); // 0.5 Hz
        const float wobbleDepth = paramWobble_ * 48.0f;                   // max 48 samples mod

        constexpr double kTwoPi = 6.28318530717958647692;

        for (int i = 0; i < numSamples; ++i)
        {
            // Wow/flutter
            wobblePhase_ += wobbleRate;
            if (wobblePhase_ >= 1.0f) wobblePhase_ -= 1.0f;

            const float lfo       = static_cast<float>(std::sin(kTwoPi * wobblePhase_));
            const float jitter    = (noiseGenerate() - 0.5f) * paramWobble_ * 4.0f;
            const float delayTime = std::clamp(baseDelay + lfo * wobbleDepth + jitter, 1.0f, static_cast<float>(kMaxDelaySamples - 4));

            // Lectura interpolada lineal L
            const float rPosL = static_cast<float>(writePos_) - delayTime;
            const int iPosL   = static_cast<int>(rPosL);
            const float fracL = rPosL - static_cast<float>(iPosL);
            const int idx0L   = iPosL & kDelayMask;
            const int idx1L   = (iPosL + 1) & kDelayMask;
            const float delL  = delayBufL_[idx0L] + fracL * (delayBufL_[idx1L] - delayBufL_[idx0L]);

            // Lectura interpolada lineal R
            const float rPosR = static_cast<float>(writePos_) - (delayTime * 1.002f);
            const int iPosR   = static_cast<int>(rPosR);
            const float fracR = rPosR - static_cast<float>(iPosR);
            const int idx0R   = iPosR & kDelayMask;
            const int idx1R   = (iPosR + 1) & kDelayMask;
            const float delR  = delayBufR_[idx0R] + fracR * (delayBufR_[idx1R] - delayBufR_[idx0R]);

            // Filtrado de tono
            lpStateL_ += lpAlpha * (delL - lpStateL_);
            lpStateR_ += lpAlpha * (delR - lpStateR_);

            // Saturación y escritura
            const float toWriteL = tapeSat(inL[i] + lpStateL_ * fb, satDrive);
            const float toWriteR = tapeSat(inR[i] + lpStateR_ * fb, satDrive);

            delayBufL_[writePos_] = toWriteL;
            delayBufR_[writePos_] = toWriteR;
            writePos_             = (writePos_ + 1) & kDelayMask;

            // Dry/wet
            outL[i] = inL[i] * (1.0f - paramMix_) + delL * paramMix_;
            outR[i] = inR[i] * (1.0f - paramMix_) + delR * paramMix_;
        }
    }

private:
    static float tapeSat(float x, float drive) noexcept
    {
        const float xd = x * drive;
        return std::tanh(xd) / std::sqrt(drive);
    }

    float noiseGenerate() noexcept
    {
        noiseSeed_ = noiseSeed_ * 1664525u + 1013904223u;
        return static_cast<float>(noiseSeed_ >> 9) * (1.0f / 8388608.0f);
    }

    double sampleRate_     = 44100.0;
    float paramMix_        = 0.35f;
    float paramTime_       = 0.35f;
    float paramFeedback_   = 0.4f;
    float paramWobble_     = 0.3f;
    float paramSaturation_ = 0.3f;
    float paramTone_       = 0.5f;

    std::array<float, kMaxDelaySamples> delayBufL_{};
    std::array<float, kMaxDelaySamples> delayBufR_{};
    int writePos_       = 0;
    float lpStateL_     = 0.0f;
    float lpStateR_     = 0.0f;
    float wobblePhase_  = 0.0f;
    uint32_t noiseSeed_ = 0xABCD1234u;
};

} // namespace abd::dsp
