/**
 * @file KorgMS20VCF.h
 * @brief Korg MS-20 style VCF emulation (Sallen-Key topology with nonlinear response).
 *        Models the characteristic aggressive filter sound of the Korg MS-20.
 * @author ABDSynths
 * @date 2026
 *
 * Real-time safe: no dynamic memory allocation in process().
 * Uses smoothed values for parameter changes to avoid zipper noise.
 *
 * Canonical home: ABDSharedCode/SynthCore/filters (namespace abd::dsp).
 *
 * Design notes:
 * - Sallen-Key topologie analog emulation with discrete-time approximation
 * - Asymmetric response characterising the MS-20's aggressive filter
 * - Hard clipping emulation for the MS-20's distinctive distortion
 * - Drive control for overdrive staging
 * - Compatible with polyphonic voice allocation via VoiceAllocator
 */

#pragma once

#include <cmath>
#include <algorithm>

namespace abd::dsp {

/**
 * \brief Enumeration of Korg MS-20 VCF modes.
 */
enum class KorgMS20Mode {
    LowPass  = 0,   // Standard low-pass (aggressive response)
    HighPass = 1,   // High-pass response
    BandPass = 2    // Band-pass response
};

/**
 * \brief Configuration for the Korg MS-20 VCF.
 */
struct KorgMS20Config {
    float sampleRate   = 48000.0f;
    float frequency    = 1000.0f;   // Cutoff frequency in Hz
    float resonance    = 0.707f;    // Resonance factor
    float drive        = 0.0f;      // Drive/overload [0.0-1.0]
    KorgMS20Mode mode  = KorgMS20Mode::LowPass;
    bool hardClip      = true;      // Enable hard clipping emulation
};

/**
 * \brief Korg MS-20 VCF emulation (Sallen-Key topology).
 *
 * Models the distinctive low-pass filter of the Korg MS-20 synthesizer,
 * known for its aggressive, sharp resonance and distinctive distortion
 * character when driven hard. Uses a discrete-time Sallen-Key approximation
 * with hard-clipping for that classic MS-20 bite.
 *
 * \realTimeSafe: No dynamic memory allocation in process(). All parameter
 * changes use smoothed values to avoid zipper noise.
 */
class KorgMS20VCF {
public:
    /** \brief Default constructor initializes to low-pass at 1kHz, no drive, hard clip. */
    KorgMS20VCF();

    /** \brief Reset the filter state (call when sample rate changes). */
    void reset(float sampleRate = 48000.0f);

    /** \brief Process one sample through the Korg MS-20 VCF.
     *  \param input Input sample (-1.0 to 1.0 range recommended).
     *  \return Filtered output with characteristic MS-20 aggression.
     *
     * \realTimeSafe Guaranteed: no dynamic memory allocation, no mutexes,
     * no std::vector::resize. Only arithmetic and SmoothedValue updates.
     */
    float process(float input);

    /** \brief Configure the Korg MS-20 VCF parameters.
     *  \param config Structure with sampleRate, frequency, resonance, drive, mode, hardClip.
     *
     * \realTimeSafe Safe to call from audio thread; smoothed values prevent zipper.
     */
    void setConfig(const KorgMS20Config& config);

    /** \brief Get the current output without updating internal state.
     *  \param input Input sample.
     *  \return Filtered output (does not update state for polyphonic efficiency). */
    float peek(float input) const;

    /** \brief Get the current configuration. */
    KorgMS20Config getConfig() const noexcept;

    /** \brief Set the filter mode (low-pass, high-pass, band-pass). */
    void setMode(KorgMS20Mode newMode) noexcept;

    /** \brief Get the current filter mode. */
    KorgMS20Mode getMode() const noexcept;

private:
    // --- Sallen-Key filter state ---
    float filterState{};   // Main Sallen-Key integrator state
    float prevInput{};     // Previous input sample (for difference equation)
    float prevOutput{};    // Previous output sample

    // Filter mode selection
    KorgMS20Mode mode{};  // Current mode (LPF, HPF, BPF)

    // Drive and clipping
    float drive{};           // Current drive amount
    bool  hardClip{};      // Hard clipping enable flag

    // Hard clipping threshold and saturation
    float clipThreshold{}; // Input threshold for hard clipping
    float satCoeff{};      // Saturation coefficient

    // Smoothed values for real-time parameter changes
    float smoothedFrequency{};   // Smoothed cutoff frequency
    float smoothedResonance{};   // Smoothed resonance
    float smoothedDrive{};       // Smoothed drive
    bool  smoothedHardClip{};  // Smoothed hard clip enable

    // Last input/output tracking
    float lastInput{};   // Last input sample
    float lastOutput{};  // Last output sample
};

} // namespace abd::dsp
