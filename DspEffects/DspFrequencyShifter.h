/*
  ==============================================================================

    DspFrequencyShifter.h
    Desplazador de frecuencia inarmónico (Bode Frequency Shifter) por transformada de Hilbert.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspFrequencyShifter.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Efecto de modulación SSB (Single Sideband) de desplazamiento de frecuencia lineal (DeepMind 12 FX Type 46).
      Inspirado en el clásico Moog / Bode Frequency Shifter:
        - A diferencia de un pitch shifter (que multiplica frecuencias armónicamente), un desplazador
          de frecuencia suma o resta un valor absoluto en Hertz (-2000 Hz a +2000 Hz) a todos los parciales.
        - Destruye la estructura armónica musical, generando texturas metálicas, campaniformes,
          efectos de anillo alienígenas y timbres de ciencia ficción retro.
        - Aproximación de Transformada de Hilbert por filtro FIR antisimétrico de 15 coeficientes
          para generar la señal analítica en cuadratura (desfasada 90°).
        - Modulación heteródina compleja: $s_{wet}(t) = s(t)\cos(\omega_0 t) + \hat{s}(t)\sin(\omega_0 t)$.
        - LFO de modulación de frecuencia (0.1 a 10 Hz) y lazo de realimentación resonante.

    RENDIMIENTO:
      - 100% Real-Time Safe: Búferes circulares estáticos de 15 muestras, cero heap en process().
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

class DspFrequencyShifter
{
public:
    static constexpr int kHilbertLen = 15;

    DspFrequencyShifter() noexcept
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
        oscPhase_ = 0.0f;
        lfoPhase_ = 0.0f;
        fbL_ = 0.0f;
        fbR_ = 0.0f;
        hilbertPosL_ = 0;
        hilbertPosR_ = 0;
        hilbertBufL_.fill(0.0f);
        hilbertBufR_.fill(0.0f);
    }

    void setMixNorm(float norm) noexcept      { paramMix_ = std::clamp(norm, 0.0f, 1.0f); }
    void setShiftNorm(float norm) noexcept    { paramShift_ = std::clamp(norm, 0.0f, 1.0f); }
    void setLfoRateNorm(float norm) noexcept  { paramLfoRate_ = std::clamp(norm, 0.0f, 1.0f); }
    void setLfoDepthNorm(float norm) noexcept { paramLfoDepth_ = std::clamp(norm, 0.0f, 1.0f); }
    void setFeedbackNorm(float norm) noexcept { paramFeedback_ = std::clamp(norm, 0.0f, 1.0f); }

    void setParameter(int index, float value) noexcept
    {
        const float val = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0: setMixNorm(val); break;
            case 1: setShiftNorm(val); break;
            case 2: setLfoRateNorm(val); break;
            case 3: setLfoDepthNorm(val); break;
            case 4: setFeedbackNorm(val); break;
            default: break;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float mix = paramMix_;
        const float lfoRate = paramLfoRate_ * 9.9f + 0.1f;
        const float lfoDepth = paramLfoDepth_;
        const float feedback = paramFeedback_;
        const float baseShift = (paramShift_ - 0.5f) * 4000.0f;
        const float lfoInc = lfoRate / static_cast<float>(sampleRate_);
        constexpr float kTwoPi = 6.28318530717958647692f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryL = inL[i];
            const float dryR = inR[i];

            // 1. Modulación por LFO
            const float lfoVal = std::sin(lfoPhase_ * kTwoPi) * lfoDepth;
            const float shiftHz = baseShift * (1.0f + lfoVal);
            lfoPhase_ += lfoInc;
            if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;

            const float oscInc = shiftHz / static_cast<float>(sampleRate_);

            // 2. Realimentación resonante
            const float sigL = dryL + fbL_ * feedback;
            const float sigR = dryR + fbR_ * feedback;

            // 3. Transformada de Hilbert FIR
            const float hilbL = hilbertTransform(sigL, hilbertBufL_, hilbertPosL_);
            const float hilbR = hilbertTransform(sigR, hilbertBufR_, hilbertPosR_);

            // 4. Portadora en cuadratura
            const float cosPart = std::cos(oscPhase_ * kTwoPi);
            const float sinPart = std::sin(oscPhase_ * kTwoPi);

            // 5. Modulación de banda lateral única (SSB)
            const float wetL = sigL * cosPart + hilbL * sinPart;
            const float wetR = sigR * cosPart + hilbR * sinPart;

            fbL_ = wetL;
            fbR_ = wetR;

            oscPhase_ += oscInc;
            if (oscPhase_ >= 1.0f) oscPhase_ -= 1.0f;
            else if (oscPhase_ < 0.0f) oscPhase_ += 1.0f;

            outL[i] = dryL + (wetL - dryL) * mix;
            outR[i] = dryR + (wetR - dryR) * mix;
        }
    }

private:
    static float hilbertTransform(float input, std::array<float, kHilbertLen>& buf, int& pos) noexcept
    {
        static constexpr float coeffs[kHilbertLen] = {
            -0.0124f, 0.0f, 0.0456f, 0.0f, -0.1327f,
            0.0f, 0.6184f, 0.0f, -0.6184f,
            0.0f, 0.1327f, 0.0f, -0.0456f,
            0.0f, 0.0124f
        };

        buf[pos] = input;
        float output = 0.0f;
        for (int j = 0; j < kHilbertLen; ++j)
        {
            const int idx = (pos - j + kHilbertLen * 2) % kHilbertLen;
            output += buf[idx] * coeffs[j];
        }
        pos = (pos + 1) % kHilbertLen;
        return output;
    }

    double sampleRate_ = 44100.0;

    float paramMix_      = 0.5f;
    float paramShift_    = 0.5f;
    float paramLfoRate_  = 0.3f;
    float paramLfoDepth_ = 0.0f;
    float paramFeedback_ = 0.0f;

    float oscPhase_ = 0.0f;
    float lfoPhase_ = 0.0f;

    float fbL_ = 0.0f;
    float fbR_ = 0.0f;

    std::array<float, kHilbertLen> hilbertBufL_{};
    std::array<float, kHilbertLen> hilbertBufR_{};
    int hilbertPosL_ = 0;
    int hilbertPosR_ = 0;
};

} // namespace abd::dsp
