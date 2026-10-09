/*
  ==============================================================================

    DspMidasEQ.h
    Ecualizador paramétrico de 4 bandas de consola analógica (Midas Heritage 3000 / Pro Series).

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspMidasEQ.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Emulación del ecualizador paramétrico de canal de 4 bandas de las consolas de
      estudio y directo Midas Heritage / PRO Series (DeepMind 12 FX Type 30).
      Consta de:
        1. Low Shelf (filtro de estante de graves a 30-20000 Hz, ±12 dB, Q=0.707).
        2. Low-Mid Peak (filtro de pico paramétrico de medios-graves a 30-20000 Hz, ±12 dB, Q=0.3 a 5.0).
        3. High-Mid Peak (filtro de pico paramétrico de medios-agudos a 30-20000 Hz, ±12 dB, Q=0.3 a 5.0).
        4. High Shelf (filtro de estante de agudos a 30-20000 Hz, ±12 dB, Q=0.707).
        5. Conmutador de Bypass limpio (EQ IN / OUT).

    IMPLEMENTACIÓN DSP:
      - Filtros biquad IIR en Forma Directa II Transpuesta (DF2T) numéricamente estables.
      - Ecuaciones analógicas estándar de Audio EQ Cookbook (RBJ).
      - Cero dependencias de JUCE (100% C++20 puro y Real-Time Safe).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include <algorithm>
#include <cmath>

namespace abd::dsp
{

class DspMidasEQ
{
public:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float s1 = 0.0f, s2 = 0.0f;

        void reset() noexcept
        {
            s1 = 0.0f;
            s2 = 0.0f;
        }

        inline float process(float in) noexcept
        {
            // Direct Form II Transposed
            const float out = b0 * in + s1;
            s1 = b1 * in - a1 * out + s2;
            s2 = b2 * in - a2 * out;
            return out;
        }

        void setLowShelf(float sampleRate, float freqHz, float gainLin, float Q = 0.7071f) noexcept
        {
            const float nyquist = static_cast<float>(sampleRate) * 0.48f;
            const float f = std::clamp(freqHz, 20.0f, nyquist);
            const float A = std::sqrt(gainLin);
            const float w0 = 6.28318530717958647692f * f / static_cast<float>(sampleRate);
            const float cosw = std::cos(w0);
            const float sinw = std::sin(w0);
            const float alpha = sinw / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / Q - 1.0f) + 2.0f);
            const float twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;

            const float a0 = (A + 1.0f) + (A - 1.0f) * cosw + twoSqrtAAlpha;
            b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw + twoSqrtAAlpha)) / a0;
            b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw)) / a0;
            b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw - twoSqrtAAlpha)) / a0;
            a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw)) / a0;
            a2 = ((A + 1.0f) + (A - 1.0f) * cosw - twoSqrtAAlpha) / a0;
        }

        void setHighShelf(float sampleRate, float freqHz, float gainLin, float Q = 0.7071f) noexcept
        {
            const float nyquist = static_cast<float>(sampleRate) * 0.48f;
            const float f = std::clamp(freqHz, 20.0f, nyquist);
            const float A = std::sqrt(gainLin);
            const float w0 = 6.28318530717958647692f * f / static_cast<float>(sampleRate);
            const float cosw = std::cos(w0);
            const float sinw = std::sin(w0);
            const float alpha = sinw / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / Q - 1.0f) + 2.0f);
            const float twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;

            const float a0 = (A + 1.0f) - (A - 1.0f) * cosw + twoSqrtAAlpha;
            b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw + twoSqrtAAlpha)) / a0;
            b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw)) / a0;
            b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw - twoSqrtAAlpha)) / a0;
            a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw)) / a0;
            a2 = ((A + 1.0f) - (A - 1.0f) * cosw - twoSqrtAAlpha) / a0;
        }

        void setPeak(float sampleRate, float freqHz, float gainLin, float Q) noexcept
        {
            const float nyquist = static_cast<float>(sampleRate) * 0.48f;
            const float f = std::clamp(freqHz, 20.0f, nyquist);
            const float A = std::sqrt(gainLin);
            const float w0 = 6.28318530717958647692f * f / static_cast<float>(sampleRate);
            const float cosw = std::cos(w0);
            const float sinw = std::sin(w0);
            const float alpha = sinw / (2.0f * std::max(0.1f, Q));

            const float a0 = 1.0f + alpha / A;
            b0 = (1.0f + alpha * A) / a0;
            b1 = (-2.0f * cosw) / a0;
            b2 = (1.0f - alpha * A) / a0;
            a1 = (-2.0f * cosw) / a0;
            a2 = (1.0f - alpha / A) / a0;
        }
    };

    DspMidasEQ() noexcept
    {
        reset();
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        lowShelfL_.reset();  lowShelfR_.reset();
        lowMidL_.reset();    lowMidR_.reset();
        highMidL_.reset();   highMidR_.reset();
        highShelfL_.reset(); highShelfR_.reset();
    }

    void setLoShelfGainNorm(float norm) noexcept { loShelfGain_ = std::clamp(norm, 0.0f, 1.0f); updateCoefficients(); }
    void setLoShelfFreqNorm(float norm) noexcept { loShelfFreq_ = std::clamp(norm, 0.0f, 1.0f); updateCoefficients(); }
    void setLoMidGainNorm(float norm) noexcept   { loMidGain_   = std::clamp(norm, 0.0f, 1.0f); updateCoefficients(); }
    void setLoMidFreqNorm(float norm) noexcept   { loMidFreq_   = std::clamp(norm, 0.0f, 1.0f); updateCoefficients(); }
    void setLoMidQNorm(float norm) noexcept      { loMidQ_      = std::clamp(norm, 0.0f, 1.0f); updateCoefficients(); }
    void setHiMidGainNorm(float norm) noexcept   { hiMidGain_   = std::clamp(norm, 0.0f, 1.0f); updateCoefficients(); }
    void setHiMidFreqNorm(float norm) noexcept   { hiMidFreq_   = std::clamp(norm, 0.0f, 1.0f); updateCoefficients(); }
    void setHiMidQNorm(float norm) noexcept      { hiMidQ_      = std::clamp(norm, 0.0f, 1.0f); updateCoefficients(); }
    void setHiShelfGainNorm(float norm) noexcept { hiShelfGain_ = std::clamp(norm, 0.0f, 1.0f); updateCoefficients(); }
    void setHiShelfFreqNorm(float norm) noexcept { hiShelfFreq_ = std::clamp(norm, 0.0f, 1.0f); updateCoefficients(); }
    void setEqIn(bool enabled) noexcept          { eqIn_        = enabled; }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        if (!eqIn_)
        {
            for (int s = 0; s < numSamples; ++s)
            {
                outL[s] = inL[s];
                outR[s] = inR[s];
            }
            return;
        }

        for (int s = 0; s < numSamples; ++s)
        {
            float l = inL[s];
            float r = inR[s];

            l = lowShelfL_.process(l);
            l = lowMidL_.process(l);
            l = highMidL_.process(l);
            l = highShelfL_.process(l);

            r = lowShelfR_.process(r);
            r = lowMidR_.process(r);
            r = highMidR_.process(r);
            r = highShelfR_.process(r);

            outL[s] = l;
            outR[s] = r;
        }
    }

private:
    void updateCoefficients() noexcept
    {
        auto mapFreq = [this](float norm) -> float {
            return std::clamp(30.0f * std::pow(666.6f, norm), 20.0f, static_cast<float>(sampleRate_ * 0.48));
        };

        auto dbToGain = [](float norm) -> float {
            const float db = (norm - 0.5f) * 24.0f; // -12 a +12 dB
            return std::pow(10.0f, db / 20.0f);
        };

        auto mapQ = [](float norm) -> float {
            return 0.3f + norm * 4.7f; // 0.3 a 5.0
        };

        const float sr = static_cast<float>(sampleRate_);

        // 1. Low Shelf
        const float lfFreq = mapFreq(loShelfFreq_);
        const float lfGain = dbToGain(loShelfGain_);
        lowShelfL_.setLowShelf(sr, lfFreq, lfGain);
        lowShelfR_.setLowShelf(sr, lfFreq, lfGain);

        // 2. Low Mid Peak
        const float lmFreq = mapFreq(loMidFreq_);
        const float lmGain = dbToGain(loMidGain_);
        const float lmQ = mapQ(loMidQ_);
        lowMidL_.setPeak(sr, lmFreq, lmGain, lmQ);
        lowMidR_.setPeak(sr, lmFreq, lmGain, lmQ);

        // 3. High Mid Peak
        const float hmFreq = mapFreq(hiMidFreq_);
        const float hmGain = dbToGain(hiMidGain_);
        const float hmQ = mapQ(hiMidQ_);
        highMidL_.setPeak(sr, hmFreq, hmGain, hmQ);
        highMidR_.setPeak(sr, hmFreq, hmGain, hmQ);

        // 4. High Shelf
        const float hfFreq = mapFreq(hiShelfFreq_);
        const float hfGain = dbToGain(hiShelfGain_);
        highShelfL_.setHighShelf(sr, hfFreq, hfGain);
        highShelfR_.setHighShelf(sr, hfFreq, hfGain);
    }

    double sampleRate_ = 44100.0;
    float loShelfGain_ = 0.5f;
    float loShelfFreq_ = 0.3f;
    float loMidGain_   = 0.5f;
    float loMidFreq_   = 0.5f;
    float loMidQ_      = 0.3f;
    float hiMidGain_   = 0.5f;
    float hiMidFreq_   = 0.7f;
    float hiMidQ_      = 0.3f;
    float hiShelfGain_ = 0.5f;
    float hiShelfFreq_ = 0.8f;
    bool eqIn_ = true;

    Biquad lowShelfL_, lowShelfR_;
    Biquad lowMidL_, lowMidR_;
    Biquad highMidL_, highMidR_;
    Biquad highShelfL_, highShelfR_;
};

} // namespace abd::dsp
