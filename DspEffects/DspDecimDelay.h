/*
  ==============================================================================

    DspDecimDelay.h
    Retardo decimador vintage lo-fi con reducción de tasa de muestreo, bit-crushing y filtro SVF.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspDecimDelay.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Retardo con etapa de degradación digital retro / bit crusher (DeepMind 12 FX Type 34).
      Emula los primeros samplers y retardos digitales de 8 y 12 bits (E-mu SP-1200, AMS DMX 15-80S):
        - Reducción de tasa de muestreo (Downsample): decimación sample-and-hold de 1x a 50x.
        - Reducción de profundidad de bits (Bit Depth): cuantización continua de 1 a 24 bits.
        - Filtro State Variable Filter (SVF) multimodo analógico (Lowpass, Highpass, Bandpass, Notch).
        - Factores rítmicos independientes por canal (1/4 a 3/1).
        - Ruteo flexible: Decimación antes del bucle de retardo (PRE) o a la salida (POST).

    RENDIMIENTO:
      - 100% Real-Time Safe: Búfer pre-asignado en prepare(), cero heap en process().
      - CERO dependencias de JUCE (C++20 estándar puro).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace abd::dsp
{

class DspDecimDelay
{
public:
    enum FilterType
    {
        kLowpass  = 0,
        kHighpass = 1,
        kBandpass = 2,
        kNotch    = 3
    };

    DspDecimDelay()
    {
        reset();
    }

    void prepare(double sampleRate)
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;

        // 4.6 segundos máximo para soportar hasta factor 3x de 1500ms
        const int maxSamples = static_cast<int>(sampleRate_ * 4.6) + 1024;
        bufL_.assign(static_cast<size_t>(maxSamples), 0.0f);
        bufR_.assign(static_cast<size_t>(maxSamples), 0.0f);

        updateDelaySamples();
        updateDecimParams();
        reset();
    }

    void reset() noexcept
    {
        std::fill(bufL_.begin(), bufL_.end(), 0.0f);
        std::fill(bufR_.begin(), bufR_.end(), 0.0f);
        writePosL_     = 0;
        writePosR_     = 0;
        decimCounterL_ = 0;
        decimCounterR_ = 0;
        lastSampleL_   = 0.0f;
        lastSampleR_   = 0.0f;
        svfLowL_ = svfLowR_ = svfBandL_ = svfBandR_ = svfHighL_ = svfHighR_ = 0.0f;
    }

    void setMixNorm(float norm) noexcept { mix_ = std::clamp(norm, 0.0f, 1.0f); }
    void setTimeNorm(float norm) noexcept
    {
        timeMs_ = std::clamp(norm, 0.0f, 1.0f);
        updateDelaySamples();
    }
    void setDownSampleNorm(float norm) noexcept
    {
        downSamp_ = std::clamp(norm, 0.0f, 1.0f);
        updateDecimParams();
    }
    void setFactorLNorm(float norm) noexcept
    {
        factorL_ = std::clamp(static_cast<int>(norm * 8.99f), 0, 8);
        updateDelaySamples();
    }
    void setFactorRNorm(float norm) noexcept
    {
        factorR_ = std::clamp(static_cast<int>(norm * 8.99f), 0, 8);
        updateDelaySamples();
    }
    void setBitReduceNorm(float norm) noexcept
    {
        bitReduce_ = std::clamp(norm, 0.0f, 1.0f);
        updateDecimParams();
    }
    void setCutoffNorm(float norm) noexcept { cutoff_ = std::clamp(norm, 0.0f, 1.0f); }
    void setResonanceNorm(float norm) noexcept { resonance_ = std::clamp(norm, 0.0f, 1.0f); }
    void setFilterType(int type) noexcept { filterType_ = std::clamp(type, 0, 3); }
    void setFilterTypeNorm(float norm) noexcept { filterType_ = std::clamp(static_cast<int>(norm * 3.99f), 0, 3); }
    void setFeedLNorm(float norm) noexcept { feedL_ = std::clamp(norm, 0.0f, 1.0f); }
    void setFeedRNorm(float norm) noexcept { feedR_ = std::clamp(norm, 0.0f, 1.0f); }
    void setDecimatePre(bool pre) noexcept { decimatePre_ = pre; }

    void setParameter(int index, float value) noexcept
    {
        const float val = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0:
                mix_ = val;
                break;
            case 1:
                timeMs_ = val;
                updateDelaySamples();
                break;
            case 2:
                downSamp_ = val;
                updateDecimParams();
                break;
            case 3:
                factorL_ = static_cast<int>(val * 8.99f);
                updateDelaySamples();
                break;
            case 4:
                factorR_ = static_cast<int>(val * 8.99f);
                updateDelaySamples();
                break;
            case 5:
                bitReduce_ = val;
                updateDecimParams();
                break;
            case 6:
                cutoff_ = val;
                break;
            case 7:
                resonance_ = val;
                break;
            case 8:
                filterType_ = static_cast<int>(val * 3.99f);
                break;
            case 9:
                feedL_ = val;
                break;
            case 10:
                feedR_ = val;
                break;
            case 11:
                decimatePre_ = (val < 0.5f);
                break;
            default:
                break;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const int bufSize = static_cast<int>(bufL_.size());
        if (bufSize <= 0) return;

        float* dL          = bufL_.data();
        float* dR          = bufR_.data();
        const float wetMix = mix_;

        // SVF filter coefficients
        float freqHz            = 30.0f * std::pow(666.0f, cutoff_);
        freqHz                  = std::clamp(freqHz, 20.0f, static_cast<float>(sampleRate_ / 6.5));
        constexpr double kTwoPi = 6.28318530717958647692;
        const float wd          = static_cast<float>(kTwoPi * freqHz / sampleRate_);
        const float gCoeff      = std::tan(wd * 0.5f);
        float rCoeff            = 1.0f / std::max(1.0f - resonance_ * 0.95f, 0.01f);
        const float maxR        = 2.0f / std::max(gCoeff, 0.001f);
        rCoeff                  = std::min(rCoeff, maxR);

        auto svf = [&](float in, float& low, float& band, float& high) {
            high = in - low - rCoeff * band;
            band = band + gCoeff * high;
            low  = low + gCoeff * band;
        };

        for (int s = 0; s < numSamples; ++s)
        {
            const float dryL = inL[s];
            const float dryR = inR[s];

            // Decimación PRE (entrada)
            float procL = decimatePre_ ? applyDecim(dryL) : dryL;
            float procR = decimatePre_ ? applyDecim(dryR) : dryR;

            // Downsample
            decimCounterL_++;
            decimCounterR_++;
            if (decimCounterL_ >= decimRateL_)
            {
                lastSampleL_   = procL;
                decimCounterL_ = 0;
            }
            if (decimCounterR_ >= decimRateR_)
            {
                lastSampleR_   = procR;
                decimCounterR_ = 0;
            }
            procL = lastSampleL_;
            procR = lastSampleR_;

            // Filtrado SVF
            svf(procL, svfLowL_, svfBandL_, svfHighL_);
            svf(procR, svfLowR_, svfBandR_, svfHighR_);

            float filtL = 0.0f;
            float filtR = 0.0f;
            switch (filterType_)
            {
                case 0:
                    filtL = svfLowL_;
                    filtR = svfLowR_;
                    break;
                case 1:
                    filtL = svfHighL_;
                    filtR = svfHighR_;
                    break;
                case 2:
                    filtL = svfBandL_;
                    filtR = svfBandR_;
                    break;
                default:
                    filtL = svfLowL_ + svfHighL_;
                    filtR = svfLowR_ + svfHighR_;
                    break;
            }

            // Decimación POST (delay)
            const float delayInL = decimatePre_ ? filtL : applyDecim(filtL);
            const float delayInR = decimatePre_ ? filtR : applyDecim(filtR);

            // Lectura delay
            int readPosL = writePosL_ - delaySamplesL_;
            if (readPosL < 0) readPosL += bufSize;
            const float delayedL = dL[readPosL];

            int readPosR = writePosR_ - delaySamplesR_;
            if (readPosR < 0) readPosR += bufSize;
            const float delayedR = dR[readPosR];

            // Escritura con realimentación
            dL[writePosL_] = delayInL + delayedL * feedL_ * 0.95f;
            dR[writePosR_] = delayInR + delayedR * feedR_ * 0.95f;
            writePosL_     = (writePosL_ + 1) % bufSize;
            writePosR_     = (writePosR_ + 1) % bufSize;

            // Mix
            outL[s] = dryL * (1.0f - wetMix) + delayedL * wetMix;
            outR[s] = dryR * (1.0f - wetMix) + delayedR * wetMix;
        }
    }

private:
    float applyDecim(float sample) const noexcept
    {
        if (bitMask_ < 0xFFFFFF)
        {
            const float quant = std::floor(sample * 8388607.0f + 0.5f) / 8388607.0f;
            const int intVal  = static_cast<int>(quant * static_cast<float>(bitMask_));
            sample            = static_cast<float>(intVal) / static_cast<float>(bitMask_);
        }
        return sample;
    }

    void updateDelaySamples() noexcept
    {
        static constexpr float factorTab[9] = {0.25f, 0.375f, 0.5f, 0.667f, 1.0f, 1.333f, 1.5f, 2.0f, 3.0f};
        const float baseMs                  = 1.0f + 1499.0f * timeMs_;
        const int maxDelaySamples           = static_cast<int>(bufL_.size());

        delaySamplesL_ = static_cast<int>(sampleRate_ * baseMs / 1000.0 * factorTab[std::clamp(factorL_, 0, 8)]);
        delaySamplesR_ = static_cast<int>(sampleRate_ * baseMs / 1000.0 * factorTab[std::clamp(factorR_, 0, 8)]);

        if (maxDelaySamples > 1)
        {
            delaySamplesL_ = std::max(1, std::min(delaySamplesL_, maxDelaySamples - 1));
            delaySamplesR_ = std::max(1, std::min(delaySamplesR_, maxDelaySamples - 1));
        }
    }

    void updateDecimParams() noexcept
    {
        decimRateL_    = static_cast<int>(1.0f + downSamp_ * 99.0f);
        decimRateR_    = static_cast<int>(1.0f + downSamp_ * 99.0f);
        const int bits = static_cast<int>(1.0f + (1.0f - bitReduce_) * 23.0f);
        bitMask_       = (1 << bits) - 1;
    }

    double sampleRate_ = 44100.0;
    float mix_         = 0.3f;
    float timeMs_      = 0.3f;
    float downSamp_    = 0.0f;
    float bitReduce_   = 0.8f;
    float cutoff_      = 0.8f;
    float resonance_   = 0.2f;
    float feedL_       = 0.3f;
    float feedR_       = 0.3f;
    int filterType_    = kLowpass;
    int factorL_       = 4;
    int factorR_       = 4;
    bool decimatePre_  = true;

    int decimRateL_ = 1;
    int decimRateR_ = 1;
    int bitMask_    = 0xFFFFFF;

    std::vector<float> bufL_;
    std::vector<float> bufR_;
    int writePosL_     = 0;
    int writePosR_     = 0;
    int delaySamplesL_ = 1000;
    int delaySamplesR_ = 1000;

    int decimCounterL_ = 0;
    int decimCounterR_ = 0;
    float lastSampleL_ = 0.0f;
    float lastSampleR_ = 0.0f;

    float svfLowL_  = 0.0f;
    float svfBandL_ = 0.0f;
    float svfHighL_ = 0.0f;
    float svfLowR_  = 0.0f;
    float svfBandR_ = 0.0f;
    float svfHighR_ = 0.0f;
};

} // namespace abd::dsp
