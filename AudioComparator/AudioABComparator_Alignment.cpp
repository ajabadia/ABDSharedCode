#include "AudioABComparator.h"
#include <cmath>

namespace abd::audio
{


// ============================================================================
// Helpers est�ticos para preprocesado de se�ales
// ============================================================================

static void mixToMono (const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output)
{
    output.setSize (1, input.getNumSamples());
    output.clear();

    if (input.getNumChannels() == 1)
    {
        output.copyFrom (0, 0, input, 0, 0, input.getNumSamples());
    }
    else if (input.getNumChannels() > 1)
    {
        float invChannels = 1.0f / (float)input.getNumChannels();
        for (int channel = 0; channel < input.getNumChannels(); ++channel)
        {
            output.addFrom (0, 0, input, channel, 0, input.getNumSamples(), invChannels);
        }
    }
}

static int64_t findSilenceBoundary (const juce::AudioBuffer<float>& monoBuffer, float thresholdDb, bool leading)
{
    const float* readPtr = monoBuffer.getReadPointer (0);
    const int numSamples = monoBuffer.getNumSamples();
    const float thresholdLinear = std::pow (10.0f, thresholdDb / 20.0f);

    if (leading)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (std::abs (readPtr[i]) >= thresholdLinear)
                return i;
        }
        return numSamples;
    }
    else
    {
        for (int i = numSamples - 1; i >= 0; --i)
        {
            if (std::abs (readPtr[i]) >= thresholdLinear)
                return i + 1;
        }
        return 0;
    }
}

// ============================================================================
// prepareSignal — Trim de silencio + mezcla a mono
// ============================================================================
AudioABComparator::PreparedSignal AudioABComparator::prepareSignal (const AudioABSignal& signal,
                                                                     const AudioABComparatorConfig& config,
                                                                     AudioABComparatorResult& /*result*/)
{
    juce::AudioBuffer<float> monoBuffer;
    mixToMono (signal.buffer, monoBuffer);

    int64_t leadingSilence = 0;
    int64_t trailingSilence = monoBuffer.getNumSamples();

    if (config.trimLeadingSilence)
        leadingSilence = findSilenceBoundary (monoBuffer, config.silenceThresholdDb, true);

    if (config.trimTrailingSilence)
        trailingSilence = findSilenceBoundary (monoBuffer, config.silenceThresholdDb, false);

    int64_t trimmedSamples = trailingSilence - leadingSilence;
    PreparedSignal prepared;
    if (trimmedSamples > 0)
    {
        prepared.buffer.setSize (1, (int)trimmedSamples);
        prepared.buffer.copyFrom (0, 0, monoBuffer, 0, (int)leadingSilence, (int)trimmedSamples);
        prepared.trimmedOffset = leadingSilence;
    }
    else
    {
        prepared.buffer.setSize (1, 0);
        prepared.trimmedOffset = 0;
    }
    return prepared;
}

// ============================================================================
// alignSignals — Correlaci�n cruzada para alinear reference y capture
// ============================================================================
void AudioABComparator::alignSignals (const PreparedSignal& ref,
                                      const PreparedSignal& cap,
                                      const AudioABComparatorConfig& config,
                                      AudioABComparatorResult& result,
                                      juce::AudioBuffer<float>& alignedRef,
                                      juce::AudioBuffer<float>& alignedCap)
{
    int offset = 0;
    double maxCorr = 0.0;

    const int N_ref = ref.buffer.getNumSamples();
    const int N_cap = cap.buffer.getNumSamples();

    if (config.enableCrossCorrelation)
    {
        int limit = std::min (config.maxAlignmentOffsetSamples, std::min (N_ref, N_cap) / 2);
        if (limit < 128) limit = std::min (N_ref, N_cap) - 1;

        double refEnergy = 0.0;
        const float* refPtr = ref.buffer.getReadPointer (0);
        for (int i = 0; i < N_ref; ++i) refEnergy += (double)(refPtr[i] * refPtr[i]);

        double capEnergy = 0.0;
        const float* capPtr = cap.buffer.getReadPointer (0);
        for (int i = 0; i < N_cap; ++i) capEnergy += (double)(capPtr[i] * capPtr[i]);

        double normFactor = std::sqrt (refEnergy * capEnergy);
        if (normFactor < 1e-9) normFactor = 1.0;

        for (int shift = -limit; shift <= limit; ++shift)
        {
            double sum = 0.0;
            int start_ref = std::max (0, -shift);
            int start_cap = std::max (0, shift);
            int len = std::min (N_ref - start_ref, N_cap - start_cap);
            if (len <= 0) continue;

            for (int k = 0; k < len; ++k)
                sum += (double)(refPtr[start_ref + k] * capPtr[start_cap + k]);

            double corr = sum / normFactor;
            if (corr > maxCorr)
            {
                maxCorr = corr;
                offset = shift;
            }
        }

        result.alignment.applied = true;
        result.alignment.sampleOffset = offset;
        result.alignment.timeOffsetMs = ((double)offset / result.input.sampleRate) * 1000.0;
        result.alignment.correlationPeak = maxCorr;
    }
    else
    {
        result.alignment.applied = false;
        result.alignment.sampleOffset = 0;
        result.alignment.timeOffsetMs = 0.0;
        result.alignment.correlationPeak = 1.0;
    }

    // Recortar al tramo com�n final alineado
    int start_ref = std::max (0, -offset);
    int start_cap = std::max (0, offset);
    int commonLength = std::min (N_ref - start_ref, N_cap - start_cap);

    if (commonLength > 0)
    {
        alignedRef.setSize (1, commonLength);
        alignedRef.copyFrom (0, 0, ref.buffer, 0, start_ref, commonLength);

        alignedCap.setSize (1, commonLength);
        alignedCap.copyFrom (0, 0, cap.buffer, 0, start_cap, commonLength);

        double totalOriginal = (double)std::max (N_ref, N_cap);
        result.alignment.overlapRatio = (double)commonLength / (totalOriginal > 0.0 ? totalOriginal : 1.0);
    }
    else
    {
        alignedRef.setSize (1, 0);
        alignedCap.setSize (1, 0);
        result.alignment.overlapRatio = 0.0;
    }
}

} // namespace abd::audio
