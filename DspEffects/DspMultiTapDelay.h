/*
  ==============================================================================

    DspMultiTapDelay.h
    Línea de retardo con múltiples tomas en paralelo (3-Tap y 4-Tap) y paneo estéreo.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspMultiTapDelay.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Procesador de retardo rítmico multi-toma en paralelo (DeepMind 12 FX Types 14 y 15).
      Inspirado en procesadores multitoma de estudio como el Lexicon PCM 70 y Roland SDE-330:
        - 3-Tap Delay: Tres tomas con tiempo, ganancia y panorama espacial independientes.
        - 4-Tap Delay: Cuatro tomas con ganancia y tiempo independientes, distribuidas en
          el campo estéreo mediante el parámetro Spread.
        - Factores métricos rítmicos: 1/4, 3/8, 1/2, 2/3, 1/1, 4/3, 3/2, 2/1, 3/1.
        - Realimentación de la última toma con opción de envío cruzado (Cross-Feedback).
        - Tiempo maestro: 1 ms a 1500 ms (hasta 4500 ms con factor 3x).

    RENDIMIENTO:
      - 100% Real-Time Safe: Búfer circular pre-asignado en prepare(), cero heap en process().
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

class DspMultiTapDelay
{
public:
    struct Tap
    {
        float factor = 1.0f;
        float gain   = 0.5f;
        float pan    = 0.0f;
    };

    explicit DspMultiTapDelay(int numTaps = 3)
        : numTaps_(numTaps)
    {
        taps_[0] = { 1.0f, 0.7f, 0.0f };
        taps_[1] = { 0.5f, 0.5f, -0.5f };
        taps_[2] = { 0.75f, 0.5f, 0.5f };
        taps_[3] = { 1.5f, 0.3f, 0.0f };
        reset();
    }

    void setTapCount(int numTaps) noexcept
    {
        numTaps_ = (numTaps == 4) ? 4 : 3;
    }

    int getTapCount() const noexcept { return numTaps_; }

    void prepare(double sampleRate)
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        
        // Máximo 4.6 segundos (1.5s * factor 3.0 + margen)
        const int maxDelaySamples = static_cast<int>(sampleRate_ * 4.6) + 1024;
        delayBufL_.assign(static_cast<size_t>(maxDelaySamples), 0.0f);
        delayBufR_.assign(static_cast<size_t>(maxDelaySamples), 0.0f);

        reset();
    }

    void reset() noexcept
    {
        std::fill(delayBufL_.begin(), delayBufL_.end(), 0.0f);
        std::fill(delayBufR_.begin(), delayBufR_.end(), 0.0f);
        writePos_ = 0;
    }

    static float getFactorFromIndex(int index) noexcept
    {
        switch (index)
        {
            case 0: return 0.25f;
            case 1: return 0.375f;
            case 2: return 0.5f;
            case 3: return 0.6667f;
            case 4: return 1.0f;
            case 5: return 1.3333f;
            case 6: return 1.5f;
            case 7: return 2.0f;
            case 8: return 3.0f;
            default: return 1.0f;
        }
    }

    void setMasterTimeNorm(float norm) noexcept { masterTime_ = std::clamp(norm, 0.0f, 1.0f); }
    void setFeedbackNorm(float norm) noexcept   { feedback_   = std::clamp(norm, 0.0f, 1.0f); }
    void setXFeed(bool enabled) noexcept        { xFeed_      = enabled; }
    void setMixNorm(float norm) noexcept        { mix_        = std::clamp(norm, 0.0f, 1.0f); }
    void setSpreadNorm(float norm) noexcept
    {
        spread_ = std::clamp(norm, 0.0f, 1.0f);
        if (numTaps_ == 4)
        {
            taps_[0].pan = -spread_;
            taps_[1].pan = -spread_ * 0.33f;
            taps_[2].pan = spread_ * 0.33f;
            taps_[3].pan = spread_;
        }
    }

    void setTapFactor(int tapIdx, float factor) noexcept
    {
        if (tapIdx >= 0 && tapIdx < 4) taps_[tapIdx].factor = factor;
    }

    void setTapGain(int tapIdx, float gain) noexcept
    {
        if (tapIdx >= 0 && tapIdx < 4) taps_[tapIdx].gain = std::clamp(gain, 0.0f, 1.0f);
    }

    void setTapPan(int tapIdx, float pan) noexcept
    {
        if (tapIdx >= 0 && tapIdx < 4) taps_[tapIdx].pan = std::clamp(pan, -1.0f, 1.0f);
    }

    void setParameter(int index, float value) noexcept
    {
        const float val = std::clamp(value, 0.0f, 1.0f);

        if (numTaps_ == 3)
        {
            switch (index)
            {
                case 0:  masterTime_ = val; break;
                case 1:  taps_[0].gain = val; break;
                case 2:  taps_[0].pan = (val - 0.5f) * 2.0f; break;
                case 3:  feedback_ = val; break;
                case 4:  taps_[1].factor = getFactorFromIndex(static_cast<int>(val * 8.9f)); break;
                case 5:  taps_[1].gain = val; break;
                case 6:  taps_[1].pan = (val - 0.5f) * 2.0f; break;
                case 7:  taps_[2].factor = getFactorFromIndex(static_cast<int>(val * 8.9f)); break;
                case 8:  taps_[2].gain = val; break;
                case 9:  taps_[2].pan = (val - 0.5f) * 2.0f; break;
                case 10: xFeed_ = (val > 0.5f); break;
                case 11: mix_ = val; break;
                default: break;
            }
        }
        else // 4-Tap
        {
            switch (index)
            {
                case 0:  masterTime_ = val; break;
                case 1:  taps_[0].gain = val; break;
                case 2:  feedback_ = val; break;
                case 3:  setSpreadNorm(val); break;
                case 4:  taps_[1].factor = getFactorFromIndex(static_cast<int>(val * 8.9f)); break;
                case 5:  taps_[1].gain = val; break;
                case 6:  taps_[2].factor = getFactorFromIndex(static_cast<int>(val * 8.9f)); break;
                case 7:  taps_[2].gain = val; break;
                case 8:  taps_[3].factor = getFactorFromIndex(static_cast<int>(val * 8.9f)); break;
                case 9:  taps_[3].gain = val; break;
                case 10: xFeed_ = (val > 0.5f); break;
                case 11: mix_ = val; break;
                default: break;
            }
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const int maxDelaySamples = static_cast<int>(delayBufL_.size());
        if (maxDelaySamples <= 0) return;

        float* dL = delayBufL_.data();
        float* dR = delayBufR_.data();

        const float baseTimeMs = 1.0f + masterTime_ * 1499.0f;
        const float baseDelaySamples = static_cast<float>(sampleRate_ * 0.001f * baseTimeMs);
        const int activeTaps = (numTaps_ == 3) ? 3 : 4;

        for (int s = 0; s < numSamples; ++s)
        {
            const float dryL = inL[s];
            const float dryR = inR[s];

            float accumL = 0.0f;
            float accumR = 0.0f;

            for (int t = 0; t < activeTaps; ++t)
            {
                const float delaySamples = baseDelaySamples * taps_[t].factor;
                float readPos = static_cast<float>(writePos_) - delaySamples;

                while (readPos < 0.0f) readPos += static_cast<float>(maxDelaySamples);
                while (readPos >= static_cast<float>(maxDelaySamples)) readPos -= static_cast<float>(maxDelaySamples);

                const int idx = static_cast<int>(readPos);
                const int nextIdx = (idx + 1) % maxDelaySamples;
                const float frac = readPos - static_cast<float>(idx);

                const float tapValL = dL[idx] * (1.0f - frac) + dL[nextIdx] * frac;
                const float tapValR = dR[idx] * (1.0f - frac) + dR[nextIdx] * frac;

                const float panVal = taps_[t].pan;
                const float gainL = std::clamp(1.0f - panVal, 0.0f, 1.0f) * taps_[t].gain;
                const float gainR = std::clamp(1.0f + panVal, 0.0f, 1.0f) * taps_[t].gain;

                accumL += tapValL * gainL;
                accumR += tapValR * gainR;
            }

            // Realimentación del último tap
            float fbInL = dryL;
            float fbInR = dryR;

            float lastTapL = 0.0f;
            float lastTapR = 0.0f;
            {
                const float delaySamples = baseDelaySamples * taps_[activeTaps - 1].factor;
                float readPos = static_cast<float>(writePos_) - delaySamples;
                if (readPos < 0.0f) readPos += static_cast<float>(maxDelaySamples);
                const int idx = static_cast<int>(readPos) % maxDelaySamples;
                lastTapL = dL[idx];
                lastTapR = dR[idx];
            }

            if (xFeed_)
            {
                fbInL += lastTapR * feedback_;
                fbInR += lastTapL * feedback_;
            }
            else
            {
                fbInL += lastTapL * feedback_;
                fbInR += lastTapR * feedback_;
            }

            dL[writePos_] = fbInL;
            dR[writePos_] = fbInR;

            writePos_ = (writePos_ + 1) % maxDelaySamples;

            outL[s] = dryL * (1.0f - mix_) + accumL * mix_;
            outR[s] = dryR * (1.0f - mix_) + accumR * mix_;
        }
    }

private:
    int numTaps_ = 3;
    double sampleRate_ = 44100.0;
    float masterTime_  = 0.3f;
    float feedback_    = 0.3f;
    bool  xFeed_       = false;
    float mix_         = 0.5f;
    float spread_      = 0.5f;

    Tap taps_[4];

    std::vector<float> delayBufL_;
    std::vector<float> delayBufR_;
    int writePos_ = 0;
};

} // namespace abd::dsp
