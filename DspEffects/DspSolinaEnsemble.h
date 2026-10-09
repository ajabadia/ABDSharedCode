/*
  ==============================================================================

    DspSolinaEnsemble.h
    Efecto Ensemble Chorus trifásico estilo Solina String Ensemble / ARP Omni.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspSolinaEnsemble.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Emulación del circuito de conjunto de cuerdas (String Ensemble) del mítico
      Eminent 310 Unique / ARP Solina String Ensemble.
      El circuito original utiliza tres líneas de retardo BBD (TDA1022 / TCA350) en
      paralelo moduladas por tres fases a 0°, 120° y 240°, generando la riqueza
      espectral y la densidad polifónica de una sección de cuerdas orquestal.

    DISEÑO DSP:
      - 3 Taps de retardo por canal con desfases angulares de 0°, 120° y 240°.
      - Interpolación cúbica Hermite de 4 puntos para lectura libre de artefactos.
      - Retardo base de 8 ms con modulación de profundidad de hasta 5 ms.
      - Separación estéreo (Spread) que desfasa simétricamente los taps entre canales L y R.
      - Realimentación resonante (Feedback) y control de mezcla Dry/Wet.
      - Buffer circular estático en array (cero asignaciones dinámicas en memoria, 100% RT-Safe).

    RENDIMIENTO:
      - Cero asignaciones en memoria dinámica (100% Real-Time Safe).
      - CERO dependencias de JUCE (C++20 estándar puro).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace abd::dsp
{

class DspSolinaEnsemble
{
public:
    static constexpr int kNumTaps       = 3;
    static constexpr float kBaseDelayMs = 8.0f;
    static constexpr float kMaxModMs    = 5.0f;
    // Buffer circular de 8192 muestras (cubre más de 40 ms a 192 kHz)
    static constexpr int kBufferSize = 8192;
    static constexpr int kBufferMask = kBufferSize - 1;

    DspSolinaEnsemble() noexcept
    {
        reset();
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        updateLFOIncrement();
        reset();
    }

    void reset() noexcept
    {
        paramRate_     = 0.4f;
        paramDepth_    = 0.5f;
        paramFeedback_ = 0.3f;
        paramSpread_   = 0.6f;
        paramMix_      = 0.5f;

        delayBufL_.fill(0.0f);
        delayBufR_.fill(0.0f);

        for (int t = 0; t < kNumTaps; ++t)
            lfoPhase_[t] = kPhaseOffsets[t];

        delayWPos_ = 0;
        feedbackL_ = 0.0f;
        feedbackR_ = 0.0f;
        updateLFOIncrement();
    }

    void setRateNorm(float rateNorm) noexcept
    {
        paramRate_ = std::clamp(rateNorm, 0.0f, 1.0f);
        updateLFOIncrement();
    }

    void setDepthNorm(float depthNorm) noexcept
    {
        paramDepth_ = std::clamp(depthNorm, 0.0f, 1.0f);
    }

    void setFeedbackNorm(float feedbackNorm) noexcept
    {
        paramFeedback_ = std::clamp(feedbackNorm, 0.0f, 1.0f) * 0.85f;
    }

    void setSpreadNorm(float spreadNorm) noexcept
    {
        paramSpread_ = std::clamp(spreadNorm, 0.0f, 1.0f);
    }

    void setMixNorm(float mixNorm) noexcept
    {
        paramMix_ = std::clamp(mixNorm, 0.0f, 1.0f);
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float spreadAngle      = paramSpread_ * 3.14159265358979323846f;
        const float depthMs          = paramDepth_ * kMaxModMs;
        const float baseDelaySamples = kBaseDelayMs * 0.001f * static_cast<float>(sampleRate_);
        const float maxModSamples    = depthMs * 0.001f * static_cast<float>(sampleRate_);
        constexpr float tapGains[3]  = {0.55f, 0.55f, 0.55f};
        constexpr float normFactor   = 1.0f / static_cast<float>(kNumTaps);

        for (int s = 0; s < numSamples; ++s)
        {
            const float dryL = inL[s];
            const float dryR = inR[s];

            // 1. Avance de las 3 fases del LFO
            for (int t = 0; t < kNumTaps; ++t)
            {
                lfoPhase_[t] += lfoInc_;
                if (lfoPhase_[t] > 6.28318530717958647692f)
                    lfoPhase_[t] -= 6.28318530717958647692f;
            }

            // 2. Suma de los 3 taps modulados
            float sumL = 0.0f;
            float sumR = 0.0f;

            for (int t = 0; t < kNumTaps; ++t)
            {
                const float mod    = std::sin(lfoPhase_[t]);
                float delaySamples = baseDelaySamples + mod * maxModSamples;
                if (delaySamples < 1.0f)
                    delaySamples = 1.0f;

                const float spreadOffsetL = spreadAngle * static_cast<float>(t - 1) * 0.3f;
                const float spreadOffsetR = -spreadOffsetL;

                const float delayedL = readDelay(delayBufL_, delaySamples - spreadOffsetL * maxModSamples * 0.3f);
                const float delayedR = readDelay(delayBufR_, delaySamples + spreadOffsetR * maxModSamples * 0.3f);

                sumL += delayedL * tapGains[t];
                sumR += delayedR * tapGains[t];
            }

            sumL *= normFactor;
            sumR *= normFactor;

            // 3. Inyección de entrada con feedback
            const float inputL = dryL + feedbackL_ * paramFeedback_;
            const float inputR = dryR + feedbackR_ * paramFeedback_;

            // 4. Escritura en el buffer circular
            delayBufL_[delayWPos_] = inputL;
            delayBufR_[delayWPos_] = inputR;
            delayWPos_             = (delayWPos_ + 1) & kBufferMask;

            // 5. Actualización de realimentación
            feedbackL_ = sumL;
            feedbackR_ = sumR;

            // 6. Mezcla Dry/Wet
            outL[s] = dryL * (1.0f - paramMix_) + sumL * paramMix_;
            outR[s] = dryR * (1.0f - paramMix_) + sumR * paramMix_;
        }
    }

private:
    void updateLFOIncrement() noexcept
    {
        const float rateHz = 0.2f + 7.8f * paramRate_;
        lfoInc_            = static_cast<float>(6.28318530717958647692 * rateHz / sampleRate_);
    }

    static inline float hermite(float frac, float y0, float y1, float y2, float y3) noexcept
    {
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    inline float readDelay(const std::array<float, kBufferSize>& buf, float delaySamples) const noexcept
    {
        float readPos = static_cast<float>(delayWPos_) - delaySamples;
        if (readPos < 0.0f)
            readPos += static_cast<float>(kBufferSize);

        const int i0     = static_cast<int>(readPos) & kBufferMask;
        const float frac = readPos - static_cast<float>(static_cast<int>(readPos));
        const int i1     = (i0 + 1) & kBufferMask;
        const int i2     = (i0 + 2) & kBufferMask;
        const int i3     = (i0 + 3) & kBufferMask;

        return hermite(frac, buf[i0], buf[i1], buf[i2], buf[i3]);
    }

    double sampleRate_   = 44100.0;
    float paramRate_     = 0.4f;
    float paramDepth_    = 0.5f;
    float paramFeedback_ = 0.3f;
    float paramSpread_   = 0.6f;
    float paramMix_      = 0.5f;

    std::array<float, kBufferSize> delayBufL_{};
    std::array<float, kBufferSize> delayBufR_{};
    int delayWPos_ = 0;

    float feedbackL_ = 0.0f;
    float feedbackR_ = 0.0f;

    static constexpr float kPhaseOffsets[3] = {0.0f, 2.0943951023931953f, 4.1887902047863905f};
    float lfoPhase_[3]                      = {0.0f, 2.0943951f, 4.1887902f};
    float lfoInc_                           = 0.0f;
};

} // namespace abd::dsp
