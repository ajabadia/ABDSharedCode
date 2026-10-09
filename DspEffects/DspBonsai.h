#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace abd::dsp
{

/**
 * @brief DspBonsai - Lo-fi Vintage Degradation & Pitch Processor.
 *
 * Inspired by Surge Bonsai (Type 55):
 * - Pitch shifting (-12 to +12 semitones) via circular interpolation buffer.
 * - Tape-like wow pitch modulation (0.5 - 3.5 Hz sinusoidal LFO).
 * - Variable sample-rate reduction (sample-and-hold decimator).
 * - Continuous bit-depth reduction (24 bits down to 3 bits).
 * - Drive saturation (normalized tanh transfer function).
 * - Dry/Wet mixing.
 *
 * 100% C++20 pure DSP engine: zero allocations in process(), RT-safe.
 */
class DspBonsai
{
public:
    static constexpr int kMaxDelay = 32768; // Power of 2 (>= 19200)

    DspBonsai()
    {
        prepare(44100.0);
    }

    void prepare(double sr)
    {
        sampleRate  = std::max(1.0, sr);
        int bufSize = 1;
        while (bufSize < kMaxDelay)
            bufSize *= 2;
        bufL.assign(static_cast<size_t>(bufSize), 0.0f);
        bufR.assign(static_cast<size_t>(bufSize), 0.0f);
        bufMask = bufSize - 1;
        reset();
    }

    void reset() noexcept
    {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        readPosL   = 0.0f;
        readPosR   = 0.0f;
        lfoPhase   = 0.0f;
        lofiAccumL = 0.0f;
        lofiAccumR = 0.0f;
    }

    void setParameter(int index, float value) noexcept
    {
        const float v = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0:
                pitchParam = v;
                break;
            case 1:
                lofiParam = v;
                break;
            case 2:
                driveParam = v;
                break;
            case 3:
                wowParam = v;
                break;
            case 4:
                mix = v;
                break;
            default:
                break;
        }
    }

    [[nodiscard]] float getParameter(int index) const noexcept
    {
        switch (index)
        {
            case 0:
                return pitchParam;
            case 1:
                return lofiParam;
            case 2:
                return driveParam;
            case 3:
                return wowParam;
            case 4:
                return mix;
            default:
                return 0.0f;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        if (bufL.empty())
        {
            for (int s = 0; s < numSamples; ++s)
            {
                outL[s] = inL[s];
                outR[s] = inR[s];
            }
            return;
        }

        const float pitchRatio = std::pow(2.0f, (pitchParam - 0.5f) * 2.0f);
        const float lfoInc     = (0.5f + wowParam * 3.0f) / static_cast<float>(sampleRate);
        const float wowDepth   = wowParam * 0.02f;

        int srFactor = 1;
        if (lofiParam > 0.3f)
            srFactor = 1 + static_cast<int>((lofiParam - 0.3f) * 10.0f);

        const float maskF = static_cast<float>(bufMask);

        for (int s = 0; s < numSamples; ++s)
        {
            const float xL = inL[s];
            const float xR = inR[s];

            const int writeIdx = static_cast<int>(readPosL) & bufMask;
            bufL[writeIdx]     = xL;
            bufR[writeIdx]     = xR;

            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0f)
                lfoPhase -= 1.0f;
            const float wow          = std::sin(6.283185307179586f * lfoPhase) * wowDepth;
            const float currentRatio = pitchRatio + wow;

            readPosL += currentRatio;
            readPosR += currentRatio;

            if (readPosL < 0.0f) readPosL += maskF;
            if (readPosL >= maskF) readPosL -= maskF;
            if (readPosR < 0.0f) readPosR += maskF;
            if (readPosR >= maskF) readPosR -= maskF;

            const int readIdxL = static_cast<int>(readPosL) & bufMask;
            const int nextIdxL = (readIdxL + 1) & bufMask;
            const float fracL  = readPosL - static_cast<float>(static_cast<int>(readPosL));
            const float pitchL = bufL[readIdxL] * (1.0f - fracL) + bufL[nextIdxL] * fracL;

            const int readIdxR = static_cast<int>(readPosR) & bufMask;
            const int nextIdxR = (readIdxR + 1) & bufMask;
            const float fracR  = readPosR - static_cast<float>(static_cast<int>(readPosR));
            const float pitchR = bufR[readIdxR] * (1.0f - fracR) + bufR[nextIdxR] * fracR;

            float srL, srR;
            if (srFactor > 1)
            {
                if (s % srFactor == 0)
                {
                    lofiAccumL = pitchL;
                    lofiAccumR = pitchR;
                }
                srL = lofiAccumL;
                srR = lofiAccumR;
            }
            else
            {
                srL = pitchL;
                srR = pitchR;
            }

            srL = lofiQuantize(srL);
            srR = lofiQuantize(srR);

            const float wetL = driveShape(srL);
            const float wetR = driveShape(srR);

            outL[s] = xL * (1.0f - mix) + wetL * mix;
            outR[s] = xR * (1.0f - mix) + wetR * mix;
        }
    }

private:
    [[nodiscard]] float driveShape(float x) const noexcept
    {
        const float g   = 1.0f + driveParam * 6.0f;
        const float den = std::tanh(g);
        if (std::abs(den) < 1e-6f)
            return x;
        return std::tanh(x * g) / den;
    }

    [[nodiscard]] float lofiQuantize(float x) const noexcept
    {
        if (lofiParam < 0.01f)
            return x;
        const float bits   = 24.0f - lofiParam * 21.0f;
        const float levels = std::pow(2.0f, bits);
        return std::round(x * levels) / levels;
    }

    double sampleRate = 44100.0;
    float pitchParam  = 0.5f;
    float lofiParam   = 0.0f;
    float driveParam  = 0.0f;
    float wowParam    = 0.0f;
    float mix         = 0.4f;

    float readPosL   = 0.0f;
    float readPosR   = 0.0f;
    float lfoPhase   = 0.0f;
    float lofiAccumL = 0.0f;
    float lofiAccumR = 0.0f;

    std::vector<float> bufL;
    std::vector<float> bufR;
    int bufMask = 0;
};

} // namespace abd::dsp
