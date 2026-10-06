#pragma once

#include <cstddef>
#include <cmath>
#include <string>
#include <string_view>
#include <array>
#include <algorithm>

namespace abd::scope {

struct TriggerResult {
    size_t triggerIndex { 0 };
    float triggerFraction { 0.0f };
    float estimatedFrequencyHz { 0.0f };
    std::string noteName;
};

/**
 * Pure C++ Zero-Crossing Detector with Adaptive Hysteresis, Sub-Bass Tracking (< 140 Hz) & Sub-Sample Lock.
 *
 * NOTE: process() builds a std::string note name and must run off the audio thread
 * (message / UI thread only).
 */
class TriggerDetector final {
public:
    /**
     * Hysteresis in normalized amplitude. Pass <= 0 (default) to enable peak-scaled
     * adaptive hysteresis: clamp(peak * 0.12, 0.005, 0.15). Pass a positive value to
     * force a fixed threshold.
     */
    static constexpr float AUTO_HYSTERESIS = 0.0f;

    static TriggerResult process(
        const float* samples,
        size_t numSamples,
        float sampleRate,
        float hysteresis = AUTO_HYSTERESIS)
    {
        TriggerResult result;
        if (samples == nullptr || numSamples < 16) return result;

        // Compute signal peak amplitude for adaptive hysteresis in sub-bass / low dynamics
        float peak = 0.0f;
        const size_t scanLimit = std::min(numSamples, size_t(2048));
        for (size_t i = 0; i < scanLimit; ++i) {
            const float a = std::fabs(samples[i]);
            if (a > peak) peak = a;
        }

        if (peak < 0.005f) return result; // Silence

        const float effectiveHysteresis = (hysteresis > 0.0f)
            ? hysteresis
            : std::clamp(peak * 0.12f, 0.005f, 0.15f);

        bool isArmed = false;
        size_t firstTrigger = 0;
        size_t secondTrigger = 0;
        size_t triggerCount = 0;

        const size_t maxSearch = std::min(numSamples - 1, size_t(4096));

        for (size_t i = 1; i < maxSearch; ++i) {
            const float prev = samples[i - 1];
            const float curr = samples[i];

            // 1. Arm trigger when signal goes below negative hysteresis threshold
            if (!isArmed) {
                if (curr < -effectiveHysteresis) {
                    isArmed = true;
                }
            } else {
                // 2. Fire on rising zero-crossing
                if (prev <= 0.0f && curr > 0.0f) {
                    const float dy = curr - prev;
                    const float frac = dy > 1e-5f ? (-prev / dy) : 0.0f;

                    if (triggerCount == 0) {
                        firstTrigger = i;
                        result.triggerIndex = i;
                        result.triggerFraction = std::clamp(frac, 0.0f, 1.0f);
                    } else if (triggerCount == 1) {
                        secondTrigger = i;
                    }
                    triggerCount++;
                    isArmed = false;

                    // Stop early if we have found two consecutive cycle points
                    if (triggerCount >= 2 && i > (numSamples / 2)) {
                        break;
                    }
                }
            }
        }

        // 3. Estimate fundamental frequency
        if (triggerCount >= 2 && secondTrigger > firstTrigger && sampleRate > 0.0f) {
            const size_t periodSamples = secondTrigger - firstTrigger;
            if (periodSamples > 1) {
                const float freq = sampleRate / static_cast<float>(periodSamples);
                if (freq >= 18.0f && freq <= 22000.0f) {
                    result.estimatedFrequencyHz = freq;
                    result.noteName = frequencyToNoteName(result.estimatedFrequencyHz);
                }
            }
        }

        return result;
    }

    /**
     * Convert frequency to a MIDI note name with octave (e.g. 440 Hz -> "A4", 55 Hz -> "A1").
     * Returns an empty string when out of range.
     */
    static std::string frequencyToNoteName(float freqHz)
    {
        if (freqHz < 16.0f || freqHz > 20000.0f) return "";

        static constexpr std::array<std::string_view, 12> NOTE_NAMES = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };

        const float midiNum = 69.0f + 12.0f * std::log2(freqHz / 440.0f);
        const int roundedMidi = static_cast<int>(std::round(midiNum));
        if (roundedMidi < 0 || roundedMidi > 127) return "";

        const size_t noteIdx = static_cast<size_t>(((roundedMidi % 12) + 12) % 12);
        const int octave = (roundedMidi / 12) - 1;
        return std::string(NOTE_NAMES[noteIdx]) + std::to_string(octave);
    }
};

} // namespace abd::scope
