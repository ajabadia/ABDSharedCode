#pragma once
#include <cmath>

namespace abd::synth {

/**
 * @brief PolyBLEP (Polynomial Band-Limited Step) helper functions.
 * Used for antialiased generation of Saw, Pulse, and Triangle waves.
 */
class PolyBLEP {
public:
    /**
     * @brief Computes 2-point / 4-point PolyBLEP residual for a step transition.
     * @param t Normalized phase [0.0, 1.0)
     * @param dt Phase increment per sample (frequency / sampleRate)
     * @return BLEP correction value to subtract/add to naive waveform
     */
    static float getResidual(float t, float dt) noexcept;

    /**
     * @brief Computes PolyBLAMP residual for integrated step (used in Triangle wave).
     */
    static float getResidualIntegrated(float t, float dt) noexcept;
};

} // namespace abd::synth
