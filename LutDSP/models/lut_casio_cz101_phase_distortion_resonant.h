// ==============================================================================
// ABDAudioLab — Auto-Generated Hardware Profile Look-Up Table
// Target Hardware : Casio CZ-101
// Target Module   : DCW Resonant Phase Distortion Peak
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

inline constexpr size_t casio_cz101_phase_distortion_resonant_SIZE = 32;

inline const alignas(16) AbdBatchedPoint casio_cz101_phase_distortion_resonant[casio_cz101_phase_distortion_resonant_SIZE] = {
    { 0.00000f, 0.00000f, 20.00000f, 0.10000f, 0.70700f, 0.00177f, 0.35000f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt1 [Cutoff: 0%, Resonance: 0%]
    { 0.14286f, 0.00000f, 53.65392f, 0.26827f, 0.70700f, 0.00177f, 0.35000f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt2 [Cutoff: 14%, Resonance: 0%]
    { 0.28571f, 0.00000f, 143.93715f, 0.71969f, 0.70700f, 0.00177f, 0.35000f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt3 [Cutoff: 29%, Resonance: 0%]
    { 0.42857f, 0.00000f, 386.13956f, 1.93070f, 0.70700f, 0.00177f, 0.35000f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt4 [Cutoff: 43%, Resonance: 0%]
    { 0.57143f, 0.00000f, 1035.89502f, 5.17947f, 0.70700f, 0.00177f, 0.35000f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt5 [Cutoff: 57%, Resonance: 0%]
    { 0.71429f, 0.00000f, 2778.99146f, 13.89496f, 0.70700f, 0.00177f, 0.35000f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt6 [Cutoff: 71%, Resonance: 0%]
    { 0.85714f, 0.00000f, 7455.18799f, 37.27594f, 0.70700f, 0.00177f, 0.35000f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt7 [Cutoff: 86%, Resonance: 0%]
    { 1.00000f, 0.00000f, 20000.00000f, 100.00000f, 0.70700f, 0.00177f, 0.35000f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt8 [Cutoff: 100%, Resonance: 0%]
    { 0.00000f, 0.33333f, 20.00000f, 0.10000f, 2.62922f, 0.00657f, 2.53785f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt9 [Cutoff: 0%, Resonance: 33%]
    { 0.14286f, 0.33333f, 53.65392f, 0.26827f, 2.62922f, 0.00657f, 2.53785f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt10 [Cutoff: 14%, Resonance: 33%]
    { 0.28571f, 0.33333f, 143.93715f, 0.71969f, 2.62922f, 0.00657f, 2.53785f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt11 [Cutoff: 29%, Resonance: 33%]
    { 0.42857f, 0.33333f, 386.13956f, 1.93070f, 2.62922f, 0.00657f, 2.53785f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt12 [Cutoff: 43%, Resonance: 33%]
    { 0.57143f, 0.33333f, 1035.89502f, 5.17947f, 2.62922f, 0.00657f, 2.53785f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt13 [Cutoff: 57%, Resonance: 33%]
    { 0.71429f, 0.33333f, 2778.99146f, 13.89496f, 2.62922f, 0.00657f, 2.53785f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt14 [Cutoff: 71%, Resonance: 33%]
    { 0.85714f, 0.33333f, 7455.18799f, 37.27594f, 2.62922f, 0.00657f, 2.53785f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt15 [Cutoff: 86%, Resonance: 33%]
    { 1.00000f, 0.33333f, 20000.00000f, 100.00000f, 2.62922f, 0.00657f, 2.53785f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt16 [Cutoff: 100%, Resonance: 33%]
    { 0.00000f, 0.66667f, 20.00000f, 0.10000f, 8.39589f, 0.02099f, 3.49629f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt17 [Cutoff: 0%, Resonance: 67%]
    { 0.14286f, 0.66667f, 53.65392f, 0.26827f, 8.39589f, 0.02099f, 3.49629f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt18 [Cutoff: 14%, Resonance: 67%]
    { 0.28571f, 0.66667f, 143.93715f, 0.71969f, 8.39589f, 0.02099f, 3.49629f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt19 [Cutoff: 29%, Resonance: 67%]
    { 0.42857f, 0.66667f, 386.13956f, 1.93070f, 8.39589f, 0.02099f, 3.49629f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt20 [Cutoff: 43%, Resonance: 67%]
    { 0.57143f, 0.66667f, 1035.89502f, 5.17947f, 8.39589f, 0.02099f, 3.49629f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt21 [Cutoff: 57%, Resonance: 67%]
    { 0.71429f, 0.66667f, 2778.99146f, 13.89496f, 8.39589f, 0.02099f, 3.49629f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt22 [Cutoff: 71%, Resonance: 67%]
    { 0.85714f, 0.66667f, 7455.18799f, 37.27594f, 8.39589f, 0.02099f, 3.49629f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt23 [Cutoff: 86%, Resonance: 67%]
    { 1.00000f, 0.66667f, 20000.00000f, 100.00000f, 8.39589f, 0.02099f, 3.49629f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt24 [Cutoff: 100%, Resonance: 67%]
    { 0.00000f, 1.00000f, 20.00000f, 0.10000f, 18.00700f, 0.04502f, 3.76510f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt25 [Cutoff: 0%, Resonance: 100%]
    { 0.14286f, 1.00000f, 53.65392f, 0.26827f, 18.00700f, 0.04502f, 3.76510f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt26 [Cutoff: 14%, Resonance: 100%]
    { 0.28571f, 1.00000f, 143.93715f, 0.71969f, 18.00700f, 0.04502f, 3.76510f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt27 [Cutoff: 29%, Resonance: 100%]
    { 0.42857f, 1.00000f, 386.13956f, 1.93070f, 18.00700f, 0.04502f, 3.76510f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt28 [Cutoff: 43%, Resonance: 100%]
    { 0.57143f, 1.00000f, 1035.89502f, 5.17947f, 18.00700f, 0.04502f, 3.76510f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt29 [Cutoff: 57%, Resonance: 100%]
    { 0.71429f, 1.00000f, 2778.99146f, 13.89496f, 18.00700f, 0.04502f, 3.76510f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt30 [Cutoff: 71%, Resonance: 100%]
    { 0.85714f, 1.00000f, 7455.18799f, 37.27594f, 18.00700f, 0.04502f, 3.76510f, 0.0f }, // casio_cz101_phase_distortion_resonant_pt31 [Cutoff: 86%, Resonance: 100%]
    { 1.00000f, 1.00000f, 20000.00000f, 100.00000f, 18.00700f, 0.04502f, 3.76510f, 0.0f } // casio_cz101_phase_distortion_resonant_pt32 [Cutoff: 100%, Resonance: 100%]
};

} // namespace abd::lutdsp::models

// Backwards compatibility alias
namespace abdaudiolab::lut
{
    using AbdBatchedPoint = abd::lutdsp::AbdBatchedPoint;
    using namespace abd::lutdsp::models;
} // namespace abdaudiolab::lut
