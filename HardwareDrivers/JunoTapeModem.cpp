/**
 * @file JunoTapeModem.cpp
 * @brief Implementation of Roland Juno-106 / Juno-60 Tape Interface CPFSK Audio Modem.
 * @details Uses zero-crossing counting for bit discrimination (1 cycle/bit = space/0,
 *          2 cycles/bit = mark/1), matching the original Roland hardware approach.
 * @author ABDSynths
 * @date 2026
 */

#include "JunoTapeModem.h"
#include <algorithm>
#include <cmath>
// #include <numbers>

namespace abd::hw
{

JunoTapeModem::JunoTapeModem(const JunoTapeConfig& cfg)
    : config(cfg)
{
}

uint8_t JunoTapeModem::calculateChecksum(const uint8_t* data, size_t size) noexcept
{
    if (data == nullptr || size == 0)
        return 0;

    int sum = 0;
    for (size_t i = 0; i < size; ++i)
    {
        sum += data[i];
    }
    // Roland 7-bit Two's complement checksum: (-sum) & 0x7F
    return static_cast<uint8_t>((-(sum & 0x7F)) & 0x7F);
}

float JunoTapeModem::computeGoertzelPower(const float* samples,
                                         int numSamples,
                                         double sampleRate,
                                         float targetFreqHz) noexcept
{
    if (samples == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return 0.0f;

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
    float norm = static_cast<float>(numSamples * numSamples);
    return std::max(0.0f, power / (norm > 0.0f ? norm : 1.0f));
}

juce::AudioBuffer<float> JunoTapeModem::encodeToAudio(const uint8_t* data,
                                                     size_t size,
                                                     double sampleRate) const
{
    if (sampleRate < 8000.0)
        sampleRate = 48000.0;

    double samplesPerBitExact = sampleRate / static_cast<double>(config.baudRate > 100.0f ? config.baudRate : 1300.0f);
    int samplesPerBit = std::max(1, static_cast<int>(std::round(samplesPerBitExact)));

    // [SyncByte] + [LenLSB, LenMSB] + [Payload] + [Checksum]
    std::vector<uint8_t> frame;
    frame.reserve(size + 4);
    frame.push_back(config.syncByte);
    frame.push_back(static_cast<uint8_t>(size & 0xFF));
    frame.push_back(static_cast<uint8_t>((size >> 8) & 0xFF));
    for (size_t i = 0; i < size; ++i)
    {
        frame.push_back(data != nullptr ? data[i] : 0);
    }
    frame.push_back(calculateChecksum(data, size));

    // Build complete bitstream: pilot (mark bits) + UART frames + trailer
    std::vector<int> allBits;
    int pilotBits = std::max(8, static_cast<int>(std::round(
        config.pilotDurationSec * (config.baudRate > 100.0f ? config.baudRate : 1300.0f))));
    allBits.reserve(static_cast<size_t>(pilotBits) + frame.size() * 10 + 8);

    // Pilot tone (continuous mark = 1 = 2600 Hz)
    for (int i = 0; i < pilotBits; ++i)
        allBits.push_back(1);

    // UART frames: Start(0), 8 data bits LSB first, Stop(1)
    for (uint8_t b : frame)
    {
        allBits.push_back(0); // Start bit (space = 1300 Hz)
        for (int i = 0; i < 8; ++i)
        {
            allBits.push_back((b >> i) & 1);
        }
        allBits.push_back(1); // Stop bit (mark = 2600 Hz)
    }
    // 4 trailer stop bits
    for (int i = 0; i < 4; ++i)
        allBits.push_back(1);

    int totalSamples = static_cast<int>(allBits.size()) * samplesPerBit;
    juce::AudioBuffer<float> buffer(1, totalSamples);
    buffer.clear();
    auto* writePtr = buffer.getWritePointer(0);

    double phase = 0.0;
    const double twoPi = 2.0 * 3.14159265358979323846;
    int sampleIdx = 0;

    // Continuous-phase FSK synthesis
    for (int bit : allBits)
    {
        // Space (0) = 1300 Hz, Mark (1) = 2600 Hz
        double freq = (bit == 0) ? config.spaceFrequencyHz : config.markFrequencyHz;
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

JunoTapeCarrierDetection JunoTapeModem::detectCarrier(const juce::AudioBuffer<float>& buffer,
                                                     double sampleRate,
                                                     float snrThresholdDb) const
{
    JunoTapeCarrierDetection result;
    if (buffer.getNumSamples() < 64 || sampleRate <= 0.0)
        return result;

    const auto* readPtr = buffer.getReadPointer(0);
    int numSamples = buffer.getNumSamples();

    // Use Goertzel on the full buffer for carrier detection (large N = good resolution)
    float markPower = computeGoertzelPower(readPtr, numSamples, sampleRate, config.markFrequencyHz);
    float spacePower = computeGoertzelPower(readPtr, numSamples, sampleRate, config.spaceFrequencyHz);
    float noisePower = computeGoertzelPower(readPtr, numSamples, sampleRate, 5000.0f);

    auto toDb = [](float p) {
        return (p > 1e-12f) ? (10.0f * std::log10(p)) : -120.0f;
    };

    result.pilotPowerDb = toDb(markPower);
    result.spacePowerDb = toDb(spacePower);
    result.noisePowerDb = toDb(noisePower);

    float peakTonePower = std::max(markPower, spacePower);
    float noiseFloor = std::max(noisePower, 1e-9f);
    result.snrDb = 10.0f * std::log10(peakTonePower / noiseFloor);

    result.detected = (result.snrDb >= snrThresholdDb) && (result.pilotPowerDb > -50.0f || result.spacePowerDb > -50.0f);
    return result;
}

std::vector<uint8_t> JunoTapeModem::decodeFromAudio(const juce::AudioBuffer<float>& buffer,
                                                   double sampleRate) const
{
    std::vector<uint8_t> emptyResult;
    if (buffer.getNumSamples() < 128 || sampleRate <= 0.0)
        return emptyResult;

    double samplesPerBitExact = sampleRate / static_cast<double>(config.baudRate > 100.0f ? config.baudRate : 1300.0f);
    int samplesPerBit = std::max(1, static_cast<int>(std::round(samplesPerBitExact)));

    const auto* samples = buffer.getReadPointer(0);
    int totalSamples = buffer.getNumSamples();

    // Zero-crossing counter for a window of samples.
    // Mark (2600 Hz) = 2 cycles per bit → ~4 zero crossings
    // Space (1300 Hz) = 1 cycle per bit  → ~2 zero crossings
    // Threshold at 3: >= 3 crossings → mark (1), < 3 → space (0)
    auto countZeroCrossings = [](const float* s, int n) -> int {
        int crossings = 0;
        for (int i = 1; i < n; ++i)
        {
            if ((s[i - 1] >= 0.0f && s[i] < 0.0f) || (s[i - 1] < 0.0f && s[i] >= 0.0f))
                ++crossings;
        }
        return crossings;
    };

    // Threshold: midpoint between expected zero crossings for space and mark
    // Space (1300 Hz, 1 cycle) → ~2 crossings, Mark (2600 Hz, 2 cycles) → ~4 crossings
    int zeroCrossThreshold = 3;

    int stepSize = std::max(1, samplesPerBit / 8);

    // Try phase offsets within one bit interval to find correct alignment
    for (int offset = 0; offset < samplesPerBit; offset += stepSize)
    {
        int numBits = (totalSamples - offset) / samplesPerBit;
        if (numBits < 20)
            continue;

        // Decode all bits using zero-crossing counting
        std::vector<int> decodedBits;
        decodedBits.reserve(static_cast<size_t>(numBits));

        for (int b = 0; b < numBits; ++b)
        {
            const float* bitSamples = samples + offset + (b * samplesPerBit);
            int crossings = countZeroCrossings(bitSamples, samplesPerBit);
            decodedBits.push_back((crossings >= zeroCrossThreshold) ? 1 : 0);
        }

        // UART framing decoder: Start bit (0), 8 data bits, Stop bit (1)
        std::vector<uint8_t> rawBytes;
        size_t bitIdx = 0;
        while (bitIdx + 10 <= decodedBits.size())
        {
            if (decodedBits[bitIdx] == 0) // Start bit
            {
                if (decodedBits[bitIdx + 9] == 1) // Stop bit
                {
                    uint8_t byteVal = 0;
                    for (int d = 0; d < 8; ++d)
                    {
                        if (decodedBits[bitIdx + 1 + static_cast<size_t>(d)] == 1)
                        {
                            byteVal |= static_cast<uint8_t>(1 << d);
                        }
                    }
                    rawBytes.push_back(byteVal);
                    bitIdx += 10;
                    continue;
                }
            }
            ++bitIdx;
        }

        // Search decoded bytes for sync byte followed by valid frame
        for (size_t s = 0; s < rawBytes.size(); ++s)
        {
            if (rawBytes[s] != config.syncByte)
                continue;

            if (s + 3 >= rawBytes.size())
                break;

            size_t payloadLen = static_cast<size_t>(rawBytes[s + 1]) | (static_cast<size_t>(rawBytes[s + 2]) << 8);
            if (payloadLen > 65536 || s + 3 + payloadLen >= rawBytes.size())
                continue;

            // Extract payload and verify checksum
            std::vector<uint8_t> candidatePayload(rawBytes.begin() + static_cast<ptrdiff_t>(s + 3),
                                                  rawBytes.begin() + static_cast<ptrdiff_t>(s + 3 + payloadLen));
            uint8_t expectedChecksum = rawBytes[s + 3 + payloadLen];
            uint8_t actualChecksum = calculateChecksum(candidatePayload.data(), candidatePayload.size());

            if (actualChecksum == expectedChecksum)
            {
                return candidatePayload;
            }
        }
    }

    return emptyResult;
}

} // namespace abd::hw


