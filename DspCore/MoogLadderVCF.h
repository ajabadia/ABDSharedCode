/**
 * @file MoogLadderVCF.h
 * @brief Moog ladder filter emulation (Huovilainen model).
 *        Classic 4-pole ladder filter modeling the Moog synthesizer sound.
 * @author ABDSynths
 * @date 2026
 *
 * Real-time safe: no dynamic memory allocation in process().
 * Uses smoothed values for parameter changes to avoid zipper noise.
 *
 * Canonical home: ABDSharedCode/SynthCore/filters (namespace abd::dsp).
 *
 * Design notes:
 * - Huovilainen's discrete-time ladder model for unconditional stability
 * - Characteristic Moog "warmth" and self-oscillation behavior
 * - Accurate response to resonance Q, approaching self-osculation at Q > 1.0
 * - Drive emulation for saturation emulating analog gain staging
 * - Compatible with polyphonic voice allocation via VoiceAllocator
 */

#pragma once

#include <cmath>
#include <algorithm>

namespace abd::dsp {

/**
 * \brief Enumeration of Moog ladder filter modes.
 */
enum class MoogLadderMode {
    LowPass  = 0,   // Standard 4-pole low-pass (self-oscillating)
    HighPass = 1,   // High-pass response (post-ladder)
    BandPass = 2    // Band-pass response (between ladder stages)
};

/**
 * \brief Configuration for the Moog ladder VCF.
 */
struct MoogLadderConfig {
    float sampleRate   = 48000.0f;
    float frequency    = 1000.0f;   // Cutoff frequency in Hz
    float resonance    = 0.707f;    // Resonance (Q) factor, >1.0 → self-oscillation
    float drive        = 0.0f;      // Pre-feedback drive [0.0-1.0]
    MoogLadderMode mode = MoogLadderMode::LowPass;
};

/**
 * \brief Moog ladder filter emulation (Huovilainen discrete-time model).
 *
 * Models the iconic 4-pole ladder filter found in Moog synthesizers.
 * Features discrete-time implementation for unconditional stability,
 * accurate resonance behavior (approaches self-oscillation at Q >= 1.0),
 * and drive emulation for analog warmth.
 *
 * \realTimeSafe: No dynamic memory allocation in process(). All parameter
 * changes use smoothed values to avoid zipper noise.
 */
class MoogLadderVCF {
public:
    /** \brief Default constructor initializes to low-pass at 1kHz, no drive. */
    MoogLadderVCF();

    /** \brief Reset the filter state (call when sample rate changes). */
    void reset(float sampleRate = 48000.0f);

    /** \brief Process one sample through the Moog ladder filter.
     *  \param input Input sample (-1.0 to 1.0 range recommended).
     *  \return Filtered output with characteristic Moog warmth.
     *
     * \realTimeSafe Guaranteed: no dynamic memory allocation, no mutexes,
     * no std::vector::resize. Only arithmetic and SmoothedValue updates.
     */
    float process(float input);

    /** \brief Configure the Moog ladder filter parameters.
     *  \param config Structure with sampleRate, frequency, resonance, drive, mode.
     *
     * \realTimeSafe Safe to call from audio thread; smoothed values prevent zipper.
     */
    void setConfig(const MoogLadderConfig& config);

    /** \brief Get the current output without updating internal state.
     *  \param input Input sample.
     *  \return Filtered output (does not update state for polyphonic efficiency). */
    float peek(float input) const;

    /** \brief Get the current configuration. */
    MoogLadderConfig getConfig() const noexcept;

    /** \brief Set the filter mode (low-pass, high-pass, band-pass). */
    void setMode(MoogLadderMode newMode) noexcept;

    /** \brief Get the current filter mode. */
    MoogLadderMode getMode() const noexcept;

private:
    // --- Ladder stage states (4 stages) ---
    // Each stage has: capacitor voltage, feedback, and input terms
    float stage1{};  // First ladder stage state
    float stage2{};    // Second ladder stage state
    float stage3{};    // Third ladder stage state
    float stage4{};    // Fourth ladder stage state

    // Ladder feedback and coupling terms
    float feedback{};        // Cross-stage feedback coefficient
    float coupling{};        // Inter-stage coupling term

    // Filter mode selection
    MoogLadderMode mode{};  // Current mode (LPF, HPF, BPF)

    // Drive pre-feedback
    float drive{};           // Current drive amount (pre-feedback)
    float tanhDrive{};       // Tanh-smoothed drive for soft clipping

    // Smoothed values for real-time parameter changes
    float smoothedFrequency{};   // Smoothed cutoff frequency
    float smoothedResonance{};   // Smoothed resonance (Q factor)
    float smoothedDrive{};       // Smoothed drive amount
    float smoothedFrequencyQ{};  // Smoothed frequency * Q product

    // Internal state for oscillation detection
    float oscDetect{};   // Oscillation detection variable
    bool  isOscillating{}; // Oscillation flag

    // Last input/output for differential processing
    float lastInput{};   // Last input sample
    float lastOutput{};  // Last output sample
};

} // namespace abd::dsp
