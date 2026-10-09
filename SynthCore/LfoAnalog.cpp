#include "LfoAnalog.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace abd::synth
{
LfoAnalog::LfoAnalog()
{
    reset();
}

void LfoAnalog::setSampleRate(double newSampleRate)
{
    sampleRate = std::max(1.0, newSampleRate);
    updatePhaseIncrement();
}

void LfoAnalog::setRate(float rateHz)
{
    float clamped = std::clamp(rateHz, 0.005f, 1280.0f);
    if (clamped == rate) return; // hot-path guard
    rate = clamped;
    updatePhaseIncrement();
}

void LfoAnalog::setShape(int shapeIndex)
{
    currentShape = static_cast<Shape>(std::clamp(shapeIndex, 0, 6));
}

void LfoAnalog::setKeySync(bool sync)
{
    keySync = sync;
}

void LfoAnalog::setDelay(float delaySec)
{
    delayTime = std::max(0.0f, delaySec);
}

void LfoAnalog::setDelayScale(float escala)
{
    delayScale = std::clamp(escala, 0.0f, 20.0f);
}

void LfoAnalog::setSlewScale(float escala)
{
    slewScale = std::clamp(escala, 0.0f, 20.0f);
}

void LfoAnalog::setSlew(float slewAmount)
{
    slew = std::clamp(slewAmount, 0.0f, 1.0f);
}

void LfoAnalog::reset()
{
    phase               = 0.0;
    delaySamplesElapsed = 0.0;
    lastOutput          = 0.0f;
    targetSH            = 0.0f;
    currentSH           = 0.0f;
}

void LfoAnalog::trigger()
{
    if (keySync)
    {
        phase               = 0.0;
        delaySamplesElapsed = 0.0;
    }
}

void LfoAnalog::setPhase(double newPhase)
{
    phase = std::fmod(newPhase, 1.0);
    if (phase < 0.0) phase += 1.0;
}

double LfoAnalog::getPhase() const
{
    return phase;
}

void LfoAnalog::setRateScale(float escala)
{
    const float recortada = std::clamp(escala, 0.05f, 20.0f);
    if (recortada == rateScale) return; // hot-path guard
    rateScale = recortada;
    updatePhaseIncrement();
}

void LfoAnalog::updatePhaseIncrement()
{
    phaseIncrement = (rate * rateScale) / sampleRate;
}

float LfoAnalog::nextRandomFloat()
{
    randomSeed = randomSeed * 1664525u + 1013904223u;
    return -1.0f + 2.0f * (static_cast<float>(randomSeed & 0x7FFFFFFFu) / 2147483647.0f);
}

float LfoAnalog::generateRawWave()
{
    float out = 0.0f;
    double p  = phase;

    switch (currentShape)
    {
        case Shape::kSine:
            out = std::sin(2.0 * M_PI * p);
            break;

        case Shape::kTriangle:
            if (p < 0.25)
                out = static_cast<float>(p * 4.0);
            else if (p < 0.75)
                out = static_cast<float>(2.0 - p * 4.0);
            else
                out = static_cast<float>(p * 4.0 - 4.0);
            break;

        case Shape::kSquare:
            out = (p < 0.5) ? 1.0f : -1.0f;
            break;

        case Shape::kRampUp:
            out = -1.0f + 2.0f * static_cast<float>(p);
            break;

        case Shape::kRampDown:
            out = 1.0f - 2.0f * static_cast<float>(p);
            break;

        case Shape::kSampleHold:
        case Shape::kSampleGlide:
            break;
    }

    return out;
}

float LfoAnalog::nextSample()
{
    // 1. Fade-in: 40% silent delay time, 60% linear amplitude ramp (Behringer DeepMind 12 specs)
    float fadeGain = 1.0f;
    if (delayTime > 0.0f)
    {
        double delaySamples = static_cast<double>(delayTime) * delayScale * sampleRate;
        if (delaySamplesElapsed < delaySamples)
        {
            double silentSamples = delaySamples * 0.4;
            if (delaySamplesElapsed < silentSamples)
            {
                fadeGain = 0.0f;
            }
            else
            {
                double fadeSamples = delaySamples * 0.6;
                fadeGain           = static_cast<float>((delaySamplesElapsed - silentSamples) / (fadeSamples > 0.0 ? fadeSamples : 1.0));
            }
            delaySamplesElapsed += 1.0;
        }
    }

    // 2. Phase advancement
    bool phaseWrapped = false;
    double nextPhase  = phase + phaseIncrement;
    if (nextPhase >= 1.0)
    {
        phaseWrapped = true;
        nextPhase    = std::fmod(nextPhase, 1.0);
    }
    phase = nextPhase;

    // 3. Sample & Hold / Sample & Glide random target generation (LCG)
    if (phaseWrapped || (currentSH == 0.0f && targetSH == 0.0f))
    {
        targetSH = nextRandomFloat();
        if (currentShape == Shape::kSampleHold)
        {
            currentSH = targetSH;
        }
    }

    // 4. Generate raw waveform value
    float rawOutput = 0.0f;
    if (currentShape == Shape::kSampleHold)
    {
        rawOutput = currentSH;
    }
    else if (currentShape == Shape::kSampleGlide)
    {
        // Glide constant proportional to LFO period, SR-independent
        float periodSec    = 1.0f / std::max(0.01f, rate);
        float glideTimeSec = periodSec * 0.1f;
        float k            = 1.0f - std::exp(-1.0f / (glideTimeSec * static_cast<float>(sampleRate)));
        currentSH += (targetSH - currentSH) * std::clamp(k, 0.001f, 1.0f);
        rawOutput = currentSH;
    }
    else
    {
        rawOutput = generateRawWave();
    }

    // 5. Slew rate limiting (sample-rate independent, time-domain constant)
    if (slew > 0.0f)
    {
        float transitionTimeSec = slew * slewScale * 0.5f;
        if (transitionTimeSec > 0.0001f)
        {
            float maxChangePerSample = 2.0f / (transitionTimeSec * static_cast<float>(sampleRate));
            float change             = rawOutput - lastOutput;
            change                   = std::clamp(change, -maxChangePerSample, maxChangePerSample);
            rawOutput                = lastOutput + change;
        }
    }

    lastOutput = rawOutput;

    // 6. Apply fade-in gain
    return rawOutput * fadeGain;
}

float LfoAnalog::getUnipolar() const
{
    return lastOutput * 0.5f + 0.5f;
}
} // namespace abd::synth
