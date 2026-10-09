/*
  ==============================================================================

    DspEnvelopeFollower.h
    Detector / seguidor de envolvente analógico de precisión en C++ estándar.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspCore/DspEnvelopeFollower.h
    NAMESPACE: abd::dsp

    QUÉ ES:
      Detector de envolvente de 1 polo con tiempos de ataque y relajación
      analógicos configurables. Diseñado para bancos de vocoder, compresores,
      compuertas de ruido y filtros seguidores de envolvente (auto-wah / envelope filter).

    CONTRATO Y RENDIMIENTO:
      - 100% C++20 estándar, CERO dependencias de JUCE.
      - 100% Real-Time Safe: sin asignaciones dinámicas en memoria.
      - Soporte de modo de pico inmediato (ataque instantáneo cuando attack <= 0.0001s).
      - Respuesta exponencial analógica mediante coeficientes de polo z:
          alpha = exp(-1 / (tiempo * sampleRate))

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <cmath>

namespace abd::dsp
{

/**
 * @brief Seguidor de envolvente analógico con ataque y decaimiento configurables.
 */
class EnvelopeFollower
{
public:
    EnvelopeFollower() = default;

    /** Inicializa el sample rate y restablece el estado. */
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        envelope_ = 0.0f;
        setAttackTime(0.005f);   // 5ms por defecto
        setReleaseTime(0.050f);  // 50ms por defecto
    }

    /** Restablece el nivel de envolvente acumulado a cero. */
    void reset() noexcept
    {
        envelope_ = 0.0f;
    }

    /** Fija el tiempo de ataque en segundos (0.0 = ataque instantáneo / peak follower). */
    void setAttackTime(float seconds) noexcept
    {
        attackTime_ = std::max(0.0f, seconds);
        if (attackTime_ <= 0.0001f)
            attackCoef_ = 0.0f;
        else
            attackCoef_ = std::exp(-1.0f / (attackTime_ * static_cast<float>(sampleRate_)));
    }

    /** Fija el tiempo de relajación/decaimiento en segundos. */
    void setReleaseTime(float seconds) noexcept
    {
        releaseTime_ = std::max(0.001f, seconds);
        releaseCoef_ = std::exp(-1.0f / (releaseTime_ * static_cast<float>(sampleRate_)));
    }

    /** Fija ambos tiempos (ataque y relajación) simultáneamente en segundos. */
    void setTimes(float attackSeconds, float releaseSeconds) noexcept
    {
        setAttackTime(attackSeconds);
        setReleaseTime(releaseSeconds);
    }

    /**
     * Procesa una muestra de audio y devuelve el nivel actual de la envolvente.
     */
    float process(float sample) noexcept
    {
        const float absVal = std::abs(sample);
        if (absVal > envelope_)
        {
            if (attackCoef_ <= 0.0f)
                envelope_ = absVal;
            else
                envelope_ = absVal + attackCoef_ * (envelope_ - absVal);
        }
        else
        {
            envelope_ = absVal + releaseCoef_ * (envelope_ - absVal);
        }
        return envelope_;
    }

    /** Devuelve el nivel instantáneo actual de la envolvente. */
    float getCurrentLevel() const noexcept
    {
        return envelope_;
    }

private:
    double sampleRate_ { 44100.0 };
    float envelope_ { 0.0f };
    float attackTime_ { 0.005f };
    float releaseTime_ { 0.050f };
    float attackCoef_ { 0.0f };
    float releaseCoef_ { 0.999f };
};

} // namespace abd::dsp
