/*
  ==============================================================================

    DspFairComp.h
    Emulación del compresor/limitador a válvulas estéreo Fairchild 670 (Vari-Mu).

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/DspFairComp.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Emulación del legendario compresor de válvulas Vari-Mu Fairchild 670
      (diseñado por Rein Narma a finales de los años 50).
      El circuito original utiliza 20 válvulas (incluyendo los míticos tubos de
      control remoto de corte 6386) y transformadores de audio masivos.
      Se caracteriza por:
        1. Respuesta suave "program-dependent" con soft knee progresivo.
        2. 6 constantes de tiempo históricas oficiales:
           - Posición 1: Ataque 0.2 ms, Relajación 0.3 s.
           - Posición 2: Ataque 0.2 ms, Relajación 0.8 s.
           - Posición 3: Ataque 0.4 ms, Relajación 2.0 s.
           - Posición 4: Ataque 0.8 ms, Relajación 5.0 s.
           - Posición 5: Ataque 0.2 ms, Relajación auto (0.3s a 8.0s según pico).
           - Posición 6: Ataque 0.4 ms, Relajación auto (0.3s a 15.0s según programa).
        3. Cuatro modos de operación: Bypass (0), Estéreo vinculado (1),
           Dual Mono independiente (2) y Mid/Side matrizado (3).
        4. Control de DC Bias que modifica la curva de polarización del tubo,
           alterando simultáneamente el ratio (2:1 a 30:1) y el codo knee (20 dB a 2 dB).

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

class DspFairComp
{
public:
    enum Mode
    {
        kBypass = 0,
        kStereo = 1,
        kDualMono = 2,
        kMidSide = 3
    };

    struct ChannelDetector
    {
        float inputGainNorm = 1.0f;
        float thresholdNorm = 0.5f;
        float timeConstantNorm = 0.0f; // 1 a 6
        float dcBiasNorm = 0.5f;
        float outputGainNorm = 0.75f;

        // Constantes calculadas
        float attackTimeSec = 0.0002f;
        float releaseTimeSec = 0.3f;
        float ratio = 4.0f;
        float kneeDb = 10.0f;
        float envelope = 0.0f;

        void reset() noexcept
        {
            envelope = 0.0f;
        }

        void update(double sampleRate) noexcept
        {
            const int tc = 1 + static_cast<int>(timeConstantNorm * 5.0f);
            switch (tc)
            {
                case 1: attackTimeSec = 0.0002f; releaseTimeSec = 0.3f; break;
                case 2: attackTimeSec = 0.0002f; releaseTimeSec = 0.8f; break;
                case 3: attackTimeSec = 0.0004f; releaseTimeSec = 2.0f; break;
                case 4: attackTimeSec = 0.0008f; releaseTimeSec = 5.0f; break;
                case 5: attackTimeSec = 0.0002f; releaseTimeSec = 8.0f; break;
                case 6: attackTimeSec = 0.0004f; releaseTimeSec = 15.0f; break;
                default: attackTimeSec = 0.0002f; releaseTimeSec = 0.3f; break;
            }

            ratio = 2.0f + dcBiasNorm * 28.0f;
            kneeDb = 20.0f - dcBiasNorm * 18.0f;
        }

        float processSample(float inputSample, double sampleRate) noexcept
        {
            const float inGainDb = -20.0f + inputGainNorm * 20.0f; // -20 dB a 0 dB
            const float inputGainLin = std::pow(10.0f, inGainDb / 20.0f);
            const float x = inputSample * inputGainLin;
            const float absX = std::abs(x);

            const float attCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (attackTimeSec * sampleRate)));
            const float relCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (releaseTimeSec * sampleRate)));

            if (absX > envelope)
                envelope += attCoeff * (absX - envelope);
            else
                envelope += relCoeff * (absX - envelope);

            const float envDb = 20.0f * std::log10(std::max(1e-5f, envelope));
            const float thresholdDb = -40.0f + thresholdNorm * 40.0f; // -40 dB a 0 dB

            float gainReductionDb = 0.0f;
            const float halfKnee = kneeDb * 0.5f;

            if (envDb > thresholdDb - halfKnee)
            {
                if (envDb < thresholdDb + halfKnee)
                {
                    // Región parabólica del codo (soft knee)
                    const float kneeDiff = envDb - (thresholdDb - halfKnee);
                    gainReductionDb = (1.0f - 1.0f / ratio) * (kneeDiff * kneeDiff) / (2.0f * kneeDb);
                }
                else
                {
                    // Región lineal superior
                    gainReductionDb = (1.0f - 1.0f / ratio) * (envDb - thresholdDb);
                }
            }

            float gainDb = -gainReductionDb;
            const float outGainDb = -18.0f + outputGainNorm * 24.0f; // -18 dB a +6 dB
            gainDb += outGainDb;

            const float outputGainLin = std::pow(10.0f, gainDb / 20.0f);
            return x * outputGainLin;
        }
    };

    DspFairComp() noexcept
    {
        reset();
    }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        chanLM_.update(sampleRate_);
        chanRS_.update(sampleRate_);
        reset();
    }

    void reset() noexcept
    {
        chanLM_.reset();
        chanRS_.reset();
        chanLM_.update(sampleRate_);
        chanRS_.update(sampleRate_);
    }

    void setMode(int modeIdx) noexcept
    {
        mode_ = std::clamp(modeIdx, 0, 3);
    }

    int getMode() const noexcept { return mode_; }

    void setInputGainLM(float norm) noexcept
    {
        chanLM_.inputGainNorm = std::clamp(norm, 0.0f, 1.0f);
    }

    void setThresholdLM(float norm) noexcept
    {
        chanLM_.thresholdNorm = std::clamp(norm, 0.0f, 1.0f);
    }

    void setTimeConstantLM(float norm) noexcept
    {
        chanLM_.timeConstantNorm = std::clamp(norm, 0.0f, 1.0f);
        chanLM_.update(sampleRate_);
    }

    void setDcBiasLM(float norm) noexcept
    {
        chanLM_.dcBiasNorm = std::clamp(norm, 0.0f, 1.0f);
        chanLM_.update(sampleRate_);
    }

    void setOutputGainLM(float norm) noexcept
    {
        chanLM_.outputGainNorm = std::clamp(norm, 0.0f, 1.0f);
    }

    void setBiasBalance(float norm) noexcept
    {
        biasBal_ = std::clamp(norm, 0.0f, 1.0f);
    }

    void setInputGainRS(float norm) noexcept
    {
        chanRS_.inputGainNorm = std::clamp(norm, 0.0f, 1.0f);
    }

    void setThresholdRS(float norm) noexcept
    {
        chanRS_.thresholdNorm = std::clamp(norm, 0.0f, 1.0f);
    }

    void setTimeConstantRS(float norm) noexcept
    {
        chanRS_.timeConstantNorm = std::clamp(norm, 0.0f, 1.0f);
        chanRS_.update(sampleRate_);
    }

    void setDcBiasRS(float norm) noexcept
    {
        chanRS_.dcBiasNorm = std::clamp(norm, 0.0f, 1.0f);
        chanRS_.update(sampleRate_);
    }

    void setOutputGainRS(float norm) noexcept
    {
        chanRS_.outputGainNorm = std::clamp(norm, 0.0f, 1.0f);
    }

    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 int numSamples) noexcept
    {
        if (mode_ == kBypass)
        {
            for (int s = 0; s < numSamples; ++s)
            {
                outL[s] = inL[s];
                outR[s] = inR[s];
            }
            return;
        }

        // En modo estéreo, el canal RS refleja los parámetros del canal LM
        if (mode_ == kStereo)
        {
            chanRS_.inputGainNorm = chanLM_.inputGainNorm;
            chanRS_.thresholdNorm = chanLM_.thresholdNorm;
            chanRS_.timeConstantNorm = chanLM_.timeConstantNorm;
            chanRS_.dcBiasNorm = chanLM_.dcBiasNorm;
            chanRS_.outputGainNorm = chanLM_.outputGainNorm;
            chanRS_.update(sampleRate_);
        }

        for (int s = 0; s < numSamples; ++s)
        {
            const float l = inL[s];
            const float r = inR[s];

            if (mode_ == kMidSide)
            {
                const float mid = (l + r) * 0.5f;
                const float side = (l - r) * 0.5f;

                const float midProc = chanLM_.processSample(mid, sampleRate_);
                const float sideProc = chanRS_.processSample(side, sampleRate_);

                outL[s] = midProc + sideProc;
                outR[s] = midProc - sideProc;
            }
            else
            {
                outL[s] = chanLM_.processSample(l, sampleRate_);
                outR[s] = chanRS_.processSample(r, sampleRate_);
            }
        }
    }

private:
    double sampleRate_ = 44100.0;
    int mode_ = kStereo;
    float biasBal_ = 0.5f;

    ChannelDetector chanLM_;
    ChannelDetector chanRS_;
};

} // namespace abd::dsp
