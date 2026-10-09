/*
  ==============================================================================

    DspShimmerDelay.h
    Retardo shimmer etéreo con transposición granular de tono (+1 octava) y difusión espacial.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspShimmerDelay.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Retardo ambiental etéreo estilo Shimmer (DeepMind 12 FX Type 41).
      Inspirado en los efectos de producción de Brian Eno / Daniel Lanois y Eventide Space:
        - Tiempo de retardo amplio: 100 ms a 2000 ms.
        - Transpositor granular de tono (+1 Octava) insertado en el lazo de regeneración (Pitch Shift 1.0x a 2.0x),
          haciendo que cada repetición ascienda armónicamente hacia registros superiores celestiales.
        - Difusor espacial de reverberación atmosférica acoplado en la cadena de retorno.
        - Desfasaje estéreo cruzado para máxima amplitud tridimensional.

    RENDIMIENTO:
      - Búferes circulares de tamaño potencia de dos con bitmasking directo (sin modulo %).
      - 100% Real-Time Safe: Memoria fija sin asignaciones dinámicas en el lazo de audio.
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

class DspShimmerDelay
{
public:
    static constexpr int kMaxDelaySamples = 131072; // >2.7s a 48kHz, potencia de 2
    static constexpr int kDelayMask       = kMaxDelaySamples - 1;

    static constexpr int kGrainSize = 512;
    static constexpr int kGrainMask = kGrainSize - 1;

    static constexpr int kReverbSize = 32768;
    static constexpr int kReverbMask = kReverbSize - 1;

    DspShimmerDelay() noexcept
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
        grainBufL_.fill(0.0f);
        grainBufR_.fill(0.0f);
        reverbBufL_.fill(0.0f);
        reverbBufR_.fill(0.0f);

        writePos_   = 0;
        grainPos_   = 0;
        grainReadL_ = 0.0f;
        grainReadR_ = 0.0f;
        reverbWPos_ = 0;
    }

    void setMixNorm(float norm) noexcept { paramMix_ = std::clamp(norm, 0.0f, 1.0f); }
    void setTimeNorm(float norm) noexcept { paramTime_ = std::clamp(norm, 0.0f, 1.0f); }
    void setFeedbackNorm(float norm) noexcept { paramFeedback_ = std::clamp(norm, 0.0f, 1.0f); }
    void setPitchNorm(float norm) noexcept { paramPitch_ = std::clamp(norm, 0.0f, 1.0f); }
    void setReverbMixNorm(float norm) noexcept { paramReverbMix_ = std::clamp(norm, 0.0f, 1.0f); }

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
                setPitchNorm(val);
                break;
            case 4:
                setReverbMixNorm(val);
                break;
            default:
                break;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        // 100ms - 2000ms
        const float baseDelayMs  = 100.0f + paramTime_ * 1900.0f;
        const float baseDelay    = static_cast<float>(sampleRate_ * 0.001f * baseDelayMs);
        const float delayClamped = std::clamp(baseDelay, 1.0f, static_cast<float>(kMaxDelaySamples - 4));
        const float fb           = paramFeedback_ * 0.95f;

        // Ratio de pitch shift: 1.0 (unísono) a 2.0 (+1 octava)
        const float pitchRatio = 1.0f + paramPitch_;

        for (int i = 0; i < numSamples; ++i)
        {
            // 1. Lectura interpolada del delay
            const float rPosL = static_cast<float>(writePos_) - delayClamped;
            const int iPosL   = static_cast<int>(rPosL);
            const float fracL = rPosL - static_cast<float>(iPosL);
            const int idx0L   = iPosL & kDelayMask;
            const int idx1L   = (iPosL + 1) & kDelayMask;
            const float delL  = delayBufL_[idx0L] + fracL * (delayBufL_[idx1L] - delayBufL_[idx0L]);

            const float rPosR = static_cast<float>(writePos_) - (delayClamped * 1.002f);
            const int iPosR   = static_cast<int>(rPosR);
            const float fracR = rPosR - static_cast<float>(iPosR);
            const int idx0R   = iPosR & kDelayMask;
            const int idx1R   = (iPosR + 1) & kDelayMask;
            const float delR  = delayBufR_[idx0R] + fracR * (delayBufR_[idx1R] - delayBufR_[idx0R]);

            // 2. Transpositor granular de tono en feedback
            grainBufL_[grainPos_] = delL;
            grainBufR_[grainPos_] = delR;
            grainPos_             = (grainPos_ + 1) & kGrainMask;

            grainReadL_ += pitchRatio;
            if (grainReadL_ >= static_cast<float>(kGrainSize)) grainReadL_ -= static_cast<float>(kGrainSize);

            grainReadR_ += pitchRatio;
            if (grainReadR_ >= static_cast<float>(kGrainSize)) grainReadR_ -= static_cast<float>(kGrainSize);

            const int gIdxL      = static_cast<int>(grainReadL_) & kGrainMask;
            const int gNextL     = (gIdxL + 1) & kGrainMask;
            const float gFracL   = grainReadL_ - static_cast<float>(static_cast<int>(grainReadL_));
            const float shiftedL = grainBufL_[gIdxL] * (1.0f - gFracL) + grainBufL_[gNextL] * gFracL;

            const int gIdxR      = static_cast<int>(grainReadR_) & kGrainMask;
            const int gNextR     = (gIdxR + 1) & kGrainMask;
            const float gFracR   = grainReadR_ - static_cast<float>(static_cast<int>(grainReadR_));
            const float shiftedR = grainBufR_[gIdxR] * (1.0f - gFracR) + grainBufR_[gNextR] * gFracR;

            // Ventana para evitar clicks granulares
            const float winL       = 0.5f * (1.0f - static_cast<float>(std::cos(6.28318530717958647692 * grainReadL_ / static_cast<float>(kGrainSize))));
            const float winR       = 0.5f * (1.0f - static_cast<float>(std::cos(6.28318530717958647692 * grainReadR_ / static_cast<float>(kGrainSize))));
            const float shimmerFbL = shiftedL * winL;
            const float shimmerFbR = shiftedR * winR;

            // 3. Difusión atmosférica de reverberación
            const int revRPosL  = (reverbWPos_ - 3417 + kReverbSize) & kReverbMask;
            const int revRPosR  = (reverbWPos_ - 4523 + kReverbSize) & kReverbMask;
            const float revOutL = reverbBufL_[revRPosL];
            const float revOutR = reverbBufR_[revRPosR];

            reverbBufL_[reverbWPos_] = delL + revOutR * 0.4f;
            reverbBufR_[reverbWPos_] = delR + revOutL * 0.4f;
            reverbWPos_              = (reverbWPos_ + 1) & kReverbMask;

            // 4. Escritura en Delay principal
            const float fbSampleL = delL * (1.0f - paramPitch_) + shimmerFbL * paramPitch_;
            const float fbSampleR = delR * (1.0f - paramPitch_) + shimmerFbR * paramPitch_;

            delayBufL_[writePos_] = inL[i] + fbSampleL * fb;
            delayBufR_[writePos_] = inR[i] + fbSampleR * fb;
            writePos_             = (writePos_ + 1) & kDelayMask;

            // 5. Mezcla final
            const float wetL = delL * (1.0f - paramReverbMix_) + revOutL * paramReverbMix_;
            const float wetR = delR * (1.0f - paramReverbMix_) + revOutR * paramReverbMix_;

            outL[i] = inL[i] * (1.0f - paramMix_) + wetL * paramMix_;
            outR[i] = inR[i] * (1.0f - paramMix_) + wetR * paramMix_;
        }
    }

private:
    double sampleRate_    = 44100.0;
    float paramMix_       = 0.35f;
    float paramTime_      = 0.35f;
    float paramFeedback_  = 0.45f;
    float paramPitch_     = 0.7f;
    float paramReverbMix_ = 0.25f;

    std::array<float, kMaxDelaySamples> delayBufL_{};
    std::array<float, kMaxDelaySamples> delayBufR_{};
    int writePos_ = 0;

    std::array<float, kGrainSize> grainBufL_{};
    std::array<float, kGrainSize> grainBufR_{};
    int grainPos_     = 0;
    float grainReadL_ = 0.0f;
    float grainReadR_ = 0.0f;

    std::array<float, kReverbSize> reverbBufL_{};
    std::array<float, kReverbSize> reverbBufR_{};
    int reverbWPos_ = 0;
};

} // namespace abd::dsp
