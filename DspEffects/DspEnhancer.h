#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace abd::dsp
{

/**
 * @brief DspEnhancer - Spectral Enhancer / Exciter 3-band processor.
 *
 * Modeled after psychoacoustic exciter / enhancer units (SPL Vitalizer / SX3040 / DeepMind Type 18):
 * - Bass shelving filter with adjustable frequency and gain.
 * - Mid peaking bandpass filter with adjustable Q and gain.
 * - High shelving exciter with adjustable frequency and gain.
 * - Solo audition mode (auditions mid band only).
 * - Stereo spatial spreading.
 *
 * 100% C++20 pure DSP engine: zero allocations in process(), RT-safe.
 */
class DspEnhancer
{
public:
    DspEnhancer() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(1.0, newSampleRate);
        reset();
        updateCoeffs();
    }

    void reset() noexcept
    {
        bassStateL = 0.0f;
        bassStateR = 0.0f;
        midStateL  = 0.0f;
        midStateR  = 0.0f;
        midDelayL  = 0.0f;
        midDelayR  = 0.0f;
        hiStateL   = 0.0f;
        hiStateR   = 0.0f;
    }

    void setParameter(int idx, float v) noexcept
    {
        v = std::clamp(v, 0.0f, 1.0f);
        switch (idx)
        {
            case 0:
                outGain = v;
                break;
            case 1:
                spread = v;
                break;
            case 2:
                bassGain = v;
                updateCoeffs();
                break;
            case 3:
                bassFreq = v;
                updateCoeffs();
                break;
            case 4:
                midGain = v;
                updateCoeffs();
                break;
            case 5:
                midQ = v;
                updateCoeffs();
                break;
            case 6:
                hiGain = v;
                updateCoeffs();
                break;
            case 7:
                hiFreq = v;
                updateCoeffs();
                break;
            case 8:
                solo = (v > 0.5f);
                break;
            default:
                break;
        }
    }

    [[nodiscard]] float getParameter(int idx) const noexcept
    {
        switch (idx)
        {
            case 0:
                return outGain;
            case 1:
                return spread;
            case 2:
                return bassGain;
            case 3:
                return bassFreq;
            case 4:
                return midGain;
            case 5:
                return midQ;
            case 6:
                return hiGain;
            case 7:
                return hiFreq;
            case 8:
                return solo ? 1.0f : 0.0f;
            default:
                return 0.0f;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float gainLin       = std::pow(10.0f, (outGain * 24.0f - 12.0f) / 20.0f);
        const float spreadAmt     = spread * 0.5f;
        const float bassGainScale = bassGain * 3.0f;
        const float hiGainScale   = hiGain * 3.0f;

        for (int s = 0; s < numSamples; ++s)
        {
            const float l = inL[s];
            const float r = inR[s];

            // Bass shelf
            bassStateL += bassCoeff * (l - bassStateL);
            bassStateR += bassCoeff * (r - bassStateR);
            const float bassL = l + (bassStateL - l) * bassGainScale;
            const float bassR = r + (bassStateR - r) * bassGainScale;

            // Mid peaking
            const float midInL  = l - bassStateL;
            const float midInR  = r - bassStateR;
            const float midOutL = midCoeffA * midInL + midDelayL;
            midDelayL           = midInL - midCoeffB * midOutL;
            const float midOutR = midCoeffA * midInR + midDelayR;
            midDelayR           = midInR - midCoeffB * midOutR;

            // High shelf
            const float hiInL = l - bassStateL;
            const float hiInR = r - bassStateR;
            hiStateL += hiCoeff * (hiInL - hiStateL);
            hiStateR += hiCoeff * (hiInR - hiStateR);
            const float hiL = hiInL + (hiStateL - hiInL) * hiGainScale;
            const float hiR = hiInR + (hiStateR - hiInR) * hiGainScale;

            // Solo mode
            float wetL, wetR;
            if (solo)
            {
                wetL = midOutL;
                wetR = midOutR;
            }
            else
            {
                wetL = bassL + midOutL + hiL;
                wetR = bassR + midOutR + hiR;
            }

            // Stereo spread
            const float m = (wetL + wetR) * 0.5f;
            float side    = (wetL - wetR) * 0.5f;
            side *= (1.0f + spreadAmt);
            outL[s] = (m + side) * gainLin;
            outR[s] = (m - side) * gainLin;
        }
    }

private:
    void updateCoeffs() noexcept
    {
        auto mapFreq = [](float norm) -> float {
            return 30.0f * std::pow(666.0f, norm);
        };
        const float bFreq = mapFreq(bassFreq);
        const float hFreq = mapFreq(hiFreq);
        bassCoeff         = static_cast<float>(bFreq / (bFreq + sampleRate * 0.5));
        hiCoeff           = static_cast<float>(hFreq / (hFreq + sampleRate * 0.5));

        // Mid peaking filter
        constexpr float mFreq = 1000.0f;
        const float qVal      = 0.5f + midQ * 5.0f;
        const float w0        = static_cast<float>(2.0 * std::numbers::pi * mFreq / sampleRate);
        const float alpha     = std::sin(w0) / (2.0f * qVal);
        const float gainVal   = midGain * 3.0f;
        midCoeffA             = (1.0f + gainVal) * alpha;
        midCoeffB             = 1.0f - alpha;
    }

    double sampleRate = 44100.0;
    float outGain     = 0.5f;
    float spread      = 0.3f;
    float bassGain    = 0.3f;
    float bassFreq    = 0.3f;
    float midGain     = 0.3f;
    float midQ        = 0.3f;
    float hiGain      = 0.3f;
    float hiFreq      = 0.3f;
    bool solo         = false;

    float bassStateL = 0.0f;
    float bassStateR = 0.0f;
    float bassCoeff  = 0.0f;

    float midStateL = 0.0f;
    float midStateR = 0.0f;
    float midDelayL = 0.0f;
    float midDelayR = 0.0f;
    float midCoeffA = 0.0f;
    float midCoeffB = 0.0f;

    float hiStateL = 0.0f;
    float hiStateR = 0.0f;
    float hiCoeff  = 0.0f;
};

} // namespace abd::dsp
