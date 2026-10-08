#include "DriftEngine.h"
#include <algorithm>
#include <cmath>

namespace abd::synth
{
    DriftEngine::DriftEngine()
    {
        // Initialize to silent state — no random offsets.
        // setDriftParams() kicks the oscillators into motion when
        // the user actually turns up drift amount.
        for (auto* osc : { &osc1Pitch, &osc2Pitch, &vcfCutoff, &vcfResonance, &envTime })
        {
            osc->targetValue  = 0.0f;
            osc->currentValue = 0.0f;
            osc->samplesUntilNextTarget = 0.0;
        }
    }

    void DriftEngine::setSampleRate(double newSampleRate)
    {
        sampleRate = std::max(1.0, newSampleRate);

        // Recalculate target intervals for all oscillators at the new sample rate
        double interval = computeTargetInterval();
        for (auto* osc : { &osc1Pitch, &osc2Pitch, &vcfCutoff, &vcfResonance, &envTime })
        {
            osc->targetIntervalSamples = interval;
        }
    }

    void DriftEngine::setDriftParams(float voiceDriftNorm, float paramDriftNorm, float driftRateNorm)
    {
        // voiceDrift: 0-1 → 0 a 50 cents de desviación máxima
        voiceDriftAmount = std::clamp(voiceDriftNorm, 0.0f, 1.0f);
        // paramDrift: 0-1 → 0 a 0.2 de modulación normalizada
        paramDriftAmount = std::clamp(paramDriftNorm, 0.0f, 1.0f);
        // driftRate: 0-1 → 0.01Hz a 10Hz (período de 100s a 0.1s)
        driftRate = std::clamp(driftRateNorm, 0.0f, 1.0f);

        // Recalcular intervalos de target para todos los osciladores
        double interval = computeTargetInterval();
        for (auto* osc : { &osc1Pitch, &osc2Pitch, &vcfCutoff, &vcfResonance, &envTime })
        {
            osc->targetIntervalSamples = interval;
        }
    }

    double DriftEngine::computeTargetInterval() const
    {
        // driftRate mapea:
        //   0.0 → período muy largo (~10 segundos = slow drift)
        //   0.5 → período medio (~1 segundo)
        //   1.0 → período rápido (~0.1 segundos = micro-shimmer)
        double minInterval = sampleRate * 0.05;   // 50ms (rápido)
        double maxInterval = sampleRate * 10.0;   // 10s (lento)
        // Mapeo exponencial para mejor respuesta perceptual
        double t = (double)driftRate;
        double interval = maxInterval * std::pow(minInterval / maxInterval, t);
        return std::max(minInterval, interval);
    }

    void DriftEngine::resetForNote(float /*voiceIndex*/)
    {
        // Drift is a continuous background Brownian process — ONLY re-randomize
        // the timing offset so drift continues smoothly without jumps.
        // Do NOT reset currentValue/targetValue (that would cause audible clicks).
        for (auto* osc : { &osc1Pitch, &osc2Pitch, &vcfCutoff, &vcfResonance, &envTime })
        {
            // nextRandomFloat() returns [-1, +1]; map to [0, 1] and scale
            float r = nextRandomFloat() * 0.5f + 0.5f; // [0, 1]
            osc->samplesUntilNextTarget = osc->targetIntervalSamples * (0.5 + 0.5 * r);
        }
    }

    void DriftEngine::nextSample()
    {
        // Cada oscilador de drift se actualiza una muestra
        updateOscillator(osc1Pitch, voiceDriftAmount, osc1PitchDrift);
        updateOscillator(osc2Pitch, voiceDriftAmount, osc2PitchDrift);
        updateOscillator(vcfCutoff, paramDriftAmount, vcfCutoffDrift);
        updateOscillator(vcfResonance, paramDriftAmount, vcfResonanceDrift);
        updateOscillator(envTime, paramDriftAmount, envTimeDrift);
    }

    void DriftEngine::updateOscillator(DriftOsc& osc, float amplitudeScale, float& output)
    {
        // When amplitudeScale is zero, no drift is requested — hold at zero
        // deterministically.  This makes drift=0 useful for calibration.
        if (amplitudeScale <= 0.0f)
        {
            osc.currentValue = 0.0f;
            osc.targetValue  = 0.0f;
            output = 0.0f;
            return;
        }

        // 1. Decrementar contador de samples hasta el próximo target
        osc.samplesUntilNextTarget -= 1.0;

        if (osc.samplesUntilNextTarget <= 0.0)
        {
            // Elegir nuevo target aleatorio con el LCG local
            osc.pickNewTarget(amplitudeScale, nextRandomFloat());
            // El slew factor determina qué tan rápido llegar al target.
            // Se expresa en Hz (per-second) y se divide por el sampleRate del DAW
            // para obtener el factor per-sample: el comportamiento físico queda
            // invariante entre sample rates.
            double slewBase = (kSlewBasePerSec + driftRate * kSlewRatePerSec) / sampleRate;
            // Variar el slew aleatoriamente en cada nuevo target
            float r1 = nextRandomFloat() * 0.5f + 0.5f; // [0, 1]
            osc.slewFactor = (float)(slewBase * (0.5 + r1));
            // Programar el próximo cambio de target con un poco de jitter
            float r2 = nextRandomFloat() * 0.5f + 0.5f; // [0, 1]
            osc.samplesUntilNextTarget = osc.targetIntervalSamples * (0.8 + 0.4 * r2);
        }

        // 2. Glide suave hacia el target (filtro de 1-polo)
        float diff = osc.targetValue - osc.currentValue;
        osc.currentValue += diff * osc.slewFactor;

        // 3. currentValue already scaled by amplitudeScale in pickNewTarget
        //    (removed double-scaling that produced amplitudeScale² output)

        // 4. Saturación suave para evitar valores extremos
        output = std::tanh(osc.currentValue * 2.0f) / 2.0f;
    }

    float DriftEngine::nextRandomFloat()
    {
        // LCG clásico (misma implementación que LFO::nextRandomFloat)
        driftSeed = driftSeed * 1664525u + 1013904223u;
        return -1.0f + 2.0f * (static_cast<float>(driftSeed & 0x7FFFFFFFu) / 2147483647.0f);
    }
}
