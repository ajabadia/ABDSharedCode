/**
 * @file FskAudioModem.cpp
 * @brief Implementation of CPFSK Audio Modem and Goertzel-based FSK detector.
 * @author ABDSynths
 * @date 2026
 */

#include "FskAudioModem.h"
#include <algorithm>
// #include <numbers>

namespace abd::hw
{

FskAudioModem::FskAudioModem(const FskModemConfig& cfg)
    : config(cfg)
{
}

float FskAudioModem::computeGoertzelPower(const float* samples,
                                          int numSamples,
                                          double sampleRate,
                                          float targetFreqHz) noexcept
{
    if (samples == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return 0.0f;

    // Standard Goertzel algorithm
    float k = 0.5f + (static_cast<float>(numSamples) * targetFreqHz / static_cast<float>(sampleRate));
    float omega = (2.0f * 3.14159265358979323846f / static_cast<float>(numSamples)) * k;
    float coeff = 2.0f * std::cos(omega);

    float s_prev = 0.0f;
    float s_prev2 = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float s = samples[i] + coeff * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev = s;
    }

    float power = (s_prev * s_prev) + (s_prev2 * s_prev2) - (coeff * s_prev * s_prev2);
    // Normalize by N^2
    float norm = static_cast<float>(numSamples * numSamples);
    return std::max(0.0f, power / (norm > 0.0f ? norm : 1.0f));
}

juce::AudioBuffer<float> FskAudioModem::modulate(const uint8_t* data, size_t size, double sampleRate) const
{
    if (sampleRate < 8000.0) sampleRate = 48000.0;
    double samplesPerBitExact = sampleRate / static_cast<double>(config.baudRate > 100.0f ? config.baudRate : 4800.0f);
    int samplesPerBit = std::max(1, static_cast<int>(std::round(samplesPerBitExact)));

    // Assemble bitstream: Preamble + (StartBit + 8 DataBits + StopBit per byte) + Trailer
    std::vector<int> bits;
    bits.reserve(static_cast<size_t>(config.preambleBits) + size * 10 + 4);

    // Preamble: carrier space (1) = line idle
    for (int i = 0; i < config.preambleBits; ++i)
        bits.push_back(1);

    for (size_t b = 0; b < size; ++b)
    {
        uint8_t byteVal = data[b];
        // Start bit: 0
        bits.push_back(0);
        // 8 data bits: LSB first
        for (int i = 0; i < 8; ++i)
        {
            bits.push_back((byteVal >> i) & 1);
        }
        // Stop bit: 1
        bits.push_back(1);
    }

    // Trailer: 4 stop bits (1)
    for (int i = 0; i < 4; ++i)
        bits.push_back(1);

    int totalSamples = static_cast<int>(bits.size()) * samplesPerBit;
    juce::AudioBuffer<float> buffer(1, totalSamples);
    buffer.clear();
    auto* writePtr = buffer.getWritePointer(0);

    double phase = 0.0;
    const double twoPi = 2.0 * 3.14159265358979323846;
    int sampleIdx = 0;

    for (int bit : bits)
    {
        double freq = (bit == 0) ? config.markFrequencyHz : config.spaceFrequencyHz;
        double phaseInc = (twoPi * freq) / sampleRate;

        for (int s = 0; s < samplesPerBit; ++s)
        {
            writePtr[sampleIdx++] = static_cast<float>(std::sin(phase) * config.amplitude);
            phase += phaseInc;
            if (phase >= twoPi) phase -= twoPi;
        }
    }

    return buffer;
}

FskCarrierDetection FskAudioModem::detectCarrier(const juce::AudioBuffer<float>& buffer,
                                                double sampleRate,
                                                float snrThresholdDb) const
{
    FskCarrierDetection res;
    if (buffer.getNumSamples() <= 0 || sampleRate <= 0.0)
        return res;

    const float* readPtr = buffer.getReadPointer(0);
    int numSamples = buffer.getNumSamples();

    // 1. Measure power around Mark (12 kHz) and Space (14 kHz)
    float markPower = computeGoertzelPower(readPtr, numSamples, sampleRate, config.markFrequencyHz);
    float spacePower = computeGoertzelPower(readPtr, numSamples, sampleRate, config.spaceFrequencyHz);

    // 2. Measure out-of-band power at reference guard frequencies (8 kHz and 18 kHz)
    float ref1 = computeGoertzelPower(readPtr, numSamples, sampleRate, 8000.0f);
    float ref2 = computeGoertzelPower(readPtr, numSamples, sampleRate, std::min(static_cast<float>(sampleRate * 0.45), 18000.0f));
    float noiseFloor = std::max({ ref1, ref2, 1e-12f });

    float carrierPower = std::max(markPower, spacePower);
    res.markPowerDb = 10.0f * std::log10(std::max(markPower, 1e-12f));
    res.spacePowerDb = 10.0f * std::log10(std::max(spacePower, 1e-12f));
    res.noisePowerDb = 10.0f * std::log10(noiseFloor);

    res.snrDb = 10.0f * std::log10(carrierPower / noiseFloor);

    // FSK carrier detected if carrier is well above noise floor and above absolute silence threshold
    if (res.snrDb >= snrThresholdDb && carrierPower > 1e-6f)
    {
        res.detected = true;
    }

    return res;
}

std::vector<uint8_t> FskAudioModem::demodulate(const juce::AudioBuffer<float>& buffer,
                                              double sampleRate) const
{
    std::vector<uint8_t> result;
    if (buffer.getNumSamples() <= 0 || sampleRate <= 0.0)
        return result;

    const float* samples = buffer.getReadPointer(0);
    int totalSamples = buffer.getNumSamples();
    double samplesPerBitExact = sampleRate / static_cast<double>(config.baudRate > 100.0f ? config.baudRate : 4800.0f);
    int samplesPerBit = std::max(1, static_cast<int>(std::round(samplesPerBitExact)));

    // Decode bit by bit across the buffer
    int totalBits = totalSamples / samplesPerBit;
    std::vector<int> decodedBits;
    decodedBits.reserve(static_cast<size_t>(totalBits));

    for (int b = 0; b < totalBits; ++b)
    {
        const float* bitSamples = samples + (b * samplesPerBit);
        float pMark = computeGoertzelPower(bitSamples, samplesPerBit, sampleRate, config.markFrequencyHz);
        float pSpace = computeGoertzelPower(bitSamples, samplesPerBit, sampleRate, config.spaceFrequencyHz);

        decodedBits.push_back((pSpace > pMark) ? 1 : 0);
    }

    // Framing decoder: Search for Start bit (0), followed by 8 data bits and Stop bit (1)
    size_t i = 0;
    while (i + 10 <= decodedBits.size())
    {
        // Start bit must be 0
        if (decodedBits[i] == 0)
        {
            // Verify stop bit at offset 9
            if (decodedBits[i + 9] == 1)
            {
                uint8_t byteVal = 0;
                for (int bitIdx = 0; bitIdx < 8; ++bitIdx)
                {
                    if (decodedBits[i + 1 + static_cast<size_t>(bitIdx)] == 1)
                    {
                        byteVal |= static_cast<uint8_t>(1 << bitIdx);
                    }
                }
                result.push_back(byteVal);
                i += 10; // Advance to next frame
                continue;
            }
        }
        ++i;
    }

    return result;
}

} // namespace abd::hw


