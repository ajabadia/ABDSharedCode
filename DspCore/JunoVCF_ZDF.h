/**
 * @file JunoVCF_ZDF.h
 * @brief Juno/DeepMind style VCF emulation (4-pole OTA filter with phase compensation).
 *        Models the characteristic analog warmth and phase response of the Juno series.
 * @author ABDSynths
 * @date 2026
 *
 * Real-time safe: no dynamic memory allocation in process().
 * Uses smoothed values for parameter changes to avoid zipper noise.
 *
 * Canonical home: ABDSharedCode/SynthCore/filters (namespace abd::dsp).
 *
 * Design notes:
 * - Zero-Delay Feedback (ZDF) architecture for analog-style stability
 * - HF pole compensation to tame the characteristic Juno brightness
 * - Soft clipping emulation for analog warmth when drive > 0
 * - Compatible with polyphonic voice allocation via VoiceAllocator
 */

#pragma once

#include <cmath>
#include <algorithm>

namespace abd::dsp {

/**
 * \brief Enumeration of Juno VCF operating modes.
 */
enum class JunoVCFMode {
    LowPass  = 0,   // Standard low-pass (4 poles)
    HighPass = 1,   // High-pass response
    BandPass = 2,   // Band-pass response
    Notch    = 3    // Notch/reject response
};

/**
 * \brief Configuration for the Juno VCF ZDF.
 */
struct JunoVCFConfig {
    float sampleRate   = 48000.0f;
    float frequency    = 1000.0f;   // Cutoff frequency in Hz
    float resonance    = 0.707f;    // Resonance (Q) factor
    float drive        = 0.0f;      // Drive/overload amount [0.0-1.0]
    JunoVCFMode mode   = JunoVCFMode::LowPass;
    bool usePhaseComp  = true;      // Enable HF phase compensation
};

/**
 * \brief Juno VCF Zero-Delay Feedback (ZDF) filter model.
 *
 * Models the 4-pole OTA (Operational Transconductance Amplifier) filter
 * characteristically found in the Roland Juno-series synthesizers.
 * Features ZDF for unconditional stability, HF pole compensation to control
 * the bright analog response, and drive emulation for saturation.
 *
 * \realTimeSafe: No dynamic memory allocation in process(). All parameter
 * changes use smoothed values to avoid zipper noise.
 */
class JunoVCF_ZDF {
public:
    /** \brief Default constructor initializes to low-pass at 1kHz, no drive. */
    JunoVCF_ZDF();

    /** \brief Reset the filter state (call when sample rate changes). */
    void reset(float sampleRate = 48000.0f);

    /** \brief Process one sample through the Juno VCF.
     *  \param input Input sample (-1.0 to 1.0 range recommended).
     *  \return Filtered output sample with characteristic Juno warmth.
     *
     * \realTimeSafe Guaranteed: no dynamic memory allocation, no mutexes,
     * no std::vector::resize. Only arithmetic and SmoothedValue updates.
     */
    float process(float input);

    /** \brief Configure the Juno VCF parameters.
     *  \param config Structure with sampleRate, frequency, resonance, drive, mode, phaseComp.
     *
     * \realTimeSafe Safe to call from audio thread; smoothed values prevent zipper.
     */
    void setConfig(const JunoVCFConfig& config);

    /** \brief Get the current output without updating internal state.
     *  \param input Input sample.
     *  \return Filtered output (does not update state for polyphonic efficiency). */
    float peek(float input) const;

    /** \brief Get the current configuration. */
    JunoVCFConfig getConfig() const noexcept;

    /** \brief Set the filter mode (low-pass, high-pass, etc.). */
    void setMode(JunoVCFMode newMode) noexcept;

    /** \brief Get the current filter mode. */
    JunoVCFMode getMode() const noexcept;

private:
    // --- ZDF filter sections (4 poles) ---
    // Each pole has: integration, feedback, and feed-forward terms for ZDF
    float pole1State{};  // First OTA pole state
    float pole2State{};  // Second OTA pole state
    float pole3State{};  // Third OTA pole state
    float pole4State{};  // Fourth OTA pole state

    // ZDF feedback coefficients (computed from frequency/resonance)
    float feedbackCoeff{};       // Primary feedback term
    float hfCompensation{};      // HF pole compensation coefficient

    // Filter mode selection
    JunoVCFMode mode{};  // Current mode (LPF, HPF, BPF, Notch)

    // Drive/saturation emulation
    float drive{};           // Current drive amount
    float tanhInput{};       // Tanh saturation input
    float tanhOutput{};      // Tanh saturation output

    // Smoothed values for real-time parameter changes
    float smoothedFrequency{};   // Smoothed cutoff frequency
    float smoothedResonance{};   // Smoothed resonance
    float smoothedDrive{};       // Smoothed drive
    bool  smoothedPhaseComp{};   // Smoothed phase compensation enable

    // Internal state tracking
    float lastInput{};   // Last input sample (for differential processing)
    float lastOutput{};  // Last output sample
};

} // namespace abd::dsp
