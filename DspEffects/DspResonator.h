#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace abd::dsp
{

/**
 * @brief DspResonator - Multi-band Tuned Resonator Filter Bank.
 *
 * Implements an 8-stage tuned resonant bandpass filter bank:
 * - Harmonic, Inharmonic (bell ratios), and Stretched string mode models.
 * - Exponential frequency mapping (50 Hz - 5000 Hz).
 * - Q factor resonance and feedback damping.
 * - Dry/Wet mixing.
 *
 * 100% C++20 pure DSP engine: zero heap allocations, 100% RT-safe.
 */
class DspResonator
{
public:
    static constexpr int kNumResonators = 8;

    DspResonator()
    {
        reset();
    }

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(1.0, newSampleRate);
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kNumResonators; ++i)
        {
            resonatorsL[i] = ResonatorState{};
            resonatorsR[i] = ResonatorState{};
        }
    }

    void setParameter(int index, float value) noexcept
    {
        value = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0:
                paramMix = value;
                break;
            case 1:
                paramFrequency = value;
                break;
            case 2:
                paramResonance = value;
                break;
            case 3:
                paramDamping = value;
                break;
            case 4:
                paramMode = value;
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
                return paramFrequency;
            case 2:
                return paramResonance;
            case 3:
                return paramDamping;
            case 4:
                return paramMode;
            default:
                return 0.0f;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float mix      = paramMix;
        const float baseFreq = exponentialMap(paramFrequency, 50.0f, 5000.0f);
        const float q        = 1.0f + paramResonance * 49.0f;
        const float damping  = paramDamping;
        const float mode     = paramMode;

        // Compute modal ratios
        std::array<float, kNumResonators> ratios{};
        for (int i = 0; i < kNumResonators; ++i)
        {
            const float n = static_cast<float>(i + 1);
            if (mode < 0.33f)
            {
                // Harmonic: 1, 2, 3, 4, ...
                ratios[i] = n;
            }
            else if (mode < 0.66f)
            {
                // Inharmonic: bell-like ratios
                static constexpr std::array<float, 8> bellRatios = {
                    1.0f, 2.4f, 3.76f, 5.12f, 6.8f, 8.3f, 10.6f, 12.9f};
                ratios[i] = bellRatios[i];
            }
            else
            {
                // Stretched: harmonic with inharmonicity factor
                const float B = 0.01f * paramResonance;
                ratios[i]     = n * std::sqrt(1.0f + B * n * n);
            }
        }

        // Update filter coefficients
        for (int i = 0; i < kNumResonators; ++i)
        {
            const float freq = baseFreq * ratios[i];
            updateResonator(resonatorsL[i], freq, q, damping);
            updateResonator(resonatorsR[i], freq, q, damping);
        }

        constexpr float gain = 1.0f / static_cast<float>(kNumResonators);

        for (int s = 0; s < numSamples; ++s)
        {
            const float dryL = inL[s];
            const float dryR = inR[s];

            float wetL = 0.0f;
            float wetR = 0.0f;

            for (int i = 0; i < kNumResonators; ++i)
            {
                wetL += processBiquad(resonatorsL[i], dryL) * gain;
                wetR += processBiquad(resonatorsR[i], dryR) * gain;
            }

            outL[s] = dryL + (wetL - dryL) * mix;
            outR[s] = dryR + (wetR - dryR) * mix;
        }
    }

private:
    struct ResonatorState
    {
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
        float freq = 0.0f;
    };

    static float processBiquad(ResonatorState& res, float input) noexcept
    {
        const float y = res.b0 * input + res.b1 * res.x1 + res.b2 * res.x2 - res.a1 * res.y1 - res.a2 * res.y2;
        res.x2        = res.x1;
        res.x1        = input;
        res.y2        = res.y1;
        res.y1        = y;
        return y;
    }

    void updateResonator(ResonatorState& res, float freq, float q, float damping) noexcept
    {
        const float nyq = static_cast<float>(sampleRate) * 0.5f;
        if (freq >= nyq) freq = nyq * 0.95f;
        if (freq < 1.0f) freq = 1.0f;

        const float w0    = 6.283185307179586f * freq / static_cast<float>(sampleRate);
        const float alpha = std::sin(w0) / (2.0f * q);
        const float cosw0 = std::cos(w0);
        const float a0    = 1.0f + alpha;

        res.b0 = alpha / a0;
        res.b1 = 0.0f;
        res.b2 = -alpha / a0;
        res.a1 = -2.0f * cosw0 / a0;
        res.a2 = (1.0f - alpha) / a0;

        const float dampingCoeff = 1.0f - damping * 0.99f;
        res.a1 *= dampingCoeff;
        res.freq = freq;
    }

    static float exponentialMap(float normalized, float minHz, float maxHz) noexcept
    {
        return minHz * std::pow(maxHz / minHz, normalized);
    }

    double sampleRate    = 44100.0;
    float paramMix       = 0.5f;
    float paramFrequency = 0.3f;
    float paramResonance = 0.5f;
    float paramDamping   = 0.3f;
    float paramMode      = 0.0f;

    std::array<ResonatorState, kNumResonators> resonatorsL{};
    std::array<ResonatorState, kNumResonators> resonatorsR{};
};

} // namespace abd::dsp
