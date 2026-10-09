/*
  ==============================================================================

    DspMultiBandDist.h
    Distorsión de 3 bandas con cruces complementarios y emulación de altavoz/cabinet.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspMultiBandDist.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Efecto de distorsión multibanda (DeepMind 12 FX Type 32) que divide el espectro
      de audio en tres bandas (Low, Mid, High) mediante filtros cruzados complementarios,
      aplica distorsión independiente por banda (Valve, Saturate, Tube o sus variantes
      con post-filtrado), y procesa la suma a través de una etapa de simulación de altavoz (Cabinet).

    PARÁMETROS:
      - InGain: Ganancia de entrada (-24 dB a +24 dB).
      - DistType: Tipo de distorsión (0=Valve, 1=Saturate, 2=Tube, 3-5=post-filtered).
      - LowLevel / LowDrive: Nivel (-12 a +12 dB) y Drive (0-100%) de banda grave.
      - XoverLowMid: Frecuencia de cruce grave-medio (30 Hz a 9000 Hz).
      - MidLevel / MidDrive: Nivel y Drive de banda media.
      - XoverMidHi: Frecuencia de cruce medio-agudo (30 Hz a 9000 Hz).
      - HiLevel / HiDrive: Nivel y Drive de banda aguda.
      - Cabinet: Tipo de altavoz/recinto (0=OFF, 1-11 activo).
      - OutGain: Ganancia de salida (-12 dB a +12 dB).

    RENDIMIENTO:
      - Cero asignaciones en memoria dinámica (100% Real-Time Safe).
      - CERO dependencias de JUCE (C++20 estándar puro).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include <algorithm>
#include <cmath>

namespace abd::dsp
{

class DspMultiBandDist
{
public:
    DspMultiBandDist() noexcept
    {
        reset();
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        updateCrossoverCoeffs();

        const float cabFreq = 4000.0f;
        cabCoeff_ = static_cast<float>(cabFreq / (cabFreq + sampleRate_ * 0.3));

        const float postFreq = 5513.0f;
        postCoeff_ = static_cast<float>(postFreq / (postFreq + sampleRate_ * 0.5));

        reset();
    }

    void reset() noexcept
    {
        xv1LowL_ = xv1LowR_ = 0.0f;
        xv2LowL_ = xv2LowR_ = 0.0f;
        cabL_ = cabR_ = 0.0f;
        postL_ = postR_ = 0.0f;
    }

    void setInGainNorm(float norm) noexcept       { inGain_ = std::clamp(norm, 0.0f, 1.0f); }
    void setDistType(int type) noexcept           { distType_ = std::clamp(type, 0, 5); }
    void setLowLevelNorm(float norm) noexcept     { lowLevel_ = std::clamp(norm, 0.0f, 1.0f); }
    void setLowDriveNorm(float norm) noexcept     { lowDrive_ = std::clamp(norm, 0.0f, 1.0f); }
    void setXoverLowMidNorm(float norm) noexcept  { xoverLowMid_ = std::clamp(norm, 0.0f, 1.0f); updateCrossoverCoeffs(); }
    void setMidLevelNorm(float norm) noexcept     { midLevel_ = std::clamp(norm, 0.0f, 1.0f); }
    void setMidDriveNorm(float norm) noexcept     { midDrive_ = std::clamp(norm, 0.0f, 1.0f); }
    void setXoverMidHiNorm(float norm) noexcept   { xoverMidHi_ = std::clamp(norm, 0.0f, 1.0f); updateCrossoverCoeffs(); }
    void setHiLevelNorm(float norm) noexcept      { hiLevel_ = std::clamp(norm, 0.0f, 1.0f); }
    void setHiDriveNorm(float norm) noexcept      { hiDrive_ = std::clamp(norm, 0.0f, 1.0f); }
    void setCabinetType(int type) noexcept        { cabinetType_ = std::clamp(type, 0, 11); }
    void setOutGainNorm(float norm) noexcept      { outGain_ = std::clamp(norm, 0.0f, 1.0f); }

    static float applyDistType(float sample, int type, float drive) noexcept
    {
        const float gain = 1.0f + drive * 15.0f; // 1x a 16x
        const float x = sample * gain;

        switch (type)
        {
            case 0: // Valve: saturación suave tanh
                return std::tanh(x);

            case 1: // Saturate: recorte con codo suave (knee)
            {
                constexpr float knee = 0.5f;
                const float absX = std::abs(x);
                if (absX < knee)
                    return x;
                const float soft = knee + std::tanh(absX - knee);
                return (x > 0.0f) ? soft : -soft;
            }

            case 2: // Tube: asimétrico de válvulas
            {
                if (x > 0.0f)
                    return std::tanh(x);
                else
                    return std::tanh(x * 0.7f) * 0.85f;
            }

            case 3:
            case 4:
            case 5:
                return applyDistType(sample, type - 3, drive);

            default:
                return std::tanh(x);
        }
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float inGainLin = std::pow(10.0f, (inGain_ * 48.0f - 24.0f) / 20.0f);
        auto mapLevel = [](float norm) -> float {
            return std::pow(10.0f, (norm * 24.0f - 12.0f) / 20.0f);
        };

        const float lowGain = mapLevel(lowLevel_);
        const float midGain = mapLevel(midLevel_);
        const float hiGain  = mapLevel(hiLevel_);
        const float outGainLin = mapLevel(outGain_);

        const bool usePostFilter = (distType_ >= 3);
        const int baseDistType = (distType_ >= 3) ? (distType_ - 3) : distType_;

        for (int s = 0; s < numSamples; ++s)
        {
            const float inL_s = inL[s] * inGainLin;
            const float inR_s = inR[s] * inGainLin;

            // 1. Crossover de 3 bandas (filtros complementarios de 1 polo)
            // Banda 1 (Low): LPF con xv1
            xv1LowL_ += xv1Coeff_ * (inL_s - xv1LowL_);
            xv1LowR_ += xv1Coeff_ * (inR_s - xv1LowR_);
            const float lowL_s = xv1LowL_;
            const float lowR_s = xv1LowR_;

            // Banda 2 (Mid): HPF(xv1) -> LPF(xv2)
            const float midInL = inL_s - xv1LowL_;
            const float midInR = inR_s - xv1LowR_;

            xv2LowL_ += xv2Coeff_ * (midInL - xv2LowL_);
            xv2LowR_ += xv2Coeff_ * (midInR - xv2LowR_);
            const float midL_s = xv2LowL_;
            const float midR_s = xv2LowR_;

            // Banda 3 (Hi): HPF(xv2) complementario
            const float hiL_s = midInL - xv2LowL_;
            const float hiR_s = midInR - xv2LowR_;

            // 2. Distorsión independiente por banda
            float distLowL = applyDistType(lowL_s, baseDistType, lowDrive_);
            float distLowR = applyDistType(lowR_s, baseDistType, lowDrive_);
            float distMidL = applyDistType(midL_s, baseDistType, midDrive_);
            float distMidR = applyDistType(midR_s, baseDistType, midDrive_);
            float distHiL  = applyDistType(hiL_s, baseDistType, hiDrive_);
            float distHiR  = applyDistType(hiR_s, baseDistType, hiDrive_);

            // 3. Post-filtrado suave si está activo
            if (usePostFilter)
            {
                const float sumL = distLowL + distMidL + distHiL;
                const float sumR = distLowR + distMidR + distHiR;
                postL_ += postCoeff_ * (sumL - postL_);
                postR_ += postCoeff_ * (sumR - postR_);
                distLowL = distMidL = distHiL = postL_;
                distLowR = distMidR = distHiR = postR_;
            }

            // 4. Recombinación con ganancias de banda
            float wetL = distLowL * lowGain + distMidL * midGain + distHiL * hiGain;
            float wetR = distLowR * lowGain + distMidR * midGain + distHiR * hiGain;

            // 5. Simulación de recinto/altavoz (Cabinet)
            if (cabinetType_ > 0)
            {
                cabL_ += cabCoeff_ * (wetL - cabL_);
                cabR_ += cabCoeff_ * (wetR - cabR_);
                wetL = cabL_;
                wetR = cabR_;
            }

            outL[s] = wetL * outGainLin;
            outR[s] = wetR * outGainLin;
        }
    }

private:
    void updateCrossoverCoeffs() noexcept
    {
        float freq1 = 30.0f * std::pow(300.0f, xoverLowMid_);
        float freq2 = 30.0f * std::pow(300.0f, xoverMidHi_);

        freq1 = std::min(freq1, freq2 * 0.8f);
        freq2 = std::max(freq2, freq1 * 1.2f);

        xv1Coeff_ = static_cast<float>(freq1 / (freq1 + sampleRate_ * 0.5));
        xv2Coeff_ = static_cast<float>(freq2 / (freq2 + sampleRate_ * 0.5));
    }

    double sampleRate_ = 44100.0;
    float inGain_ = 0.5f;
    int distType_ = 0;
    float lowLevel_ = 0.5f;
    float lowDrive_ = 0.3f;
    float xoverLowMid_ = 0.3f;
    float midLevel_ = 0.5f;
    float midDrive_ = 0.3f;
    float xoverMidHi_ = 0.7f;
    float hiLevel_ = 0.5f;
    float hiDrive_ = 0.3f;
    int cabinetType_ = 0;
    float outGain_ = 0.5f;

    float xv1Coeff_ = 0.05f;
    float xv2Coeff_ = 0.2f;
    float cabCoeff_ = 0.2f;
    float postCoeff_ = 0.2f;

    float xv1LowL_ = 0.0f, xv1LowR_ = 0.0f;
    float xv2LowL_ = 0.0f, xv2LowR_ = 0.0f;
    float cabL_ = 0.0f, cabR_ = 0.0f;
    float postL_ = 0.0f, postR_ = 0.0f;
};

} // namespace abd::dsp
