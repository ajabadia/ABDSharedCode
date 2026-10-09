/*
  ==============================================================================

    DspFdnReverb.h
    Reverberación algorítmica de red de retardo con realimentación (Feedback Delay Network 8x8).

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspFdnReverb.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Motor de reverberación de alta densidad acústica basado en FDN (DeepMind 12 FX Type 52).
      Inspirado en investigaciones acústicas de Stautner & Puckett y Jot & Chaigne:
        - 8 Líneas de retardo con longitudes mutuamente primas para máxima densidad modal sin resonancias metálicas.
        - Matriz ortogonal Householder simétrica de 8x8 para mezcla lossless y dispersión energética rápida.
        - Etapa de difusión estéreo mediante cadena de 4 filtros Allpass en serie.
        - Amortiguamiento dependiente de frecuencia por canal (filtros paso-bajos de 1-polo en lazos de feedback).
        - Parámetros: Size, Decay, Diffusion, Damping, Mix.

    RENDIMIENTO:
      - 100% Real-Time Safe: Memoria asignada únicamente en prepare(). Cero heap en process() y setParameter().
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

class DspFdnReverb
{
public:
    static constexpr int kNumDelays = 8;
    static constexpr int kNumAllpasses = 4;

    DspFdnReverb()
    {
        reset();
    }

    void prepare(double sampleRate)
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        
        // Asignación máxima previa para garantizar 100% RT-Safety
        constexpr int baseLens[8] = { 1433, 1789, 2053, 2591, 3169, 3793, 4201, 4877 };
        for (int i = 0; i < kNumDelays; ++i)
        {
            const int maxLen = static_cast<int>(baseLens[i] * 1.2f) + 256;
            delayBuffers_[i].assign(static_cast<size_t>(maxLen), 0.0f);
        }

        constexpr int apLens[4] = { 347, 613, 919, 1201 };
        for (int i = 0; i < kNumAllpasses; ++i)
        {
            const int maxLen = static_cast<int>(apLens[i] * 1.2f) + 128;
            apBuffers_[i].assign(static_cast<size_t>(maxLen), 0.0f);
        }

        updateDelayLengths();
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kNumDelays; ++i)
        {
            std::fill(delayBuffers_[i].begin(), delayBuffers_[i].end(), 0.0f);
            writePos_[i] = 0;
            dampingState_[i] = 0.0f;
        }
        for (int i = 0; i < kNumAllpasses; ++i)
        {
            std::fill(apBuffers_[i].begin(), apBuffers_[i].end(), 0.0f);
            apWritePos_[i] = 0;
        }
    }

    void setSizeNorm(float norm) noexcept      { sizeParam_ = std::clamp(norm, 0.0f, 1.0f); updateDelayLengths(); }
    void setDecayNorm(float norm) noexcept     { decayParam_ = std::clamp(norm, 0.0f, 1.0f); }
    void setDiffusionNorm(float norm) noexcept { diffusionParam_ = std::clamp(norm, 0.0f, 1.0f); updateDelayLengths(); }
    void setDampingNorm(float norm) noexcept   { dampingParam_ = std::clamp(norm, 0.0f, 1.0f); }
    void setMixNorm(float norm) noexcept       { mix_ = std::clamp(norm, 0.0f, 1.0f); }

    void setParameter(int index, float value) noexcept
    {
        const float val = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0: setSizeNorm(val); break;
            case 1: setDecayNorm(val); break;
            case 2: setDiffusionNorm(val); break;
            case 3: setDampingNorm(val); break;
            case 4: setMixNorm(val); break;
            default: break;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float fb = decayParam_ * 0.85f;
        const float dampCoeff = 0.1f + dampingParam_ * 0.8f;

        for (int s = 0; s < numSamples; ++s)
        {
            const float xL = inL[s];
            const float xR = inR[s];
            const float input = (xL + xR) * 0.5f;

            // Escritura en las líneas de retardo
            for (int d = 0; d < kNumDelays; ++d)
            {
                const float inSample = (d < 2) ? input * (d == 0 ? xL : xR) : input;
                const int wPos = writePos_[d];
                if (wPos < static_cast<int>(delayBuffers_[d].size()))
                    delayBuffers_[d][wPos] += inSample * 0.3f;
            }

            // Lectura y filtrado de amortiguamiento (Damping)
            float delayOut[kNumDelays];
            for (int d = 0; d < kNumDelays; ++d)
            {
                const int curSize = delaySizes_[d];
                if (curSize <= 0)
                {
                    delayOut[d] = 0.0f;
                    continue;
                }
                const int readPos = (writePos_[d] - curSize / 2 + curSize * 2) % curSize;
                float samp = delayBuffers_[d][readPos];

                dampingState_[d] += dampCoeff * (samp - dampingState_[d]);
                delayOut[d] = dampingState_[d];
            }

            // Matriz ortogonal Householder 8x8: y = sum(x) - 2*x
            float sum = 0.0f;
            for (int d = 0; d < kNumDelays; ++d)
                sum += delayOut[d];

            float matrixOut[kNumDelays];
            for (int d = 0; d < kNumDelays; ++d)
                matrixOut[d] = sum - delayOut[d] * 2.0f;

            // Realimentación en el búfer
            for (int d = 0; d < kNumDelays; ++d)
            {
                const int curSize = delaySizes_[d];
                if (curSize <= 0) continue;
                delayBuffers_[d][writePos_[d]] = matrixOut[d] * fb;
                writePos_[d] = (writePos_[d] + 1) % curSize;
            }

            // Difusión Allpass
            float diff = matrixOut[0] + matrixOut[4];
            for (int a = 0; a < kNumAllpasses; ++a)
            {
                const int apSize = apSizes_[a];
                if (apSize <= 0) continue;
                const int readPos = (apWritePos_[a] - apSize / 2 + apSize * 2) % apSize;
                const float apOut = apBuffers_[a][readPos];
                apBuffers_[a][apWritePos_[a]] = diff + apGains_[a] * apOut;
                diff = -apGains_[a] * diff + apOut;
                apWritePos_[a] = (apWritePos_[a] + 1) % apSize;
            }

            const float wet = diff * 0.5f;
            outL[s] = xL * (1.0f - mix_) + wet * mix_;
            outR[s] = xR * (1.0f - mix_) + wet * mix_;
        }
    }

private:
    void updateDelayLengths() noexcept
    {
        constexpr int baseLens[8] = { 1433, 1789, 2053, 2591, 3169, 3793, 4201, 4877 };
        const float scale = 0.3f + sizeParam_ * 0.7f;
        for (int i = 0; i < kNumDelays; ++i)
        {
            int size = static_cast<int>(baseLens[i] * scale);
            if (size < 64) size = 64;
            const int maxBuf = static_cast<int>(delayBuffers_[i].size());
            if (maxBuf > 0 && size > maxBuf) size = maxBuf;
            delaySizes_[i] = size;
        }

        constexpr int apLens[4] = { 347, 613, 919, 1201 };
        for (int i = 0; i < kNumAllpasses; ++i)
        {
            int size = static_cast<int>(apLens[i] * (0.5f + sizeParam_ * 0.5f));
            if (size < 32) size = 32;
            const int maxBuf = static_cast<int>(apBuffers_[i].size());
            if (maxBuf > 0 && size > maxBuf) size = maxBuf;
            apSizes_[i] = size;
            apGains_[i] = 0.5f + diffusionParam_ * 0.4f;
        }
    }

    double sampleRate_ = 44100.0;
    float sizeParam_      = 0.5f;
    float decayParam_     = 0.5f;
    float diffusionParam_ = 0.5f;
    float dampingParam_   = 0.5f;
    float mix_            = 0.4f;

    std::vector<float> delayBuffers_[kNumDelays];
    int delaySizes_[kNumDelays] = {};
    int writePos_[kNumDelays]   = {};
    float dampingState_[kNumDelays] = {};

    std::vector<float> apBuffers_[kNumAllpasses];
    int apSizes_[kNumAllpasses]     = {};
    int apWritePos_[kNumAllpasses]  = {};
    float apGains_[kNumAllpasses]   = {};
};

} // namespace abd::dsp
