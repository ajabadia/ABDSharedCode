#include "AudioABVerdictEngine.h"
#include <cmath>

namespace abd::audio
{


juce::var AudioABVerdictResult::toVar() const
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("level", level);
    obj->setProperty ("reason_code", reasonCode);

    juce::Array<juce::var> rules;
    for (const auto& r : triggeredRules) rules.add (r);
    obj->setProperty ("triggered_rules", rules);

    return juce::var (obj.get());
}

AudioABVerdictResult AudioABVerdictEngine::evaluate (const AudioABComparatorResult& comp,
                                                     const AudioABVerdictTolerances& tol,
                                                     const std::array<uint8_t, 242>& activeParams)
{
    AudioABVerdictResult res;

    // Si la comparación reportó error estructural, el veredicto es fail inmediato
    if (comp.status == "error")
    {
        res.level = "fail";
        res.reasonCode = comp.reasonCode;
        res.triggeredRules.add ("STRUCTURAL_ERROR: " + comp.reasonCode);
        return res;
    }

    // 1. Validar alineación / correlación
    if (comp.alignment.applied)
    {
        if (std::abs (comp.alignment.sampleOffset) > tol.maxSampleOffset)
        {
            res.level = "fail";
            res.reasonCode = "ALIGNMENT_OFFSET_EXCEEDED";
            res.triggeredRules.add ("sampleOffset (" + juce::String (comp.alignment.sampleOffset) + ") > maxSampleOffset (" + juce::String (tol.maxSampleOffset) + ")");
        }

        if (comp.alignment.correlationPeak < tol.minCorrelationPeak)
        {
            res.level = "fail";
            res.reasonCode = "CORRELATION_PEAK_TOO_LOW";
            res.triggeredRules.add ("correlationPeak (" + juce::String (comp.alignment.correlationPeak) + ") < minCorrelationPeak (" + juce::String (tol.minCorrelationPeak) + ")");

            // SUGERENCIA DE CALIBRACIÓN LFO:
            // LFO1 rate está en offset 70 del edit buffer. Si es un patch con modulación LFO activa
            if (activeParams[70] > 0)
            {
                res.triggeredRules.add ("  [SUGERENCIA DE CALIBRACIÓN LFO] -> La correlación de fase baja indica desincronización de LFO. Use el control deslizante para calibrar la constante de LFO.");
            }
        }

        if (comp.alignment.overlapRatio < tol.minOverlapRatio)
        {
            if (res.level != "fail")
            {
                res.level = "warn";
                res.reasonCode = "OVERLAP_RATIO_LOW";
            }
            res.triggeredRules.add ("overlapRatio (" + juce::String (comp.alignment.overlapRatio) + ") < minOverlapRatio (" + juce::String (tol.minOverlapRatio) + ")");
        }
    }

    // 2. Validar delta RMS y picos
    if (std::abs (comp.time.rmsDeltaDb) > tol.maxRmsDeltaDb)
    {
        if (res.level != "fail")
        {
            res.level = "fail";
            res.reasonCode = "RMS_DELTA_EXCEEDED";
        }
        res.triggeredRules.add ("rmsDeltaDb (" + juce::String (comp.time.rmsDeltaDb) + " dB) > maxRmsDeltaDb (" + juce::String (tol.maxRmsDeltaDb) + " dB)");
    }

    // 3. Validar delta espectral por bandas
    if (comp.spectral.logMagMeanAbsDiffDb > tol.maxSpectralDeltaDb)
    {
        if (res.level != "fail")
        {
            res.level = "fail";
            res.reasonCode = "SPECTRAL_DIFF_EXCEEDED";
        }
        res.triggeredRules.add ("logMagMeanAbsDiffDb (" + juce::String (comp.spectral.logMagMeanAbsDiffDb) + " dB) > maxSpectralDeltaDb (" + juce::String (tol.maxSpectralDeltaDb) + " dB)");

        // SUGERENCIAS ESPECTRALES SEGÚN EL PRESSET:
        // Si el filtro Cutoff (offset 39) está activo:
        if (activeParams[39] > 0 && std::abs(comp.spectral.highBandDeltaDb) > tol.maxSpectralDeltaDb)
        {
            res.triggeredRules.add ("  [SUGERENCIA DE CALIBRACIÓN VCF] -> Gran desviación espectral en frecuencias altas. Calibre la constante 'vcfCutoff.minHz' o 'vcfCutoff.curveBase'.");
        }
    }

    if (res.triggeredRules.isEmpty())
    {
        res.level = "pass";
        res.reasonCode = "WITHIN_TOLERANCE";
    }

    return res;
}

} // namespace abd::audio
