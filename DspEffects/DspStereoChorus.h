/*
  ==============================================================================

    DspStereoChorus.h
    Chorus estéreo analógico con LFO multifunción (Tri/Sin), desfase y spread estéreo.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspStereoChorus.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Efecto de Chorus estéreo analógico clásico (DeepMind 12 FX Type 10).
      Utiliza dos líneas de retardo moduladas independientemente por canal:
        - Tasa LFO (Speed): 0.1 Hz a 10 Hz.
        - Profundidades de modulación L y R independientes (0 ms a 10 ms).
        - Retardos base independientes (0.5 ms a 50 ms).
        - Desfase angular entre LFOs (0° a 180°).
        - Forma de onda de modulación continua (Triangular a Sinusoidal).
        - Realimentación interna (Feedback) para resonancia de chorus (0.2).
        - Búfer circular estático en array (cero asignaciones dinámicas, 100% RT-Safe).

    RENDIMIENTO:
      - Cero asignaciones en memoria dinámica (100% Real-Time Safe).
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

class DspStereoChorus
{
public:
    static constexpr int kMaxDelaySamples = 16384; // >80ms a 192 kHz
    static constexpr int kDelayMask = kMaxDelaySamples - 1;

    DspStereoChorus() noexcept
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

    void setSpeedNorm(float norm) noexcept     { rate_ = std::clamp(norm, 0.0f, 1.0f); updateLFOIncrement(); }
    void setWidthLNorm(float norm) noexcept    { depthL_ = std::clamp(norm, 0.0f, 1.0f); }
    void setWidthRNorm(float norm) noexcept    { depthR_ = std::clamp(norm, 0.0f, 1.0f); }
    void setDelayLNorm(float norm) noexcept    { baseDelayL_ = std::clamp(norm, 0.0f, 1.0f); }
    void setDelayRNorm(float norm) noexcept    { baseDelayR_ = std::clamp(norm, 0.0f, 1.0f); }
    void setPhaseNorm(float norm) noexcept     { phase_ = std::clamp(norm, 0.0f, 1.0f); }
    void setWaveNorm(float norm) noexcept      { wave_ = std::clamp(norm, 0.0f, 1.0f); }
    void setSpreadNorm(float norm) noexcept    { spread_ = std::clamp(norm, 0.0f, 1.0f); }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float maxDepthSamplesL = static_cast<float>(sampleRate_ * 0.010 * depthL_);
        const float maxDepthSamplesR = static_cast<float>(sampleRate_ * 0.010 * depthR_);
        const float baseSampL = static_cast<float>(sampleRate_ * (0.0005f + 0.0495f * baseDelayL_));
        const float baseSampR = static_cast<float>(sampleRate_ * (0.0005f + 0.0495f * baseDelayR_));

        float phaseOffset = phase_ * 0.5f + spread_ * 0.25f;
        if (phaseOffset >= 1.0f) phaseOffset -= 1.0f;

        for (int s = 0; s < numSamples; ++s)
        {
            // 1. Avance del LFO
            lfoPhaseL_ += lfoInc_;
            if (lfoPhaseL_ >= 1.0) lfoPhaseL_ -= 1.0;

            lfoPhaseR_ = lfoPhaseL_ + phaseOffset;
            if (lfoPhaseR_ >= 1.0) lfoPhaseR_ -= 1.0;

            const float dryL = inL[s];
            const float dryR = inR[s];

            // 2. Modulación de forma de onda (tri -> sin)
            const float modL = computeWave(lfoPhaseL_);
            const float modR = computeWave(lfoPhaseR_);

            const float delayOffsetL = baseSampL + (modL + 1.0f) * 0.5f * maxDepthSamplesL;
            const float delayOffsetR = baseSampR + (modR + 1.0f) * 0.5f * maxDepthSamplesR;

            // 3. Lectura interpolada lineal
            const float wetL = readDelay(delayBufL_, writePosL_, delayOffsetL);
            const float wetR = readDelay(delayBufR_, writePosR_, delayOffsetR);

            // 4. Escritura en buffer circular estático con feedback
            delayBufL_[writePosL_] = dryL + wetL * feedback_;
            delayBufR_[writePosR_] = dryR + wetR * feedback_;

            writePosL_ = (writePosL_ + 1) & kDelayMask;
            writePosR_ = (writePosR_ + 1) & kDelayMask;

            // 100% wet (el slot gestiona la mezcla)
            outL[s] = wetL;
            outR[s] = wetR;
        }
    }

private:
    void updateLFOIncrement() noexcept
    {
        const float freqHz = 0.1f + 9.9f * rate_;
        lfoInc_ = freqHz / sampleRate_;
    }

    float computeWave(double phaseVal) const noexcept
    {
        const float sinVal = static_cast<float>(std::sin(6.28318530717958647692 * phaseVal));
        const float tri = static_cast<float>(4.0 * std::abs(phaseVal - std::floor(phaseVal + 0.5)) - 1.0);
        return tri * (1.0f - wave_) + sinVal * wave_;
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
    float rate_ = 0.3f;
    float depthL_ = 0.4f;
    float depthR_ = 0.4f;
    float baseDelayL_ = 0.5f;
    float baseDelayR_ = 0.5f;
    float phase_ = 0.25f;
    float wave_ = 0.5f;
    float spread_ = 0.0f;
    float feedback_ = 0.2f;

    double lfoPhaseL_ = 0.0;
    double lfoPhaseR_ = 0.25;
    double lfoInc_ = 0.0;

    std::array<float, kMaxDelaySamples> delayBufL_{};
    std::array<float, kMaxDelaySamples> delayBufR_{};
    int writePosL_ = 0;
    int writePosR_ = 0;
};

} // namespace abd::dsp
