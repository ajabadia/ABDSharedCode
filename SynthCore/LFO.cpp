#include "LFO.h"
#include "DSPUtils.h"
#include <cmath>

namespace abd::synth {

// ═══════════════════════════════════════════════════════════════
// MS2000 canonical Sync Note → multiplier table (SysEx 0x00–0x0E)
// Multiplier = (quarter-note rate factor).
// freqHz = (bpm / 60.0) * multiplier.
// ═══════════════════════════════════════════════════════════════
static constexpr float kSyncNoteMultipliers[15] = {
    1.0f / 4.0f,    // 0:  1/1   whole
    1.0f / 3.0f,    // 1:  3/4
    1.0f / 2.0f,    // 2:  1/2
    3.0f / 4.0f,    // 3:  1/2T
    1.0f,           // 4:  1/4
    1.5f,           // 5:  1/4T
    2.0f,           // 6:  1/8
    3.0f,           // 7:  1/8T
    4.0f,           // 8:  1/16
    6.0f,           // 9:  1/16T
    8.0f,           // 10: 1/32
    12.0f,          // 11: 1/32T
    16.0f,          // 12: 1/64
    24.0f,          // 13: 1/64T
    32.0f           // 14: 1/128
};

float LFO::syncNoteToMultiplier(int idx) noexcept
{
    if (idx < 0 || idx > 14) return kSyncNoteMultipliers[4];
    return kSyncNoteMultipliers[idx];
}

void LFO::prepare(double sampleRate) noexcept
{
    sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
    setFrequencyHz(frequency_);
    reset();
}

void LFO::reset(float initialPhase) noexcept
{
    phase_ = static_cast<double>(initialPhase);
    while (phase_ >= 1.0) phase_ -= 1.0;
    while (phase_ < 0.0)  phase_ += 1.0;
    currentValue_ = 0.0f;
    shHeldValue_ = nextRandom();
    squarePlusPW_ = 0.5f;
}

void LFO::setFrequencyHz(float freqHz) noexcept
{
    // MS2000 measured range: 0.01 Hz to 20.0 Hz via exponential curve
    frequency_ = DSPUtils::clamp(freqHz, 0.01f, 20.0f);
    phaseIncrement_ = static_cast<double>(frequency_ / sampleRate_);
}

void LFO::setTempoSync(bool enabled, int syncNoteIdx) noexcept
{
    tempoSyncEnabled_ = enabled;
    syncNoteIdx_ = (syncNoteIdx >= 0 && syncNoteIdx < 15) ? syncNoteIdx : 4;

    if (tempoSyncEnabled_)
    {
        // Compute frequency from current BPM + sync note division
        float multiplier = kSyncNoteMultipliers[syncNoteIdx_];
        float syncHz = static_cast<float>(bpm_ / 60.0) * multiplier;
        frequency_ = DSPUtils::clamp(syncHz, 0.01f, 20.0f);
        phaseIncrement_ = static_cast<double>(frequency_ / sampleRate_);
    }
}

void LFO::setBpm(double bpm) noexcept
{
    bpm_ = (bpm > 1.0 && bpm < 400.0) ? bpm : 120.0;

    if (tempoSyncEnabled_)
    {
        float multiplier = kSyncNoteMultipliers[syncNoteIdx_];
        float syncHz = static_cast<float>(bpm_ / 60.0) * multiplier;
        frequency_ = DSPUtils::clamp(syncHz, 0.01f, 20.0f);
        phaseIncrement_ = static_cast<double>(frequency_ / sampleRate_);
    }
}

float LFO::nextRandom() noexcept
{
    return DSPUtils::randomBipolar(rngState_);
}

void LFO::syncPhase() noexcept
{
    phase_ = 0.0;
    currentValue_ = 0.0f;
    shHeldValue_ = nextRandom();
    // Randomize Square+ pulse width: 5% to 95%
    squarePlusPW_ = 0.05f + 0.9f * (nextRandom() * 0.5f + 0.5f);
}

void LFO::triggerKeySync(bool isFirstTimbreNote) noexcept
{
    // keySyncMode_: 0 = Off (never reset), 1 = Timbre (only on first note), 2 = Voice (every note)
    if (keySyncMode_ == 2) // Voice
    {
        syncPhase();
    }
    else if (keySyncMode_ == 1 && isFirstTimbreNote) // Timbre
    {
        syncPhase();
    }
    // Off (0): do nothing, free-running
}

void LFO::resetPhase() noexcept
{
    phase_ = 0.0;
}

void LFO::advancePhase() noexcept
{
    phase_ += phaseIncrement_;
    if (phase_ >= 1.0)
    {
        phase_ -= 1.0;
        shHeldValue_ = nextRandom();
        // New Square+ pulse width each cycle
        squarePlusPW_ = 0.05f + 0.9f * (nextRandom() * 0.5f + 0.5f);
    }
}

float LFO::getNextSample(float frequencyModulationOctaves) noexcept
{

    if (std::abs(frequencyModulationOctaves) > 0.0001f)
    {
        float modFreq = frequency_ * std::pow(2.0f, frequencyModulationOctaves * 2.0f);
        modFreq = DSPUtils::clamp(modFreq, 0.001f, 100.0f);
        phase_ += static_cast<double>(modFreq / sampleRate_);
        if (phase_ >= 1.0)
        {
            phase_ -= 1.0;
            shHeldValue_ = nextRandom();
            squarePlusPW_ = 0.05f + 0.9f * (nextRandom() * 0.5f + 0.5f);
        }
    }
    else
    {
        advancePhase();
    }

    if (isLFO2_)
    {
        // ── LFO2: Saw, Square+, Sine, Sample & Hold ──
        switch (static_cast<LFOWaveformLFO2>(waveType_))
        {
            case LFOWaveformLFO2::Sawtooth:

                currentValue_ = 1.0f - static_cast<float>(2.0 * phase_);
                break;

            case LFOWaveformLFO2::SquarePlus:
                currentValue_ = (phase_ < static_cast<double>(squarePlusPW_)) ? 1.0f : -1.0f;
                break;

            case LFOWaveformLFO2::Sine:
                currentValue_ = std::sin(DSPUtils::TWO_PI * static_cast<float>(phase_));
                break;

            case LFOWaveformLFO2::SampleAndHold:
                currentValue_ = shHeldValue_;
                break;
        }
    }
    else
    {
        // ── LFO1: Saw, Square, Triangle, Sample & Hold ──
        switch (static_cast<LFOWaveform>(waveType_))
        {
            case LFOWaveform::Sawtooth:
                currentValue_ = 1.0f - static_cast<float>(2.0 * phase_);
                break;

            case LFOWaveform::Square:
                currentValue_ = (phase_ < 0.5) ? 1.0f : -1.0f;
                break;

            case LFOWaveform::Triangle:
                if (phase_ < 0.25)
                    currentValue_ = static_cast<float>(4.0 * phase_);
                else if (phase_ < 0.75)
                    currentValue_ = static_cast<float>(1.0 - 4.0 * (phase_ - 0.25));
                else
                    currentValue_ = static_cast<float>(-1.0 + 4.0 * (phase_ - 0.75));
                break;

            case LFOWaveform::SampleAndHold:
                currentValue_ = shHeldValue_;
                break;
        }
    }

    return currentValue_;
}

} // namespace abd::synth