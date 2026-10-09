#pragma once

#include "DspVAOnePole.h"
#include <algorithm>
#include <cmath>

namespace abd::dsp
{
/**
 * @brief KorgMS20VCF: Korg MS-20 Sallen-Key K35 filter topology.
 *
 * Topology: 2 LP + 2 HP VAOnePoleFilter stages in series with diode feedback clipper.
 * LP path: input -> LP1 -> LP2 -> output (feedback from LP2 to LP1 input).
 * Feedback through diode clipper (tanh waveshaper) for authentic aggressive analog saturation.
 * Resonance: k = 0.1 + 6.0 * resonance (0.1 .. 6.1 range, capable of self-oscillation).
 *
 * Pole modes:
 *   0 = 4-pole LP/HP (24 dB/oct) — full cascade path
 *   1 = 2-pole LP/HP (12 dB/oct) — stage 1 only
 *
 * Sub modes:
 *   0 = K35 Lowpass mode
 *   1 = K35 Highpass mode
 */
class KorgMS20VCF
{
public:
    KorgMS20VCF() noexcept = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(1.0, newSampleRate);
        reset();
        updateCoefficients();
    }

    void reset() noexcept
    {
        lp1.reset();
        lp2.reset();
        hp1.reset();
        hp2.reset();
    }

    void setCutoff(float hz) noexcept
    {
        const float maxCutoff = static_cast<float>(sampleRate * 0.49);
        const float clamped   = std::clamp(hz, 10.0f, maxCutoff);
        if (std::abs(clamped - cutoffHz) > 0.01f)
        {
            cutoffHz = clamped;
            updateCoefficients();
        }
    }

    void setResonance(float res) noexcept
    {
        const float clamped = std::clamp(res, 0.0f, 1.0f);
        if (std::abs(clamped - resonance) > 0.001f)
        {
            resonance = clamped;
            updateCoefficients();
        }
    }

    void setPoleMode(int mode) noexcept
    {
        poleMode = std::clamp(mode, 0, 1);
    }

    void setSubMode(int mode) noexcept
    {
        subMode = std::clamp(mode, 0, 1);
    }

    [[nodiscard]] float getCutoff() const noexcept { return cutoffHz; }
    [[nodiscard]] float getResonance() const noexcept { return resonance; }
    [[nodiscard]] int getPoleMode() const noexcept { return poleMode; }
    [[nodiscard]] int getSubMode() const noexcept { return subMode; }

    float process(float sample) noexcept
    {
        const double k = 0.1 + 6.0 * static_cast<double>(resonance);

        if (subMode == 1)
        {
            // K35 Highpass mode: input -> HP1 -> HP2 -> output
            // Feedback from hp2 output through diode clipper (tanh waveshaper)
            const double hpFeedback = std::tanh(static_cast<double>(hp2.getLP()) * k);
            const double xHP        = static_cast<double>(sample) + hpFeedback;

            const float hpOut1 = hp1.process(static_cast<float>(xHP));
            const float hpOut2 = hp2.process(hpOut1);

            return (poleMode == 1) ? hpOut1 : hpOut2;
        }
        else
        {
            // K35 Lowpass mode (standard): input -> LP1 -> LP2 -> output
            // Feedback from LP2 output through diode clipper (tanh waveshaper)
            const double feedbackSignal = std::tanh(static_cast<double>(lp2.getLP()) * k);
            const double x              = static_cast<double>(sample) + feedbackSignal;

            const float lpOut1 = lp1.process(static_cast<float>(x));
            const float lpOut2 = lp2.process(lpOut1);

            return (poleMode == 1) ? lpOut1 : lpOut2;
        }
    }

    double processSample(double sample) noexcept
    {
        return static_cast<double>(process(static_cast<float>(sample)));
    }

private:
    double sampleRate = 44100.0;
    float cutoffHz    = 1000.0f;
    float resonance   = 0.0f;
    int poleMode      = 0; // 0 = 4-pole, 1 = 2-pole
    int subMode       = 0; // 0 = K35 LP, 1 = K35 HP

    VAOnePoleFilter lp1, lp2; // LP cascade
    VAOnePoleFilter hp1, hp2; // HP cascade

    void updateCoefficients() noexcept
    {
        lp1.setCutoff(cutoffHz, sampleRate);
        lp2.setCutoff(cutoffHz, sampleRate);

        // HP stages: 2x cutoff frequency (MS-20 convention)
        const float hpCutoff = std::min(cutoffHz * 2.0f, static_cast<float>(sampleRate * 0.48));
        hp1.setCutoff(hpCutoff, sampleRate);
        hp2.setCutoff(hpCutoff, sampleRate);

        const double k = 0.1 + 6.0 * static_cast<double>(resonance);

        lp1.setFeedback(k);
        lp2.setFeedback(0.0);
        hp1.setFeedback(0.0);
        hp2.setFeedback(0.0);
    }
};
} // namespace abd::dsp
