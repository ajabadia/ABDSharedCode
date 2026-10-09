/*
  ==============================================================================

    DspPitchShifter.h
    Pitch Shifter dual y vintage polifónico por micro-granos con ventana Hann.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspPitchShifter.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Motor de transposición tonal polifónica estéreo de 2 voces independientes (DeepMind 12 FX Types 29 y 35).
      Inspirado en armonizadores y procesadores de pitch clásicos como Eventide H910 / H949:
        - 2 Voces de transposición de pitch independientes (-12 a +12 semitonos, -50 a +50 cents).
        - Desplazamiento temporal independiente por voz (Delay 1 y Delay 2 hasta 500 ms).
        - Desvanecimiento cruzado mediante micro-granos con modulación de fase continua y ventana Hann.
        - Paneo y ganancia estéreo independientes por voz.
        - Filtro HiCut paso-bajos de 1-polo en la etapa de mezcla (200 Hz a 20 kHz).
        - Modo Vintage (Tipo 35): Realimentación cruzada de las señales transpuestas hacia la entrada
          para generar cascadas armónicas de subida/bajada continua (shimmer / arpegios de pitch infinitos).

    RENDIMIENTO:
      - 100% Real-Time Safe: Búferes pre-asignados en prepare(), cero heap en process().
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

class DspPitchShifter
{
public:
    explicit DspPitchShifter(bool vintageMode = false) noexcept
        : vintage_(vintageMode)
    {
        reset();
    }

    void setVintageMode(bool vintage) noexcept
    {
        vintage_ = vintage;
    }

    bool isVintageMode() const noexcept { return vintage_; }

    void prepare(double sampleRate)
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;

        // 500 ms de capacidad máxima de retardo
        maxDelaySamples_ = std::max(64, static_cast<int>(sampleRate_ * 0.5));
        bufL_.assign(static_cast<size_t>(maxDelaySamples_), 0.0f);
        bufR_.assign(static_cast<size_t>(maxDelaySamples_), 0.0f);

        updateGrainParams();
        reset();
    }

    void reset() noexcept
    {
        std::fill(bufL_.begin(), bufL_.end(), 0.0f);
        std::fill(bufR_.begin(), bufR_.end(), 0.0f);
        writePosL_ = 0;
        writePosR_ = 0;
        phase1_    = 0.0;
        phase2_    = 0.0;
        fbL_       = 0.0f;
        fbR_       = 0.0f;
        hcStateL_  = 0.0f;
        hcStateR_  = 0.0f;
    }

    void setSemi1Norm(float norm) noexcept
    {
        semi1_ = std::clamp(norm, 0.0f, 1.0f);
        updateGrainParams();
    }
    void setCent1Norm(float norm) noexcept
    {
        cent1_ = std::clamp(norm, 0.0f, 1.0f);
        updateGrainParams();
    }
    void setDelay1Norm(float norm) noexcept { delay1_ = std::clamp(norm, 0.0f, 1.0f); }
    void setGain1Norm(float norm) noexcept { gain1_ = std::clamp(norm, 0.0f, 1.0f); }
    void setPan1Norm(float norm) noexcept { pan1_ = std::clamp(norm, 0.0f, 1.0f); }

    void setSemi2Norm(float norm) noexcept
    {
        semi2_ = std::clamp(norm, 0.0f, 1.0f);
        updateGrainParams();
    }
    void setCent2Norm(float norm) noexcept
    {
        cent2_ = std::clamp(norm, 0.0f, 1.0f);
        updateGrainParams();
    }
    void setDelay2Norm(float norm) noexcept { delay2_ = std::clamp(norm, 0.0f, 1.0f); }
    void setGain2Norm(float norm) noexcept { gain2_ = std::clamp(norm, 0.0f, 1.0f); }
    void setPan2Norm(float norm) noexcept { pan2_ = std::clamp(norm, 0.0f, 1.0f); }

    void setMixNorm(float norm) noexcept { mix_ = std::clamp(norm, 0.0f, 1.0f); }
    void setHiCutNorm(float norm) noexcept
    {
        hiCut_ = std::clamp(norm, 0.0f, 1.0f);
        updateGrainParams();
    }

    void setParameter(int index, float value) noexcept
    {
        const float val = std::clamp(value, 0.0f, 1.0f);
        switch (index)
        {
            case 0:
                setSemi1Norm(val);
                break;
            case 1:
                setCent1Norm(val);
                break;
            case 2:
                setDelay1Norm(val);
                break;
            case 3:
                setGain1Norm(val);
                break;
            case 4:
                setPan1Norm(val);
                break;
            case 5:
                setMixNorm(val);
                break;
            case 6:
                setSemi2Norm(val);
                break;
            case 7:
                setCent2Norm(val);
                break;
            case 8:
                setDelay2Norm(val);
                break;
            case 9:
                setGain2Norm(val);
                break;
            case 10:
                setPan2Norm(val);
                break;
            case 11:
                setHiCutNorm(val);
                break;
            default:
                break;
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        if (maxDelaySamples_ <= 0) return;

        float* dL = bufL_.data();
        float* dR = bufR_.data();

        auto calcPan = [](float norm) -> float {
            return (norm - 0.5f) * 2.0f;
        };
        const float pLeft1  = 1.0f - std::max(0.0f, calcPan(pan1_)) * 0.5f;
        const float pRight1 = 1.0f - std::max(0.0f, -calcPan(pan1_)) * 0.5f;
        const float pLeft2  = 1.0f - std::max(0.0f, calcPan(pan2_)) * 0.5f;
        const float pRight2 = 1.0f - std::max(0.0f, -calcPan(pan2_)) * 0.5f;

        const float delaySamp1  = delay1_ * 0.5f * static_cast<float>(sampleRate_);
        const float delaySamp2  = delay2_ * 0.5f * static_cast<float>(sampleRate_);
        constexpr double kTwoPi = 6.28318530717958647692;

        for (int s = 0; s < numSamples; ++s)
        {
            const float dryL = inL[s];
            const float dryR = inR[s];

            float inWetL = dryL;
            float inWetR = dryR;

            if (vintage_)
            {
                inWetL += fbL_ * gain1_;
                inWetR += fbR_ * gain2_;
            }

            dL[writePosL_] = inWetL;
            dR[writePosR_] = inWetR;

            // Avance de fase de grano y ventana Hann
            phase1_ += grainInc1_;
            if (phase1_ >= 1.0 || phase1_ < 0.0) phase1_ -= std::floor(phase1_);
            const float win1 = static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * phase1_));

            phase2_ += grainInc2_;
            if (phase2_ >= 1.0 || phase2_ < 0.0) phase2_ -= std::floor(phase2_);
            const float win2 = static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * phase2_));

            // Lectura Voice 1
            const float readOff1  = delaySamp1 + static_cast<float>(phase1_ * kGrainLen);
            const float shiftedL1 = readDelay(dL, writePosL_, readOff1, maxDelaySamples_);
            const float shiftedR1 = readDelay(dR, writePosR_, readOff1, maxDelaySamples_);

            // Lectura Voice 2
            const float readOff2  = delaySamp2 + static_cast<float>(phase2_ * kGrainLen);
            const float shiftedL2 = readDelay(dL, writePosL_, readOff2, maxDelaySamples_);
            const float shiftedR2 = readDelay(dR, writePosR_, readOff2, maxDelaySamples_);

            writePosL_ = (writePosL_ + 1) % maxDelaySamples_;
            writePosR_ = (writePosR_ + 1) % maxDelaySamples_;

            // Mezcla de voces con panorama y ponderación de ventana
            const float voiceL = shiftedL1 * win1 * gain1_ * pLeft1 + shiftedL2 * win2 * gain2_ * pLeft2;
            const float voiceR = shiftedR1 * win1 * gain1_ * pRight1 + shiftedR2 * win2 * gain2_ * pRight2;

            if (vintage_)
            {
                fbL_ = voiceL;
                fbR_ = voiceR;
            }

            // Filtro paso-bajos HiCut
            hcStateL_ += hiCutLP_ * (voiceL - hcStateL_);
            hcStateR_ += hiCutLP_ * (voiceR - hcStateR_);

            outL[s] = dryL * (1.0f - mix_) + hcStateL_ * mix_;
            outR[s] = dryR * (1.0f - mix_) + hcStateR_ * mix_;
        }
    }

private:
    static float readDelay(const float* buf, int writePos, float readOffset, int maxSamp) noexcept
    {
        float readPos = static_cast<float>(writePos) - std::fmod(readOffset, static_cast<float>(maxSamp));
        if (readPos < 0.0f) readPos += static_cast<float>(maxSamp);
        const int idx    = static_cast<int>(readPos);
        const int next   = (idx + 1) % maxSamp;
        const float frac = readPos - static_cast<float>(idx);
        return buf[idx] * (1.0f - frac) + buf[next] * frac;
    }

    void updateGrainParams() noexcept
    {
        auto calcRatio = [](float semiNorm, float centNorm) -> double {
            const int st = static_cast<int>((semiNorm - 0.5f) * 24.0f + 0.5f);  // -12..+12
            const int ct = static_cast<int>((centNorm - 0.5f) * 100.0f + 0.5f); // -50..+50
            return std::pow(2.0, (st * 100.0 + ct) / 1200.0);
        };

        const double ratio1 = calcRatio(semi1_, cent1_);
        grainInc1_          = (1.0 - ratio1) / kGrainLen;

        const double ratio2 = calcRatio(semi2_, cent2_);
        grainInc2_          = (1.0 - ratio2) / kGrainLen;

        const float freqHz = hiCut_ * 19800.0f + 200.0f;
        hiCutLP_           = static_cast<float>(freqHz / (freqHz + sampleRate_ * 0.5));
    }

    static constexpr int kGrainLen = 1024;

    double sampleRate_ = 44100.0;
    bool vintage_      = false;

    float semi1_  = 0.5f;
    float cent1_  = 0.5f;
    float delay1_ = 0.3f;
    float gain1_  = 0.5f;
    float pan1_   = 0.5f;

    float semi2_  = 0.5f;
    float cent2_  = 0.5f;
    float delay2_ = 0.3f;
    float gain2_  = 0.5f;
    float pan2_   = 0.5f;

    float mix_     = 0.5f;
    float hiCut_   = 0.8f;
    float hiCutLP_ = 0.5f;

    std::vector<float> bufL_;
    std::vector<float> bufR_;
    int writePosL_       = 0;
    int writePosR_       = 0;
    int maxDelaySamples_ = 0;

    double phase1_    = 0.0;
    double phase2_    = 0.0;
    double grainInc1_ = 0.0;
    double grainInc2_ = 0.0;

    float fbL_      = 0.0f;
    float fbR_      = 0.0f;
    float hcStateL_ = 0.0f;
    float hcStateR_ = 0.0f;
};

} // namespace abd::dsp
