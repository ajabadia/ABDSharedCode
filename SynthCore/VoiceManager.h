/**
 * @file VoiceManager.h
 * \brief Synth-specific voice manager that uses VoiceAllocator for pure allocation.
 * \brief Separates voice allocation (math) from voice management (timbres, splits, routing).
 * @author ABDSynths
 * @date 2026
 *
 * Hybrid design: VoiceAllocator provides the mathematical core of voice robbing,
 * while VoiceManager handles synth-specific voice lifecycle (timbres, splits, routing).
 *
 * Canonical home: ABDSharedCode/SynthCore (namespace abd::synth).
 *
 * Design notes:
 * - VoiceAllocator::allocateNoteOn()/allocateNoteOff() handle voice indices purely
 * - VoiceManager adds: timbres, splits, oscillator routing, voice-to-slot mapping
 * - Compatible with both MS2000 (4 voices, 2 timbres) and DeepMind 12 (12 voices, unison)
 * - Real-time safe: no dynamic allocation in audio thread
 */

#pragma once

#include "VoiceAllocator.h"
#include <vector>
#include <array>
#include <cstdint>
#include <string>

namespace abd::synth {

/**
 * \brief Timbre definition for multi-timbral synthesis.
 */
struct Timbre {
    std::string name;              // Human-readable timbre name
    float oscillatorLevel = 1.0f;  // Overall oscillator amplitude
    float filterFrequency = 1000.0f; // Default cutoff frequency
    float filterResonance = 0.707f;  // Default resonance
    float pitchShift = 0.0f;       // Pitch shift in semitones
    int  oscillatorType = 0;       // Waveform/synthesis type index
};

/** \brief MS2000-specific timbre configuration.
 *  \brief MS2000 tiene 2 timbres: Main (abajo del split) y Sub/Dual/Layer (arriba del split).
 */
struct MS2000Config {
    static constexpr size_t MAX_TIMBRES = 2;
    
    struct TimbreConfig {
        float oscillatorLevel = 1.0f;   // Overall oscillator amplitude
        float filterFrequency = 1000.0f; // Default cutoff frequency
        float filterResonance = 0.707f;  // Default resonance
        float pitchShift = 0.0f;       // Pitch shift in semitones
        int  oscillatorType = 0;       // 0 = Classic, 1 = Pulse, 2 = Noise
    } timbres[2];
    
    // Split point in MIDI notes (0-127). Abajo del split: timbre 0 (Main).
    // Arriba del split: timbre 1 (Sub/Dual/Layer).
    float splitPoint = 60.0f;  // Do central (MIDI note 60)
};

/**
 * \brief Voice routing information for a single voice.
 */
struct VoiceRouting {
    int  oscillatorSelect = 0;    // Which oscillator/waveform to use
    int  filterSelect     = 0;    // Which filter to engage
    int  insertEffect     = -1;   // Insert effect slot index (-1 = none)
    int  sendLevel        = 0;    // Send level to effect bus (0-127)
};

/**
 * \brief Synth-specific voice manager.
 *
 * Extends VoiceAllocator pure mathematical core with synth-specific
 * voice management: timbres, splits, oscillator routing, and voice-to-slot
 * mapping. Each synthesizer instantiates its own VoiceManager subclass
 * or configures this base with appropriate parameters.
 *
 * \realTimeSafe: allocateNoteOn/allocateNoteOff are safe for audio thread.
 * setTimbres/setSplitPoints should be called from control thread or between notes.
 */
class VoiceManager {
public:
    /** \brief Default constructor initializes for MS2000-like configuration (4 voices). */
    VoiceManager();

    /** \brief Reset all voice state and configuration. */
    void reset() noexcept;

    /** \brief Set the polyphony mode (affects VoiceAllocator behavior). */
    void setPolyMode(VoiceAllocator::PolyMode newMode) noexcept;

    /** \brief Get the current polyphony mode. */
    VoiceAllocator::PolyMode getPolyMode() const noexcept;

    /** \brief Set the steal policy (affects VoiceAllocator stealing behavior). */
    void setStealPolicy(VoiceAllocator::StealPolicy newPolicy) noexcept;

    /** \brief Get the current steal policy. */
    VoiceAllocator::StealPolicy getStealPolicy() const noexcept;

    /** \brief Configure polyphony: number of voices and initial mode.
     *  \param numVoices Total polyphony count.
     *  \param mode Initial polyphony mode.
     *
     * \realTimeSafe Safe to call between notes; configures VoiceAllocator internally. */
    void configure(size_t numVoices, VoiceAllocator::PolyMode mode = VoiceAllocator::PolyMode::Poly1);

    /** \brief Set the timbre for a specific key range (split).
     *  \param splitKey MIDI note at which this split begins.
     *  \param timbre Timbre configuration for keys below splitKey.
     *
     * \realTimeSafe Safe from audio thread; modifies timbre lookup table. */
    void setSplitPoint(float splitKey, const Timbre& timbre);

    /** \brief Get the timbre responsible for a given MIDI note. */
    const Timbre& getTimbreForNote(int midiNote) const noexcept;

    /** \brief Set the voice routing for a specific allocated voice.
     *  \param voiceIndex Index of the allocated voice (from VoiceAllocator).
     *  \param routing VoiceRouting configuration for this voice.
     *
     * \realTimeSafe Safe from audio thread; updates routing table. */
    void setVoiceRouting(int voiceIndex, const VoiceRouting& routing);

    /** \brief Get the voice routing for a specific allocated voice. */
    VoiceRouting getVoiceRouting(int voiceIndex) const noexcept;

    /** \brief Activate a voice with note-on information.
     *  \param voiceIndex Index from VoiceAllocator::allocateNoteOn().
     *  \param midiNote MIDI note number (0-127).
     *  \param velocity Note velocity (0-127).
     *  \param timbreIndex Index into timbres array for this voice.
     *  \param routing VoiceRouting for this voice.
     *
     * \realTimeSafe Safe for audio thread (just updates shadow state). */
    void activateVoice(int voiceIndex, int midiNote, uint8_t velocity,
                       int timbreIndex, const VoiceRouting& routing);

    /** \brief Deactivate a voice with note-off information.
     *  \param voiceIndex Index of voice to release.
     *  \param velocity Note-off velocity (0-127).
     *  \param isLegato true if this is a legato note-on (same pitch as previous).
     *
     * \realTimeSafe Safe for audio thread. */
    void deactivateVoice(int voiceIndex, uint8_t velocity, bool isLegato = false);

    /** \brief Retrieve current shadow state for a voice.
     *  \param voiceIndex Index from VoiceAllocator.
     *  \return Reference to voice state structure. */
    const VoiceAllocator::VoiceState& getVoiceState(int voiceIndex) const noexcept;

    /** \brief Get number of currently active voices. */
    size_t getNumActiveVoices() const noexcept;

    /** \brief Get the configuration summary. */
    struct ConfigSummary {
        size_t      numVoices;
        int         polyMode;
        int         stealPolicy;
        size_t      numTimbreSplits;
        MS2000Config  ms2000Config;
    } getConfig() const noexcept;

private:
    // --- VoiceAllocator integration ---
    VoiceAllocator          allocator;  // Pure mathematical core

    // --- Timbre/split management ---
    static constexpr size_t MAX_SPLITS = 16;
    Timbre                  timbres[MAX_SPLITS];
    size_t                  numTimbreSplits = 0;
    std::array<float, 128>  noteToTimbreMap;  // MIDI note -> timbre index

    // --- Voice routing table ---
    static constexpr size_t MAX_ROUTING = 32;
    VoiceRouting            routings[MAX_ROUTING];
    size_t                  numRoutings = 0;

    // --- Voice shadow state (mirrors VoiceAllocator state plus extra info) ---
    struct VoiceShadow {
        int  midiNote      = -1;
        float velocity     = 0.0f;
        bool active        = false;
        int  timbreIndex   = 0;      // Index into timbres array
        int  routingIndex  = 0;      // Index into routings array
        uint32_t timestamp = 0;
    };
    std::array<VoiceShadow, 128> voiceShadows;  // Max 128 voices tracked

    // --- Configuration ---
    size_t      numVoices      = 4;       // Default: MS2000-like 4 voices
    int         polyMode       = 0;       // Poly1 (round-robin)
    int         stealPolicy    = 0;       // LegacyTimestamp
};

/** \brief Factory function to create a VoiceManager configured for MS2000 (4 voices, 2 timbres). \return Unique pointer to VoiceManager instance.
 * \realTimeSafe Construction is safe (no audio thread involvement). */
std::unique_ptr<VoiceManager> createMS2000VoiceManager();

/** \brief Factory function to create a VoiceManager configured for DeepMind 12 (12 voices, unison). \return Unique pointer to VoiceManager instance.
 \realTimeSafe Construction is safe (no audio thread involvement). */
std::unique_ptr<VoiceManager> createDeepMind12VoiceManager();

} // namespace abd::synth
```



















Now let me verify the file was created correctly:
<tool_call>
<function=pwsh>
<parameter=command>
type "D:\desarrollos\ABDSynths\ABDSharedCode\SynthCore\VoiceManager.h" | head -n 80