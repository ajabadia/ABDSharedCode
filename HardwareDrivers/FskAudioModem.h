/**
 * @file FskAudioModem.h
 * @brief Continuous-Phase Frequency Shift Keying (CPFSK) Audio Modem & Detector.
 * @details Implements 12 kHz (Mark / 0) and 14 kHz (Space / 1) audio carrier modulation
 *          and detection for Roland AIRA Modular units (Torcido, Bitrazer, Demora, Scooper)
 *          via the REMOTE IN / Audio In jack as documented in docs/google ia research/001.txt.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cstdint>
#include <cmath>

namespace abd::hw
{

/**
 * @struct FskModemConfig
 * @brief Configuration parameters for the FSK audio modem.
 */
struct FskModemConfig
{
    float markFrequencyHz  { 12000.0f }; /**< Carrier frequency for binary 0 (Mark): 12 kHz. */
    float spaceFrequencyHz { 14000.0f }; /**< Carrier frequency for binary 1 (Space): 14 kHz. */
    float baudRate         { 1200.0f };  /**< Symbol rate in bauds (symbols per second). */
    float amplitude        { 0.75f };    /**< Audio peak amplitude [0.0 .. 1.0]. */
    int preambleBits       { 16 };       /**< Preamble clock synchronization bits. */
};

/**
 * @struct FskCarrierDetection
 * @brief Spectral analysis result for FSK tone detection on an incoming audio signal.
 */
struct FskCarrierDetection
{
    bool detected { false };        /**< True if dual-frequency FSK signature is confirmed. */
    float markPowerDb { -100.0f };  /**< Power level in dB around Mark frequency (12 kHz). */
    float spacePowerDb { -100.0f }; /**< Power level in dB around Space frequency (14 kHz). */
    float noisePowerDb { -100.0f }; /**< Out-of-band noise/spectral baseline power in dB. */
    float snrDb { 0.0f };           /**< Signal-to-noise ratio in dB. */
};

/**
 * @class FskAudioModem
 * @brief Modulator, carrier detector, and demodulator for audio-in FSK hardware communication.
 */
class FskAudioModem
{
public:
    explicit FskAudioModem(const FskModemConfig& config = {});
    ~FskAudioModem() = default;

    [[nodiscard]] const FskModemConfig& getConfig() const noexcept { return config; }
    void setConfig(const FskModemConfig& newConfig) noexcept { config = newConfig; }

    /**
     * @brief Synthesizes a Continuous-Phase FSK audio buffer from raw data bytes.
     * @param data Pointer to input byte buffer.
     * @param size Number of bytes to modulate.
     * @param sampleRate Output audio sampling rate (e.g. 48000.0, 96000.0).
     * @return Single-channel AudioBuffer containing the synthesized FSK waveform.
     */
    [[nodiscard]] juce::AudioBuffer<float> modulate(const uint8_t* data, size_t size, double sampleRate) const;

    /**
     * @brief Overload taking a std::vector of bytes.
     */
    [[nodiscard]] juce::AudioBuffer<float> modulate(const std::vector<uint8_t>& data, double sampleRate) const
    {
        return modulate(data.data(), data.size(), sampleRate);
    }

    /**
     * @brief Analyzes an incoming audio buffer for the presence of the 12 kHz / 14 kHz FSK carrier.
     * @param buffer Incoming audio signal to inspect.
     * @param sampleRate Sampling rate of the buffer.
     * @param snrThresholdDb Minimum SNR in dB required to confirm FSK presence (default: 8.0 dB).
     * @return Carrier detection result.
     */
    [[nodiscard]] FskCarrierDetection detectCarrier(const juce::AudioBuffer<float>& buffer,
                                                    double sampleRate,
                                                    float snrThresholdDb = 8.0f) const;

    /**
     * @brief Demodulates an audio buffer containing an FSK transmission back into bytes.
     * @param buffer Audio buffer with FSK signal.
     * @param sampleRate Audio sampling rate.
     * @return Decoded payload bytes.
     */
    [[nodiscard]] std::vector<uint8_t> demodulate(const juce::AudioBuffer<float>& buffer,
                                                  double sampleRate) const;

    /**
     * @brief Evaluates tone magnitude/power at an exact frequency using the Goertzel algorithm.
     * @param samples Pointer to audio samples.
     * @param numSamples Number of samples to analyze.
     * @param sampleRate Sampling rate in Hz.
     * @param targetFreqHz Target frequency in Hz to probe.
     * @return Normalized power magnitude.
     */
    [[nodiscard]] static float computeGoertzelPower(const float* samples,
                                                    int numSamples,
                                                    double sampleRate,
                                                    float targetFreqHz) noexcept;

private:
    FskModemConfig config;
};

} // namespace abd::hw
