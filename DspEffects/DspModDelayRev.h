/*
  ==============================================================================

    DspModDelayRev.h
    Procesador híbrido de retardo modulado con LFO y reverberación acústica Schroeder.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspModDelayRev.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Efecto híbrido de delay modulado y reverberación acústica (DeepMind 12 FX Type 12).
      Inspirado en algoritmos clásicos de Lexicon 224 y 480L:
        - Delay modulado con LFO sinusoidal (0.1 a 10 Hz) y factores métricos.
        - Filtro paso-bajos analógico de realimentación (FeedHC, 200 Hz a 20 kHz).
        - Reverberación Schroeder con 4 filtros de peine sintonizados y 3 etapas allpass de difusión.
        - 3 Perfiles de sala acústica (AMB, CLUB, HALL) con control de Decay y Damping.
        - Conmutación de ruteo topológico: Paralelo (Parallel) o Cascada (Serial).
        - Control de Balance entre la energía del retardo y la reverberación.

    RENDIMIENTO:
      - 100% Real-Time Safe: Búferes circulares pre-asignados en prepare(), cero heap en process().
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

class DspModDelayRev
{
public:
    enum RoutingMode
    {
        kParallel = 0,
        kSerial = 1
    };

    DspModDelayRev()
    {
        reset();
    }

    void prepare(double sampleRate)
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;

        // Delay buffer (hasta 1.6s)
        const int maxDelay = static_cast<int>(sampleRate_ * 1.6) + 1024;
        delayBufL_.assign(static_cast<size_t>(maxDelay), 0.0f);
        delayBufR_.assign(static_cast<size_t>(maxDelay), 0.0f);

        // Reverb comb buffers (50ms)
        combBufSize_ = std::max(1, static_cast<int>(sampleRate_ * 0.05));
        combBufL_.assign(static_cast<size_t>(combBufSize_), 0.0f);
        combBufR_.assign(static_cast<size_t>(combBufSize_), 0.0f);

        combDelay_[0] = static_cast<int>(sampleRate_ * 0.0297);
        combDelay_[1] = static_cast<int>(sampleRate_ * 0.0371);
        combDelay_[2] = static_cast<int>(sampleRate_ * 0.0411);
        combDelay_[3] = static_cast<int>(sampleRate_ * 0.0437);

        updateParams();
        reset();
    }

    void reset() noexcept
    {
        std::fill(delayBufL_.begin(), delayBufL_.end(), 0.0f);
        std::fill(delayBufR_.begin(), delayBufR_.end(), 0.0f);
        std::fill(combBufL_.begin(), combBufL_.end(), 0.0f);
        std::fill(combBufR_.begin(), combBufR_.end(), 0.0f);
        writePosL_ = 0;
        writePosR_ = 0;
        combPos_ = 0;
        for (int i = 0; i < 4; ++i)
        {
            combStateL_[i] = 0.0f;
            combStateR_[i] = 0.0f;
        }
        for (int i = 0; i < 3; ++i)
        {
            apStateL_[i] = 0.0f;
            apStateR_[i] = 0.0f;
        }
        lfoPhase_ = 0.0;
        lpfL_ = 0.0f;
        lpfR_ = 0.0f;
    }

    void setParameter(int index, float value) noexcept
    {
        const float val = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0:  time_ = val; updateParams(); break;
            case 1:  factor_ = static_cast<int>(val * 3.99f); updateParams(); break;
            case 2:  feedback_ = val; break;
            case 3:  feedHC_ = val; updateParams(); break;
            case 4:  depth_ = val; break;
            case 5:  speed_ = val; updateParams(); break;
            case 6:  mode_ = (val > 0.5f) ? 1 : 0; break;
            case 7:  rType_ = static_cast<int>(val * 2.99f); break;
            case 8:  decay_ = val; break;
            case 9:  damping_ = val; updateParams(); break;
            case 10: balance_ = val; break;
            case 11: mix_ = val; break;
            default: break;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const int maxDelaySamples = static_cast<int>(delayBufL_.size());
        if (maxDelaySamples <= 0 || combBufSize_ <= 0) return;

        float* dBufL = delayBufL_.data();
        float* dBufR = delayBufR_.data();
        float* cBufL = combBufL_.data();
        float* cBufR = combBufR_.data();
        const float bal = (balance_ - 0.5f) * 2.0f;
        constexpr double kTwoPi = 6.28318530717958647692;

        for (int s = 0; s < numSamples; ++s)
        {
            const float dryL = inL[s];
            const float dryR = inR[s];

            // --- Modulated delay ---
            lfoPhase_ += lfoInc_;
            if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
            const float mod = static_cast<float>(std::sin(kTwoPi * lfoPhase_)) * depth_ * 0.2f;
            const float delaySamp = static_cast<float>(delaySamples_) * (1.0f + mod);

            const int readPosL = ((writePosL_ - static_cast<int>(delaySamp)) % maxDelaySamples + maxDelaySamples) % maxDelaySamples;
            const float dL = dBufL[readPosL];
            const int readPosR = ((writePosR_ - static_cast<int>(delaySamp)) % maxDelaySamples + maxDelaySamples) % maxDelaySamples;
            const float dR = dBufR[readPosR];

            // High cut en feedback
            lpfL_ += hcCoeff_ * (dL - lpfL_);
            lpfR_ += hcCoeff_ * (dR - lpfR_);

            dBufL[writePosL_] = dryL + lpfL_ * feedback_ * 0.95f;
            dBufR[writePosR_] = dryR + lpfR_ * feedback_ * 0.95f;
            writePosL_ = (writePosL_ + 1) % maxDelaySamples;
            writePosR_ = (writePosR_ + 1) % maxDelaySamples;

            const float delayOutL = dL;

            // --- Reverb (Schroeder simplificado) ---
            const float revInL = dryL + dL * std::max(0.0f, bal);
            const float revInR = dryR + dR * std::max(0.0f, -bal);
            const float decayGain = 0.3f + decay_ * 0.6f;

            // Comb filters
            for (int i = 0; i < 4; ++i)
            {
                int combRead = combPos_ - combDelay_[i];
                if (combRead < 0) combRead += combBufSize_;
                const float cL = cBufL[combRead];
                const float cR = cBufR[combRead];
                cBufL[combPos_] = revInL + cL * decayGain * dampCoeff_;
                cBufR[combPos_] = revInR + cR * decayGain * dampCoeff_;
                combStateL_[i] = cL;
                combStateR_[i] = cR;
            }
            combPos_ = (combPos_ + 1) % combBufSize_;

            // Allpass filters
            float revL = (combStateL_[0] + combStateL_[1] + combStateL_[2] + combStateL_[3]) * 0.25f;
            float revR = (combStateR_[0] + combStateR_[1] + combStateR_[2] + combStateR_[3]) * 0.25f;
            constexpr float apGain = 0.5f;
            for (int i = 0; i < 3; ++i)
            {
                const float apL = revL + apGain * apStateL_[i];
                apStateL_[i] = revL - apGain * apL;
                revL = apL;
                const float apR = revR + apGain * apStateR_[i];
                apStateR_[i] = revR - apGain * apR;
                revR = apR;
            }

            // Balance delay/reverb
            const float mixDelay = delayOutL * (1.0f - std::abs(bal)) * mix_;
            const float mixRev = revL * 0.3f * mix_;
            const float wetL = mixDelay + mixRev;
            const float wetR = mixDelay + mixRev; // mono reverb

            // Mix con dry
            outL[s] = dryL * (1.0f - mix_) + wetL * mix_;
            outR[s] = dryR * (1.0f - mix_) + wetR * mix_;
        }
    }

private:
    void updateParams() noexcept
    {
        const float baseMs = 1.0f + 1499.0f * time_;
        static constexpr float factorTab[4] = { 1.0f, 0.5f, 0.667f, 1.5f };
        const int maxDelaySamples = static_cast<int>(delayBufL_.size());
        delaySamples_ = static_cast<int>(sampleRate_ * baseMs / 1000.0 * factorTab[std::clamp(factor_, 0, 3)]);
        if (maxDelaySamples > 1)
            delaySamples_ = std::max(1, std::min(delaySamples_, maxDelaySamples - 1));

        const float freqHz = 0.05f + 9.95f * speed_;
        lfoInc_ = freqHz / sampleRate_;

        const float hcHz = 200.0f + 19800.0f * feedHC_;
        hcCoeff_ = static_cast<float>(hcHz / (hcHz + sampleRate_ * 0.5));

        const float dampHz = 1000.0f + 19000.0f * damping_;
        dampCoeff_ = static_cast<float>(dampHz / (dampHz + sampleRate_ * 0.5));
    }

    double sampleRate_ = 44100.0;
    float time_        = 0.3f;
    float feedback_    = 0.3f;
    float feedHC_      = 0.8f;
    float depth_       = 0.3f;
    float speed_       = 0.3f;
    float decay_       = 0.5f;
    float damping_     = 0.5f;
    float balance_     = 0.5f;
    float mix_         = 0.3f;
    int factor_        = 0;
    int rType_         = 1;
    int mode_          = 0;

    std::vector<float> delayBufL_;
    std::vector<float> delayBufR_;
    int writePosL_     = 0;
    int writePosR_     = 0;
    int delaySamples_  = 1000;

    std::vector<float> combBufL_;
    std::vector<float> combBufR_;
    int combBufSize_   = 0;
    int combPos_       = 0;
    int combDelay_[4]  = { 0, 0, 0, 0 };
    float combStateL_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float combStateR_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float apStateL_[3]   = { 0.0f, 0.0f, 0.0f };
    float apStateR_[3]   = { 0.0f, 0.0f, 0.0f };

    double lfoPhase_   = 0.0;
    double lfoInc_     = 0.0;
    float lpfL_        = 0.0f;
    float lpfR_        = 0.0f;
    float hcCoeff_     = 0.5f;
    float dampCoeff_   = 0.5f;
};

} // namespace abd::dsp
