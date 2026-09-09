/**
 * @file AnalogLutFilterModule.h
 * @brief Polyphonic 8-voice SIMD receptor module for real-time LUT evaluation.
 * @author ABDSynths
 * @date 2026
 *
 * Consumes alignas(16) AbdBatchedPoint tables produced by ABDAudioLab.
 * Implements branchless AVX2/FMA exponential ballistic parameter smoothing
 * and SIMD vectorized bilinear modulation for polyphonic voice engines.
 */

#pragma once

#include "LutEvaluatorSimd.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace abd::lutdsp
{

class AnalogLutFilterModule
{
public:
    static constexpr int kMaxVoices = 8;

    AnalogLutFilterModule()
    {
        for (int v = 0; v < kMaxVoices; ++v)
        {
            p1Current[v] = 0.0f;
            p2Current[v] = 0.0f;
            p1Target[v]  = 0.0f;
            p2Target[v]  = 0.0f;
        }

        setSmoothingTime(15.0, 44100.0);
    }

    ~AnalogLutFilterModule() = default;

    /**
     * @brief Configures exponential ballistic smoothing time constant.
     */
    void setSmoothingTime(double timeMs, double sampleRate) noexcept
    {
        if (sampleRate > 0.0 && timeMs > 0.0)
        {
            double seconds = timeMs * 0.001;
            smoothingCoef = static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sampleRate)));
        }
    }

    /**
     * @brief Sets target parameters for a specific voice (e.g. Cutoff, Resonance).
     */
    void setVoiceParameters(int voiceIndex, float param1, float param2) noexcept
    {
        if (voiceIndex >= 0 && voiceIndex < kMaxVoices)
        {
            p1Target[voiceIndex] = juce::jlimit(0.0f, 1.0f, param1);
            p2Target[voiceIndex] = juce::jlimit(0.0f, 1.0f, param2);
        }
    }

    /**
     * @brief Immediately snaps current voice parameters to target without smoothing.
     */
    void snapVoiceParameters(int voiceIndex, float param1, float param2) noexcept
    {
        if (voiceIndex >= 0 && voiceIndex < kMaxVoices)
        {
            float c1 = juce::jlimit(0.0f, 1.0f, param1);
            float c2 = juce::jlimit(0.0f, 1.0f, param2);
            p1Target[voiceIndex]  = c1;
            p2Target[voiceIndex]  = c2;
            p1Current[voiceIndex] = c1;
            p2Current[voiceIndex] = c2;
        }
    }

    [[nodiscard]] float getVoiceParam1Current(int voiceIndex) const noexcept
    {
        if (voiceIndex >= 0 && voiceIndex < kMaxVoices)
            return p1Current[voiceIndex];
        return 0.0f;
    }

    [[nodiscard]] float getVoiceParam2Current(int voiceIndex) const noexcept
    {
        if (voiceIndex >= 0 && voiceIndex < kMaxVoices)
            return p2Current[voiceIndex];
        return 0.0f;
    }

    /**
     * @brief Processes a polyphonic audio block modulating voice channels via SIMD.
     * @param voiceOutputs Array of float pointers to audio buffers for each voice.
     * @param numSamples Number of samples in current audio block.
     * @param lut Pointer to contiguous lookup table exported from ABDAudioLab.
     * @param gridSize Dimension of the LUT grid (e.g. 64, 128, 256).
     * @param metric Metric to evaluate (default: PrimaryMean).
     */
    void processPolyphonicBlock(float** voiceOutputs,
                                int numSamples,
                                const AbdBatchedPoint* lut,
                                int gridSize,
                                LutMetric metric = LutMetric::PrimaryMean) noexcept
    {
        if (voiceOutputs == nullptr || lut == nullptr || gridSize < 2 || numSamples <= 0)
            return;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            alignas(32) std::array<float, kMaxVoices> filterGains;

#if defined(__AVX2__)
            // 1. Branchless exponential smoothing for all 8 voices in 256-bit AVX registers
            __m256 coef_vec = _mm256_set1_ps(smoothingCoef);
            __m256 p1_curr  = _mm256_load_ps(p1Current.data());
            __m256 p1_targ  = _mm256_load_ps(p1Target.data());
            __m256 p2_curr  = _mm256_load_ps(p2Current.data());
            __m256 p2_targ  = _mm256_load_ps(p2Target.data());

            // Formula: curr = curr + coef * (targ - curr) via FMA
            p1_curr = _mm256_fmadd_ps(coef_vec, _mm256_sub_ps(p1_targ, p1_curr), p1_curr);
            p2_curr = _mm256_fmadd_ps(coef_vec, _mm256_sub_ps(p2_targ, p2_curr), p2_curr);

            _mm256_store_ps(p1Current.data(), p1_curr);
            _mm256_store_ps(p2Current.data(), p2_curr);

            // 2. Vectorized 8-voice bilinear evaluation
            __m256 res_8v = LutEvaluatorSimd::evaluateBilinear8Voices(p1Current.data(), p2Current.data(), lut, gridSize, metric);
            _mm256_store_ps(filterGains.data(), res_8v);
#else
            // Fallback: 4-voice SSE or scalar
            for (int v = 0; v < kMaxVoices; ++v)
            {
                p1Current[v] += smoothingCoef * (p1Target[v] - p1Current[v]);
                p2Current[v] += smoothingCoef * (p2Target[v] - p2Current[v]);
            }

            __m128 res_0_3 = LutEvaluatorSimd::evaluateBilinear4Voices(p1Current.data(), p2Current.data(), lut, gridSize, metric);
            __m128 res_4_7 = LutEvaluatorSimd::evaluateBilinear4Voices(p1Current.data() + 4, p2Current.data() + 4, lut, gridSize, metric);
            _mm_store_ps(filterGains.data(), res_0_3);
            _mm_store_ps(filterGains.data() + 4, res_4_7);
#endif

            // 3. Modulate voice audio signals sample-by-sample
            for (int v = 0; v < kMaxVoices; ++v)
            {
                if (voiceOutputs[v] != nullptr)
                {
                    voiceOutputs[v][sample] *= filterGains[v];
                }
            }
        }
    }

private:
    float smoothingCoef { 0.01f };

    alignas(32) std::array<float, kMaxVoices> p1Current;
    alignas(32) std::array<float, kMaxVoices> p2Current;
    alignas(32) std::array<float, kMaxVoices> p1Target;
    alignas(32) std::array<float, kMaxVoices> p2Target;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogLutFilterModule)
};

} // namespace abd::lutdsp
