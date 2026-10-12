/**
 * @file VAOnePoleFilter.h
 * \brief Single-pole low-pass filter (VA: Voltage-Controlled Analog simple).
 * \brief The simplest filter topology: single-pole RC filter emulation.
 * @author ABDSynths
 * @date 2026
 *
 * Real-time safe: minimal operations in process(), no dynamic allocation.
 * Uses smoothing for parameter changes.
 *
 * Canonical home: ABDSharedCode/SynthCore/filters (namespace abd::dsp).
 *
 * Design notes:
 * - Single-pole low-pass: simplest analog filter, -6dB/octave roll-off
 * - Perfect for subtle tonal shaping, LFO target smoothing, or simple
 *   voice detuning without CPU overhead
 * - CPU-leightweight: ideal for per-voice processing in polyphonic contexts
 * - Compatible with VoiceAllocator-driven voice management
 */

#pragma once

#include <cmath>

namespace abd::dsp {

/**
 * \brief Enumeration of one-pole filter types.
 */
enum class VAOnePoleType {
    LowPass  = 0,   // Standard single-pole low-pass
    HighPass = 1    // Single-pole high-pass (difference of input and LP output)
};

/**
 * \brief Configuration for the single-pole VA filter.
 */
struct VAOnePoleConfig {
    float sampleRate   = 48000.0f;
    float frequency    = 1000.0f;   // Cutoff frequency in Hz
    float resonance    = 1.0f;      // Not really used for single-pole, but kept for API consistency
    float drive        = 0.0f;      // Simple gain, not saturation [0.0-1.0]
    VAOnePoleType type = VAOnePoleType::LowPass;
};

/**
 * \brief Single-pole low-pass / high-pass filter emulation.
 *
 * The simplest possible filter topology: a first-order RC filter emulation.
 * -6dB/octave roll-off rate. Useful for subtle tonalaping, smoothing
 * control signals, or per-voice light filtering in polyphonic contexts.
 *
 * \realTimeSafe: Guaranteed minimal operations in process(). No dynamic
 * memory allocation, no mutexes, trivial arithmetic only.
 */
class VAOnePoleFilter {
public:
    /** \brief Default constructor initializes to low-pass at 1kHz. */
    VAOnePoleFilter();

    /** \brief Reset the filter state (call when sample rate changes). */
    void reset(float sampleRate = 48000.0f);

    /** \brief Process one sample through the one-pole filter.
     *  \param input Input sample (-1.0 to 1.0 range recommended).
     *  \return Filtered output sample.
     *
     * \realTimeSafe Guaranteed: no dynamic memory allocation, no mutexes,
     * no container operations. Only a few arithmetic operations.
     */
    float process(float input);

    /** \brief Configure the one-pole filter parameters.
     *  \param config Structure with sampleRate, frequency, resonance, drive, type.
     *
     * \realTimeSafe Safe to call from audio thread; trivial update of internal state.
     */
    void setConfig(const VAOnePoleConfig& config);

    /** \brief Get the current output without updating internal state.
     *  \param input Input sample.
     *  \return Filtered output (does not update state for polyphonic efficiency). */
    float peek(float input) const;

    /** \brief Get the current configuration. */
    VAOnePoleConfig getConfig() const noexcept;

    /** \brief Set the filter type (low-pass or high-pass). */
    void setType(VAOnePoleType newType) noexcept;

    /** \brief Get the current filter type. */
    VAOnePoleType getType() const noexcept;

private:
    // --- Single-pole low-pass state ---
    float poleState{};   // Integrator state (previous output)
    float rcCoeff{};     // RC time constant coefficient = 1/(1 + dt/(2*RC))

    // --- Single-pole high-pass state ---
    float hpState{};     // HPF state: input - LP output (smoothed)

    // Filter type selection
    VAOnePoleType type{};  // Current type (LPF or HPF)

    // Drive simple gain (not saturation like higher-order filters)
    float drive{};           // Simple gain applied after filtering

    // Smoothed value for cutoff frequency changes (zipper prevention)
    float smoothedFrequency{};   // Smoothed cutoff frequency

    // Last input/output tracking
    float lastInput{};   // Last input sample
    float lastOutput{};  // Last output sample
};

} // namespace abd::dsp
