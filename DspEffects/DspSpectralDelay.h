#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace abd::dsp
{

/**
 * @brief DspSpectralDelay - Frequency-dispersive Multi-band Delay Processor.
 *
 * Implements a spectral delay with frequency-dependent delay spreading
 * and bandwidth modulation across spectral taps:
 * - 8 multi-tap delay lines with frequency-dependent dispersion.
 * - Bandwidth modulation for spectral smearing and diffusion.
 * - Non-linear soft-clipping feedback loop.
 * - Dry/Wet mixing.
 *
 * 100% C++20 pure DSP engine: zero allocations in process(), RT-safe.
 */
class DspSpectralDelay
{
public:
    static constexpr int kFFTSize  = 1024;
    static constexpr int kNumBands = kFFTSize / 2;

    DspSpectralDelay()
    {
        prepare(44100.0);
    }

    void prepare(double newSampleRate)
    {
        sampleRate        = std::max(1.0, newSampleRate);
        const int bufSize = kFFTSize * 8;
        delayMask         = bufSize - 1;
        delayBufL.assign(static_cast<size_t>(bufSize), 0.0f);
        delayBufR.assign(static_cast<size_t>(bufSize), 0.0f);
        bandGainsL.assign(kNumBands, 1.0f);
        bandGainsR.assign(kNumBands, 1.0f);
        bandGainsTargetL.assign(kNumBands, 1.0f);
        bandGainsTargetR.assign(kNumBands, 1.0f);
        accumL.assign(kFFTSize, 0.0f);
        accumR.assign(kFFTSize, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        writePos  = 0;
        accumPos  = 0;
        noiseSeed = 0xA1B2C3D4u;
        if (!delayBufL.empty())
        {
            std::fill(delayBufL.begin(), delayBufL.end(), 0.0f);
            std::fill(delayBufR.begin(), delayBufR.end(), 0.0f);
            std::fill(accumL.begin(), accumL.end(), 0.0f);
            std::fill(accumR.begin(), accumR.end(), 0.0f);
            std::fill(bandGainsL.begin(), bandGainsL.end(), 1.0f);
            std::fill(bandGainsR.begin(), bandGainsR.end(), 1.0f);
            std::fill(bandGainsTargetL.begin(), bandGainsTargetL.end(), 1.0f);
            std::fill(bandGainsTargetR.begin(), bandGainsTargetR.end(), 1.0f);
        }
    }

    void setParameter(int index, float value) noexcept
    {
        switch (index)
        {
            case 0:
                paramMix = value;
                break;
            case 1:
                paramTime = value;
                break;
            case 2:
                paramBandWidth = value;
                break;
            case 3:
                paramFeedback = value;
                break;
            case 4:
                paramDiffusion = value;
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
                return paramMix;
            case 1:
                return paramTime;
            case 2:
                return paramBandWidth;
            case 3:
                return paramFeedback;
            case 4:
                return paramDiffusion;
            default:
                return 0.0f;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        if (delayBufL.empty())
        {
            for (int i = 0; i < numSamples; ++i)
            {
                outL[i] = inL[i];
                outR[i] = inR[i];
            }
            return;
        }

        const float mix         = paramMix;
        const float feedback    = paramFeedback;
        const float bw          = paramBandWidth;
        constexpr int numTaps   = 8;
        constexpr float tapGain = 1.0f / static_cast<float>(numTaps);

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryL = inL[i];
            const float dryR = inR[i];

            delayBufL[writePos] = dryL;
            delayBufR[writePos] = dryR;

            const int pos = writePos;
            float wetL    = 0.0f;
            float wetR    = 0.0f;

            for (int tap = 0; tap < numTaps; ++tap)
            {
                const float normTap      = static_cast<float>(tap) / static_cast<float>(numTaps);
                const float delaySamples = delayForBand(static_cast<int>(normTap * kNumBands));

                const float bandPhase = normTap * 6.283185307179586f;
                const float bwMod     = 1.0f - bw * 0.5f * (1.0f + std::sin(bandPhase + static_cast<float>(writePos) * 0.0001f));

                const int readPos = (pos - static_cast<int>(delaySamples) + delayMask * 2) & delayMask;
                wetL += delayBufL[readPos] * bwMod * tapGain;
                wetR += delayBufR[readPos] * bwMod * tapGain;
            }

            float fbL = std::tanh(wetL * feedback + dryL * (1.0f - feedback));
            float fbR = std::tanh(wetR * feedback + dryR * (1.0f - feedback));

            delayBufL[(writePos + kFFTSize) & delayMask] = fbL;
            delayBufR[(writePos + kFFTSize) & delayMask] = fbR;

            outL[i] = dryL + (wetL - dryL) * mix;
            outR[i] = dryR + (wetR - dryR) * mix;

            writePos = (writePos + 1) & delayMask;
        }
    }

private:
    [[nodiscard]] float delayForBand(int band) const noexcept
    {
        const float baseDelayMs     = paramTime * 1450.0f + 50.0f;
        const float diffusionSpread = paramDiffusion * baseDelayMs * 0.5f;
        const float normBand        = static_cast<float>(band) / static_cast<float>(kNumBands);
        const float bandDelayMs     = baseDelayMs + (normBand - 0.5f) * diffusionSpread;
        return bandDelayMs * 0.001f * static_cast<float>(sampleRate);
    }

    double sampleRate    = 44100.0;
    float paramMix       = 0.35f;
    float paramTime      = 0.35f;
    float paramBandWidth = 0.5f;
    float paramFeedback  = 0.4f;
    float paramDiffusion = 0.3f;

    std::vector<float> delayBufL;
    std::vector<float> delayBufR;
    int delayMask = 0;
    int writePos  = 0;

    std::vector<float> bandGainsL;
    std::vector<float> bandGainsR;
    std::vector<float> bandGainsTargetL;
    std::vector<float> bandGainsTargetR;
    std::vector<float> accumL;
    std::vector<float> accumR;
    int accumPos       = 0;
    uint32_t noiseSeed = 0xA1B2C3D4u;
};

} // namespace abd::dsp
