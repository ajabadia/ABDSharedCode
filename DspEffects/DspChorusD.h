/*
  ==============================================================================

    DspChorusD.h
    Emulación del procesador espacial estéreo Roland Dimension D (SDH-320).

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspChorusD.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Emulación del legendario coro de estudio Roland SDD-320 "Dimension D"
      (DeepMind 12 FX Type 17).
      A diferencia de un chorus tradicional, el Dimension D utiliza dos líneas
      BBD moduladas por osciladores LFO duales independientes en desfase
      de 90° y 270°, sumados con proporciones 0.6 y 0.4, y con mezcla cruzada
      espacial invertida (delayedL - delayedR * 0.4f) que expande el campo
      estéreo cancelando la percepción del barrido oscilatorio.

    PARÁMETROS:
      - On: Interruptor de encendido (Bypass si está en falso).
      - MonoMode: Modo de entrada Mono o Estéreo.
      - Mix: Mezcla Dry/Wet (0 a 100%).
      - Presets (sw1 a sw4) con frecuencias y profundidades fijas analógicas:
        * Modo 1: Rate 0.25 / 0.35 Hz, Depth 2.0 ms.
        * Modo 2: Rate 0.50 / 0.65 Hz, Depth 3.0 ms.
        * Modo 3: Rate 0.80 / 1.00 Hz, Depth 4.0 ms.
        * Modo 4: Rate 1.20 / 1.50 Hz, Depth 6.0 ms.

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

class DspChorusD
{
public:
    static constexpr int kMaxDelaySamples = 16384; // >80ms a 192 kHz
    static constexpr int kDelayMask = kMaxDelaySamples - 1;

    DspChorusD() noexcept
    {
        reset();
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        updateLFOs();
        reset();
    }

    void reset() noexcept
    {
        delayBufL_.fill(0.0f);
        delayBufR_.fill(0.0f);
        writePos_ = 0;
        lfoPhaseL1_ = 0.0;
        lfoPhaseR1_ = 0.25; // 90°
        lfoPhaseL2_ = 0.0;
        lfoPhaseR2_ = 0.75; // 270°
    }

    void setOn(bool on) noexcept               { on_ = on; }
    void setMonoMode(bool mono) noexcept       { monoMode_ = mono; }
    void setMixNorm(float norm) noexcept       { mix_ = std::clamp(norm, 0.0f, 1.0f); }
    void setPreset(int idx, bool active) noexcept
    {
        if (idx >= 0 && idx < 4)
        {
            sw_[idx] = active;
            updateLFOs();
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        if (!on_)
        {
            for (int s = 0; s < numSamples; ++s)
            {
                outL[s] = inL[s];
                outR[s] = inR[s];
            }
            return;
        }

        int presetIdx = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (sw_[i]) presetIdx = i + 1;
        }
        if (presetIdx == 0) presetIdx = 1;

        float depthMs = 2.0f;
        switch (presetIdx)
        {
            case 1: depthMs = 2.0f; break;
            case 2: depthMs = 3.0f; break;
            case 3: depthMs = 4.0f; break;
            case 4: depthMs = 6.0f; break;
        }

        const float baseDelaySamples = static_cast<float>(sampleRate_ * 0.015f); // 15ms
        const float modDepthSamples = static_cast<float>(sampleRate_ * 0.001f * depthMs);

        for (int s = 0; s < numSamples; ++s)
        {
            lfoPhaseL1_ += lfoInc1_; if (lfoPhaseL1_ >= 1.0) lfoPhaseL1_ -= 1.0;
            lfoPhaseR1_ += lfoInc1_; if (lfoPhaseR1_ >= 1.0) lfoPhaseR1_ -= 1.0;
            lfoPhaseL2_ += lfoInc2_; if (lfoPhaseL2_ >= 1.0) lfoPhaseL2_ -= 1.0;
            lfoPhaseR2_ += lfoInc2_; if (lfoPhaseR2_ >= 1.0) lfoPhaseR2_ -= 1.0;

            float dryL = inL[s];
            float dryR = inR[s];

            if (monoMode_)
            {
                const float mono = (dryL + dryR) * 0.5f;
                dryL = mono;
                dryR = mono;
            }

            const float modValL = static_cast<float>(
                std::sin(6.28318530717958647692 * lfoPhaseL1_) * 0.6 +
                std::sin(6.28318530717958647692 * lfoPhaseL2_) * 0.4);

            const float modValR = static_cast<float>(
                std::sin(6.28318530717958647692 * lfoPhaseR1_) * 0.6 +
                std::sin(6.28318530717958647692 * lfoPhaseR2_) * 0.4);

            const float delayOffsetL = baseDelaySamples + modValL * modDepthSamples;
            const float delayOffsetR = baseDelaySamples + modValR * modDepthSamples;

            // Lectura interpolada lineal
            const float delayedL = readDelay(delayBufL_, writePos_, delayOffsetL);
            const float delayedR = readDelay(delayBufR_, writePos_, delayOffsetR);

            // Escritura en buffer de delay
            delayBufL_[writePos_] = dryL;
            delayBufR_[writePos_] = dryR;
            writePos_ = (writePos_ + 1) & kDelayMask;

            // Spatial cross-mixing con fase invertida (amplitud estéreo Dimension D)
            const float wetL = delayedL - delayedR * 0.4f;
            const float wetR = delayedR - delayedL * 0.4f;

            outL[s] = dryL * (1.0f - mix_) + wetL * mix_;
            outR[s] = dryR * (1.0f - mix_) + wetR * mix_;
        }
    }

private:
    void updateLFOs() noexcept
    {
        int presetIdx = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (sw_[i]) presetIdx = i + 1;
        }
        if (presetIdx == 0) presetIdx = 1;

        float speed1 = 0.25f;
        float speed2 = 0.40f;

        switch (presetIdx)
        {
            case 1: speed1 = 0.25f; speed2 = 0.35f; break;
            case 2: speed1 = 0.50f; speed2 = 0.65f; break;
            case 3: speed1 = 0.80f; speed2 = 1.00f; break;
            case 4: speed1 = 1.20f; speed2 = 1.50f; break;
        }

        lfoInc1_ = speed1 / sampleRate_;
        lfoInc2_ = speed2 / sampleRate_;
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
    bool on_ = true;
    bool monoMode_ = false;
    float mix_ = 0.5f;
    bool sw_[4] = { true, false, false, false };

    double lfoPhaseL1_ = 0.0, lfoPhaseR1_ = 0.25;
    double lfoPhaseL2_ = 0.0, lfoPhaseR2_ = 0.75;
    double lfoInc1_ = 0.0, lfoInc2_ = 0.0;

    std::array<float, kMaxDelaySamples> delayBufL_{};
    std::array<float, kMaxDelaySamples> delayBufR_{};
    int writePos_ = 0;
};

} // namespace abd::dsp
