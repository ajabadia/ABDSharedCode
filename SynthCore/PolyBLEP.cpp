#include "PolyBLEP.h"

namespace abd::synth {

float PolyBLEP::getResidual(float t, float dt) noexcept
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

float PolyBLEP::getResidualIntegrated(float t, float dt) noexcept
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

} // namespace abd::synth
