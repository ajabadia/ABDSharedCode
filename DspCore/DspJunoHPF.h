/*
  ==============================================================================

    DspJunoHPF.h
    Modelado analógico y TPT del filtro pasa-altos (HPF) de la familia Roland Juno:
      - Juno-106: Filtro Bass Boost biquad derivado de circuito analógico + corte TPT.
      - Juno-60: Frecuencias fijas conmutadas mediante selector analógico CD4051B.
      - Juno-6: Control continuo mediante interpolación monótona PCHIP medida.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspCore (namespace abd::dsp).
    100% C++ puro, zero-alloc, RT-safe, sin dependencias de JUCE.

  ==============================================================================
*/

#pragma once

#include "DspCore.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace abd::dsp
{

//==============================================================================
// Modo de operación para el filtro pasa-altos conmutable (Juno-60 / Juno-106)
//==============================================================================
enum class HPFMode
{
    J106 = 0,
    J60,
    J6Continuous
};

//==============================================================================
// Frecuencias fijas de conmutación del Juno-60 (selector CD4051B)
// Obtenidas de simulación ngspice AC con carga VCA de 30kΩ
//==============================================================================
inline float getJuno60HPFFreq(int position) noexcept
{
    switch (position)
    {
        case 0:
            return 0.f; // FLAT (bypass)
        case 1:
            return 122.f; // .022µF, ngspice: 122 Hz
        case 2:
            return 269.f; // .01µF,  ngspice: 269 Hz
        case 3:
            return 571.f; // .0047µF, ngspice: 571 Hz
        default:
            return 0.f;
    }
}

//==============================================================================
// Frecuencias fijas de conmutación del Juno-106
// Posición 0: Refuerzo de graves (Bass Boost, centinela -1.f)
// Posición 1: FLAT (bypass)
//==============================================================================
inline float getJuno106HPFFreq(int position) noexcept
{
    switch (position)
    {
        case 0:
            return -1.f; // Bass boost
        case 1:
            return 0.f; // FLAT (bypass)
        case 2:
            return 236.f; // .015µF
        case 3:
            return 754.f; // .0047µF
        default:
            return 0.f;
    }
}

//==============================================================================
// Curva continua del potenciómetro HPF del Juno-6 (PCHIP cúbico de Hermite)
// Mapea la entrada normalizada [0..1] a frecuencia de corte en Hz (38.6 – 1394.2 Hz)
// basada en 11 puntos medidos en hardware real.
//==============================================================================
inline float getJuno6HPFFreqPCHIP(float x) noexcept
{
    static constexpr float y[] = {
        38.6f, 83.5f, 181.3f, 394.7f, 418.4f,
        437.1f, 455.8f, 605.5f, 988.6f, 1183.2f, 1394.2f};
    static constexpr int N   = 11;
    static constexpr float h = 0.1f;

    if (x <= 0.0f) return y[0];
    if (x >= 1.0f) return y[N - 1];

    float x_scaled = x * 10.0f;
    int i          = static_cast<int>(x_scaled);
    if (i >= N - 1) i = N - 2;
    float t = x_scaled - static_cast<float>(i);

    auto get_slope = [&](int idx) noexcept -> float {
        if (idx <= 0 || idx >= N - 1) return 0.0f;
        float d_prev = (y[idx] - y[idx - 1]) / h;
        float d_next = (y[idx + 1] - y[idx]) / h;
        if (d_prev * d_next <= 0.0f) return 0.0f;
        return 2.0f / (1.0f / d_prev + 1.0f / d_next);
    };

    float m_i    = get_slope(i);
    float m_next = get_slope(i + 1);

    float t2  = t * t;
    float t3  = t2 * t;
    float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 = t3 - 2.0f * t2 + t;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 = t3 - t2;

    return h00 * y[i] + h10 * h * m_i + h01 * y[i + 1] + h11 * h * m_next;
}

//==============================================================================
/** Biquad de refuerzo de graves (Bass Boost) derivado del circuito analógico
    del Juno-106 (Posición 0 de HPF).
    Implementado en Direct Form II Transposed con estado interno en double.
*/
struct BassBoostFilter
{
    static constexpr float kR1  = 47e3f;
    static constexpr float kC1  = 0.047e-6f;
    static constexpr float kCA  = 0.01e-6f;
    static constexpr float kRg  = 10e3f;
    static constexpr float kRf  = 100e3f;
    static constexpr float kCf  = 0.022e-6f;
    static constexpr float kR43 = 47e3f;
    static constexpr float kR44 = 220e3f;
    static constexpr float kR45 = 47e3f;

    float b0 = 1.f, b1 = 0.f, b2 = 0.f;
    float a1 = 0.f, a2 = 0.f;
    double z1 = 0.0, z2 = 0.0;

    void init(double sampleRate) noexcept
    {
        init(static_cast<float>(sampleRate));
    }

    void init(float sampleRate) noexcept
    {
        const float tau_1z = kR1 * kC1;
        const float tau_1p = kR1 * (kC1 + kCA);
        const float tau_2z = (kRg * kRf / (kRg + kRf)) * kCf;
        const float tau_2p = kRf * kCf;

        const float G2_dc  = 1.f + kRf / kRg;
        const float alpha  = kR45 / kR44;
        const float direct = kR45 / kR43;

        const float D0 = 1.f;
        const float D1 = tau_1p + tau_2p;
        const float D2 = tau_1p * tau_2p;

        const float Nb0 = 1.f;
        const float Nb1 = tau_1z + tau_2z;
        const float Nb2 = tau_1z * tau_2z;

        const float ag = alpha * G2_dc;
        const float N0 = direct * D0 + ag * Nb0;
        const float N1 = direct * D1 + ag * Nb1;
        const float N2 = direct * D2 + ag * Nb2;

        const float K  = 2.f * sampleRate;
        const float K2 = K * K;
        const float a0 = D0 + D1 * K + D2 * K2;
        b0             = (N0 + N1 * K + N2 * K2) / a0;
        b1             = 2.f * (N0 - N2 * K2) / a0;
        b2             = (N0 - N1 * K + N2 * K2) / a0;
        a1             = 2.f * (D0 - D2 * K2) / a0;
        a2             = (D0 - D1 * K + D2 * K2) / a0;

        reset();
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    float process(float x) noexcept
    {
        float y = b0 * x + static_cast<float>(z1);
        z1      = b1 * x - a1 * y + z2;
        z2      = b2 * x - a2 * y;
        return y;
    }
};

//==============================================================================
/** JunoHPFSwitched — Filtro pasa-altos conmutable por posiciones (Juno-60 / Juno-106)
    o modo continuo Juno-6.

    Semántica de hardware:
      - Juno-106 Pos 0: Refuerzo de graves analógico (filtro HPF en bypass).
      - Posición FLAT (J60 pos 0, J106 pos 1): Bypass directo.
      - Posiciones de corte: 1-polo TPT HPF.
*/
struct JunoHPFSwitched
{
    BassBoostFilter bassBoost;

    // 1-pole TPT state
    float hpState = 0.f;
    float hpG     = 0.f;

    HPFMode mode        = HPFMode::J106;
    int currentPos      = 1;
    float currentFreqHz = 0.f;
    float sampleRate    = 44100.f;
    float bassBoostGain = 1.0f;

    void prepare(double sr) noexcept
    {
        prepare(static_cast<float>(sr));
    }

    void prepare(float sr) noexcept
    {
        sampleRate = sr;
        bassBoost.init(sr);
        reset();
    }

    void reinit(double sr) noexcept
    {
        reinit(static_cast<float>(sr));
    }

    void reinit(float sr) noexcept
    {
        sampleRate = sr;
        bassBoost.init(sr);
    }

    void reset() noexcept
    {
        hpState = 0.f;
        bassBoost.reset();
    }

    void setMode(HPFMode newMode) noexcept
    {
        mode = newMode;
    }

    // Call when position or calibration values change (for J106 and J60 discrete modes)
    void setPosition(int pos, float freq1Hz = 0.f, float freq2Hz = 0.f, float freq3Hz = 0.f, float bbGain = 1.0f) noexcept
    {
        currentPos    = std::clamp(pos, 0, 3);
        bassBoostGain = bbGain;

        if (mode == HPFMode::J6Continuous)
        {
            // Map pos (0-3) to continuous value 0..1
            setContinuousPosition(static_cast<float>(pos) / 3.0f, bbGain);
            return;
        }

        if (mode == HPFMode::J60)
        {
            currentFreqHz = getJuno60HPFFreq(currentPos);
            if (currentPos == 1 && freq1Hz > 0.f)
                currentFreqHz = freq1Hz;
            else if (currentPos == 2 && freq2Hz > 0.f)
                currentFreqHz = freq2Hz;
            else if (currentPos == 3 && freq3Hz > 0.f)
                currentFreqHz = freq3Hz;
        }
        else // J106
        {
            currentFreqHz = getJuno106HPFFreq(currentPos);
            if (currentPos == 1 && freq1Hz >= 0.f)
                currentFreqHz = freq1Hz;
            else if (currentPos == 2 && freq2Hz > 0.f)
                currentFreqHz = freq2Hz;
            else if (currentPos == 3 && freq3Hz > 0.f)
                currentFreqHz = freq3Hz;
        }

        updateCoefs();
    }

    // Set HPF directly to a continuous value (Juno-6 mode)
    void setContinuousPosition(float sliderVal, float bbGain = 1.0f) noexcept
    {
        bassBoostGain = bbGain;
        currentFreqHz = getJuno6HPFFreqPCHIP(sliderVal);
        updateCoefs();
    }

    void updateCoefs() noexcept
    {
        if (currentFreqHz > 0.f)
        {
            float fc = std::min(currentFreqHz / sampleRate, 0.49f);
            hpG      = std::tan(MathConstants<float>::pi * fc);
        }
        else
        {
            hpG = 0.f;
        }
    }

    // Process one sample
    float process(float x) noexcept
    {
        // Bass Boost (Juno-106 mode position 0 has currentFreqHz == -1.f)
        if (mode == HPFMode::J106 && currentFreqHz < 0.f)
        {
            return bassBoost.process(x) * bassBoostGain;
        }

        // FLAT (bypass)
        if (currentFreqHz <= 0.f)
        {
            return x;
        }

        // 1-pole TPT HPF
        if (hpG <= 0.f) return x;
        float v  = (x - hpState) * hpG / (1.f + hpG);
        float lp = hpState + v;
        hpState  = lp + v;
        return x - lp;
    }

    void processBlock(float* buffer, size_t numSamples) noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
            buffer[i] = process(buffer[i]);
    }
};

//==============================================================================
/** JunoHPFContinuous — Filtro pasa-altos híbrido continuo con refuerzo de graves
    independiente (utilizado en ABDEep).

    Semántica en cascada serie:
      x -> 1-pole TPT HPF (si cutoff > 0) -> BassBoost (si activo) -> y
*/
struct JunoHPFContinuous
{
    BassBoostFilter bassBoost;

    // 1-pole TPT state
    float hpState = 0.f;
    float hpG     = 0.f;

    float currentFreqHz  = 0.f;
    float sampleRate     = 44100.f;
    float bassBoostGain  = 1.0f;
    bool bassBoostActive = false;

    void prepare(double sr) noexcept
    {
        sampleRate = static_cast<float>(sr);
        bassBoost.init(static_cast<float>(sr));
        reset();
    }

    void prepare(float sr) noexcept
    {
        sampleRate = sr;
        bassBoost.init(sr);
        reset();
    }

    void reset() noexcept
    {
        hpState = 0.f;
        bassBoost.reset();
    }

    // Set HPF cutoff directly in Hz (0 = FLAT/bypass)
    void setCutoff(float cutoffHz) noexcept
    {
        float clamped = std::max(cutoffHz, 0.f);
        if (clamped == currentFreqHz) return; // hot-path guard
        currentFreqHz = clamped;
        updateCoefs();
    }

    // Set HPF from continuous slider value [0..1]
    // Maps through PCHIP interpolation to 38.6–1394.2 Hz
    void setContinuousPosition(float sliderVal) noexcept
    {
        float freq = getJuno6HPFFreqPCHIP(sliderVal);
        if (freq == currentFreqHz) return; // hot-path guard
        currentFreqHz = freq;
        updateCoefs();
    }

    void setBassBoostGain(float gain) noexcept
    {
        bassBoostGain = std::clamp(gain, 0.1f, 3.0f);
    }

    void setBassBoostActive(bool active) noexcept
    {
        bassBoostActive = active;
    }

    void updateCoefs() noexcept
    {
        if (currentFreqHz > 0.f)
        {
            float fc = std::min(currentFreqHz / sampleRate, 0.49f);
            hpG      = std::tan(MathConstants<float>::pi * fc);
        }
        else
        {
            hpG = 0.f;
        }
    }

    // Process one sample
    float process(float x) noexcept
    {
        float y = x;

        // 1-pole TPT HPF (active when cutoff > 0)
        if (hpG > 0.f)
        {
            float v  = (y - hpState) * hpG / (1.f + hpG);
            float lp = hpState + v;
            hpState  = lp + v;
            y        = y - lp;
        }

        // Bass boost (independent, always available)
        if (bassBoostActive)
        {
            y = bassBoost.process(y) * bassBoostGain;
        }

        return y;
    }

    void processBlock(float* buffer, size_t numSamples) noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
            buffer[i] = process(buffer[i]);
    }
};

} // namespace abd::dsp
