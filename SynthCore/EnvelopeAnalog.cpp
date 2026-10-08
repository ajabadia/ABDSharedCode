#include "EnvelopeAnalog.h"
#include <cmath>
#include <algorithm>

namespace abd::synth
{
    EnvelopeAnalog::EnvelopeAnalog()
    {
        reset();
    }

    void EnvelopeAnalog::setSampleRate(double newSampleRate)
    {
        sampleRate = std::max(1.0, newSampleRate);
    }

    void EnvelopeAnalog::setParameters(float attackTimeSec, float decayTimeSec, float sustainLvl, float releaseTimeSec)
    {
        attackTime = std::max(0.001f, attackTimeSec);
        decayTime = std::max(0.001f, decayTimeSec);
        sustainLevel = std::clamp(sustainLvl, 0.0f, 1.0f);
        releaseTime = std::max(0.001f, releaseTimeSec);
    }

    void EnvelopeAnalog::setCurves(float attackCrv, float decayCrv, float sustainCrv, float releaseCrv)
    {
        attackCurve = std::clamp(attackCrv, -1.0f, 1.0f);
        decayCurve = std::clamp(decayCrv, -1.0f, 1.0f);
        sustainCurve = std::clamp(sustainCrv, -1.0f, 1.0f);
        releaseCurve = std::clamp(releaseCrv, -1.0f, 1.0f);
    }

    void EnvelopeAnalog::setSustainOffset(float desplazamiento)
    {
        sustainOffset = std::clamp(desplazamiento, -1.0f, 1.0f);
    }

    void EnvelopeAnalog::setCurveModulation(Stage stage, float modulacion)
    {
        curveModulation[(int)stage] = std::clamp(modulacion, -1.0f, 1.0f);
    }

    void EnvelopeAnalog::trigger()
    {
        startLevel = currentLevel;
        targetLevel = 1.0f;
        changeStage(Stage::kAttack);
    }

    void EnvelopeAnalog::release()
    {
        if (currentStage != Stage::kIdle)
        {
            startLevel = currentLevel;
            targetLevel = 0.0f;
            changeStage(Stage::kRelease);
        }
    }

    void EnvelopeAnalog::reset()
    {
        currentStage = Stage::kIdle;
        currentLevel = 0.0f;
        currentProgress = 0.0;
        progressIncrement = 0.0;
    }

    void EnvelopeAnalog::changeStage(Stage newStage)
    {
        currentStage = newStage;
        currentProgress = 0.0;

        switch (currentStage)
        {
            case Stage::kAttack:
                currentStageDurationSec = attackTime;
                break;
            case Stage::kDecay:
                currentStageDurationSec = decayTime;
                break;
            case Stage::kSustain:
                currentStageDurationSec = 1.0; // No increment until release
                break;
            case Stage::kRelease:
                currentStageDurationSec = releaseTime;
                break;
            case Stage::kIdle:
            default:
                currentStageDurationSec = 1.0;
                break;
        }

        progressIncrement = 1.0 / (currentStageDurationSec * sampleRate * (double)timeScale);
    }

    void EnvelopeAnalog::setLoopMode(bool loop)
    {
        loopMode = loop;
    }

    void EnvelopeAnalog::setBypassSustain(bool bypass)
    {
        bypassSustain = bypass;
    }

    void EnvelopeAnalog::setTimeScale(float scale)
    {
        scale = std::max(0.1f, scale); // evitar scale <= 0
        if (scale == timeScale) return; // hot-path guard (se llama por muestra)
        timeScale = scale;
        progressIncrement = 1.0 / (currentStageDurationSec * sampleRate * (double)scale);
    }

    float EnvelopeAnalog::sustainEfectivo() const
    {
        return std::clamp(sustainLevel + sustainOffset, 0.0f, 1.0f);
    }

    float EnvelopeAnalog::applyCurve(float progress, float curveAmount)
    {
        if (std::abs(curveAmount) < 0.005f)
            return progress;

        float exponent = 1.0f;
        if (curveAmount < 0.0f)
            exponent = 1.0f - curveAmount * 3.0f; // curves up to 4.0
        else
            exponent = 1.0f / (1.0f + curveAmount * 3.0f); // curves down to 0.25

        return std::pow(progress, exponent);
    }

    float EnvelopeAnalog::nextSample()
    {
        if (currentStage == Stage::kIdle)
            return 0.0f;

        if (currentStage == Stage::kSustain)
        {
            // Sustain curve: modula sutilmente el nivel sostenido sobre el tiempo
            //   curve=0:  sustain plano (comportamiento estándar)
            //   curve<0:  el nivel decrece gradualmente (slow fade)
            //   curve>0:  el nivel crece gradualmente (slow swell)
            currentProgress += progressIncrement;
            if (currentProgress >= 1.0) currentProgress = 1.0;
            
            float curveMod = 0.0f;
            if (std::abs(sustainCurve) > 0.005f)
            {
                float progress = (float)currentProgress;
                float curvedProgress = applyCurve(progress,
                                                  sustainCurve + curveModulation[(int)Stage::kSustain]);
                // Desviación máxima: ±10% del sustain level
                float deviation = (curvedProgress - progress) * sustainEfectivo() * 0.1f;
                curveMod = deviation;
            }
            currentLevel = std::clamp(sustainEfectivo() + curveMod, 0.0f, 1.0f);
            return currentLevel;
        }

        currentProgress += progressIncrement;
        if (currentProgress >= 1.0)
        {
            currentProgress = 1.0;
            
            // Advance stage
            if (currentStage == Stage::kAttack)
            {
                startLevel = 1.0f;
                targetLevel = sustainEfectivo();
                changeStage(Stage::kDecay);
            }
            else if (currentStage == Stage::kDecay)
            {
                if (loopMode)
                {
                    // Loop mode: al completar Decay, re-trigger a Attack (sin Sustain)
                    startLevel = currentLevel;
                    targetLevel = 1.0f;
                    changeStage(Stage::kAttack);
                }
                else if (bypassSustain)
                {
                    // One-shot: salta Sustain y va directo a Release
                    startLevel = currentLevel;
                    targetLevel = 0.0f;
                    changeStage(Stage::kRelease);
                }
                else
                {
                    changeStage(Stage::kSustain);
                }
            }
            else if (currentStage == Stage::kRelease)
            {
                reset();
                return 0.0f;
            }
        }

        float progress = (float)currentProgress;
        float curvedProgress = progress;

        if (currentStage == Stage::kAttack)
        {
            // Curva de ataque: norm 0 (internal -1) = exponential (subida rápida inicial),
            // norm 255 (internal +1) = logarithmic (subida lenta inicial). applyCurve(+,amount)
            // da exponent<1 -> subida rápida inicial. Se niega para invertir la polaridad.
            curvedProgress = applyCurve(progress, -(attackCurve + curveModulation[(int)Stage::kAttack]));
            currentLevel = startLevel + (targetLevel - startLevel) * curvedProgress;
        }
        else if (currentStage == Stage::kDecay)
        {
            // Curva de decay: norm 0 (internal -1) = exponential (caída rápida inicial),
            // norm 255 (internal +1) = logarithmic (caída lenta inicial). El complemento
            // 1-(1-p)^e con signo positivo da caída rápida inicial cuando decayCurve<0.
            curvedProgress = 1.0f - applyCurve(1.0f - progress,
                                               decayCurve + curveModulation[(int)Stage::kDecay]);
            currentLevel = startLevel + (targetLevel - startLevel) * curvedProgress;
        }
        else if (currentStage == Stage::kRelease)
        {
            // Misma semántica que decay: exponencial = caída rápida inicial (norm 0),
            // logarítmica = caída lenta inicial (norm 255).
            curvedProgress = 1.0f - applyCurve(1.0f - progress,
                                               releaseCurve + curveModulation[(int)Stage::kRelease]);
            currentLevel = startLevel + (targetLevel - startLevel) * curvedProgress;
        }

        return currentLevel;
    }
}
