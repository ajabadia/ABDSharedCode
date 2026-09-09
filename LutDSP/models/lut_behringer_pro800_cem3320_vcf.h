// ==============================================================================
// ABDAudioLab — Auto-Generated Hardware Profile Look-Up Table
// Target Hardware : Behringer PRO-800
// Target Module   : CEM3320 Curtis 4-Pole Low-Pass VCF
// Operator Mode   : AUTOMATIC_MIDI_CC
// Sample Rate     : 96000 Hz
// Total Points    : 32
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

#if __has_include(<LutDSP/LutEvaluatorSimd.h>)
#include <LutDSP/LutEvaluatorSimd.h>
#elif __has_include("dsp/LutEvaluatorSimd.h")
#include "dsp/LutEvaluatorSimd.h"
#else
namespace abd::lutdsp
{
struct alignas(16) AbdBatchedPoint
{
    float p1;         // Parameter 1 (Normalized [0, 1])
    float p2;         // Parameter 2 (Normalized [0, 1])
    float mu;         // Primary Value Mean (Stable control response)
    float sigma;      // Primary Value StdDev (Thermal drift / ACB chaos)
    float sec_mu;     // Secondary Value Mean (e.g. Q factor / sustain)
    float sec_sigma;  // Secondary Value StdDev
    float thd_percent;// Total Harmonic Distortion %
    float reserved;   // 16-byte alignment padding
};
} // namespace abd::lutdsp
#endif

namespace abd::lutdsp::models
{

using AbdBatchedPoint = abd::lutdsp::AbdBatchedPoint;

inline constexpr size_t behringer_pro800_cem3320_vcf_SIZE = 32;

inline const alignas(16) AbdBatchedPoint behringer_pro800_cem3320_vcf[behringer_pro800_cem3320_vcf_SIZE] = {
    { 0.00000f, 0.00000f, 20.00000f, 0.36000f, 0.70700f, 0.00636f, 0.42000f, 0.0f }, // behringer_pro800_cem3320_vcf_pt1 [Cutoff: 0%, Resonance: 0%]
    { 0.14286f, 0.00000f, 53.65392f, 0.96577f, 0.70700f, 0.00636f, 0.42000f, 0.0f }, // behringer_pro800_cem3320_vcf_pt2 [Cutoff: 14%, Resonance: 0%]
    { 0.28571f, 0.00000f, 143.93715f, 2.59087f, 0.70700f, 0.00636f, 0.42000f, 0.0f }, // behringer_pro800_cem3320_vcf_pt3 [Cutoff: 29%, Resonance: 0%]
    { 0.42857f, 0.00000f, 386.13956f, 6.95051f, 0.70700f, 0.00636f, 0.42000f, 0.0f }, // behringer_pro800_cem3320_vcf_pt4 [Cutoff: 43%, Resonance: 0%]
    { 0.57143f, 0.00000f, 1035.89502f, 18.64611f, 0.70700f, 0.00636f, 0.42000f, 0.0f }, // behringer_pro800_cem3320_vcf_pt5 [Cutoff: 57%, Resonance: 0%]
    { 0.71429f, 0.00000f, 2778.99146f, 50.02184f, 0.70700f, 0.00636f, 0.42000f, 0.0f }, // behringer_pro800_cem3320_vcf_pt6 [Cutoff: 71%, Resonance: 0%]
    { 0.85714f, 0.00000f, 7455.18799f, 134.19337f, 0.70700f, 0.00636f, 0.42000f, 0.0f }, // behringer_pro800_cem3320_vcf_pt7 [Cutoff: 86%, Resonance: 0%]
    { 1.00000f, 0.00000f, 20000.00000f, 359.99997f, 0.70700f, 0.00636f, 0.42000f, 0.0f }, // behringer_pro800_cem3320_vcf_pt8 [Cutoff: 100%, Resonance: 0%]
    { 0.00000f, 0.33333f, 20.00000f, 0.36000f, 2.62922f, 0.02366f, 2.60785f, 0.0f }, // behringer_pro800_cem3320_vcf_pt9 [Cutoff: 0%, Resonance: 33%]
    { 0.14286f, 0.33333f, 53.65392f, 0.96577f, 2.62922f, 0.02366f, 2.60785f, 0.0f }, // behringer_pro800_cem3320_vcf_pt10 [Cutoff: 14%, Resonance: 33%]
    { 0.28571f, 0.33333f, 143.93715f, 2.59087f, 2.62922f, 0.02366f, 2.60785f, 0.0f }, // behringer_pro800_cem3320_vcf_pt11 [Cutoff: 29%, Resonance: 33%]
    { 0.42857f, 0.33333f, 386.13956f, 6.95051f, 2.62922f, 0.02366f, 2.60785f, 0.0f }, // behringer_pro800_cem3320_vcf_pt12 [Cutoff: 43%, Resonance: 33%]
    { 0.57143f, 0.33333f, 1035.89502f, 18.64611f, 2.62922f, 0.02366f, 2.60785f, 0.0f }, // behringer_pro800_cem3320_vcf_pt13 [Cutoff: 57%, Resonance: 33%]
    { 0.71429f, 0.33333f, 2778.99146f, 50.02184f, 2.62922f, 0.02366f, 2.60785f, 0.0f }, // behringer_pro800_cem3320_vcf_pt14 [Cutoff: 71%, Resonance: 33%]
    { 0.85714f, 0.33333f, 7455.18799f, 134.19337f, 2.62922f, 0.02366f, 2.60785f, 0.0f }, // behringer_pro800_cem3320_vcf_pt15 [Cutoff: 86%, Resonance: 33%]
    { 1.00000f, 0.33333f, 20000.00000f, 359.99997f, 2.62922f, 0.02366f, 2.60785f, 0.0f }, // behringer_pro800_cem3320_vcf_pt16 [Cutoff: 100%, Resonance: 33%]
    { 0.00000f, 0.66667f, 20.00000f, 0.36000f, 8.39589f, 0.07556f, 3.56629f, 0.0f }, // behringer_pro800_cem3320_vcf_pt17 [Cutoff: 0%, Resonance: 67%]
    { 0.14286f, 0.66667f, 53.65392f, 0.96577f, 8.39589f, 0.07556f, 3.56629f, 0.0f }, // behringer_pro800_cem3320_vcf_pt18 [Cutoff: 14%, Resonance: 67%]
    { 0.28571f, 0.66667f, 143.93715f, 2.59087f, 8.39589f, 0.07556f, 3.56629f, 0.0f }, // behringer_pro800_cem3320_vcf_pt19 [Cutoff: 29%, Resonance: 67%]
    { 0.42857f, 0.66667f, 386.13956f, 6.95051f, 8.39589f, 0.07556f, 3.56629f, 0.0f }, // behringer_pro800_cem3320_vcf_pt20 [Cutoff: 43%, Resonance: 67%]
    { 0.57143f, 0.66667f, 1035.89502f, 18.64611f, 8.39589f, 0.07556f, 3.56629f, 0.0f }, // behringer_pro800_cem3320_vcf_pt21 [Cutoff: 57%, Resonance: 67%]
    { 0.71429f, 0.66667f, 2778.99146f, 50.02184f, 8.39589f, 0.07556f, 3.56629f, 0.0f }, // behringer_pro800_cem3320_vcf_pt22 [Cutoff: 71%, Resonance: 67%]
    { 0.85714f, 0.66667f, 7455.18799f, 134.19337f, 8.39589f, 0.07556f, 3.56629f, 0.0f }, // behringer_pro800_cem3320_vcf_pt23 [Cutoff: 86%, Resonance: 67%]
    { 1.00000f, 0.66667f, 20000.00000f, 359.99997f, 8.39589f, 0.07556f, 3.56629f, 0.0f }, // behringer_pro800_cem3320_vcf_pt24 [Cutoff: 100%, Resonance: 67%]
    { 0.00000f, 1.00000f, 20.00000f, 0.36000f, 18.00700f, 0.16206f, 3.83510f, 0.0f }, // behringer_pro800_cem3320_vcf_pt25 [Cutoff: 0%, Resonance: 100%]
    { 0.14286f, 1.00000f, 53.65392f, 0.96577f, 18.00700f, 0.16206f, 3.83510f, 0.0f }, // behringer_pro800_cem3320_vcf_pt26 [Cutoff: 14%, Resonance: 100%]
    { 0.28571f, 1.00000f, 143.93715f, 2.59087f, 18.00700f, 0.16206f, 3.83510f, 0.0f }, // behringer_pro800_cem3320_vcf_pt27 [Cutoff: 29%, Resonance: 100%]
    { 0.42857f, 1.00000f, 386.13956f, 6.95051f, 18.00700f, 0.16206f, 3.83510f, 0.0f }, // behringer_pro800_cem3320_vcf_pt28 [Cutoff: 43%, Resonance: 100%]
    { 0.57143f, 1.00000f, 1035.89502f, 18.64611f, 18.00700f, 0.16206f, 3.83510f, 0.0f }, // behringer_pro800_cem3320_vcf_pt29 [Cutoff: 57%, Resonance: 100%]
    { 0.71429f, 1.00000f, 2778.99146f, 50.02184f, 18.00700f, 0.16206f, 3.83510f, 0.0f }, // behringer_pro800_cem3320_vcf_pt30 [Cutoff: 71%, Resonance: 100%]
    { 0.85714f, 1.00000f, 7455.18799f, 134.19337f, 18.00700f, 0.16206f, 3.83510f, 0.0f }, // behringer_pro800_cem3320_vcf_pt31 [Cutoff: 86%, Resonance: 100%]
    { 1.00000f, 1.00000f, 20000.00000f, 359.99997f, 18.00700f, 0.16206f, 3.83510f, 0.0f } // behringer_pro800_cem3320_vcf_pt32 [Cutoff: 100%, Resonance: 100%]
};

} // namespace abd::lutdsp::models

// Backwards compatibility alias
namespace abdaudiolab::lut
{
    using AbdBatchedPoint = abd::lutdsp::AbdBatchedPoint;
    using namespace abd::lutdsp::models;
} // namespace abdaudiolab::lut
