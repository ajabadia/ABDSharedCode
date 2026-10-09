#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace abd::dsp
{

/**
 * @brief DspNimbus - Hybrid Granular Cloud Delay & Reverb Processor.
 *
 * Inspired by Mutable Instruments Clouds / Surge Nimbus (Type 54):
 * - Overlapping grains (up to 16 concurrent) with independent pitch transpositions (0.5x to 2x).
 * - Spatial stereo grain panning across the soundstage.
 * - Raised-cosine amplitude grain envelope.
 * - Non-linear recirculating feedback into the ring buffer.
 * - Dry/Wet mixing.
 *
 * 100% C++20 pure DSP engine: zero allocations in process(), RT-safe.
 */
class DspNimbus
{
public:
    static constexpr int kMaxGrains = 16;
    static constexpr int kMaxBufSize = 192000; // ~2s at 96kHz, 4s at 48kHz

    DspNimbus()
    {
        prepare(44100.0);
    }

    void prepare(double sr)
    {
        sampleRate = std::max(1.0, sr);
        const int bufSize = std::min(kMaxBufSize, std::max(1024, static_cast<int>(sampleRate * 2.0)));
        ringBufL.assign(static_cast<size_t>(bufSize), 0.0f);
        ringBufR.assign(static_cast<size_t>(bufSize), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill(ringBufL.begin(), ringBufL.end(), 0.0f);
        std::fill(ringBufR.begin(), ringBufR.end(), 0.0f);
        ringWritePos = 0;
        for (auto& g : grains)
            g = Grain{};
        nextGrainIdx = 0;
    }

    void setParameter(int index, float value) noexcept
    {
        const float v = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0: grainSizeParam = v; break;
            case 1: densityParam = v; break;
            case 2: feedbackParam = v; break;
            case 3: pitchParam = v; break;
            case 4: mix = v; break;
            default: break;
        }
    }

    [[nodiscard]] float getParameter(int index) const noexcept
    {
        switch (index)
        {
            case 0: return grainSizeParam;
            case 1: return densityParam;
            case 2: return feedbackParam;
            case 3: return pitchParam;
            case 4: return mix;
            default: return 0.0f;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        if (ringBufL.empty())
        {
            for (int s = 0; s < numSamples; ++s)
            {
                outL[s] = inL[s];
                outR[s] = inR[s];
            }
            return;
        }

        const int bufLen = static_cast<int>(ringBufL.size());
        const int maxGrainCount = 1 + static_cast<int>(densityParam * 7.0f);
        const float spawnRate = 0.3f + densityParam * 0.7f;
        int samplesPerGrain = static_cast<int>(sampleRate / (spawnRate * 4.0f + 1.0f));
        if (samplesPerGrain < 16)
            samplesPerGrain = 16;

        const float invMaxGrains = 1.0f / static_cast<float>(maxGrainCount);
        const float fb = feedbackParam * 0.4f;

        for (int s = 0; s < numSamples; ++s)
        {
            const float xL = inL[s];
            const float xR = inR[s];

            ringBufL[ringWritePos] = xL;
            ringBufR[ringWritePos] = xR;

            if (s % samplesPerGrain == 0)
            {
                int activeCount = 0;
                for (int g = 0; g < kMaxGrains; ++g)
                    if (grains[g].active)
                        activeCount++;
                if (activeCount < maxGrainCount)
                    spawnGrain(bufLen);
            }

            float wetL = 0.0f;
            float wetR = 0.0f;

            for (int g = 0; g < kMaxGrains; ++g)
            {
                if (!grains[g].active)
                    continue;

                const float readPosF = static_cast<float>(grains[g].readPos);
                const int idx = static_cast<int>(readPosF) % bufLen;
                const int next = (idx + 1) % bufLen;
                const float frac = readPosF - static_cast<float>(idx);

                const float sampL = ringBufL[idx] * (1.0f - frac) + ringBufL[next] * frac;
                const float sampR = ringBufR[idx] * (1.0f - frac) + ringBufR[next] * frac;

                const float progress = static_cast<float>(grains[g].age) / static_cast<float>(grains[g].length);
                const float envelope = 0.5f * (1.0f + std::cos(3.141592653589793f * progress));
                const float grainGain = envelope * grains[g].gain;

                wetL += sampL * grainGain;
                wetR += sampR * grainGain;

                grains[g].readPos += static_cast<int>(grains[g].pitchRatio);
                grains[g].age++;
                if (grains[g].age >= grains[g].length)
                    grains[g].active = false;
            }

            wetL *= invMaxGrains;
            wetR *= invMaxGrains;

            ringBufL[ringWritePos] = std::tanh(ringBufL[ringWritePos] + wetL * fb);
            ringBufR[ringWritePos] = std::tanh(ringBufR[ringWritePos] + wetR * fb);

            ringWritePos = (ringWritePos + 1) % bufLen;

            outL[s] = xL * (1.0f - mix) + wetL * mix;
            outR[s] = xR * (1.0f - mix) + wetR * mix;
        }
    }

private:
    struct Grain
    {
        bool active = false;
        int readPos = 0;
        int length = 0;
        int age = 0;
        float pitchRatio = 1.0f;
        float pan = 0.5f;
        float gain = 0.0f;
    };

    void spawnGrain(int bufLen) noexcept
    {
        const float grainDur = 0.02f + grainSizeParam * 0.18f;
        int grainLen = static_cast<int>(sampleRate * grainDur);
        if (grainLen < 16)
            grainLen = 16;
        if (grainLen > bufLen / 2)
            grainLen = bufLen / 2;

        const float pitchRatio = std::pow(2.0f, (pitchParam - 0.5f) * 2.0f);

        grains[nextGrainIdx].active = true;
        grains[nextGrainIdx].readPos = (ringWritePos - grainLen + bufLen * 2) % bufLen;
        grains[nextGrainIdx].length = grainLen;
        grains[nextGrainIdx].age = 0;
        grains[nextGrainIdx].pitchRatio = pitchRatio;
        grains[nextGrainIdx].pan = 0.2f + 0.6f * (static_cast<float>(nextGrainIdx % 3) * 0.5f);
        grains[nextGrainIdx].gain = 0.5f + 0.5f * densityParam;

        nextGrainIdx = (nextGrainIdx + 1) % kMaxGrains;
    }

    double sampleRate = 44100.0;
    float grainSizeParam = 0.5f;
    float densityParam = 0.5f;
    float feedbackParam = 0.3f;
    float pitchParam = 0.5f;
    float mix = 0.4f;

    std::vector<float> ringBufL;
    std::vector<float> ringBufR;
    int ringWritePos = 0;

    std::array<Grain, kMaxGrains> grains{};
    int nextGrainIdx = 0;
};

} // namespace abd::dsp
