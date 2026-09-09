// ==============================================================================
// ABDAudioLab — Auto-Generated Hardware Profile Look-Up Table
// Target Hardware : Roland Juno-106
// Target Module   : IR3109 4-Pole OTA Low-Pass Filter
// Operator Mode   : AUTOMATIC_ROLAND_SYSEX
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

inline constexpr size_t roland_juno106_ir3109_vcf_SIZE = 32;

inline const alignas(16) AbdBatchedPoint roland_juno106_ir3109_vcf[roland_juno106_ir3109_vcf_SIZE] = {
    { 0.00000f, 0.00000f, 20.00000f, 0.30000f, 0.70700f, 0.00530f, 0.24000f, 0.0f }, // roland_juno106_ir3109_vcf_pt1 [Cutoff: 0%, Resonance: 0%]
    { 0.14286f, 0.00000f, 53.65392f, 0.80481f, 0.70700f, 0.00530f, 0.24000f, 0.0f }, // roland_juno106_ir3109_vcf_pt2 [Cutoff: 14%, Resonance: 0%]
    { 0.28571f, 0.00000f, 143.93715f, 2.15906f, 0.70700f, 0.00530f, 0.24000f, 0.0f }, // roland_juno106_ir3109_vcf_pt3 [Cutoff: 29%, Resonance: 0%]
    { 0.42857f, 0.00000f, 386.13956f, 5.79209f, 0.70700f, 0.00530f, 0.24000f, 0.0f }, // roland_juno106_ir3109_vcf_pt4 [Cutoff: 43%, Resonance: 0%]
    { 0.57143f, 0.00000f, 1035.89502f, 15.53842f, 0.70700f, 0.00530f, 0.24000f, 0.0f }, // roland_juno106_ir3109_vcf_pt5 [Cutoff: 57%, Resonance: 0%]
    { 0.71429f, 0.00000f, 2778.99146f, 41.68487f, 0.70700f, 0.00530f, 0.24000f, 0.0f }, // roland_juno106_ir3109_vcf_pt6 [Cutoff: 71%, Resonance: 0%]
    { 0.85714f, 0.00000f, 7455.18799f, 111.82782f, 0.70700f, 0.00530f, 0.24000f, 0.0f }, // roland_juno106_ir3109_vcf_pt7 [Cutoff: 86%, Resonance: 0%]
    { 1.00000f, 0.00000f, 20000.00000f, 300.00000f, 0.70700f, 0.00530f, 0.24000f, 0.0f }, // roland_juno106_ir3109_vcf_pt8 [Cutoff: 100%, Resonance: 0%]
    { 0.00000f, 0.33333f, 20.00000f, 0.30000f, 2.62922f, 0.01972f, 2.42785f, 0.0f }, // roland_juno106_ir3109_vcf_pt9 [Cutoff: 0%, Resonance: 33%]
    { 0.14286f, 0.33333f, 53.65392f, 0.80481f, 2.62922f, 0.01972f, 2.42785f, 0.0f }, // roland_juno106_ir3109_vcf_pt10 [Cutoff: 14%, Resonance: 33%]
    { 0.28571f, 0.33333f, 143.93715f, 2.15906f, 2.62922f, 0.01972f, 2.42785f, 0.0f }, // roland_juno106_ir3109_vcf_pt11 [Cutoff: 29%, Resonance: 33%]
    { 0.42857f, 0.33333f, 386.13956f, 5.79209f, 2.62922f, 0.01972f, 2.42785f, 0.0f }, // roland_juno106_ir3109_vcf_pt12 [Cutoff: 43%, Resonance: 33%]
    { 0.57143f, 0.33333f, 1035.89502f, 15.53842f, 2.62922f, 0.01972f, 2.42785f, 0.0f }, // roland_juno106_ir3109_vcf_pt13 [Cutoff: 57%, Resonance: 33%]
    { 0.71429f, 0.33333f, 2778.99146f, 41.68487f, 2.62922f, 0.01972f, 2.42785f, 0.0f }, // roland_juno106_ir3109_vcf_pt14 [Cutoff: 71%, Resonance: 33%]
    { 0.85714f, 0.33333f, 7455.18799f, 111.82782f, 2.62922f, 0.01972f, 2.42785f, 0.0f }, // roland_juno106_ir3109_vcf_pt15 [Cutoff: 86%, Resonance: 33%]
    { 1.00000f, 0.33333f, 20000.00000f, 300.00000f, 2.62922f, 0.01972f, 2.42785f, 0.0f }, // roland_juno106_ir3109_vcf_pt16 [Cutoff: 100%, Resonance: 33%]
    { 0.00000f, 0.66667f, 20.00000f, 0.30000f, 8.39589f, 0.06297f, 3.38629f, 0.0f }, // roland_juno106_ir3109_vcf_pt17 [Cutoff: 0%, Resonance: 67%]
    { 0.14286f, 0.66667f, 53.65392f, 0.80481f, 8.39589f, 0.06297f, 3.38629f, 0.0f }, // roland_juno106_ir3109_vcf_pt18 [Cutoff: 14%, Resonance: 67%]
    { 0.28571f, 0.66667f, 143.93715f, 2.15906f, 8.39589f, 0.06297f, 3.38629f, 0.0f }, // roland_juno106_ir3109_vcf_pt19 [Cutoff: 29%, Resonance: 67%]
    { 0.42857f, 0.66667f, 386.13956f, 5.79209f, 8.39589f, 0.06297f, 3.38629f, 0.0f }, // roland_juno106_ir3109_vcf_pt20 [Cutoff: 43%, Resonance: 67%]
    { 0.57143f, 0.66667f, 1035.89502f, 15.53842f, 8.39589f, 0.06297f, 3.38629f, 0.0f }, // roland_juno106_ir3109_vcf_pt21 [Cutoff: 57%, Resonance: 67%]
    { 0.71429f, 0.66667f, 2778.99146f, 41.68487f, 8.39589f, 0.06297f, 3.38629f, 0.0f }, // roland_juno106_ir3109_vcf_pt22 [Cutoff: 71%, Resonance: 67%]
    { 0.85714f, 0.66667f, 7455.18799f, 111.82782f, 8.39589f, 0.06297f, 3.38629f, 0.0f }, // roland_juno106_ir3109_vcf_pt23 [Cutoff: 86%, Resonance: 67%]
    { 1.00000f, 0.66667f, 20000.00000f, 300.00000f, 8.39589f, 0.06297f, 3.38629f, 0.0f }, // roland_juno106_ir3109_vcf_pt24 [Cutoff: 100%, Resonance: 67%]
    { 0.00000f, 1.00000f, 20.00000f, 0.30000f, 18.00700f, 0.13505f, 3.65510f, 0.0f }, // roland_juno106_ir3109_vcf_pt25 [Cutoff: 0%, Resonance: 100%]
    { 0.14286f, 1.00000f, 53.65392f, 0.80481f, 18.00700f, 0.13505f, 3.65510f, 0.0f }, // roland_juno106_ir3109_vcf_pt26 [Cutoff: 14%, Resonance: 100%]
    { 0.28571f, 1.00000f, 143.93715f, 2.15906f, 18.00700f, 0.13505f, 3.65510f, 0.0f }, // roland_juno106_ir3109_vcf_pt27 [Cutoff: 29%, Resonance: 100%]
    { 0.42857f, 1.00000f, 386.13956f, 5.79209f, 18.00700f, 0.13505f, 3.65510f, 0.0f }, // roland_juno106_ir3109_vcf_pt28 [Cutoff: 43%, Resonance: 100%]
    { 0.57143f, 1.00000f, 1035.89502f, 15.53842f, 18.00700f, 0.13505f, 3.65510f, 0.0f }, // roland_juno106_ir3109_vcf_pt29 [Cutoff: 57%, Resonance: 100%]
    { 0.71429f, 1.00000f, 2778.99146f, 41.68487f, 18.00700f, 0.13505f, 3.65510f, 0.0f }, // roland_juno106_ir3109_vcf_pt30 [Cutoff: 71%, Resonance: 100%]
    { 0.85714f, 1.00000f, 7455.18799f, 111.82782f, 18.00700f, 0.13505f, 3.65510f, 0.0f }, // roland_juno106_ir3109_vcf_pt31 [Cutoff: 86%, Resonance: 100%]
    { 1.00000f, 1.00000f, 20000.00000f, 300.00000f, 18.00700f, 0.13505f, 3.65510f, 0.0f } // roland_juno106_ir3109_vcf_pt32 [Cutoff: 100%, Resonance: 100%]
};

} // namespace abd::lutdsp::models

// Backwards compatibility alias
namespace abdaudiolab::lut
{
    using AbdBatchedPoint = abd::lutdsp::AbdBatchedPoint;
    using namespace abd::lutdsp::models;
} // namespace abdaudiolab::lut
