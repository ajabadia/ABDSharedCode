/*
  ==============================================================================

    DspTapeDelay.h
    Emulación de eco de cinta magnética analógica estilo T-Ray con wow/flutter y saturación.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspTapeDelay.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Efecto de eco de cinta magnética analógica (DeepMind 12 FX Type 21 T-Ray).
      Emula las características físicas y acústicas de las unidades de cinta magnética:
        - Tiempo de retardo ajustable: 10 ms a 1500 ms.
        - Arrastre mecánico e irregularidad del motor (Wobble): oscilación LFO de baja frecuencia (~1.8 Hz)
          modulando la distancia de retardo en hasta 2.5 ms.
        - Saturación magnética suave no lineal por histéresis en el cabezal de grabación (curva tanh).
        - Filtro Tone paso-bajos analógico de 1-polo en el lazo de realimentación (400 Hz a 15.4 kHz).
        - Control de persistencia y retroalimentación (Sustain) hasta un 95%.

    RENDIMIENTO:
      - 100% Real-Time Safe: Búfer pre-asignado en prepare(), cero heap en process().
      - CERO dependencias de JUCE (C++20 estándar puro).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace abd::dsp
{

class DspTapeDelay
{
public:
    DspTapeDelay()
    {
        reset();
    }

    void prepare(double sampleRate)
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;

        // Máximo 2 segundos
        const int maxSamples = static_cast<int>(sampleRate_ * 2.0) + 1024;
        bufL_.assign(static_cast<size_t>(maxSamples), 0.0f);
        bufR_.assign(static_cast<size_t>(maxSamples), 0.0f);

        lfoInc_ = 1.8 / sampleRate_; // ~1.8 Hz wow/flutter
        updateDelay();
        reset();
    }

    void reset() noexcept
    {
        std::fill(bufL_.begin(), bufL_.end(), 0.0f);
        std::fill(bufR_.begin(), bufR_.end(), 0.0f);
        writePosL_ = 0;
        writePosR_ = 0;
        lfoPhase_  = 0.0;
        lpfL_      = 0.0f;
        lpfR_      = 0.0f;
    }

    void setMixNorm(float norm) noexcept { mix_ = std::clamp(norm, 0.0f, 1.0f); }
    void setDelayNorm(float norm) noexcept
    {
        delayPct_ = std::clamp(norm, 0.0f, 1.0f);
        updateDelay();
    }
    void setSustainNorm(float norm) noexcept { sustain_ = std::clamp(norm, 0.0f, 1.0f); }
    void setWobbleNorm(float norm) noexcept { wobble_ = std::clamp(norm, 0.0f, 1.0f); }
    void setToneNorm(float norm) noexcept
    {
        tone_ = std::clamp(norm, 0.0f, 1.0f);
        updateDelay();
    }

    void setParameter(int index, float value) noexcept
    {
        const float val = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0:
                setMixNorm(val);
                break;
            case 1:
                setDelayNorm(val);
                break;
            case 2:
                setSustainNorm(val);
                break;
            case 3:
                setWobbleNorm(val);
                break;
            case 4:
                setToneNorm(val);
                break;
            default:
                break;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const int bufSize = static_cast<int>(bufL_.size());
        if (bufSize <= 0) return;

        float* dL = bufL_.data();
        float* dR = bufR_.data();

        const float wetMix      = mix_;
        const float susGain     = sustain_ * 0.97f;
        constexpr double kTwoPi = 6.28318530717958647692;

        for (int s = 0; s < numSamples; ++s)
        {
            // Wow/flutter: modula la velocidad de lectura
            lfoPhase_ += lfoInc_;
            if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
            const float wob = static_cast<float>(std::sin(kTwoPi * lfoPhase_)) * wobble_ * 0.03f;

            // Tape saturation (soft clip)
            const float satL = std::tanh(inL[s] * 1.2f) * 0.85f;
            const float satR = std::tanh(inR[s] * 1.2f) * 0.85f;

            // Wobble delay read position (interpolated)
            const float baseDelay = static_cast<float>(delaySamples_);
            const float wobOffset = wob * baseDelay;

            float readPosL = static_cast<float>(writePosL_) - baseDelay - wobOffset;
            if (readPosL < 0.0f) readPosL += static_cast<float>(bufSize);
            const int idxL       = static_cast<int>(readPosL) % bufSize;
            const int nextL      = (idxL + 1) % bufSize;
            const float fracL    = readPosL - static_cast<float>(static_cast<int>(readPosL));
            const float delayedL = dL[idxL] * (1.0f - fracL) + dL[nextL] * fracL;

            float readPosR = static_cast<float>(writePosR_) - baseDelay - wobOffset;
            if (readPosR < 0.0f) readPosR += static_cast<float>(bufSize);
            const int idxR       = static_cast<int>(readPosR) % bufSize;
            const int nextR      = (idxR + 1) % bufSize;
            const float fracR    = readPosR - static_cast<float>(static_cast<int>(readPosR));
            const float delayedR = dR[idxR] * (1.0f - fracR) + dR[nextR] * fracR;

            // Tone filter on feedback
            lpfL_ += toneCoeff_ * (delayedL - lpfL_);
            lpfR_ += toneCoeff_ * (delayedR - lpfR_);
            const float fbL = lpfL_ * susGain;
            const float fbR = lpfR_ * susGain;

            // Write to buffer (input + feedback)
            dL[writePosL_] = satL + fbL;
            dR[writePosR_] = satR + fbR;
            writePosL_     = (writePosL_ + 1) % bufSize;
            writePosR_     = (writePosR_ + 1) % bufSize;

            // Mix
            outL[s] = inL[s] * (1.0f - wetMix) + delayedL * wetMix;
            outR[s] = inR[s] * (1.0f - wetMix) + delayedR * wetMix;
        }
    }

private:
    void updateDelay() noexcept
    {
        // 0-100% -> 20ms - 1500ms
        const float ms            = 20.0f + 1480.0f * delayPct_;
        const int maxDelaySamples = static_cast<int>(bufL_.size());
        delaySamples_             = static_cast<int>(sampleRate_ * ms / 1000.0);
        if (maxDelaySamples > 1)
            delaySamples_ = std::max(1, std::min(delaySamples_, maxDelaySamples - 1));
        lfoInc_            = (4.0 + wobble_ * 2.0) / sampleRate_; // 4-6 Hz wobble
        const float toneHz = tone_ * 19000.0f + 1000.0f;
        toneCoeff_         = static_cast<float>(toneHz / (toneHz + sampleRate_ * 0.5));
    }

    double sampleRate_ = 44100.0;
    float mix_         = 0.3f;
    float delayPct_    = 0.3f;
    float sustain_     = 0.3f;
    float wobble_      = 0.2f;
    float tone_        = 0.5f;

    std::vector<float> bufL_;
    std::vector<float> bufR_;
    int writePosL_    = 0;
    int writePosR_    = 0;
    int delaySamples_ = 1000;

    double lfoPhase_ = 0.0;
    double lfoInc_   = 0.0;
    float lpfL_      = 0.0f;
    float lpfR_      = 0.0f;
    float toneCoeff_ = 0.5f;
};

} // namespace abd::dsp
