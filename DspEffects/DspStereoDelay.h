/*
  ==============================================================================

    DspStereoDelay.h
    Línea de retardo estéreo con topologías multimodo y filtrado analógico de feedback.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspStereoDelay.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Motor de retardo digital/analógico de alta resolución (DeepMind 12 FX Type 13),
      inspirado en procesadores clásicos como el Roland SDE-3000 y Korg SDD-3000.
      Características:
        - Tiempo base maestro: 1 ms a 2000 ms (curva cuadrática musical).
        - Sub-divisiones métricas independientes por canal (Factor L/R): 1/4, 3/8, 1/2, 2/3, 1/1, 4/3, 3/2, 2/1, 3/1.
        - Desplazamiento micro-temporal estéreo (Offset): -100 ms a +100 ms.
        - Cuatro topologías de realimentación (Modes):
          * Mode 0 (ST): Estéreo desacoplado independiente.
          * Mode 1 (X): Realimentación cruzada (Cross-Feedback Ping-Pong).
          * Mode 2 (M): Suma monofónica en la cadena de retardo y feedback.
          * Mode 3 (P-P): Ping-Pong estricto asimétrico.
        - Filtro paso-bajos analógico de 1-polo en el lazo de feedback (200 Hz a 20 kHz)
          para simular la amortiguación natural de altas frecuencias.

    RENDIMIENTO:
      - 100% Real-Time Safe: Memoria asignada únicamente en prepare(). Cero heap en process().
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

class DspStereoDelay
{
public:
    enum RoutingMode
    {
        kStereo = 0,
        kCross = 1,
        kMono = 2,
        kPingPong = 3
    };

    DspStereoDelay()
    {
        reset();
    }

    void prepare(double sampleRate)
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        
        // Capacidad máxima de 2.2 segundos para cubrir márgenes y offset
        const int maxSamples = static_cast<int>(sampleRate_ * 2.2) + 1024;
        delayBufL_.assign(static_cast<size_t>(maxSamples), 0.0f);
        delayBufR_.assign(static_cast<size_t>(maxSamples), 0.0f);

        writePosL_ = 0;
        writePosR_ = 0;
        updateDelaySamples();
        updateLPFCoeff();
        reset();
    }

    void reset() noexcept
    {
        std::fill(delayBufL_.begin(), delayBufL_.end(), 0.0f);
        std::fill(delayBufR_.begin(), delayBufR_.end(), 0.0f);
        writePosL_ = 0;
        writePosR_ = 0;
        lpfStateL_ = 0.0f;
        lpfStateR_ = 0.0f;
    }

    void setTimeNorm(float norm) noexcept     { timeParam_ = std::clamp(norm, 0.0f, 1.0f); updateDelaySamples(); }
    void setMode(int mode) noexcept           { mode_ = std::clamp(mode, 0, 3); }
    void setModeNorm(float norm) noexcept     { mode_ = std::clamp(static_cast<int>(norm * 3.99f), 0, 3); }
    void setFactorLNorm(float norm) noexcept  { factorL_ = std::clamp(norm, 0.0f, 1.0f); updateDelaySamples(); }
    void setFactorRNorm(float norm) noexcept  { factorR_ = std::clamp(norm, 0.0f, 1.0f); updateDelaySamples(); }
    void setOffsetNorm(float norm) noexcept   { offsetParam_ = std::clamp(norm, 0.0f, 1.0f); updateDelaySamples(); }
    void setFeedbackLNorm(float norm) noexcept{ feedbackL_ = std::clamp(norm, 0.0f, 1.0f); }
    void setFeedbackRNorm(float norm) noexcept{ feedbackR_ = std::clamp(norm, 0.0f, 1.0f); }
    void setHiCutNorm(float norm) noexcept    { lpfCutoff_ = std::clamp(norm, 0.0f, 1.0f); updateLPFCoeff(); }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const int bufSize = static_cast<int>(delayBufL_.size());
        if (bufSize <= 0) return;

        float* dL = delayBufL_.data();
        float* dR = delayBufR_.data();

        const float fbL = feedbackL_ * 0.99f;
        const float fbR = feedbackR_ * 0.99f;

        for (int s = 0; s < numSamples; ++s)
        {
            const float dryL = inL[s];
            const float dryR = inR[s];

            int readPosL = writePosL_ - delaySamplesL_;
            if (readPosL < 0) readPosL += bufSize;

            int readPosR = writePosR_ - delaySamplesR_;
            if (readPosR < 0) readPosR += bufSize;

            const float delayedL = dL[readPosL];
            const float delayedR = dR[readPosR];

            // Filtro pasa-bajos en el camino de retorno
            lpfStateL_ += lpfCoeff_ * (delayedL - lpfStateL_);
            lpfStateR_ += lpfCoeff_ * (delayedR - lpfStateR_);

            float writeL = 0.0f;
            float writeR = 0.0f;

            switch (mode_)
            {
                case kCross: // Realimentación cruzada
                    writeL = dryL + lpfStateR_ * fbR;
                    writeR = dryR + lpfStateL_ * fbL;
                    break;
                case kMono: // Mezcla mono
                {
                    const float mono = (lpfStateL_ + lpfStateR_) * 0.5f;
                    const float fb = (fbL + fbR) * 0.5f;
                    writeL = dryL + mono * fb;
                    writeR = dryR + mono * fb;
                    break;
                }
                case kPingPong: // Ping-Pong
                    writeL = dryL + lpfStateL_ * fbL;
                    writeR = dryR;
                    break;
                case kStereo:
                default:
                    writeL = dryL + lpfStateL_ * fbL;
                    writeR = dryR + lpfStateR_ * fbR;
                    break;
            }

            dL[writePosL_] = writeL;
            dR[writePosR_] = writeR;

            writePosL_ = (writePosL_ + 1) % bufSize;
            writePosR_ = (writePosR_ + 1) % bufSize;

            // 100% wet
            outL[s] = delayedL;
            outR[s] = delayedR;
        }
    }

private:
    static float factorToScale(float normalized) noexcept
    {
        static constexpr float scales[] = { 0.25f, 0.375f, 0.5f, 0.6667f, 1.0f, 1.3333f, 1.5f, 2.0f, 3.0f };
        const int idx = std::clamp(static_cast<int>(normalized * 8.99f), 0, 8);
        return scales[idx];
    }

    void updateDelaySamples() noexcept
    {
        const float masterMs = 1.0f + 1999.0f * (timeParam_ * timeParam_);
        float leftMs = masterMs * factorToScale(factorL_);
        const float offsetMs = (offsetParam_ - 0.5f) * 200.0f;
        float rightMs = masterMs * factorToScale(factorR_) + offsetMs;

        leftMs = std::clamp(leftMs, 1.0f, 2000.0f);
        rightMs = std::clamp(rightMs, 1.0f, 2000.0f);

        delaySamplesL_ = std::max(1, static_cast<int>(sampleRate_ * leftMs * 0.001));
        delaySamplesR_ = std::max(1, static_cast<int>(sampleRate_ * rightMs * 0.001));
    }

    void updateLPFCoeff() noexcept
    {
        float freqHz = 200.0f * std::pow(100.0f, lpfCutoff_);
        freqHz = std::min(freqHz, static_cast<float>(sampleRate_ * 0.45));
        lpfCoeff_ = static_cast<float>(std::exp(-6.28318530717958647692 * freqHz / sampleRate_));
    }

    double sampleRate_ = 44100.0;
    float timeParam_   = 0.5f;
    int   mode_        = kStereo;
    float factorL_     = 0.5f;
    float factorR_     = 0.5f;
    float offsetParam_ = 0.5f;
    float feedbackL_   = 0.3f;
    float feedbackR_   = 0.3f;
    float lpfCutoff_   = 0.8f;

    std::vector<float> delayBufL_;
    std::vector<float> delayBufR_;
    int writePosL_     = 0;
    int writePosR_     = 0;
    int delaySamplesL_ = 1000;
    int delaySamplesR_ = 1000;

    float lpfStateL_ = 0.0f;
    float lpfStateR_ = 0.0f;
    float lpfCoeff_  = 0.5f;
};

} // namespace abd::dsp
