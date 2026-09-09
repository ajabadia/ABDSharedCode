#pragma once

namespace abd::synth {

/**
 * @brief Exponential Portamento / Glide processor for smooth note pitch transitions.
 */
class PortamentoGlide {
public:
    PortamentoGlide() = default;

    void prepare(double sampleRate) noexcept;
    void reset(float initialMidiNote = 60.0f) noexcept;

    void setTargetNote(float targetMidiNote, bool glideEnabled) noexcept;
    void setGlideTime(float timeParam0to1) noexcept; // 0 (0ms) to 1 (approx 2.5s)

    float getNextPitchSemitones() noexcept;
    float getCurrentPitch() const noexcept { return currentPitch_; }

private:
    double sampleRate_{ 44100.0 };
    float currentPitch_{ 60.0f };
    float targetPitch_{ 60.0f };
    float timeParam_{ 0.0f };
    double slewMultiplier_{ 0.0 };

    void updateMultiplier() noexcept;
};

} // namespace abd::synth
