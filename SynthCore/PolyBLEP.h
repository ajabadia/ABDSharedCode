#pragma once
#include <cmath>

namespace abd::synth
{

/**
 * @brief PolyBLEP (Polynomial Band-Limited Step) helper functions.
 * Used for antialiased generation of Saw, Pulse, and Triangle waves.
 */
class PolyBLEP
{
public:
    /**
     * @brief Computes 2-point / 4-point PolyBLEP residual for a step transition.
     * @param t Normalized phase [0.0, 1.0)
     * @param dt Phase increment per sample (frequency / sampleRate)
     * @return BLEP correction value to subtract/add to naive waveform
     */
    static inline float getResidual(float t, float dt) noexcept
    {
        if (dt <= 0.0f) return 0.0f;

        // 0 <= t < dt (Just after discontinuity)
        if (t < dt)
        {
            float ratio = t / dt;
            return ratio + ratio - ratio * ratio - 1.0f;
        }
        // 1 - dt < t <= 1 (Just before discontinuity)
        else if (t > 1.0f - dt)
        {
            float ratio = (t - 1.0f) / dt;
            return ratio * ratio + ratio + ratio + 1.0f;
        }

        return 0.0f;
    }

    /**
     * @brief Computes PolyBLAMP residual for integrated step (used in Triangle wave).
     */
    static inline float getResidualIntegrated(float t, float dt) noexcept
    {
        if (dt <= 0.0f) return 0.0f;

        if (t < dt)
        {
            float ratio = t / dt;
            return (dt / 3.0f) * (ratio * ratio * ratio - 3.0f * ratio * ratio + 3.0f * ratio - 1.0f);
        }
        else if (t > 1.0f - dt)
        {
            float ratio = (t - 1.0f) / dt;
            return (dt / 3.0f) * (ratio * ratio * ratio + 3.0f * ratio * ratio + 3.0f * ratio + 1.0f);
        }

        return 0.0f;
    }
};

} // namespace abd::synth
