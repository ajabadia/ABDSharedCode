#pragma once

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace abd::dsp
{
/**
 * @brief VAOnePoleFilter: Minimal TPT ZDF (Topology-Preserving Transform / Zero-Delay Feedback)
 *        one-pole filter building block.
 *
 * Used as standard linear/resonant stage for MoogLadderVCF, KorgMS20VCF, and other cascade filters.
 *
 * Topology: TPT ZDF with bilinear transform.
 * State: z1 (double precision accumulator for numerical stability).
 * Anti-denormal protection: subnormal values flushed to zero.
 */
class VAOnePoleFilter
{
public:
    VAOnePoleFilter() noexcept = default;

    /** Reset internal integrator state. */
    void reset() noexcept
    {
        z1 = 0.0;
    }

    /**
     * Recalculate filter coefficient from cutoff frequency and sample rate.
     * @param cutoffHz  Filter cutoff in Hz (clamped to [10.0, fs * 0.49])
     * @param fs        Sample rate in Hz
     */
    void setCutoff(float cutoffHz, double fs) noexcept
    {
        const double safeFs = (fs > 1.0) ? fs : 44100.0;
        const double fc     = std::clamp(static_cast<double>(cutoffHz), 10.0, safeFs * 0.49);
        const double g      = std::tan(M_PI * fc / safeFs);
        alpha               = g / (1.0 + g); // [0..1]
    }

    /**
     * Set feedback amount (used by ladder and Sallen-Key topologies for resonance).
     */
    void setFeedback(double fb) noexcept
    {
        feedback = fb;
    }

    /**
     * Process one sample through the filter.
     * @param xn  Input sample
     * @return    Lowpass filtered output
     */
    float process(float xn) noexcept
    {
        const double x = static_cast<double>(xn);

        // TPT ZDF one-pole: feedback sums at input junction
        const double vn = alpha * (x + feedback) - z1 * alpha;
        const double lp = vn + z1;
        z1              = vn + lp; // state update: 2*vn + z1_prev

        // Anti-denormal flush
        if (std::abs(z1) < 1.0e-15)
            z1 = 0.0;

        return static_cast<float>(lp);
    }

    /**
     * Process one sample, returning both lowpass and highpass outputs.
     * Highpass is derived via subtractive complementary synthesis: hp = input - lp.
     */
    void process(float xn, float& lp, float& hp) noexcept
    {
        const double x = static_cast<double>(xn);

        const double vn    = alpha * (x + feedback) - z1 * alpha;
        const double lpOut = vn + z1;
        z1                 = vn + lpOut;

        if (std::abs(z1) < 1.0e-15)
            z1 = 0.0;

        lp = static_cast<float>(lpOut);
        hp = static_cast<float>(x - lpOut);
    }

    /** Get current lowpass estimate from state without advancing time. */
    [[nodiscard]] float getLP() const noexcept
    {
        return static_cast<float>(z1 * 0.5);
    }

private:
    double alpha    = 0.0; // feedforward coefficient [0..1]
    double feedback = 0.0; // feedback amount
    double z1       = 0.0; // integrator state (double precision)
};
} // namespace abd::dsp
