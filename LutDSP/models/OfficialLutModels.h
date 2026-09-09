/**
 * @file OfficialLutModels.h
 * @brief Canonical index of official hardware profile Look-Up Tables in ABDSharedCode.
 * @author ABDSynths
 * @date 2026
 *
 * Provides ready-to-use 16-byte aligned AbdBatchedPoint tables for filter emulators
 * and DSP modeling modules (LutEvaluatorSimd, AnalogLutFilterModule).
 */

#pragma once

#include "lut_mock_va_synth_moog_ladder.h"
#include "lut_roland_juno106_ir3109_vcf.h"
#include "lut_casio_cz101_phase_distortion_resonant.h"
#include "lut_behringer_pro800_cem3320_vcf.h"
#include "lut_manual_eurorack_vcf_diode_ladder.h"
#include "lut_roland_aira_bitrazer_filter.h"
