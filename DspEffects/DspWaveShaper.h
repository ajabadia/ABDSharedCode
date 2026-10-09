/*
  ==============================================================================

    DspWaveShaper.h
    Modelador de ondas no lineal (Waveshaper) con familias de transferencia continua.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspWaveShaper.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Efecto de conformación de onda (waveshaping) no lineal con interpolación
      continua entre familias de curvas de transferencia (Soft-Clip tanh, Foldback, Sine-clip)
      y control de simetría armónica par/impar.
      Inspirado en el Waveshaper de Surge XT / DeepMind 12 FX Type 51.

    PARÁMETROS:
      - Shape (0-1): interpola suavemente entre Soft Clip (tanh), Wavefold y Sine-based.
      - Symmetry (0-1): balance de armónicos pares (asimetría cuadrática) e impares (cúbica).
      - Gain (0-1): ganancia de entrada pre-shaper (1x a 13x).
      - Tone (0-1): filtro pasabajos de un polo post-saturación.
      - Mix (0-1): balance Dry / Wet.

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

class DspWaveShaper
{
public:
    DspWaveShaper() noexcept
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
        toneL_ = 0.0f;
        toneR_ = 0.0f;
    }

    void setShapeNorm(float norm) noexcept
    {
        shape_ = std::clamp(norm, 0.0f, 1.0f);
    }

    void setSymmetryNorm(float norm) noexcept
    {
        symmetry_ = std::clamp(norm, 0.0f, 1.0f);
    }

    void setGainNorm(float norm) noexcept
    {
        gainParam_ = std::clamp(norm, 0.0f, 1.0f);
    }

    void setToneNorm(float norm) noexcept
    {
        toneParam_ = std::clamp(norm, 0.0f, 1.0f);
    }

    void setMixNorm(float norm) noexcept
    {
        mix_ = std::clamp(norm, 0.0f, 1.0f);
    }

    float transfer(float x) const noexcept
    {
        const float g = 1.0f + gainParam_ * 12.0f;
        const float y = x * g;

        // Familias de funciones de transferencia:
        // 0.00 a 0.33: soft clip (tanh) a foldback
        // 0.33 a 0.66: foldback a sin-based
        // 0.66 a 1.00: sin-based puro
        const float softClip = std::tanh(y);
        const float fold     = 2.0f * std::abs(2.0f * (y * 0.5f - std::floor(y * 0.5f + 0.5f))) - 1.0f;
        const float sinBased = std::sin(y * 1.5707963267948966f); // sin(pi/2 * y)

        float result = 0.0f;
        if (shape_ < 0.33f)
        {
            const float t = shape_ / 0.33f;
            result        = softClip * (1.0f - t) + fold * t;
        }
        else if (shape_ < 0.66f)
        {
            const float t = (shape_ - 0.33f) / 0.33f;
            result        = fold * (1.0f - t) + sinBased * t;
        }
        else
        {
            result = sinBased;
        }

        // Control de simetría (armónicos pares vs impares)
        const float sym = symmetry_ * 2.0f - 1.0f; // -1..+1
        if (sym > 0.0f)
            result += sym * result * result; // Más armónicos pares
        else
            result -= (-sym) * result * result * result; // Más armónicos impares

        return result / (1.0f + gainParam_ * 0.5f); // Compensación de ganancia
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float toneCoeff = 0.05f + toneParam_ * 0.85f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float wetL = transfer(inL[i]);
            const float wetR = transfer(inR[i]);

            toneL_ += toneCoeff * (wetL - toneL_);
            toneR_ += toneCoeff * (wetR - toneR_);

            outL[i] = inL[i] * (1.0f - mix_) + toneL_ * mix_;
            outR[i] = inR[i] * (1.0f - mix_) + toneR_ * mix_;
        }
    }

private:
    double sampleRate_ = 44100.0;
    float shape_       = 0.3f;
    float symmetry_    = 0.5f;
    float gainParam_   = 0.5f;
    float toneParam_   = 0.5f;
    float mix_         = 0.4f;

    float toneL_ = 0.0f;
    float toneR_ = 0.0f;
};

} // namespace abd::dsp
