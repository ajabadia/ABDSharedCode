/**
 * @file MultiModeFilter.h
 * @brief Multi-mode filter implementation for MS2000 emulation.
 *        12/24dB LPF/HPF/BPF with saturation and resonance compensation.
 * @author ABDSynths
 * @date 2026
 *
 * Real-time safe: no dynamic memory allocation in process().
 * Uses Juce::SmoothedValue for parameter changes free of zipper noise.
 *
 * Canonical home: ABDSharedCode/SynthCore/filters (namespace abd::dsp).
 */

#pragma once

#include <cmath>
#include <algorithm>

namespace abd::dsp {

/**
 * \brief Enumeration of available filter types for the multi-mode filter.
 */
enum class MultiModeFilterType {
    LP12  = 0,  // 12dB Low-Pass
    LP24  = 1,  // 24dB Low-Pass
    HP    = 2,  // High-Pass
    BP    = 3,  // Band-Pass
    NOTCH = 4   // Notch (reject)
};

/**
 \brief Configuration parameters for the multi-mode filter.
 */
struct MultiModeFilterConfig {
    float sampleRate       = 48000.0f;
    float frequency        = 1000.0f;   // Cutoff frequency in Hz
    float resonance        = 0.707f;    // Resonance (Q) factor
    float drive            = 0.0f;      // Saturation/drive amount [0.0-1.0]
    MultiModeFilterType type = MultiModeFilterType::LP24;
};

/**
 * \brief Multi-mode filter implementation emulating the MS2000's multimode filter.
 *
 * Features:
 * - 12dB and 24dB low-pass, high-pass, band-pass, and notch modes
 * - Polynomial drive saturation emulating analog circuit non-linearity
 * - Resonance compensation to maintain stability at high Q
 * - Smooth parameter changes via smoothed values (no zipper noise)
 * - Real-time safe: no dynamic memory allocation in process()
 */
class MultiModeFilter {
public:
    /** \brief Default constructor initializes filter to LP24 mode at 1kHz. */
    MultiModeFilter();

    /** \brief Reset the filter state (call when sample rate changes). */
    void reset(float sampleRate = 48000.0f);

    /** \brief Process one sample through the filter.
     *  \param input Input sample (-1.0 to 1.0 range recommended).
     *  \return Filtered output sample.
     *
     * \realTimeSafe Guaranteed: no dynamic memory allocation, no mutexes,
     * no std::vector::resize. Only arithmetic operations and SmoothedValue updates.
     */
    float process(float input);

    /** \brief Configure the filter parameters.
     *  \param config Structure with sampleRate, frequency, resonance, drive, type.
     *
     * \realTimeSafe Safe to call from audio thread; smoothed values prevent zipper.
     */
    void setConfig(const MultiModeFilterConfig& config);

    /** \brief Get the current filtered output without updating internal state.
     *  \param input Input sample.
     *  \return Filtered output (does not update state for efficiency in polyphonic contexts). */
    float peek(float input) const;

    /** \brief Get the current configuration. */
    MultiModeFilterConfig getConfig() const noexcept;

private:
    // --- Low-pass section (12dB and 24dB) ---
    float lpf12State{};      // State for 12dB LPF
    float lpf24State{};      // State for 24dB LPF (cascaded two 12dB sections)
    float lpf12Coeff{};      // 12dB coefficient
    float lpf24Coeff1{};     // 24dB first stage coefficient
    float lpf24Coeff2{};     // 24dB second stage coefficient

    // --- High-pass section ---
    float hpState{};         // HPF state
    float hpCoeff{};         // HPF coefficient

    // --- Band-pass section ---
    float bpState{};         // BPF state
    float bpCoeff{};         // BPF coefficient

    // --- Notch section ---
    float notchState{};      // Notch state
    float notchCoeff{};      // Notch coefficient

    // --- Drive/saturation ---
    float drive{};           // Current drive amount
    float tanhScale{};       // Tanh scaling for soft clipping

    // --- Current type ---
    MultiModeFilterType type{};  // Current filter type

    // --- Smoothed values for parameter changes (real-time safety) ---
    float smoothedFrequency{};   // Smoothed cutoff frequency
    float smoothedResonance{};   // Smoothed resonance
    float smoothedDrive{};       // Smoothed drive
};

} // namespace abd::dsp
