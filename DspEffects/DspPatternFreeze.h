#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace abd::dsp
{

/**
 * @brief DspPatternFreeze - Audio buffer capture and frozen looping processor.
 *
 * Captures audio into a circular freeze buffer (200ms - 4000ms), looping
 * and crossfading audio segments to produce infinite ambient drones and pads.
 *
 * 100% C++20 pure DSP engine: zero allocations in process(), RT-safe.
 */
class DspPatternFreeze
{
public:
    static constexpr int kMaxBuffer = 384000; // ~4s at 96kHz, >8s at 44.1kHz

    DspPatternFreeze()
    {
        freezeBufL.assign(kMaxBuffer + 1, 0.0f);
        freezeBufR.assign(kMaxBuffer + 1, 0.0f);
        bufferMask = kMaxBuffer;
        reset();
    }

    void prepare(double newSampleRate)
    {
        sampleRate = std::max(1.0, newSampleRate);
        const int required = std::max(kMaxBuffer, static_cast<int>(std::ceil(sampleRate * 4.2)) + 64);
        if (static_cast<int>(freezeBufL.size()) < required + 1)
        {
            freezeBufL.assign(required + 1, 0.0f);
            freezeBufR.assign(required + 1, 0.0f);
        }
        reset();
    }

    void reset() noexcept
    {
        std::fill(freezeBufL.begin(), freezeBufL.end(), 0.0f);
        std::fill(freezeBufR.begin(), freezeBufR.end(), 0.0f);
        writePos = 0;
        readPos = 0;
        xfadeCount = 0;
        xfadeLength = 0;
        noiseSeed = 0x13572468u;
    }

    void setParameter(int index, float value) noexcept
    {
        const float v = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0: paramMix = v; break;
            case 1: paramLength = v; break;
            case 2: paramFeedback = v; break;
            case 3: paramRegenerate = v; break;
            default: break;
        }
    }

    [[nodiscard]] float getParameter(int index) const noexcept
    {
        switch (index)
        {
            case 0: return paramMix;
            case 1: return paramLength;
            case 2: return paramFeedback;
            case 3: return paramRegenerate;
            default: return 0.0f;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float bufLenMs = 200.0f + paramLength * 3800.0f;
        const int maxAvail = static_cast<int>(freezeBufL.size()) - 1;
        const int bufLen = std::clamp(static_cast<int>(bufLenMs * static_cast<float>(sampleRate) * 0.001f),
                                      1, maxAvail);
        const int bufMask = bufLen - 1;

        const float fb = paramFeedback * 0.95f;
        const float regen = paramRegenerate;
        const int xfadeLen = std::clamp(static_cast<int>(regen * 2048.0f), 0, 2048);

        for (int i = 0; i < numSamples; ++i)
        {
            const float inL_s = inL[i];
            const float inR_s = inR[i];

            const int wIdx = writePos & bufMask;
            const float writeL = inL_s + freezeBufL[wIdx] * fb;
            const float writeR = inR_s + freezeBufR[wIdx] * fb;
            freezeBufL[wIdx] = writeL + noiseGenerate();
            freezeBufR[wIdx] = writeR + noiseGenerate();

            const int rIdx = readPos & bufMask;
            float readSampleL = freezeBufL[rIdx];
            float readSampleR = freezeBufR[rIdx];

            if (xfadeLen > 0)
            {
                const int loopEnd = writePos;
                const int dist = (readPos - loopEnd + bufMask + 1) & bufMask;
                if (dist < xfadeLen)
                {
                    const float xf = static_cast<float>(dist) / static_cast<float>(xfadeLen);
                    const int prevIdx = (readPos - bufLen + bufMask + 1) & bufMask;
                    readSampleL = readSampleL * (1.0f - xf) + freezeBufL[prevIdx] * xf;
                    readSampleR = readSampleR * (1.0f - xf) + freezeBufR[prevIdx] * xf;
                }
            }

            writePos++;
            readPos++;

            if (readPos - writePos >= bufLen)
                readPos = writePos - bufLen;

            outL[i] = inL_s * (1.0f - paramMix) + readSampleL * paramMix;
            outR[i] = inR_s * (1.0f - paramMix) + readSampleR * paramMix;
        }
    }

private:
    float noiseGenerate() noexcept
    {
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(noiseSeed)) * (1.0f / 2147483648.0f) * 0.005f;
    }

    double sampleRate = 44100.0;
    float paramMix = 0.4f;
    float paramLength = 0.3f;
    float paramFeedback = 0.6f;
    float paramRegenerate = 0.5f;

    std::vector<float> freezeBufL;
    std::vector<float> freezeBufR;
    int bufferMask = 0;
    int writePos = 0;
    int readPos = 0;

    int xfadeCount = 0;
    int xfadeLength = 0;
    uint32_t noiseSeed = 0x13572468u;
};

} // namespace abd::dsp
