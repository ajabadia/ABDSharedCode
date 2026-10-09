#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace abd::dsp
{

/**
 * @brief DspTreemonster - Pitch-tracking Modulated Delay Processor.
 *
 * Inspired by Surge Treemonster (Type 56):
 * - Real-time zero-crossing pitch detector with confidence tracking.
 * - Dynamic modulation switching (pitch-tracked oscillator when confidence > 0.3, LFO fallback).
 * - Variable delay line (5 - 105 ms) with fractional linear interpolation.
 * - Non-linear soft-clipping feedback loop (tanh).
 * - Dry/Wet mixing.
 *
 * 100% C++20 pure DSP engine: zero allocations in process(), RT-safe.
 */
class DspTreemonster
{
public:
    static constexpr int kMaxDelaySamples = 16384; // Power of 2 (>= 4800, accommodates 192kHz)

    DspTreemonster()
    {
        prepare(44100.0);
    }

    void prepare(double sr)
    {
        sampleRate = std::max(1.0, sr);
        int bufSize = 1;
        while (bufSize < kMaxDelaySamples)
            bufSize *= 2;
        delayL.assign(static_cast<size_t>(bufSize), 0.0f);
        delayR.assign(static_cast<size_t>(bufSize), 0.0f);
        delayMask = bufSize - 1;
        reset();
    }

    void reset() noexcept
    {
        std::fill(delayL.begin(), delayL.end(), 0.0f);
        std::fill(delayR.begin(), delayR.end(), 0.0f);
        writePos = 0.0f;
        detectorPhase = 0.0f;
        detectorFreq = 200.0f;
        detectorConfidence = 0.0f;
        prevSample = 0.0f;
        zeroCrossings = 0;
        windowSamples = 0.0f;
        lfoPhase = 0.0f;
    }

    void setParameter(int index, float value) noexcept
    {
        const float v = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0: speedParam = v; break;
            case 1: depthParam = v; break;
            case 2: feedbackParam = v; break;
            case 3: trackingParam = v; break;
            case 4: mix = v; break;
            default: break;
        }
    }

    [[nodiscard]] float getParameter(int index) const noexcept
    {
        switch (index)
        {
            case 0: return speedParam;
            case 1: return depthParam;
            case 2: return feedbackParam;
            case 3: return trackingParam;
            case 4: return mix;
            default: return 0.0f;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        if (delayL.empty())
        {
            for (int s = 0; s < numSamples; ++s)
            {
                outL[s] = inL[s];
                outR[s] = inR[s];
            }
            return;
        }

        const float baseDelay = 5.0f + speedParam * 100.0f;
        const float depth = depthParam * baseDelay * 0.5f;
        const float fbGain = feedbackParam * 0.7f;
        const float lfoInc = (1.0f + speedParam * 5.0f) / static_cast<float>(sampleRate);
        const float maskF = static_cast<float>(delayMask + 1);

        for (int s = 0; s < numSamples; ++s)
        {
            const float xL = inL[s];
            const float xR = inR[s];

            const float mono = (xL + xR) * 0.5f;
            const float detectedFreq = detectPitch(mono);

            float modPhase;
            if (detectorConfidence > 0.3f)
            {
                modPhase = detectorPhase;
                detectorPhase += detectedFreq / static_cast<float>(sampleRate);
                if (detectorPhase >= 1.0f)
                    detectorPhase -= 1.0f;
            }
            else
            {
                modPhase = lfoPhase;
                lfoPhase += lfoInc;
                if (lfoPhase >= 1.0f)
                    lfoPhase -= 1.0f;
            }

            const float modAmount = std::sin(6.283185307179586f * modPhase) * depth;
            const float delayTime = baseDelay + modAmount;
            const float delaySamples = delayTime * 0.001f * static_cast<float>(sampleRate);

            const int wPos = static_cast<int>(writePos) & delayMask;
            delayL[wPos] = xL;
            delayR[wPos] = xR;

            float readPos = writePos - delaySamples;
            if (readPos < 0.0f)
                readPos += maskF;
            const int rIdx = static_cast<int>(readPos) & delayMask;
            const int nextIdx = (rIdx + 1) & delayMask;
            const float frac = readPos - static_cast<float>(static_cast<int>(readPos));

            const float delayedL = delayL[rIdx] * (1.0f - frac) + delayL[nextIdx] * frac;
            const float delayedR = delayR[rIdx] * (1.0f - frac) + delayR[nextIdx] * frac;

            delayL[wPos] = std::tanh(delayL[wPos] + delayedL * fbGain);
            delayR[wPos] = std::tanh(delayR[wPos] + delayedR * fbGain);

            writePos += 1.0f;
            if (writePos >= maskF)
                writePos -= maskF;

            outL[s] = xL * (1.0f - mix) + delayedL * mix;
            outR[s] = xR * (1.0f - mix) + delayedR * mix;
        }
    }

private:
    float detectPitch(float sample) noexcept
    {
        const float diff = sample - prevSample;
        prevSample = sample;

        if (diff > 0.0f && sample >= 0.0f && prevSample < 0.0f)
        {
            if (zeroCrossings > 0 && windowSamples > 0.0f)
            {
                const float period = windowSamples / static_cast<float>(zeroCrossings);
                const float freq = static_cast<float>(sampleRate) / period;
                if (freq > 40.0f && freq < 4000.0f)
                {
                    const float alpha = trackingParam * 0.3f;
                    detectorFreq += alpha * (freq - detectorFreq);
                    detectorConfidence = std::min(1.0f, detectorConfidence + 0.1f);
                }
            }
            zeroCrossings = 0;
            windowSamples = 0.0f;
        }

        zeroCrossings++;
        windowSamples += 1.0f;

        if (windowSamples > static_cast<float>(sampleRate) * 0.05f)
        {
            detectorConfidence *= 0.95f;
            zeroCrossings = 0;
            windowSamples = 0.0f;
        }

        return detectorFreq;
    }

    double sampleRate = 44100.0;
    float speedParam = 0.5f;
    float depthParam = 0.5f;
    float feedbackParam = 0.3f;
    float trackingParam = 0.5f;
    float mix = 0.4f;

    std::vector<float> delayL;
    std::vector<float> delayR;
    int delayMask = 0;
    float writePos = 0.0f;

    float detectorPhase = 0.0f;
    float detectorFreq = 200.0f;
    float detectorConfidence = 0.0f;
    float prevSample = 0.0f;
    int zeroCrossings = 0;
    float windowSamples = 0.0f;

    float lfoPhase = 0.0f;
};

} // namespace abd::dsp
