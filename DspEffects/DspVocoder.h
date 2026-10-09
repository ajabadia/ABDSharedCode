/*
  ==============================================================================

    DspVocoder.h
    Motor universal de vocoder multibanda por bancos de filtros pasabanda (Channel Vocoder).

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspVocoder.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Emulación algorítmica de vocoder analógico clásico (EMS Vocoder 2000 /
      Roland SVC-350 / VP-330 / Korg MS2000).
      Consta de:
        1. Banco de análisis: filtros biquad pasabanda sobre la señal moduladora (voz/mic).
        2. Detectores de envolvente analógicos por banda (`abd::dsp::EnvelopeFollower`).
        3. Banco de resíntesis: filtros biquad pasabanda sobre la señal portadora (carrier/sinte).
        4. Matriz de desplazamiento de formantes (`FormantShift`).
        5. Generador interno de ruido rosa y resonadores de formantes para modo de voz sintética interna.

    RENDIMIENTO:
      - 100% Real-Time Safe: bancos estáticos con tamaño máximo por plantilla (default 32).
      - CERO dependencias de JUCE (C++20 estándar puro).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "DspCore/DspEnvelopeFollower.h"
#include <algorithm>
#include <cmath>

namespace abd::dsp
{

/**
 * Filtro biquad pasabanda estándar de segundo orden (DF2T).
 */
struct VocoderBiquad
{
    float b0{0.0f}, b1{0.0f}, b2{0.0f};
    float a1{0.0f}, a2{0.0f};
    float s1{0.0f}, s2{0.0f};

    void reset() noexcept
    {
        s1 = s2 = 0.0f;
    }

    void setBandpass(float freqHz, float q, double sampleRate) noexcept
    {
        const float nyq   = static_cast<float>(sampleRate) * 0.49f;
        const float f     = std::clamp(freqHz, 20.0f, nyq);
        const float w0    = 6.283185307179586f * f / static_cast<float>(sampleRate);
        const float alpha = std::sin(w0) / (2.0f * std::max(0.1f, q));
        const float cosw0 = std::cos(w0);
        const float a0    = 1.0f + alpha;

        b0 = alpha / a0;
        b1 = 0.0f;
        b2 = -alpha / a0;
        a1 = -2.0f * cosw0 / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float process(float in) noexcept
    {
        const float out = b0 * in + s1;
        s1              = b1 * in - a1 * out + s2;
        s2              = b2 * in - a2 * out;
        return out;
    }
};

/**
 * Vocoder multibanda por banco de filtros.
 */
template <int kMaxBands = 32>
class DspVocoderBank
{
public:
    static constexpr int kMaxBandCapacity = kMaxBands;

    DspVocoderBank() = default;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        reset();
        updateFrequencies();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kMaxBands; ++i)
        {
            analysisL_[i].reset();
            analysisR_[i].reset();
            resynthL_[i].reset();
            resynthR_[i].reset();
            formantL_[i].reset();
            formantR_[i].reset();
            followersL_[i].prepare(sampleRate_);
            followersR_[i].prepare(sampleRate_);
        }
        noiseStateL_ = 0.0f;
        noiseStateR_ = 0.0f;
        noiseSeed_   = 12345u;
    }

    void setBandCount(int count) noexcept
    {
        activeBands_ = std::clamp(count, 4, kMaxBands);
        updateFrequencies();
    }

    int getBandCount() const noexcept { return activeBands_; }

    void setFormantShift(float shiftMultiplier) noexcept
    {
        formantMultiplier_ = std::clamp(shiftMultiplier, 0.25f, 4.0f);
        updateFrequencies();
    }

    void setFormantShiftNorm(float formantNorm) noexcept
    {
        const float factor = std::pow(2.0f, (std::clamp(formantNorm, 0.0f, 1.0f) - 0.5f) * 2.0f);
        setFormantShift(factor);
    }

    void setAttackRelease(float attackSec, float releaseSec) noexcept
    {
        attackSec_  = std::max(0.001f, attackSec);
        releaseSec_ = std::max(0.001f, releaseSec);

        for (int i = 0; i < kMaxBands; ++i)
        {
            followersL_[i].setTimes(attackSec_, releaseSec_);
            followersR_[i].setTimes(attackSec_, releaseSec_);
        }
    }

    void setCustomFrequencies(const float* freqs, int count) noexcept
    {
        activeBands_ = std::clamp(count, 4, kMaxBands);
        for (int i = 0; i < activeBands_; ++i)
            bandFreqs_[i] = freqs[i];
        customFreqs_ = true;
        updateFrequencies();
    }

    void setLogFrequencies(float minHz = 100.0f, float maxHz = 15000.0f) noexcept
    {
        customFreqs_ = false;
        minFreqHz_   = minHz;
        maxFreqHz_   = maxHz;
        updateFrequencies();
    }

    void setUseInternalModulator(bool useInternal) noexcept
    {
        useInternalMod_ = useInternal;
    }

    bool getUseInternalModulator() const noexcept { return useInternalMod_; }

    /**
     * Procesa un bloque de audio estéreo: modula la señal portadora (carrier)
     * con la envolvente del modulador (voz/mic).
     */
    void process(const float* carrierL, const float* carrierR,
                 const float* modL, const float* modR,
                 float* wetOutL, float* wetOutR,
                 int numSamples) noexcept
    {
        const float bandGain = 1.0f / static_cast<float>(activeBands_);

        for (int s = 0; s < numSamples; ++s)
        {
            float mL = 0.0f;
            float mR = 0.0f;

            if (useInternalMod_)
            {
                // Modulador interno: ruido rosa a través de resonadores de formantes
                const float nL = generatePinkNoise(noiseStateL_);
                const float nR = generatePinkNoise(noiseStateR_);

                for (int b = 0; b < activeBands_; ++b)
                {
                    mL += formantL_[b].process(nL);
                    mR += formantR_[b].process(nR);
                }
                mL *= bandGain * 0.8f;
                mR *= bandGain * 0.8f;
            }
            else
            {
                mL = modL ? modL[s] : carrierL[s];
                mR = modR ? modR[s] : carrierR[s];
            }

            const float cL = carrierL[s];
            const float cR = carrierR[s];

            float sumL = 0.0f;
            float sumR = 0.0f;

            for (int b = 0; b < activeBands_; ++b)
            {
                // 1. Análisis del modulador
                const float filteredModL = analysisL_[b].process(mL);
                const float filteredModR = analysisR_[b].process(mR);

                const float envL = followersL_[b].process(filteredModL);
                const float envR = followersR_[b].process(filteredModR);

                // 2. Resíntesis de la portadora
                const float filteredCarL = resynthL_[b].process(cL);
                const float filteredCarR = resynthR_[b].process(cR);

                sumL += filteredCarL * envL * bandGain;
                sumR += filteredCarR * envR * bandGain;
            }

            wetOutL[s] = sumL;
            wetOutR[s] = sumR;
        }
    }

private:
    void updateFrequencies() noexcept
    {
        const float nyq = static_cast<float>(sampleRate_) * 0.45f;

        if (!customFreqs_)
        {
            const float minF = std::max(50.0f, minFreqHz_);
            const float maxF = std::min(nyq, maxFreqHz_);
            for (int i = 0; i < activeBands_; ++i)
            {
                const float t = (activeBands_ > 1) ? static_cast<float>(i) / static_cast<float>(activeBands_ - 1) : 0.0f;
                bandFreqs_[i] = minF * std::pow(maxF / minF, t);
            }
        }

        const float q = static_cast<float>(activeBands_) * 0.5f;

        for (int b = 0; b < activeBands_; ++b)
        {
            float shiftedFreq = bandFreqs_[b] * formantMultiplier_;
            shiftedFreq       = std::clamp(shiftedFreq, 20.0f, nyq);

            analysisL_[b].setBandpass(shiftedFreq, q, sampleRate_);
            analysisR_[b].setBandpass(shiftedFreq, q, sampleRate_);
            resynthL_[b].setBandpass(shiftedFreq, q, sampleRate_);
            resynthR_[b].setBandpass(shiftedFreq, q, sampleRate_);

            if (useInternalMod_)
            {
                formantL_[b].setBandpass(shiftedFreq * 0.85f, q * 0.7f, sampleRate_);
                formantR_[b].setBandpass(shiftedFreq * 0.85f, q * 0.7f, sampleRate_);
            }
        }
    }

    float generatePinkNoise(float& state) noexcept
    {
        noiseSeed_        = noiseSeed_ * 1103515245u + 12345u;
        const float white = (static_cast<float>(noiseSeed_ & 0x7FFFFFFF) / 1073741823.5f) - 1.0f;
        state             = state * 0.99765f + white * 0.0412156f;
        return state * 2.0f;
    }

    double sampleRate_{44100.0};
    int activeBands_{16};
    float formantMultiplier_{1.0f};
    float attackSec_{0.010f};
    float releaseSec_{0.050f};
    bool customFreqs_{false};
    float minFreqHz_{100.0f};
    float maxFreqHz_{15000.0f};
    bool useInternalMod_{false};

    float bandFreqs_[kMaxBands]{};
    VocoderBiquad analysisL_[kMaxBands];
    VocoderBiquad analysisR_[kMaxBands];
    VocoderBiquad resynthL_[kMaxBands];
    VocoderBiquad resynthR_[kMaxBands];
    VocoderBiquad formantL_[kMaxBands];
    VocoderBiquad formantR_[kMaxBands];
    EnvelopeFollower followersL_[kMaxBands];
    EnvelopeFollower followersR_[kMaxBands];

    float noiseStateL_{0.0f};
    float noiseStateR_{0.0f};
    uint32_t noiseSeed_{12345u};
};

} // namespace abd::dsp
