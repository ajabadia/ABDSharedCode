#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "AudioABComparator.h"

namespace abd::audio
{



struct AudioABVerdictTolerances
{
    double maxSampleOffset = 512.0;       // Max offset de muestras permitido
    double minCorrelationPeak = 0.92;      // Correlación normalizada mínima
    double maxRmsDeltaDb = 1.0;            // Delta de volumen RMS permitido en dB
    double maxSpectralDeltaDb = 2.5;       // Diferencia de magnitud de bandas permitida en dB
    double minOverlapRatio = 0.95;         // Proporción común mínima
};

struct AudioABVerdictResult
{
    juce::String level = "pass";          // "pass" | "warn" | "fail"
    juce::String reasonCode = "WITHIN_TOLERANCE";
    juce::StringArray triggeredRules;

    juce::var toVar() const;
};

class AudioABVerdictEngine
{
public:
    AudioABVerdictEngine() = default;
    ~AudioABVerdictEngine() = default;

    AudioABVerdictResult evaluate (const AudioABComparatorResult& comparisonResult,
                                   const AudioABVerdictTolerances& tolerances,
                                   const std::array<uint8_t, 242>& activeParams = {});
};

} // namespace abd::audio
