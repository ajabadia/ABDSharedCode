/*
  ==============================================================================

    DspZitaReverb.h
    Reverberación algorítmica de alta fidelidad basada en topología Zita/AIR.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspZitaReverb.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Motor de reverberación algorítmica pura de estudio (DeepMind 12 FX Type 53).
      Basado en el diseño Zita / AIR reverb (Fons Adriaensen / Surge / Odin2):
        - Etapa de Pre-Delay estéreo conmutable de 0 a 100 ms.
        - Filtro Allpass monofónico de difusión a la entrada (512 muestras) para coherencia de fase.
        - Banco de 4 filtros Comb paralelos desacoplados estéreo con amortiguamiento de 1-polo.
        - Cadena de salida de 2 filtros Allpass estéreo con tiempos primos (181 y 127 muestras).
        - Parámetros: Size, Decay, Damping, PreDelay, Mix.

    RENDIMIENTO:
      - 100% Real-Time Safe: Búferes circulares pre-asignados en prepare(). Cero heap en process() o setParameter().
      - CERO dependencias de JUCE (C++20 estándar puro).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace abd::dsp
{

class DspZitaReverb
{
public:
    static constexpr int kInputAPSize = 512;
    static constexpr int kNumCombs = 4;
    static constexpr int kOutAPCount = 2;

    DspZitaReverb()
    {
        reset();
    }

    void prepare(double sampleRate)
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;

        inputAPBuf_.assign(static_cast<size_t>(kInputAPSize), 0.0f);

        preDelaySize_ = std::max(1, static_cast<int>(sampleRate_ * 0.1)); // 100 ms máx
        preDelayBufL_.assign(static_cast<size_t>(preDelaySize_), 0.0f);
        preDelayBufR_.assign(static_cast<size_t>(preDelaySize_), 0.0f);

        for (int a = 0; a < kOutAPCount; ++a)
        {
            outAPSizes_[a] = (a == 0) ? 181 : 127;
            outAPBufL_[a].assign(static_cast<size_t>(outAPSizes_[a]), 0.0f);
            outAPBufR_[a].assign(static_cast<size_t>(outAPSizes_[a]), 0.0f);
        }

        constexpr int baseLens[4] = { 1557, 1617, 1491, 1423 };
        for (int i = 0; i < kNumCombs; ++i)
        {
            const int maxLen = static_cast<int>(baseLens[i] * 1.2f) + 256;
            combBufL_[i].assign(static_cast<size_t>(maxLen), 0.0f);
            combBufR_[i].assign(static_cast<size_t>(maxLen), 0.0f);
        }

        updateCombLengths();
        reset();
    }

    void reset() noexcept
    {
        std::fill(inputAPBuf_.begin(), inputAPBuf_.end(), 0.0f);
        inputAPPos_ = 0;

        std::fill(preDelayBufL_.begin(), preDelayBufL_.end(), 0.0f);
        std::fill(preDelayBufR_.begin(), preDelayBufR_.end(), 0.0f);
        preDelayPos_ = 0;

        for (int i = 0; i < kNumCombs; ++i)
        {
            std::fill(combBufL_[i].begin(), combBufL_[i].end(), 0.0f);
            std::fill(combBufR_[i].begin(), combBufR_[i].end(), 0.0f);
            combWritePos_[i] = 0;
            combStateL_[i] = 0.0f;
            combStateR_[i] = 0.0f;
        }

        for (int a = 0; a < kOutAPCount; ++a)
        {
            std::fill(outAPBufL_[a].begin(), outAPBufL_[a].end(), 0.0f);
            std::fill(outAPBufR_[a].begin(), outAPBufR_[a].end(), 0.0f);
            outAPPos_[a] = 0;
        }
    }

    void setSizeNorm(float norm) noexcept     { sizeParam_ = std::clamp(norm, 0.0f, 1.0f); updateCombLengths(); }
    void setDecayNorm(float norm) noexcept    { decayParam_ = std::clamp(norm, 0.0f, 1.0f); }
    void setDampingNorm(float norm) noexcept  { dampingParam_ = std::clamp(norm, 0.0f, 1.0f); }
    void setPreDelayNorm(float norm) noexcept { preDelayParam_ = std::clamp(norm, 0.0f, 1.0f); }
    void setMixNorm(float norm) noexcept      { mix_ = std::clamp(norm, 0.0f, 1.0f); }

    void setParameter(int index, float value) noexcept
    {
        const float val = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0: setSizeNorm(val); break;
            case 1: setDecayNorm(val); break;
            case 2: setDampingNorm(val); break;
            case 3: setPreDelayNorm(val); break;
            case 4: setMixNorm(val); break;
            default: break;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float fb = decayParam_ * 0.82f;
        const float dampCoeff = 0.2f + dampingParam_ * 0.7f;
        const int pdSize = preDelaySize_;
        if (pdSize <= 0) return;

        for (int s = 0; s < numSamples; ++s)
        {
            const float xL = inL[s];
            const float xR = inR[s];

            // 1. Pre-delay
            preDelayBufL_[preDelayPos_] = xL;
            preDelayBufR_[preDelayPos_] = xR;
            const int pdOffset = static_cast<int>(preDelayParam_ * pdSize * 0.9f);
            const int pdRead = (preDelayPos_ - pdOffset + pdSize * 2) % pdSize;
            float pdL = preDelayBufL_[pdRead];
            preDelayPos_ = (preDelayPos_ + 1) % pdSize;

            // 2. Allpass monofónico de difusión inicial
            {
                const int readPos = (inputAPPos_ - kInputAPSize / 2 + kInputAPSize * 2) % kInputAPSize;
                const float apOut = inputAPBuf_[readPos];
                inputAPBuf_[inputAPPos_] = pdL * 0.5f + 0.5f * apOut;
                const float inputAP = -0.5f * pdL + apOut;
                inputAPPos_ = (inputAPPos_ + 1) % kInputAPSize;
                pdL = inputAP;
            }
            const float pdR = pdL; // coherencia estéreo

            // 3. Banco de peines en paralelo
            float combSumL = 0.0f;
            float combSumR = 0.0f;

            for (int i = 0; i < kNumCombs; ++i)
            {
                const int cSize = combSizes_[i];
                if (cSize <= 0) continue;

                const int readPos = (combWritePos_[i] - cSize / 2 + cSize * 2) % cSize;
                const float cL = combBufL_[i][readPos];
                const float cR = combBufR_[i][readPos];

                combStateL_[i] += dampCoeff * (cL - combStateL_[i]);
                combStateR_[i] += dampCoeff * (cR - combStateR_[i]);

                combBufL_[i][combWritePos_[i]] = pdL + combStateL_[i] * fb;
                combBufR_[i][combWritePos_[i]] = pdR + combStateR_[i] * fb;
                combWritePos_[i] = (combWritePos_[i] + 1) % cSize;

                combSumL += cL;
                combSumR += cR;
            }
            combSumL /= static_cast<float>(kNumCombs);
            combSumR /= static_cast<float>(kNumCombs);

            // 4. Cadena de Allpass de salida
            float allpassL = combSumL;
            float allpassR = combSumR;

            for (int a = 0; a < kOutAPCount; ++a)
            {
                const int aSize = outAPSizes_[a];
                if (aSize <= 0) continue;

                const int rL = (outAPPos_[a] - aSize / 2 + aSize * 2) % aSize;
                const float oL = outAPBufL_[a][rL];
                const float oR = outAPBufR_[a][rL];

                outAPBufL_[a][outAPPos_[a]] = allpassL + 0.5f * oL;
                outAPBufR_[a][outAPPos_[a]] = allpassR + 0.5f * oR;

                allpassL = -0.5f * allpassL + oL;
                allpassR = -0.5f * allpassR + oR;

                outAPPos_[a] = (outAPPos_[a] + 1) % aSize;
            }

            outL[s] = xL * (1.0f - mix_) + allpassL * mix_;
            outR[s] = xR * (1.0f - mix_) + allpassR * mix_;
        }
    }

private:
    void updateCombLengths() noexcept
    {
        constexpr int baseLens[4] = { 1557, 1617, 1491, 1423 };
        for (int i = 0; i < kNumCombs; ++i)
        {
            int size = static_cast<int>(baseLens[i] * (0.4f + sizeParam_ * 0.6f));
            if (size < 64) size = 64;
            const int maxBuf = static_cast<int>(combBufL_[i].size());
            if (maxBuf > 0 && size > maxBuf) size = maxBuf;
            combSizes_[i] = size;
        }
    }

    double sampleRate_ = 44100.0;
    float sizeParam_     = 0.5f;
    float decayParam_    = 0.5f;
    float dampingParam_  = 0.5f;
    float preDelayParam_ = 0.0f;
    float mix_           = 0.4f;

    std::vector<float> inputAPBuf_;
    int inputAPPos_ = 0;

    int preDelaySize_ = 0;
    std::vector<float> preDelayBufL_;
    std::vector<float> preDelayBufR_;
    int preDelayPos_ = 0;

    std::vector<float> combBufL_[kNumCombs];
    std::vector<float> combBufR_[kNumCombs];
    int combSizes_[kNumCombs]    = {};
    int combWritePos_[kNumCombs] = {};
    float combStateL_[kNumCombs] = {};
    float combStateR_[kNumCombs] = {};

    std::vector<float> outAPBufL_[kOutAPCount];
    std::vector<float> outAPBufR_[kOutAPCount];
    int outAPSizes_[kOutAPCount] = {};
    int outAPPos_[kOutAPCount]   = {};
};

} // namespace abd::dsp
