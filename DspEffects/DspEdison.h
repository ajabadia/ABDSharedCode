#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace abd::dsp
{

/**
 * @brief DspEdison - Klark Teknik / Behringer Edison EX1 Stereo & Mid/Side Spatial Processor.
 *
 * Emulates the classic psychoacoustic spatial enhancer and M/S processor:
 * - Selectable input/output domain (Stereo or Mid/Side).
 * - Stereo spread adjustment on the side channel (-50% to +50%).
 * - Low-Mid Frequency (LMF) crossover (~300 Hz) with independent spread for tight bass.
 * - Center distortion (tanh overdrive on the mid channel) to add harmonics and focus.
 * - Balance and output gain controls.
 *
 * 100% C++20 pure DSP engine: zero allocations in process(), RT-safe.
 */
class DspEdison
{
public:
    DspEdison() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(1.0, newSampleRate);
        reset();
        updateLMF();
    }

    void reset() noexcept
    {
        lmfSideL = 0.0f;
        lmfSideR = 0.0f;
    }

    void setParameter(int index, float value) noexcept
    {
        value = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0: on = (value > 0.5f); break;
            case 1: inMode = (value > 0.5f) ? 1 : 0; break;
            case 2: outMode = (value > 0.5f) ? 1 : 0; break;
            case 3: stSpread = value; break;
            case 4: lmfSpread = value; updateLMF(); break;
            case 5: balance = value; break;
            case 6: cntrDist = value; break;
            case 7: gain = value; break;
            default: break;
        }
    }

    [[nodiscard]] float getParameter(int index) const noexcept
    {
        switch (index)
        {
            case 0: return on ? 1.0f : 0.0f;
            case 1: return static_cast<float>(inMode);
            case 2: return static_cast<float>(outMode);
            case 3: return stSpread;
            case 4: return lmfSpread;
            case 5: return balance;
            case 6: return cntrDist;
            case 7: return gain;
            default: return 0.0f;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        if (!on)
        {
            for (int s = 0; s < numSamples; ++s)
            {
                outL[s] = inL[s];
                outR[s] = inR[s];
            }
            return;
        }

        const float spreadVal = (stSpread - 0.5f) * 2.0f;
        const float lmfVal = (lmfSpread - 0.5f) * 2.0f;
        const float balVal = (balance - 0.5f) * 2.0f;
        const float centerDistAmt = (cntrDist - 0.5f) * 2.0f;
        const float gainLin = std::pow(10.0f, (gain * 24.0f - 12.0f) / 20.0f);
        const float spreadFactor = 1.0f + spreadVal;
        const float lmfSpreadFactor = 1.0f + lmfVal;
        const float balGainL = 1.0f - std::max(0.0f, balVal) * 0.5f;
        const float balGainR = 1.0f - std::max(0.0f, -balVal) * 0.5f;
        const bool hasLmf = std::abs(lmfVal) > 0.01f;
        const bool hasCntrDist = std::abs(centerDistAmt) > 0.01f;
        const float absDist = std::abs(centerDistAmt);
        const float distGain = 1.0f + absDist * 8.0f;

        for (int s = 0; s < numSamples; ++s)
        {
            const float left = inL[s];
            const float right = inR[s];

            float mid, side;
            if (inMode == 1)
            {
                mid = left;
                side = right;
            }
            else
            {
                mid = (left + right) * 0.5f;
                side = (left - right) * 0.5f;
            }

            side *= spreadFactor;

            if (hasLmf)
            {
                lmfSideL += lmfCoeff * (side - lmfSideL);
                lmfSideR += lmfCoeff * (side - lmfSideR);

                const float sideLowL = lmfSideL;
                const float sideHighL = side - lmfSideL;
                side = sideLowL * lmfSpreadFactor + sideHighL;
            }

            if (hasCntrDist)
            {
                float distMid = std::tanh(mid * distGain);
                mid = mid * (1.0f - absDist) + distMid * absDist;
            }

            float outLeft, outRight;
            if (outMode == 1)
            {
                outLeft = mid * balGainL;
                outRight = side * balGainR;
            }
            else
            {
                outLeft = (mid + side) * balGainL;
                outRight = (mid - side) * balGainR;
            }

            outL[s] = outLeft * gainLin;
            outR[s] = outRight * gainLin;
        }
    }

private:
    void updateLMF() noexcept
    {
        constexpr float freq = 300.0f;
        lmfCoeff = static_cast<float>(freq / (freq + sampleRate * 0.5));
    }

    double sampleRate = 44100.0;
    bool on = true;
    int inMode = 0;
    int outMode = 0;
    float stSpread = 0.5f;
    float lmfSpread = 0.5f;
    float balance = 0.5f;
    float cntrDist = 0.0f;
    float gain = 0.5f;

    float lmfSideL = 0.0f;
    float lmfSideR = 0.0f;
    float lmfCoeff = 0.0f;
};

} // namespace abd::dsp
