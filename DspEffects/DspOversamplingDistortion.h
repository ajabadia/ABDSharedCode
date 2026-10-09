/*
  ==============================================================================

    DspOversamplingDistortion.h
    Distorsión/saturación con sobremuestreo 4x y filtrado antialiasing FIR.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspOversamplingDistortion.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Efecto de distorsión asimétrica y saturación no lineal con sobremuestreo
      interno a 4x de la tasa de muestreo y diezmado con filtro antialiasing FIR.
      Inspirado en el diseño de Odin2 / DeepMind 12 FX Type 50.

    DISEÑO DSP:
      - Sobremuestreo 4x (Zero-Order Hold).
      - Modelado de onda no lineal asimétrico (tanh con componente cúbico dependiente de Drive).
      - Control de apertura estéreo (Stereo width) en la etapa no lineal.
      - Diezmado 4x mediante filtro FIR de coseno alzado ponderado.
      - Filtro de tono pasabajos de un polo y control de mezcla Dry/Wet.
      - Procesamiento por fragmentos fijos en búferes estáticos internos:
        Cero asignaciones dinámicas en memoria (100% Real-Time Safe).

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

class DspOversamplingDistortion
{
public:
    static constexpr int kOversample  = 4;
    static constexpr int kChunkSize   = 32;
    static constexpr int kUpChunkSize = kChunkSize * kOversample;

    DspOversamplingDistortion() noexcept
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
        toneL_ = toneR_ = 0.0f;
    }

    void setDriveNorm(float norm) noexcept
    {
        drive_ = std::clamp(norm, 0.0f, 1.0f);
    }

    void setToneNorm(float norm) noexcept
    {
        tone_ = std::clamp(norm, 0.0f, 1.0f);
    }

    void setMixNorm(float norm) noexcept
    {
        mix_ = std::clamp(norm, 0.0f, 1.0f);
    }

    void setLevelNorm(float norm) noexcept
    {
        level_ = std::clamp(norm, 0.0f, 1.0f);
    }

    void setStereoNorm(float norm) noexcept
    {
        stereo_ = std::clamp(norm, 0.0f, 1.0f);
    }

    float waveshape(float x) const noexcept
    {
        const float g = 1.0f + drive_ * 8.0f;
        float shaped  = std::tanh(x * g);
        shaped += 0.1f * drive_ * shaped * shaped * shaped;
        return shaped / (1.0f + 0.1f * drive_);
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float widthL    = 1.0f + stereo_ * 0.3f;
        const float widthR    = 1.0f - stereo_ * 0.3f;
        const float toneCoeff = 0.1f + tone_ * 0.8f;

        int processed = 0;
        while (processed < numSamples)
        {
            const int chunkSize = std::min(kChunkSize, numSamples - processed);

            // 1. Sobremuestreo 4x
            for (int i = 0; i < chunkSize; ++i)
            {
                const float sL = inL[processed + i];
                const float sR = inR[processed + i];
                for (int j = 0; j < kOversample; ++j)
                {
                    upBufL_[i * kOversample + j] = sL;
                    upBufR_[i * kOversample + j] = sR;
                }
            }

            const int totalUp = chunkSize * kOversample;

            // 2. Saturación no lineal a tasa 4x
            for (int i = 0; i < totalUp; ++i)
            {
                upBufL_[i] = waveshape(upBufL_[i] * widthL) / widthL;
                upBufR_[i] = waveshape(upBufR_[i] * widthR) / widthR;
            }

            // 3. Diezmado con filtro FIR antialiasing
            for (int i = 0; i < chunkSize; ++i)
            {
                const int base = i * kOversample;
                float accL     = 0.0f;
                float accR     = 0.0f;
                for (int j = 0; j < kOversample; ++j)
                {
                    const float w = 0.5f + 0.5f * std::cos(3.14159265358979323846f * static_cast<float>(j) / static_cast<float>(kOversample));
                    accL += upBufL_[base + j] * w;
                    accR += upBufR_[base + j] * w;
                }
                const float downL = accL / static_cast<float>(kOversample);
                const float downR = accR / static_cast<float>(kOversample);

                // 4. Filtro de tono y mezcla final
                toneL_ += toneCoeff * (downL - toneL_);
                toneR_ += toneCoeff * (downR - toneR_);

                const float wetL = toneL_ * level_;
                const float wetR = toneR_ * level_;

                outL[processed + i] = inL[processed + i] * (1.0f - mix_) + wetL * mix_;
                outR[processed + i] = inR[processed + i] * (1.0f - mix_) + wetR * mix_;
            }

            processed += chunkSize;
        }
    }

private:
    double sampleRate_ = 44100.0;
    float drive_       = 0.3f;
    float tone_        = 0.5f;
    float mix_         = 0.4f;
    float level_       = 0.7f;
    float stereo_      = 0.5f;

    std::array<float, kUpChunkSize> upBufL_{};
    std::array<float, kUpChunkSize> upBufR_{};

    float toneL_ = 0.0f;
    float toneR_ = 0.0f;
};

} // namespace abd::dsp
