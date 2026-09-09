#pragma once
#include <cstdint>

namespace abd::synth {

enum class LFOWaveform {
    Sawtooth = 0,
    Square,
    Triangle,
    SampleAndHold
};

enum class LFOWaveformLFO2 {
    Sawtooth = 0,
    SquarePlus,     // Randomized pulse width per cycle (MS2000 LFO2 exclusive)
    Sine,
    SampleAndHold
};

/**
 * @brief MS2000 Low Frequency Oscillator.
 * LFO1: Saw, Square, Triangle, Sample & Hold.
 * LFO2: Saw, Square+ (random pulse width), Sine, Sample & Hold.
 * Frequency range: logarithmic 0.01 Hz to 20 Hz (exponent ~1.25 per HW measurements).
 * Key Sync: Off (free-run), Timbre (reset on first note of layer), Voice (reset per key press).
 */
class LFO {
public:
    LFO() = default;

    void prepare(double sampleRate) noexcept;
    void reset(float initialPhase = 0.0f) noexcept;

    void setFrequencyHz(float freqHz) noexcept;

    // Tempo Sync: overrides manual frequency with BPM-derived rate.
    // syncNoteIdx: 0..14 = 1/1 .. 1/128 (per MS2000 SysEx spec).
    void setTempoSync(bool enabled, int syncNoteIdx = 4) noexcept;
    void setBpm(double bpm) noexcept;

    void setWaveformLFO1(LFOWaveform wave) noexcept { waveType_ = static_cast<int>(wave); isLFO2_ = false; }
    void setWaveformLFO2(LFOWaveformLFO2 wave) noexcept { waveType_ = static_cast<int>(wave); isLFO2_ = true; }

    // Key Sync: 0=Off, 1=Timbre, 2=Voice (matches HW SysEx packed byte)
    void setKeySyncMode(int mode) noexcept { keySyncMode_ = mode; }

    // Called when a new voice triggers.
    // isFirstTimbreNote: true if this is the first held note in the layer (for Timbre sync).
    void triggerKeySync(bool isFirstTimbreNote) noexcept;

    // Raw phase reset (used internally by Timbre sync)
    void resetPhase() noexcept;

    float getNextSample(float frequencyModulationOctaves = 0.0f) noexcept;
    float getCurrentValue() const noexcept { return currentValue_; }


    // Sync note multiplier lookup (MS2000 canonical: 0=1/1 .. 14=1/128)
    static float syncNoteToMultiplier(int idx) noexcept;

private:
    double sampleRate_{ 44100.0 };
    float frequency_{ 1.0f };
    double phase_{ 0.0 };
    double phaseIncrement_{ 0.0 };

    int waveType_{ 2 };  // Default: Triangle
    bool isLFO2_{ false };
    int keySyncMode_{ 2 }; // 0=Off, 1=Timbre, 2=Voice

    // Tempo Sync
    bool tempoSyncEnabled_{ false };
    int syncNoteIdx_{ 4 };    // Default: 1/4
    double bpm_{ 120.0 };

    float currentValue_{ 0.0f };
    float shHeldValue_{ 0.0f };

    // Square+ per-cycle random pulse width
    float squarePlusPW_{ 0.5f };

    uint32_t rngState_{ 0x98765432 };

    float nextRandom() noexcept;
    void advancePhase() noexcept;
    void syncPhase() noexcept;
};

} // namespace abd::synth