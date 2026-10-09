/*
  ==============================================================================

    DspMoodFilter.h
    Filtro multimodo analógico con overdrive, LFO multifunción y seguidor de envolvente.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspMoodFilter.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Emulación del filtro analógico Moogerfooger MF-101 / Minimoog Ladder y filtros
      multimodo de sintetizador tipo DeepMind 12 FX Type 8.
      Combina:
        1. Etapa de overdrive no lineal pre-filtro (saturación tanh de 0% a +500%).
        2. Filtro multimodo State Variable (SVF) con topologías de 2 polos (12 dB/oct)
           y 4 polos (24 dB/oct).
        3. Cuatro modos de salida: Lowpass, Highpass, Bandpass y Notch.
        4. LFO multifunción con 7 formas de onda (Tri, Sin, Saw+, Saw-, Ramp, Sq, Rand)
           y rango de frecuencia de 0.05 Hz a 20 Hz.
        5. Seguidor de envolvente dinámico que modula la frecuencia de corte
           (EnvMod bipolar de -100% a +100%).
        6. Saturación suave tanh en las variables de estado internas para prevenir
           desbordamiento y simular saturación analógica del núcleo resonante.

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

class DspMoodFilter
{
public:
    enum FilterType
    {
        kLowpass = 0,
        kHighpass = 1,
        kBandpass = 2,
        kNotch = 3
    };

    enum WaveShape
    {
        kTriangle = 0,
        kSine = 1,
        kSawUp = 2,
        kSawDown = 3,
        kRamp = 4,
        kSquare = 5,
        kRandom = 6
    };

    DspMoodFilter() noexcept
    {
        reset();
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        updateLFO();
        updateEnvCoeffs();
        reset();
    }

    void reset() noexcept
    {
        svfLowL_ = svfLowR_ = 0.0f;
        svfBandL_ = svfBandR_ = 0.0f;
        svfHighL_ = svfHighR_ = 0.0f;
        envStateL_ = envStateR_ = 0.0f;
        lfoPhase_ = 0.0;
        lfoValue_ = 0.0f;
        rngState_ = 12345u;
    }

    void setSpeedNorm(float speedNorm) noexcept
    {
        speed_ = std::clamp(speedNorm, 0.0f, 1.0f);
        updateLFO();
    }

    void setDepthNorm(float depthNorm) noexcept
    {
        depth_ = std::clamp(depthNorm, 0.0f, 1.0f);
    }

    void setResonanceNorm(float resoNorm) noexcept
    {
        reso_ = std::clamp(resoNorm, 0.0f, 1.0f);
    }

    void setBaseFreqNorm(float baseFreqNorm) noexcept
    {
        baseFreq_ = std::clamp(baseFreqNorm, 0.0f, 1.0f);
    }

    void setFilterType(int type) noexcept
    {
        filterType_ = std::clamp(type, 0, 3);
    }

    void setWaveShape(int shape) noexcept
    {
        waveShape_ = std::clamp(shape, 0, 6);
    }

    void setEnvModNorm(float envModNorm) noexcept
    {
        envMod_ = std::clamp(envModNorm, 0.0f, 1.0f);
    }

    void setAttackNorm(float attackNorm) noexcept
    {
        attackParam_ = std::clamp(attackNorm, 0.0f, 1.0f);
        updateEnvCoeffs();
    }

    void setReleaseNorm(float releaseNorm) noexcept
    {
        releaseParam_ = std::clamp(releaseNorm, 0.0f, 1.0f);
        updateEnvCoeffs();
    }

    void setDriveNorm(float driveNorm) noexcept
    {
        drive_ = std::clamp(driveNorm, 0.0f, 1.0f);
    }

    void setFourPole(bool fourPole) noexcept
    {
        fourPole_ = fourPole ? 1 : 0;
    }

    /**
     * Procesa un bloque de audio estéreo devolviendo la señal procesada al 100% wet.
     */
    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        const float envAmt = (envMod_ - 0.5f) * 2.0f; // -1..+1
        const float baseHz = 20.0f * std::pow(750.0f, baseFreq_);
        const float modRange = baseHz * 4.0f;
        const float nyquistLimit = static_cast<float>(sampleRate_ * 0.45);

        for (int s = 0; s < numSamples; ++s)
        {
            // 1. Avance del LFO
            lfoPhase_ += lfoInc_;
            if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
            lfoValue_ = computeWaveform(lfoPhase_, waveShape_);

            // 2. Seguidor de envolvente
            const float absL = std::abs(inL[s]);
            const float absR = std::abs(inR[s]);
            envStateL_ = (absL > envStateL_)
                ? envStateL_ + envAttack_ * (absL - envStateL_)
                : envStateL_ + envRelease_ * (absL - envStateL_);
            envStateR_ = (absR > envStateR_)
                ? envStateR_ + envAttack_ * (absR - envStateR_)
                : envStateR_ + envRelease_ * (absR - envStateR_);
            const float env = (envStateL_ + envStateR_) * 0.5f;

            // 3. Modulación combinada (LFO + Envelope)
            float mod = (lfoValue_ * depth_) + (env * 4.0f * envAmt);
            mod = std::clamp(mod, -1.0f, 1.0f);

            // 4. Cálculo de frecuencia de corte y coeficientes SVF
            const float freqHz = std::clamp(baseHz + mod * modRange, 20.0f, nyquistLimit);
            updateCoefficients(freqHz);

            // 5. Saturación overdrive pre-filtro
            const float driveL = std::tanh(inL[s] * driveGain_);
            const float driveR = std::tanh(inR[s] * driveGain_);

            // 6. Procesamiento SVF con soft-limiting
            processSvfChannel(driveL, svfLowL_, svfBandL_, svfHighL_);
            processSvfChannel(driveR, svfLowR_, svfBandR_, svfHighR_);

            // 7. Selección de salida según modo
            float wetL = 0.0f;
            float wetR = 0.0f;
            switch (filterType_)
            {
                case kLowpass:
                    wetL = svfLowL_;  wetR = svfLowR_;  break;
                case kHighpass:
                    wetL = svfHighL_; wetR = svfHighR_; break;
                case kBandpass:
                    wetL = svfBandL_; wetR = svfBandR_; break;
                case kNotch:
                default:
                    wetL = svfLowL_ + svfHighL_;
                    wetR = svfLowR_ + svfHighR_;
                    break;
            }

            outL[s] = wetL;
            outR[s] = wetR;
        }
    }

private:
    void updateLFO() noexcept
    {
        const float freqHz = 0.05f + 19.95f * speed_;
        lfoInc_ = freqHz / sampleRate_;
    }

    void updateEnvCoeffs() noexcept
    {
        envAttack_  = 0.005f + 0.5f * attackParam_;
        envRelease_ = 0.0005f + 0.5f * releaseParam_;
    }

    float fastRand() noexcept
    {
        rngState_ = rngState_ * 1103515245u + 12345u;
        return static_cast<float>(rngState_ >> 16) / 65536.0f; // 0..1
    }

    float computeWaveform(double phase, int shape) noexcept
    {
        const double p = phase - std::floor(phase);
        switch (shape)
        {
            case kTriangle: return static_cast<float>(4.0 * std::abs(p - 0.5) - 1.0);
            case kSine:     return std::sin(static_cast<float>(6.28318530717958647692 * p));
            case kSawUp:    return static_cast<float>(2.0 * p - 1.0);
            case kSawDown:  return static_cast<float>(1.0 - 2.0 * p);
            case kRamp:     return static_cast<float>(1.0 - 4.0 * std::abs(p - 0.5));
            case kSquare:   return (p < 0.5) ? 1.0f : -1.0f;
            case kRandom:   return fastRand() * 2.0f - 1.0f;
            default:        return std::sin(static_cast<float>(6.28318530717958647692 * p));
        }
    }

    void updateCoefficients(float freqHz) noexcept
    {
        const float wd = static_cast<float>(6.28318530717958647692 * freqHz / sampleRate_);
        gCoeff_ = std::min(std::tan(wd * 0.5f), 1.9f);
        const float damping = 1.0f - reso_ * 0.95f;
        rCoeff_ = std::min(1.0f / std::max(damping, 0.01f), 4.0f);
        driveGain_ = 1.0f + drive_ * 5.0f;
    }

    inline void processSvfChannel(float in, float& low, float& band, float& high) noexcept
    {
        high = in - low - rCoeff_ * band;
        band = band + gCoeff_ * high;
        low = low + gCoeff_ * band;

        band = std::tanh(band);
        low = std::tanh(low);

        if (fourPole_)
        {
            const float low2 = low + gCoeff_ * (band - low);
            const float low2Sat = std::tanh(low2);
            band = std::tanh(band + gCoeff_ * (low2Sat - band));
            low = low2Sat;
        }
    }

    double sampleRate_ = 44100.0;
    float speed_ = 0.3f;
    float depth_ = 0.5f;
    float reso_ = 0.2f;
    float baseFreq_ = 0.5f;
    int filterType_ = 0;
    int waveShape_ = 0;
    int fourPole_ = 0;
    float envMod_ = 0.0f;
    float drive_ = 0.0f;
    float attackParam_ = 0.02f;
    float releaseParam_ = 0.002f;

    // Estado del LFO
    double lfoPhase_ = 0.0;
    double lfoInc_ = 0.0;
    float lfoValue_ = 0.0f;

    // Coeficientes y ganancia
    float gCoeff_ = 0.0f;
    float rCoeff_ = 0.0f;
    float driveGain_ = 1.0f;

    // Estado del seguidor de envolvente
    float envStateL_ = 0.0f;
    float envStateR_ = 0.0f;
    float envAttack_ = 0.01f;
    float envRelease_ = 0.001f;

    // Variables de estado del filtro
    float svfLowL_ = 0.0f, svfLowR_ = 0.0f;
    float svfBandL_ = 0.0f, svfBandR_ = 0.0f;
    float svfHighL_ = 0.0f, svfHighR_ = 0.0f;

    // Generador pseudoaleatorio LCG
    unsigned int rngState_ = 12345u;
};

} // namespace abd::dsp
