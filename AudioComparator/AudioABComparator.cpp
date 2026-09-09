#include "AudioABComparator.h"
#include <cmath>

namespace abd::audio
{


// ============================================================================
// Helper estático: validación de valores finitos
// ============================================================================
static bool isBufferFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const float* readPtr = buffer.getReadPointer (channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (std::isnan (readPtr[i]) || std::isinf (readPtr[i]))
                return false;
        }
    }
    return true;
}

// ============================================================================
// toVar — Serializa el resultado de comparaci�n a JSON var
// ============================================================================
juce::var AudioABComparatorResult::toVar() const
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("run_id", runId);
    obj->setProperty ("comparator_version", comparatorVersion);
    obj->setProperty ("status", status);
    obj->setProperty ("reason_code", reasonCode);

    juce::DynamicObject::Ptr inp = new juce::DynamicObject();
    inp->setProperty ("sample_rate", input.sampleRate);
    inp->setProperty ("num_channels_reference", input.numChannelsReference);
    inp->setProperty ("num_channels_capture", input.numChannelsCapture);
    inp->setProperty ("original_samples_reference", (int)input.originalSamplesReference);
    inp->setProperty ("original_samples_capture", (int)input.originalSamplesCapture);
    inp->setProperty ("analyzed_samples", (int)input.analyzedSamples);
    inp->setProperty ("channels_mode", input.channelsMode);
    inp->setProperty ("trimmed_samples_reference", (int)input.trimmedSamplesReference);
    inp->setProperty ("trimmed_samples_capture", (int)input.trimmedSamplesCapture);
    obj->setProperty ("input", juce::var (inp.get()));

    juce::DynamicObject::Ptr align = new juce::DynamicObject();
    align->setProperty ("applied", alignment.applied);
    align->setProperty ("sample_offset", alignment.sampleOffset);
    align->setProperty ("time_offset_ms", alignment.timeOffsetMs);
    align->setProperty ("correlation_peak", alignment.correlationPeak);
    align->setProperty ("overlap_ratio", alignment.overlapRatio);
    obj->setProperty ("alignment", juce::var (align.get()));

    juce::DynamicObject::Ptr t = new juce::DynamicObject();
    t->setProperty ("peak_ref_dbfs", time.peakRefDbfs);
    t->setProperty ("peak_cap_dbfs", time.peakCapDbfs);
    t->setProperty ("peak_delta_db", time.peakDeltaDb);
    t->setProperty ("rms_ref_dbfs", time.rmsRefDbfs);
    t->setProperty ("rms_cap_dbfs", time.rmsCapDbfs);
    t->setProperty ("rms_delta_db", time.rmsDeltaDb);
    t->setProperty ("residual_rms_dbfs", time.residualRmsDbfs);
    t->setProperty ("residual_peak_dbfs", time.residualPeakDbfs);
    t->setProperty ("mae", time.mae);
    t->setProperty ("rmse", time.rmse);
    obj->setProperty ("time_metrics", juce::var (t.get()));

    juce::DynamicObject::Ptr s = new juce::DynamicObject();
    s->setProperty ("log_mag_mean_abs_diff_db", spectral.logMagMeanAbsDiffDb);
    s->setProperty ("spectral_centroid_delta_hz", spectral.spectralCentroidDeltaHz);
    s->setProperty ("spectral_flatness_delta", spectral.spectralFlatnessDelta);
    s->setProperty ("low_band_delta_db", spectral.lowBandDeltaDb);
    s->setProperty ("mid_band_delta_db", spectral.midBandDeltaDb);
    s->setProperty ("high_band_delta_db", spectral.highBandDeltaDb);
    obj->setProperty ("spectral_metrics", juce::var (s.get()));

    juce::Array<juce::var> warnArr;
    for (const auto& w : warnings) warnArr.add (w);
    obj->setProperty ("warnings", warnArr);

    juce::Array<juce::var> errArr;
    for (const auto& e : errors) errArr.add (e);
    obj->setProperty ("errors", errArr);

    return juce::var (obj.get());
}

// ============================================================================
// compare — Orquestador principal del pipeline de comparaci�n
// ============================================================================
AudioABComparatorResult AudioABComparator::compare (const AudioABSignal& reference,
                                                    const AudioABSignal& capture,
                                                    const AudioABRunContext& context,
                                                    const AudioABComparatorConfig& config)
{
    AudioABComparatorResult result;
    result.runId = context.runId;

    // 1. Validaciones iniciales contractuales
    if (reference.buffer.getNumSamples() == 0 || capture.buffer.getNumSamples() == 0)
    {
        result.status = "error";
        result.reasonCode = "EMPTY_BUFFER";
        result.errors.add ("Uno o ambos buffers de audio est�n vac�os.");
        return result;
    }

    if (reference.sampleRate != capture.sampleRate)
    {
        result.status = "error";
        result.reasonCode = "SR_MISMATCH";
        result.errors.add ("Frecuencias de muestreo incompatibles: " + juce::String (reference.sampleRate) + " vs " + juce::String (capture.sampleRate));
        return result;
    }

    if (!isBufferFinite (reference.buffer) || !isBufferFinite (capture.buffer))
    {
        result.status = "error";
        result.reasonCode = "NAN_INF_DETECTED";
        result.errors.add ("Se detectaron valores NaN o infinitos en las muestras de audio.");
        return result;
    }

    result.input.sampleRate = reference.sampleRate;
    result.input.numChannelsReference = reference.numChannels;
    result.input.numChannelsCapture = capture.numChannels;
    result.input.originalSamplesReference = reference.buffer.getNumSamples();
    result.input.originalSamplesCapture = capture.buffer.getNumSamples();
    result.input.channelsMode = config.forceMonoForAnalysis ? "mono-analysis" : "stereo-analysis";

    // 2. Preprocesado (trimming y matching mono)
    auto refPrepared = prepareSignal (reference, config, result);
    auto capPrepared = prepareSignal (capture, config, result);

    result.input.trimmedSamplesReference = refPrepared.buffer.getNumSamples();
    result.input.trimmedSamplesCapture = capPrepared.buffer.getNumSamples();

    if (refPrepared.buffer.getNumSamples() == 0 || capPrepared.buffer.getNumSamples() == 0)
    {
        result.status = "error";
        result.reasonCode = "EMPTY_BUFFER";
        result.errors.add ("Las se�ales quedaron vac�as tras aplicar el trim de silencio.");
        return result;
    }

    // 3. Alineaci�n efectiva
    juce::AudioBuffer<float> alignedRef, alignedCap;
    alignSignals (refPrepared, capPrepared, config, result, alignedRef, alignedCap);

    result.input.analyzedSamples = alignedRef.getNumSamples();

    if (alignedRef.getNumSamples() == 0)
    {
        result.status = "error";
        result.reasonCode = "EMPTY_BUFFER";
        result.errors.add ("El tramo com�n de an�lisis tras la alineaci�n es cero.");
        return result;
    }

    // 4. M�tricas en el dominio del tiempo
    computeTimeMetrics (alignedRef, alignedCap, result);

    // 5. M�tricas espectrales
    computeSpectralMetrics (alignedRef, alignedCap, config, result);

    return result;
}

// ============================================================================
// computeTimeMetrics — Peak, RMS, MAE, RMSE
// ============================================================================
void AudioABComparator::computeTimeMetrics (const juce::AudioBuffer<float>& ref,
                                             const juce::AudioBuffer<float>& cap,
                                             AudioABComparatorResult& result)
{
    const float* refPtr = ref.getReadPointer (0);
    const float* capPtr = cap.getReadPointer (0);
    const int N = ref.getNumSamples();

    double maxRef = 0.0, maxCap = 0.0;
    double energyRef = 0.0, energyCap = 0.0;
    double absoluteErrorSum = 0.0, squaredErrorSum = 0.0;
    double residualMax = 0.0, residualEnergy = 0.0;

    for (int i = 0; i < N; ++i)
    {
        double r = (double)refPtr[i];
        double c = (double)capPtr[i];

        maxRef = std::max (maxRef, std::abs (r));
        maxCap = std::max (maxCap, std::abs (c));

        energyRef += r * r;
        energyCap += c * c;

        double diff = r - c;
        absoluteErrorSum += std::abs (diff);
        squaredErrorSum += diff * diff;

        residualMax = std::max (residualMax, std::abs (diff));
        residualEnergy += diff * diff;
    }

    result.time.peakRefDbfs = maxRef > 0.0 ? 20.0 * std::log10 (maxRef) : -100.0;
    result.time.peakCapDbfs = maxCap > 0.0 ? 20.0 * std::log10 (maxCap) : -100.0;
    result.time.peakDeltaDb = std::abs (result.time.peakRefDbfs - result.time.peakCapDbfs);

    double rmsRef = std::sqrt (energyRef / (double)N);
    double rmsCap = std::sqrt (energyCap / (double)N);
    result.time.rmsRefDbfs = rmsRef > 0.0 ? 20.0 * std::log10 (rmsRef) : -100.0;
    result.time.rmsCapDbfs = rmsCap > 0.0 ? 20.0 * std::log10 (rmsCap) : -100.0;
    result.time.rmsDeltaDb = std::abs (result.time.rmsRefDbfs - result.time.rmsCapDbfs);

    double residualRms = std::sqrt (residualEnergy / (double)N);
    result.time.residualRmsDbfs = residualRms > 0.0 ? 20.0 * std::log10 (residualRms) : -100.0;
    result.time.residualPeakDbfs = residualMax > 0.0 ? 20.0 * std::log10 (residualMax) : -100.0;

    result.time.mae = absoluteErrorSum / (double)N;
    result.time.rmse = std::sqrt (squaredErrorSum / (double)N);
}

} // namespace abd::audio
