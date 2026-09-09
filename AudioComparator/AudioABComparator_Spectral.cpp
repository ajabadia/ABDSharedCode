#include "AudioABComparator.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace abd::audio
{


// ============================================================================
// computeSpectralMetrics — FFT, bandas espectrales, centroid, flatness
// ============================================================================
void AudioABComparator::computeSpectralMetrics (const juce::AudioBuffer<float>& ref,
                                                 const juce::AudioBuffer<float>& cap,
                                                 const AudioABComparatorConfig& config,
                                                 AudioABComparatorResult& result)
{
    const float* refPtr = ref.getReadPointer (0);
    const float* capPtr = cap.getReadPointer (0);
    const int N = ref.getNumSamples();

    const int fftSize = config.fftSize;
    if (N < fftSize)
    {
        result.warnings.add ("La se�al es demasiado corta para calcular m�tricas espectrales.");
        return;
    }

    // Ventana Hanning
    juce::AudioBuffer<float> window (1, fftSize);
    float* winPtr = window.getWritePointer (0);
    for (int i = 0; i < fftSize; ++i)
        winPtr[i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * (float)i / (float)(fftSize - 1)));

    juce::dsp::FFT fft (std::log2 (fftSize));
    const int numBins = fftSize / 2 + 1;

    std::vector<double> avgMagRef (numBins, 0.0);
    std::vector<double> avgMagCap (numBins, 0.0);

    int count = 0;
    for (int start = 0; start <= N - fftSize; start += fftSize / 2) // 50% overlap
    {
        std::vector<float> frameRef (fftSize * 2, 0.0f);
        std::vector<float> frameCap (fftSize * 2, 0.0f);

        for (int i = 0; i < fftSize; ++i)
        {
            frameRef[i] = refPtr[start + i] * winPtr[i];
            frameCap[i] = capPtr[start + i] * winPtr[i];
        }

        fft.performFrequencyOnlyForwardTransform (frameRef.data());
        fft.performFrequencyOnlyForwardTransform (frameCap.data());

        for (int bin = 0; bin < numBins; ++bin)
        {
            avgMagRef[bin] += (double)frameRef[bin];
            avgMagCap[bin] += (double)frameCap[bin];
        }
        count++;
    }

    if (count > 0)
    {
        for (int bin = 0; bin < numBins; ++bin)
        {
            avgMagRef[bin] /= (double)count;
            avgMagCap[bin] /= (double)count;
        }
    }

    // Diferencias acumuladas por bandas
    double logMagDiffSum = 0.0;
    double centroidRef = 0.0, centroidCap = 0.0;
    double sumRef = 0.0, sumCap = 0.0;
    double logSumRef = 0.0, logSumCap = 0.0;
    int validBins = 0;

    double binWidthHz = result.input.sampleRate / (double)fftSize;
    double lowSum = 0.0, midSum = 0.0, highSum = 0.0;
    int lowCount = 0, midCount = 0, highCount = 0;

    for (int bin = 0; bin < numBins; ++bin)
    {
        double freqHz = (double)bin * binWidthHz;
        double refDb = 20.0 * std::log10 (std::max (avgMagRef[bin], 1e-5));
        double capDb = 20.0 * std::log10 (std::max (avgMagCap[bin], 1e-5));

        double diffDb = std::abs (refDb - capDb);
        logMagDiffSum += diffDb;

        centroidRef += freqHz * avgMagRef[bin];
        sumRef += avgMagRef[bin];
        centroidCap += freqHz * avgMagCap[bin];
        sumCap += avgMagCap[bin];

        logSumRef += std::log (std::max (avgMagRef[bin], 1e-12));
        logSumCap += std::log (std::max (avgMagCap[bin], 1e-12));
        validBins++;

        if (freqHz < 200.0)       { lowSum  += diffDb; lowCount++;  }
        else if (freqHz < 2000.0) { midSum  += diffDb; midCount++;  }
        else                      { highSum += diffDb; highCount++; }
    }

    result.spectral.logMagMeanAbsDiffDb = logMagDiffSum / (double)numBins;
    result.spectral.spectralCentroidDeltaHz = std::abs (
        (sumRef > 0.0 ? centroidRef / sumRef : 0.0) - (sumCap > 0.0 ? centroidCap / sumCap : 0.0));

    double arithMeanRef = sumRef / (double)validBins;
    double arithMeanCap = sumCap / (double)validBins;
    double geoMeanRef = std::exp (logSumRef / (double)validBins);
    double geoMeanCap = std::exp (logSumCap / (double)validBins);
    double flatnessRef = (arithMeanRef > 1e-12) ? geoMeanRef / arithMeanRef : 0.0;
    double flatnessCap = (arithMeanCap > 1e-12) ? geoMeanCap / arithMeanCap : 0.0;
    result.spectral.spectralFlatnessDelta = std::abs (flatnessRef - flatnessCap);

    result.spectral.lowBandDeltaDb  = lowCount  > 0 ? lowSum  / (double)lowCount  : 0.0;
    result.spectral.midBandDeltaDb  = midCount  > 0 ? midSum  / (double)midCount  : 0.0;
    result.spectral.highBandDeltaDb = highCount > 0 ? highSum / (double)highCount : 0.0;
}

} // namespace abd::audio
