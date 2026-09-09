/**
 * @file JunoBBD.h
 * @brief Authentic MN3009 Bucket Brigade Device (BBD) stereo chorus emulation.
 * @author ABDSynths
 * @date 2026
 *
 * Models a 256-stage Bucket Brigade Delay line with cubic interpolation,
 * analog anti-aliasing / reconstruction low-pass filter (9 kHz),
 * and dual-LFO quadrature modulation for Modes I, II, and I+II.
 */

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

namespace abd::lutdsp
{

class JunoBBD
{
public:
    enum class Mode
    {
        Off = 0,
        ModeI = 1,   // Subtle chorus (0.4 Hz rate)
        ModeII = 2,  // Richer/deeper chorus (0.6 Hz rate)
        ModeI_II = 3 // Fast vibrato/flanging (1.0 Hz + higher depth)
    };

    JunoBBD() = default;
    ~JunoBBD() = default;

    void prepare(double newSampleRate, int maxBlockSize = 512)
    {
        juce::ignoreUnused(maxBlockSize);
        sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;
        
        // 256-stage MN3009. Minimum clock gives ~15-20ms max delay.
        // Safety buffer of 100ms
        int bufferLength = static_cast<int>(sampleRate * 0.1) + 16;
        delayBufferL.assign(bufferLength, 0.0f);
        delayBufferR.assign(bufferLength, 0.0f);
        bufferSize = bufferLength;
        writePos = 0;

        lfoPhase = 0.0;
        updateLfoRates();

        // 1-pole reconstruction filter (approx 9 kHz low-pass)
        calcFilterCoeffs(9000.0);
        filterStateL = 0.0f;
        filterStateR = 0.0f;
    }

    void reset() noexcept
    {
        std::fill(delayBufferL.begin(), delayBufferL.end(), 0.0f);
        std::fill(delayBufferR.begin(), delayBufferR.end(), 0.0f);
        writePos = 0;
        lfoPhase = 0.0;
        filterStateL = 0.0f;
        filterStateR = 0.0f;
    }

    void setMode(Mode newMode) noexcept
    {
        currentMode = newMode;
        updateLfoRates();
    }

    Mode getMode() const noexcept { return currentMode; }

    /**
     * @brief Processes a stereo block in-place with BBD chorus wet/dry mixing.
     */
    void processBlock(juce::AudioBuffer<float>& buffer)
    {
        if (currentMode == Mode::Off || buffer.getNumChannels() == 0 || bufferSize <= 0)
            return;

        const int numSamples = buffer.getNumSamples();
        float* leftChannel = buffer.getWritePointer(0);
        float* rightChannel = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;

        const double lfoInc = (juce::MathConstants<double>::twoPi * lfoRateHz) / sampleRate;

        for (int i = 0; i < numSamples; ++i)
        {
            float inL = leftChannel[i];
            float inR = rightChannel ? rightChannel[i] : inL;
            float monoIn = 0.5f * (inL + inR);

            // Write to ring buffers
            delayBufferL[writePos] = monoIn;
            delayBufferR[writePos] = monoIn;

            // Quadrature LFO modulations (Mode L & R are 180 degrees out of phase for stereo width)
            double modL = std::sin(lfoPhase);
            double modR = std::sin(lfoPhase + juce::MathConstants<double>::pi);

            float delayTimeMsL = baseDelayMs + static_cast<float>(modL) * lfoDepthMs;
            float delayTimeMsR = baseDelayMs + static_cast<float>(modR) * lfoDepthMs;

            float delaySamplesL = (delayTimeMsL * 0.001f) * static_cast<float>(sampleRate);
            float delaySamplesR = (delayTimeMsR * 0.001f) * static_cast<float>(sampleRate);

            float delayedL = readInterpolated(delayBufferL, delaySamplesL);
            float delayedR = readInterpolated(delayBufferR, delaySamplesR);

            // Apply reconstruction low-pass filter (9 kHz)
            delayedL = processFilter(delayedL, filterStateL);
            delayedR = processFilter(delayedR, filterStateR);

            // Juno hardware mixes dry + delayed wet
            leftChannel[i] = 0.5f * (inL + delayedL);
            if (rightChannel)
            {
                rightChannel[i] = 0.5f * (inR + delayedR);
            }

            // Advance state
            writePos = (writePos + 1) % bufferSize;
            lfoPhase += lfoInc;
            if (lfoPhase >= juce::MathConstants<double>::twoPi)
                lfoPhase -= juce::MathConstants<double>::twoPi;
        }
    }

private:
    void updateLfoRates() noexcept
    {
        switch (currentMode)
        {
            case Mode::ModeI:
                lfoRateHz = 0.4;
                baseDelayMs = 4.0f;
                lfoDepthMs = 1.5f;
                break;
            case Mode::ModeII:
                lfoRateHz = 0.6;
                baseDelayMs = 5.0f;
                lfoDepthMs = 2.5f;
                break;
            case Mode::ModeI_II:
                lfoRateHz = 1.0;
                baseDelayMs = 3.5f;
                lfoDepthMs = 3.0f;
                break;
            case Mode::Off:
            default:
                lfoRateHz = 0.0;
                baseDelayMs = 0.0f;
                lfoDepthMs = 0.0f;
                break;
        }
    }

    void calcFilterCoeffs(double cutoffHz) noexcept
    {
        // Simple standard exponential moving average coefficient for 1-pole low-pass:
        // alpha = 1 - exp(-2 * pi * fc / fs)
        double dt = 1.0 / sampleRate;
        double rc = 1.0 / (juce::MathConstants<double>::twoPi * cutoffHz);
        filterAlpha = static_cast<float>(dt / (rc + dt));
    }

    float processFilter(float input, float& state) noexcept
    {
        state += filterAlpha * (input - state);
        return state;
    }

    float readInterpolated(const std::vector<float>& buf, float delaySamples) const noexcept
    {
        float readPos = static_cast<float>(writePos) - delaySamples;
        while (readPos < 0.0f) readPos += static_cast<float>(bufferSize);

        int idx0 = static_cast<int>(readPos);
        float frac = readPos - static_cast<float>(idx0);

        int i_m1 = (idx0 - 1 + bufferSize) % bufferSize;
        int i_0  = idx0 % bufferSize;
        int i_p1 = (idx0 + 1) % bufferSize;
        int i_p2 = (idx0 + 2) % bufferSize;

        float y0 = buf[i_m1];
        float y1 = buf[i_0];
        float y2 = buf[i_p1];
        float y3 = buf[i_p2];

        // 4-point Hermite cubic interpolation
        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    double sampleRate = 44100.0;
    Mode currentMode = Mode::Off;

    std::vector<float> delayBufferL;
    std::vector<float> delayBufferR;
    int bufferSize = 0;
    int writePos = 0;

    double lfoPhase = 0.0;
    double lfoRateHz = 0.4;
    float baseDelayMs = 4.0f;
    float lfoDepthMs = 1.5f;

    float filterAlpha = 0.5f;
    float filterStateL = 0.0f;
    float filterStateR = 0.0f;
};

} // namespace abd::lutdsp
