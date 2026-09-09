#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace abd::audio
{



struct AudioABSignal
{
    juce::String sourceId;              // "reference" | "capture"
    double sampleRate = 44100.0;
    int numChannels = 2;
    juce::AudioBuffer<float> buffer;
    int64_t originalNumSamples = 0;
    juce::String filePath;
};

struct AudioABRunContext
{
    juce::String runId;
    juce::String presetId;
    juce::String presetName;
    juce::String manifestVersion = "2.0.0";
    juce::String engineVersion = "1.0.0";
    int blockSize = 0;
    double sampleRate = 44100.0;
    juce::String captureMode = "offline";
};

struct AudioABComparatorConfig
{
    bool trimLeadingSilence = true;
    bool trimTrailingSilence = true;
    float silenceThresholdDb = -72.0f;
    bool normalizeGain = false;
    bool forceMonoForAnalysis = true;
    bool enableCrossCorrelation = true;
    int maxAlignmentOffsetSamples = 8192;
    int fftSize = 1024;
};

struct AudioABComparatorResult
{
    juce::String runId;
    juce::String comparatorVersion = "1.0.0";
    juce::String status = "ok";         // "ok" | "error"
    juce::String reasonCode = "SUCCESS"; // "SUCCESS" | "WAV_MISSING" | "SR_MISMATCH" | "NAN_INF_DETECTED" | "EMPTY_BUFFER"

    struct InputSummary {
        double sampleRate = 0.0;
        int numChannelsReference = 0;
        int numChannelsCapture = 0;
        int64_t originalSamplesReference = 0;
        int64_t originalSamplesCapture = 0;
        int64_t analyzedSamples = 0;
        juce::String channelsMode = "mono-analysis";
        int64_t trimmedSamplesReference = 0;
        int64_t trimmedSamplesCapture = 0;
    } input;

    struct Alignment {
        bool applied = false;
        int sampleOffset = 0;           // offset de capture respecto a reference
        double timeOffsetMs = 0.0;
        double correlationPeak = 0.0;
        double overlapRatio = 0.0;
    } alignment;

    struct TimeMetrics {
        double peakRefDbfs = -100.0;
        double peakCapDbfs = -100.0;
        double peakDeltaDb = 0.0;
        double rmsRefDbfs = -100.0;
        double rmsCapDbfs = -100.0;
        double rmsDeltaDb = 0.0;
        double residualRmsDbfs = -100.0;
        double residualPeakDbfs = -100.0;
        double mae = 0.0;
        double rmse = 0.0;
    } time;

    struct SpectralMetrics {
        double logMagMeanAbsDiffDb = 0.0;
        double spectralCentroidDeltaHz = 0.0;
        double spectralFlatnessDelta = 0.0;
        double lowBandDeltaDb = 0.0;
        double midBandDeltaDb = 0.0;
        double highBandDeltaDb = 0.0;
    } spectral;

    juce::StringArray warnings;
    juce::StringArray errors;

    juce::var toVar() const;
};

class AudioABComparator
{
public:
    AudioABComparator() = default;
    ~AudioABComparator() = default;

    AudioABComparatorResult compare (const AudioABSignal& reference,
                                     const AudioABSignal& capture,
                                     const AudioABRunContext& context,
                                     const AudioABComparatorConfig& config);

private:
    struct PreparedSignal
    {
        juce::AudioBuffer<float> buffer;
        int64_t trimmedOffset = 0;
    };

    PreparedSignal prepareSignal (const AudioABSignal& signal, const AudioABComparatorConfig& config, AudioABComparatorResult& result);
    void alignSignals (const PreparedSignal& ref, const PreparedSignal& cap, const AudioABComparatorConfig& config, AudioABComparatorResult& result, juce::AudioBuffer<float>& alignedRef, juce::AudioBuffer<float>& alignedCap);
    void computeTimeMetrics (const juce::AudioBuffer<float>& ref, const juce::AudioBuffer<float>& cap, AudioABComparatorResult& result);
    void computeSpectralMetrics (const juce::AudioBuffer<float>& ref, const juce::AudioBuffer<float>& cap, const AudioABComparatorConfig& config, AudioABComparatorResult& result);
};

} // namespace abd::audio
