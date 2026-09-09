/**
 * @file VoiceDispersionModel.h
 * @brief Stochastic component tolerance and thermal drift dispersion model for analog polyphonic voices.
 * @author ABDSynths
 * @date 2026
 *
 * Implements deterministic Gaussian pseudo-random component tolerances
 * (VCF cutoff trim spread, resonance Q variance, VCA gain offset, tracking deviation)
 * and slow thermal brownian motion pitch wandering for polyphonic synths.
 */

#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <random>
#include <cmath>

namespace abd::lutdsp
{

template <size_t NumVoices = 8>
class VoiceDispersionModel
{
public:
    struct VoiceOffset
    {
        float cutoffOffsetNorm = 0.0f;   // Static offset in normalized [0, 1] parameter space
        float resonanceOffset = 0.0f;    // Offset in resonance Q factor
        float vcaGainMultiplier = 1.0f;  // Component gain tolerance (e.g. 1.0 +- 0.024)
        float trackingOffsetCents = 0.0f;// Octave tracking slope error
        float dynamicThermalPitchCents = 0.0f; // Wandering thermal pitch drift
    };

    VoiceDispersionModel()
    {
        reseed(106); // Default deterministic Roland Juno-106 seed
    }

    /**
     * @brief Generates reproducible static tolerances per voice based on a seed.
     * @param seed Random seed for deterministic reproducibility.
     * @param vcfSpreadNorm Maximum spread of VCF cutoff (typical ~0.02 to 0.05).
     * @param vcaSpreadRatio Maximum VCA gain tolerance (typical ~0.024 = +-2.4%).
     * @param trackingSpreadCents Maximum tracking spread (typical ~10 cents/oct).
     */
    void reseed(uint32_t seed, 
                float vcfSpreadNorm = 0.035f, 
                float vcaSpreadRatio = 0.024f, 
                float trackingSpreadCents = 10.0f)
    {
        std::mt19937 rng(seed);
        std::normal_distribution<float> dist(0.0f, 1.0f);

        for (size_t v = 0; v < NumVoices; ++v)
        {
            // Truncated normal distribution (+-2 sigma)
            float vcfRnd = juce::jlimit(-2.0f, 2.0f, dist(rng)) * 0.5f;
            float vcaRnd = juce::jlimit(-2.0f, 2.0f, dist(rng)) * 0.5f;
            float trackRnd = juce::jlimit(-2.0f, 2.0f, dist(rng)) * 0.5f;

            offsets[v].cutoffOffsetNorm = vcfRnd * vcfSpreadNorm;
            offsets[v].resonanceOffset = vcfRnd * 0.05f;
            offsets[v].vcaGainMultiplier = 1.0f + (vcaRnd * vcaSpreadRatio);
            offsets[v].trackingOffsetCents = trackRnd * trackingSpreadCents;
            offsets[v].dynamicThermalPitchCents = 0.0f;
            thermalWalkTarget[v] = 0.0f;
        }
    }

    /**
     * @brief Steps thermal drift random walk (call periodically, e.g. every audio block).
     * @param maxDriftCents Maximum wandering amplitude in cents (typical +-2.0 to 3.0).
     * @param migrationRate Speed of wandering (typical ~0.001 to 0.005).
     */
    void stepThermalDrift(float maxDriftCents = 2.5f, float migrationRate = 0.002f) noexcept
    {
        for (size_t v = 0; v < NumVoices; ++v)
        {
            // If near target, pick a new random target within [-maxDriftCents, maxDriftCents]
            if (std::abs(offsets[v].dynamicThermalPitchCents - thermalWalkTarget[v]) < 0.05f)
            {
                float unitRnd = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
                thermalWalkTarget[v] = (unitRnd * 2.0f - 1.0f) * maxDriftCents;
            }

            // Smooth migration towards target (exponential decay / 1st order lag)
            offsets[v].dynamicThermalPitchCents += migrationRate * (thermalWalkTarget[v] - offsets[v].dynamicThermalPitchCents);
        }
    }

    /**
     * @brief Applies cutoff dispersion to a nominal [0, 1] parameter value.
     */
    float applyCutoffDispersion(size_t voiceIndex, float nominalCutoff) const noexcept
    {
        if (voiceIndex >= NumVoices) return nominalCutoff;
        return juce::jlimit(0.0f, 1.0f, nominalCutoff + offsets[voiceIndex].cutoffOffsetNorm);
    }

    /**
     * @brief Applies VCA gain tolerance multiplier to an audio sample.
     */
    float applyVcaDispersion(size_t voiceIndex, float rawAudio) const noexcept
    {
        if (voiceIndex >= NumVoices) return rawAudio;
        return rawAudio * offsets[voiceIndex].vcaGainMultiplier;
    }

    /**
     * @brief Computes frequency multiplier for voice pitch incorporating thermal drift and tracking error.
     */
    float getVoicePitchRatio(size_t voiceIndex, int midiNote) const noexcept
    {
        if (voiceIndex >= NumVoices) return 1.0f;

        float octaveOffset = static_cast<float>(midiNote - 60) / 12.0f;
        float totalCents = offsets[voiceIndex].dynamicThermalPitchCents + (octaveOffset * offsets[voiceIndex].trackingOffsetCents);
        
        return std::pow(2.0f, totalCents / 1200.0f);
    }

    const VoiceOffset& getVoiceOffset(size_t voiceIndex) const noexcept
    {
        static const VoiceOffset defaultOffset;
        if (voiceIndex >= NumVoices) return defaultOffset;
        return offsets[voiceIndex];
    }

private:
    std::array<VoiceOffset, NumVoices> offsets;
    std::array<float, NumVoices> thermalWalkTarget;
};

} // namespace abd::lutdsp