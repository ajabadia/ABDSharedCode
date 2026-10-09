/*
  ==============================================================================

    DspFlanger.h
    Flanger estéreo analógico de peine variable y retroalimentación negativa.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspFlanger.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Efecto Flanger estéreo clásico (DeepMind 12 FX Type 11).
      Simula el clásico flanger de cinta y BBD (estilo TC Electronic / ADA Flanger):
        - Retardo ultracorto modulado (0.5 ms a 20 ms).
        - Modulación sinusoidal con velocidad de 0.05 Hz a 8 Hz.
        - Profundidades de modulación L y R independientes (0 ms a 5 ms).
        - Desfase angular LFO estéreo (0° a 180°).
        - Realimentación intensa (Feedback) de hasta ±90% con fase invertida,
          produciendo las crestas y valles resonantes en peine características.

    RENDIMIENTO:
      - Búfer circular estático en array (cero asignaciones dinámicas, 100% RT-Safe).
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

class DspFlanger
{
public:
    static constexpr int kMaxDelaySamples = 16384; // >80ms a 192 kHz
    static constexpr int kDelayMask = kMaxDelaySamples - 1;

    DspFlanger() noexcept
    {
        reset();
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        updateLFOIncrement();
        reset();
    }

    void reset() noexcept
    {
        delayBufL_.fill(0.0f);
        delayBufR_.fill(0.0f);
        writePosL_ = 0;
        writePosR_ = 0;
        lfoPhaseL_ = 0.0;
        lfoPhaseR_ = 0.25;
    }

    void setRateNorm(float norm) noexcept        { rate_ = std::clamp(norm, 0.0f, 1.0f); updateLFOIncrement(); }
    void setDepthLNorm(float norm) noexcept      { depthL_ = std::clamp(norm, 0.0f, 1.0f); }
    void setDepthRNorm(float norm) noexcept      { depthR_ = std::clamp(norm, 0.0f, 1.0f); }
    void setBaseDelayLNorm(float norm) noexcept  { baseDelayL_ = std::clamp(norm, 0.0f, 1.0f); }
    void setBaseDelayRNorm(float norm) noexcept  { baseDelayR_ = std::clamp(norm, 0.0f, 1.0f); }
    void setPhaseNorm(float norm) noexcept       { phase_ = std::clamp(norm, 0.0f, 1.0f); }
    void setFeedbackNorm(float norm) noexcept    { feedback_ = std::clamp(norm, 0.0f, 1.0f) * 0.9f; }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float maxDepthSampL = static_cast<float>(sampleRate_ * 0.005 * depthL_);
        const float maxDepthSampR = static_cast<float>(sampleRate_ * 0.005 * depthR_);
        const float baseSampL = static_cast<float>(sampleRate_ * (0.0005f + 0.0195f * baseDelayL_));
        const float baseSampR = static_cast<float>(sampleRate_ * (0.0005f + 0.0195f * baseDelayR_));

        float phaseOffset = phase_ * 0.5f;
        if (phaseOffset >= 1.0f) phaseOffset -= 1.0f;

        for (int s = 0; s < numSamples; ++s)
        {
            // 1. Avance del LFO
            lfoPhaseL_ += lfoPhaseInc_;
            if (lfoPhaseL_ >= 1.0) lfoPhaseL_ -= 1.0;

            lfoPhaseR_ = lfoPhaseL_ + phaseOffset;
            if (lfoPhaseR_ >= 1.0) lfoPhaseR_ -= 1.0;

            const float dryL = inL[s];
            const float dryR = inR[s];

            // 2. Modulación sinusoidal
            const float modL = static_cast<float>(std::sin(6.28318530717958647692 * lfoPhaseL_));
            const float modR = static_cast<float>(std::sin(6.28318530717958647692 * lfoPhaseR_));

            const float delaySampL = baseSampL + (modL + 1.0f) * 0.5f * maxDepthSampL;
            const float delaySampR = baseSampR + (modR + 1.0f) * 0.5f * maxDepthSampR;

            // 3. Lectura con interpolación lineal
            const float delayedL = readDelay(delayBufL_, writePosL_, delaySampL);
            const float delayedR = readDelay(delayBufR_, writePosR_, delaySampR);

            // 4. Escritura en buffer: entrada + feedback con fase invertida (flanger característico)
            delayBufL_[writePosL_] = dryL + delayedL * -feedback_;
            delayBufR_[writePosR_] = dryR + delayedR * -feedback_;

            writePosL_ = (writePosL_ + 1) & kDelayMask;
            writePosR_ = (writePosR_ + 1) & kDelayMask;

            // 100% wet (el slot gestiona la mezcla)
            outL[s] = delayedL;
            outR[s] = delayedR;
        }
    }

private:
    void updateLFOIncrement() noexcept
    {
        const float freqHz = 0.05f + 7.95f * rate_;
        lfoPhaseInc_ = freqHz / sampleRate_;
    }

    static inline float readDelay(const std::array<float, kMaxDelaySamples>& buf, int writePos, float delaySamples) noexcept
    {
        const float clampedDel = std::clamp(delaySamples, 0.0f, static_cast<float>(kMaxDelaySamples - 2));
        float readPos = static_cast<float>(writePos) - clampedDel;
        if (readPos < 0.0f) readPos += static_cast<float>(kMaxDelaySamples);

        const int idx = static_cast<int>(readPos) & kDelayMask;
        const int next = (idx + 1) & kDelayMask;
        const float frac = readPos - static_cast<float>(static_cast<int>(readPos));

        return buf[idx] * (1.0f - frac) + buf[next] * frac;
    }

    double sampleRate_ = 44100.0;
    float rate_ = 0.2f;
    float depthL_ = 0.5f;
    float depthR_ = 0.5f;
    float baseDelayL_ = 0.3f;
    float baseDelayR_ = 0.3f;
    float phase_ = 0.25f;
    float feedback_ = 0.45f;

    double lfoPhaseL_ = 0.0;
    double lfoPhaseR_ = 0.25;
    double lfoPhaseInc_ = 0.0;

    std::array<float, kMaxDelaySamples> delayBufL_{};
    std::array<float, kMaxDelaySamples> delayBufR_{};
    int writePosL_ = 0;
    int writePosR_ = 0;
};

} // namespace abd::dsp
