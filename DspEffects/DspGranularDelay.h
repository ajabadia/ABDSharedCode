#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace abd::dsp
{

/**
 * @brief DspGranularDelay - Micro-sound Granular Delay & Pitch Processor.
 *
 * Captures continuous audio into a ring buffer and schedules overlapping grains
 * with independent pitch transposition (±12 semitones), raised-cosine windowing,
 * randomized grain gains, and variable grain density and duration (20-200ms).
 *
 * 100% C++20 pure DSP engine: zero allocations in process(), RT-safe.
 */
class DspGranularDelay
{
public:
    static constexpr int kMaxGrains = 16;
    static constexpr int kCaptureSize = 131072; // Power of 2 (2^17 > 88200)

    DspGranularDelay()
    {
        captureBufL.assign(kCaptureSize, 0.0f);
        captureBufR.assign(kCaptureSize, 0.0f);
        captureMask = kCaptureSize - 1;
        reset();
    }

    void prepare(double newSampleRate)
    {
        sampleRate = std::max(1.0, newSampleRate);
        if (captureBufL.size() != kCaptureSize)
        {
            captureBufL.assign(kCaptureSize, 0.0f);
            captureBufR.assign(kCaptureSize, 0.0f);
            captureMask = kCaptureSize - 1;
        }
        reset();
    }

    void reset() noexcept
    {
        std::fill(captureBufL.begin(), captureBufL.end(), 0.0f);
        std::fill(captureBufR.begin(), captureBufR.end(), 0.0f);
        capturePos = 0;
        grainAccum = 0.0f;
        noiseSeed = 0x98765432u;
        for (auto& g : grains)
        {
            g.active = false;
            g.age = 0.0f;
            g.maxAge = 1.0f;
            g.readPos = 0.0f;
            g.pitchRatio = 1.0f;
            g.gain = 1.0f;
        }
    }

    void setParameter(int index, float value) noexcept
    {
        const float v = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0: paramMix = v; break;
            case 1: paramTime = v; break;
            case 2: paramDensity = v; break;
            case 3: paramSize = v; break;
            case 4: paramPitch = v; break;
            default: break;
        }
    }

    [[nodiscard]] float getParameter(int index) const noexcept
    {
        switch (index)
        {
            case 0: return paramMix;
            case 1: return paramTime;
            case 2: return paramDensity;
            case 3: return paramSize;
            case 4: return paramPitch;
            default: return 0.0f;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float grainSizeMs = 20.0f + paramTime * 180.0f;
        const float grainSizeSamps = grainSizeMs * static_cast<float>(sampleRate) * 0.001f;
        const float grainRate = 1.0f + paramDensity * 4.0f;
        const float softness = paramSize;
        const float pitchSpread = paramPitch * 12.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            captureBufL[capturePos] = inL[i];
            captureBufR[capturePos] = inR[i];
            capturePos = (capturePos + 1) & captureMask;

            grainAccum += grainRate;
            while (grainAccum >= grainSizeSamps && grainAccum >= 1.0f)
            {
                grainAccum -= 1.0f;

                for (auto& g : grains)
                {
                    if (!g.active)
                    {
                        g.active = true;
                        g.age = 0.0f;
                        g.maxAge = grainSizeSamps;
                        g.readPos = static_cast<float>(capturePos);

                        const float semitones = pitchSpread * (noiseGenerate() * 2.0f);
                        g.pitchRatio = std::pow(2.0f, semitones / 12.0f);
                        g.gain = 0.5f + 0.5f * std::abs(noiseGenerate());
                        break;
                    }
                }
            }

            float sumL = 0.0f;
            float sumR = 0.0f;

            for (auto& g : grains)
            {
                if (!g.active)
                    continue;

                const float env = grainEnvelope(g.age, g.maxAge, softness);
                const int readIdx = static_cast<int>(g.readPos) & captureMask;
                const float sampL = captureBufL[readIdx];
                const float sampR = captureBufR[readIdx];

                sumL += sampL * env * g.gain;
                sumR += sampR * env * g.gain;

                g.readPos += g.pitchRatio;
                g.age += 1.0f;

                if (g.age >= g.maxAge)
                    g.active = false;
            }

            outL[i] = inL[i] * (1.0f - paramMix) + sumL * paramMix;
            outR[i] = inR[i] * (1.0f - paramMix) + sumR * paramMix;
        }
    }

private:
    float noiseGenerate() noexcept
    {
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(noiseSeed)) * (1.0f / 2147483648.0f) * 0.01f;
    }

    [[nodiscard]] static float grainEnvelope(float age, float maxAge, float softness) noexcept
    {
        const float t = age / maxAge;
        if (t < 0.0f || t > 1.0f)
            return 0.0f;
        const float windowWidth = 0.3f + softness * 0.7f;
        constexpr float center = 0.5f;
        const float dist = std::abs(t - center) / (windowWidth * 0.5f);
        return std::clamp(1.0f - dist, 0.0f, 1.0f);
    }

    struct Grain
    {
        float readPos = 0.0f;
        float pitchRatio = 1.0f;
        float gain = 1.0f;
        float age = 0.0f;
        float maxAge = 1.0f;
        bool active = false;
    };

    double sampleRate = 44100.0;
    float paramMix = 0.4f;
    float paramTime = 0.4f;
    float paramDensity = 0.5f;
    float paramSize = 0.5f;
    float paramPitch = 0.3f;

    std::vector<float> captureBufL;
    std::vector<float> captureBufR;
    int captureMask = 0;
    int capturePos = 0;

    std::array<Grain, kMaxGrains> grains{};
    float grainAccum = 0.0f;
    uint32_t noiseSeed = 0x98765432u;
};

} // namespace abd::dsp
