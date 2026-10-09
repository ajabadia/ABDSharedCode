#pragma once
#include <functional>

namespace abd::dsp
{
// ============================================================
// VCF voicing calibration
// Central home for every parameter constant that shapes the ZDF
// ladder's sound. Values are measured against the reference
// Juno-106 voice (ABDJUNiO601) at 44.1 kHz; they are sample-rate
// agnostic (no 44100 appears in any computation).
// ============================================================
namespace VcfCalibration
{
// Max normalized cutoff (frq = cutoffHz / Nyquist). Hard clamp at
// process() entry: keeps std::tan stable. 0.907 corresponds to the
// hardware's 20 kHz cutoff ceiling at the lowest common DAW sample
// rate (44.1 kHz: 20000/22050 = 0.90697) and stays just below the
// fastTan Taylor knee (frq·π/2 = 0.907·1.5708 = 1.4247 < 1.425).
constexpr float kMaxNormalizedFreq = 0.907f;

// Output gain: aligns the unity-gain ZDF core with the reference
// Juno-106 voice's acoustic output level.
constexpr float kOutputScale = 3.22f;

// Self-oscillation region reference. The resonance fader maps to
// feedback k via a single voicing curve (resonanceCurve → ResK_J106).
// ResK_J106 reaches the ladder's self-oscillation threshold (k=4) at
// res≈0.855, and SoftClipK (knee 4.0) leaves k unclipped up to that
// point, so the filter begins to self-oscillate right at the hardware
// onset — fader ~220/255 = 0.863.
constexpr float kSelfOscThreshold = 0.86f;
constexpr float kSelfOscIntensity = 1.0f;

// High-resonance output taming: with both cutoff open and resonance
// near self-oscillation the final gain is reduced by up to
// kOutTameMaxReduction (-6 dB) so screaming patches do not blast the
// voice bus.
constexpr float kOutTameFreqFloor    = 0.02f; // frq below this: full gain
constexpr float kOutTameFreqSlope    = 20.0f; // reduction ramp per unit frq
constexpr float kOutTameResStart     = 2.0f;  // k below this: full gain
constexpr float kOutTameResSlope     = 0.5f;  // reduction ramp per unit k
constexpr float kOutTameMaxReduction = 0.5f;  // -6 dB max reduction

// Adaptive input noise (masks zipper artifacts): the noise peak is
// scaled by 1 / (oversample * (1 + energy * kNoiseEnergyGain)).
constexpr float kNoiseLevelBase  = 1.0e-2f;
constexpr float kNoiseEnergyGain = 1000.0f;

// Input-energy envelope time constant (seconds); drives the adaptive
// noise level. SR-agnostic (used as tau in exp(-1/(tau*sr*os))).
constexpr float kInputEnvTauSec = 0.022f;

// DeepMind voicing defaults.
// Resonance curve must be the single res→k mapping and reach k≈4 at
// res≈0.86 (self-oscillation onset). ResK_J106 already does: 4.04 at
// 0.86, 4.19 at 1.0. A scale < 1.0 would push the peak below the
// self-osc threshold, so it is 1.0 (DeepMind keeps its distinct tone
// through gainCompCurve + stageSaturationAmount instead).
constexpr float kDeepMindResCurveScale    = 1.0f; // resonance-curve multiplier
constexpr float kDeepMindGainCompStrength = 2.0f; // 1 / (1 + a*r^2) gain comp
constexpr float kDeepMindStageSaturation  = 0.7f; // OTA per-stage tanh drive
} // namespace VcfCalibration

struct VcfVoicing
{
    // Curva de resonancia: mapea el valor unipolar 0..1 del fader al feedback K del solver
    std::function<float(float)> resonanceCurve;

    // Compensación de ganancia: escala el nivel final de salida en función del nivel de resonancia
    std::function<float(float)> gainCompCurve;

    // Factor de saturación por etapa: escala de distorsión analógica (std::tanh)
    float stageSaturationAmount = 1.0f;
};
} // namespace abd::dsp
