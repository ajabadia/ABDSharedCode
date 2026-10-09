/*
  ==============================================================================

    DspRackAmp.h
    Simulador de preamplificador y amplificador de guitarra estilo Tech 21 / SansAmp.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspRackAmp.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Emulación de previo y amplificador analógico en rack para guitarra/sintetizador
      (DeepMind 12 FX Type 7).
      Incorpora:
        1. Etapa de preamplificación de entrada (PreAmp).
        2. Realces pre-distorsión: Punch (medios-graves) y Buzz (agudos).
        3. Generación de armónicos asimétricos (Crunch) y saturación valvular (Drive).
        4. Compresión natural posterior a la saturación.
        5. Ecualizador shelving de 2 bandas (Low y High) activo post-saturación.
        6. Filtro resonante paso-bajos de 2 polos que simula la respuesta en frecuencia
           de un altavoz y cono de guitarra (Cabinet).

    PARÁMETROS:
      - PreAmp: Ganancia del previo (1x a 11x).
      - Buzz: Realce de agudos previo a la distorsión.
      - Punch: Realce de medios/graves previo a la distorsión.
      - Crunch: Cantidad de distorsión asimétrica (rectificación de media onda).
      - Drive: Cantidad de saturación no lineal general (1x a 21x).
      - Level: Volumen master de salida.
      - LowEQ: Ecualizador de graves (shelving a 200 Hz).
      - HighEQ: Ecualizador de agudos (shelving a 5 kHz).
      - Cabinet: Conmutador de simulación de altavoz (0=OFF, 1=ON).

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

class DspRackAmp
{
public:
    DspRackAmp() noexcept
    {
        reset();
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        updateEQCoeffs();
        updateCabinetCoeffs();
        reset();
    }

    void reset() noexcept
    {
        lowStateL_ = lowStateR_ = 0.0f;
        highStateL_ = highStateR_ = 0.0f;
        cabState1L_ = cabState1R_ = 0.0f;
        cabState2L_ = cabState2R_ = 0.0f;
    }

    void setPreAmpNorm(float norm) noexcept { preAmp_ = std::clamp(norm, 0.0f, 1.0f); }
    void setBuzzNorm(float norm) noexcept
    {
        buzz_ = std::clamp(norm, 0.0f, 1.0f);
        updateEQCoeffs();
    }
    void setPunchNorm(float norm) noexcept
    {
        punch_ = std::clamp(norm, 0.0f, 1.0f);
        updateEQCoeffs();
    }
    void setCrunchNorm(float norm) noexcept { crunch_ = std::clamp(norm, 0.0f, 1.0f); }
    void setDriveNorm(float norm) noexcept { drive_ = std::clamp(norm, 0.0f, 1.0f); }
    void setLevelNorm(float norm) noexcept { level_ = std::clamp(norm, 0.0f, 1.0f); }
    void setLowEQNorm(float norm) noexcept
    {
        lowEQ_ = std::clamp(norm, 0.0f, 1.0f);
        updateEQCoeffs();
    }
    void setHighEQNorm(float norm) noexcept
    {
        highEQ_ = std::clamp(norm, 0.0f, 1.0f);
        updateEQCoeffs();
    }
    void setCabinet(bool on) noexcept { cabinet_ = on ? 1.0f : 0.0f; }

    static float applyDistortion(float sample, float driveAmt, float crunchAmt) noexcept
    {
        const float preGain = 1.0f + driveAmt * 20.0f; // 1x a 21x
        float shaped        = sample * preGain;

        if (crunchAmt > 0.01f)
        {
            const float crunchGain = 1.0f + crunchAmt * 15.0f;
            const float crunchSig  = sample * crunchGain;
            if (crunchSig > 0.0f)
                shaped += std::tanh(crunchSig * 0.5f) * crunchAmt;
            else
                shaped += std::tanh(crunchSig * 0.3f) * crunchAmt * 0.7f;
        }

        shaped = std::tanh(shaped);
        shaped *= (1.0f + 0.3f * (1.0f - std::abs(shaped)));

        return shaped;
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float preAmpGain = 1.0f + preAmp_ * 10.0f;
        const float levelGain  = 0.3f + level_ * 0.7f;

        for (int s = 0; s < numSamples; ++s)
        {
            const float sampL = inL[s] * preAmpGain;
            const float sampR = inR[s] * preAmpGain;

            // 1. Shelving pre-EQ de graves con realce Punch
            lowStateL_ += lowCoeff_ * (sampL - lowStateL_);
            lowStateR_ += lowCoeff_ * (sampR - lowStateR_);
            const float preEqL = sampL + (lowStateL_ - sampL) * lowBoost_;
            const float preEqR = sampR + (lowStateR_ - sampR) * lowBoost_;

            // 2. Distorsión no lineal con Crunch
            const float distL = applyDistortion(preEqL, drive_, crunch_);
            const float distR = applyDistortion(preEqR, drive_, crunch_);

            // 3. Shelving post-EQ de agudos con realce Buzz
            highStateL_ += highCoeff_ * (distL - highStateL_);
            highStateR_ += highCoeff_ * (distR - highStateR_);
            float wetL = distL + (highStateL_ - distL) * highBoost_;
            float wetR = distR + (highStateR_ - distR) * highBoost_;

            // 4. Simulación de altavoz/cabinet (LPF resonante de 2 polos)
            if (cabinet_ > 0.5f)
            {
                cabState1L_ += cabCoeff_ * (wetL - cabState1L_ + cabRes_ * (cabState1L_ - cabState2L_));
                cabState2L_ += cabCoeff_ * (cabState1L_ - cabState2L_);
                wetL = cabState2L_;

                cabState1R_ += cabCoeff_ * (wetR - cabState1R_ + cabRes_ * (cabState1R_ - cabState2R_));
                cabState2R_ += cabCoeff_ * (cabState1R_ - cabState2R_);
                wetR = cabState2R_;
            }

            outL[s] = wetL * levelGain;
            outR[s] = wetR * levelGain;
        }
    }

private:
    void updateEQCoeffs() noexcept
    {
        const float lowFreq = 200.0f;
        lowCoeff_           = static_cast<float>(lowFreq / (lowFreq + sampleRate_ * 0.5));
        lowBoost_           = (lowEQ_ - 0.5f) * 2.0f + (punch_ * 2.0f) * 0.2f;

        const float highFreq = 5000.0f;
        highCoeff_           = static_cast<float>(highFreq / (highFreq + sampleRate_ * 0.5));
        highBoost_           = (highEQ_ - 0.5f) * 2.0f + (buzz_ * 2.0f) * 0.3f;
    }

    void updateCabinetCoeffs() noexcept
    {
        const float cabFreq = 3500.0f;
        cabCoeff_           = static_cast<float>(cabFreq / (cabFreq + sampleRate_ * 0.3));
        cabRes_             = 0.2f;
    }

    double sampleRate_ = 44100.0;
    float preAmp_      = 0.3f;
    float buzz_        = 0.3f;
    float punch_       = 0.3f;
    float crunch_      = 0.2f;
    float drive_       = 0.3f;
    float level_       = 0.5f;
    float lowEQ_       = 0.5f;
    float highEQ_      = 0.5f;
    float cabinet_     = 1.0f;

    float lowCoeff_  = 0.05f;
    float lowBoost_  = 0.0f;
    float highCoeff_ = 0.2f;
    float highBoost_ = 0.0f;
    float cabCoeff_  = 0.2f;
    float cabRes_    = 0.2f;

    float lowStateL_ = 0.0f, lowStateR_ = 0.0f;
    float highStateL_ = 0.0f, highStateR_ = 0.0f;
    float cabState1L_ = 0.0f, cabState1R_ = 0.0f;
    float cabState2L_ = 0.0f, cabState2R_ = 0.0f;
};

} // namespace abd::dsp
