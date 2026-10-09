#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace abd::dsp
{

/**
 * @brief DspCombulator - Crossed Stereo Comb Filter Network.
 *
 * Implements two cross-coupled comb filters (L+R):
 * - Independent left and right delay times (1 to 50 ms).
 * - Feedback loop with 30% cross-channel recirculation.
 * - One-pole lowpass feedback damping.
 * - Soft-clipping non-linear saturation (tanh).
 * - Full Dry/Wet mixing.
 *
 * 100% C++20 pure DSP engine: zero allocations in process(), RT-safe.
 */
class DspCombulator
{
public:
    DspCombulator()
    {
        prepare(44100.0);
    }

    void prepare(double newSampleRate)
    {
        sampleRate = std::max(1.0, newSampleRate);
        const int requiredSamples = static_cast<int>(std::ceil(sampleRate * 0.055)) + 64;
        maxDelay = std::max(5000, requiredSamples);

        delayBufL.assign(static_cast<size_t>(maxDelay), 0.0f);
        delayBufR.assign(static_cast<size_t>(maxDelay), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        writePosL = 0;
        writePosR = 0;
        lpfStateL = 0.0f;
        lpfStateR = 0.0f;
        noiseSeed = 0xDEADBEEFu;
        if (!delayBufL.empty())
        {
            std::fill(delayBufL.begin(), delayBufL.end(), 0.0f);
            std::fill(delayBufR.begin(), delayBufR.end(), 0.0f);
        }
    }

    void setParameter(int index, float value) noexcept
    {
        value = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0: paramMix = value; break;
            case 1: paramDelayL = value; break;
            case 2: paramDelayR = value; break;
            case 3: paramFeedback = value; break;
            case 4: paramDamping = value; break;
            default: break;
        }
    }

    [[nodiscard]] float getParameter(int index) const noexcept
    {
        switch (index)
        {
            case 0: return paramMix;
            case 1: return paramDelayL;
            case 2: return paramDelayR;
            case 3: return paramFeedback;
            case 4: return paramDamping;
            default: return 0.0f;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        if (maxDelay <= 0 || delayBufL.empty())
        {
            for (int i = 0; i < numSamples; ++i)
            {
                outL[i] = inL[i];
                outR[i] = inR[i];
            }
            return;
        }

        const float mix = paramMix;
        const float feedback = paramFeedback;
        const float damping = paramDamping;

        float delaySamplesL = (paramDelayL * 49.0f + 1.0f) * static_cast<float>(sampleRate) * 0.001f;
        float delaySamplesR = (paramDelayR * 49.0f + 1.0f) * static_cast<float>(sampleRate) * 0.001f;
        const float dampingCoeff = 1.0f - damping * 0.95f;

        const int bufLen = maxDelay;
        const int dSamplesLInt = static_cast<int>(delaySamplesL);
        const int dSamplesRInt = static_cast<int>(delaySamplesR);

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryL = inL[i];
            const float dryR = inR[i];

            const int readPosL = (writePosL - dSamplesLInt + bufLen * 2) % bufLen;
            const int readPosR = (writePosR - dSamplesRInt + bufLen * 2) % bufLen;

            const float delayedL = delayBufL[readPosL];
            const float delayedR = delayBufR[readPosR];

            lpfStateL += dampingCoeff * (delayedL - lpfStateL);
            lpfStateR += dampingCoeff * (delayedR - lpfStateR);

            const float fbL = lpfStateL + lpfStateR * 0.3f;
            const float fbR = lpfStateR + lpfStateL * 0.3f;

            delayBufL[writePosL] = std::tanh(dryL + fbL * feedback);
            delayBufR[writePosR] = std::tanh(dryR + fbR * feedback);

            outL[i] = dryL + fbL * mix;
            outR[i] = dryR + fbR * mix;

            writePosL = (writePosL + 1) % bufLen;
            writePosR = (writePosR + 1) % bufLen;
        }
    }

private:
    double sampleRate = 44100.0;
    int maxDelay = 5000;

    float paramMix = 0.4f;
    float paramDelayL = 0.25f;
    float paramDelayR = 0.35f;
    float paramFeedback = 0.5f;
    float paramDamping = 0.3f;

    std::vector<float> delayBufL;
    std::vector<float> delayBufR;
    int writePosL = 0;
    int writePosR = 0;

    float lpfStateL = 0.0f;
    float lpfStateR = 0.0f;
    uint32_t noiseSeed = 0xDEADBEEFu;
};

} // namespace abd::dsp
