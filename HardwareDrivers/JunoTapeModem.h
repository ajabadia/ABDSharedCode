/**
 * @file JunoTapeModem.h
 * @brief Continuous-Phase FSK Audio Modem for Roland Juno Tape Interface.
 * @details Implements 1.3 kHz (Space / 0) and 2.6 kHz (Mark / 1) audio carrier
 *          modulation, pilot tone detection, and demodulation for Roland Juno-60,
 *          Juno-6, and HS-60 analog Tape Save / Load minijack interface.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace abd::hw
{

/**
 * @struct JunoTapeConfig
 * @brief Configuration parameters for the Juno tape audio modem.
 */
struct JunoTapeConfig
{
    float spaceFrequencyHz       { 1300.0f }; /**< Space / binary 0: 1.3 kHz. */
    float markFrequencyHz        { 2600.0f }; /**< Mark / binary 1: 2.6 kHz. */
    float baudRate               { 1300.0f }; /**< 1300 symbols/sec (1 cycle of 1.3 kHz = 2 cycles of 2.6 kHz per bit). */
    float pilotFrequencyHz       { 2600.0f }; /**< Continuous high pilot tone before data frames. */
    float pilotDurationSec       { 0.30f };   /**< Duration of lead-in pilot tone in seconds. */
    float amplitude              { 0.75f };   /**< Peak audio amplitude [0.0 .. 1.0]. */
    uint8_t syncByte             { 0xA5 };    /**< Frame synchronization byte. */
};

/**
 * @struct JunoTapeCarrierDetection
 * @brief Spectral analysis result for Juno tape pilot and data carrier tones.
 */
struct JunoTapeCarrierDetection
{
    bool detected { false };         /**< True if Juno tape FSK tones are identified. */
    float pilotPowerDb { -100.0f };  /**< Power level in dB at 2.6 kHz. */
    float spacePowerDb { -100.0f };  /**< Power level in dB at 1.3 kHz. */
    float noisePowerDb { -100.0f };  /**< Baseline noise power in dB. */
    float snrDb { 0.0f };            /**< Signal-to-noise ratio in dB. */
};

/**
 * @class JunoTapeModem
 * @brief Audio modem encoder and decoder for Roland Juno Tape Save/Load signal.
 */
class JunoTapeModem
{
public:
    explicit JunoTapeModem(const JunoTapeConfig& config = {});
    ~JunoTapeModem() = default;

    [[nodiscard]] const JunoTapeConfig& getConfig() const noexcept { return config; }
    void setConfig(const JunoTapeConfig& newConfig) noexcept { config = newConfig; }

    /**
     * @brief Modulates payload bytes into an analog audio buffer (Continuous-Phase FSK).
     * @param data Pointer to raw data bytes.
     * @param size Number of bytes.
     * @param sampleRate Output audio sampling rate (44100, 48000, 96000, etc.).
     * @return Mono AudioBuffer containing the synthesized tape waveform.
     */
    [[nodiscard]] juce::AudioBuffer<float> encodeToAudio(const uint8_t* data, size_t size, double sampleRate) const;

    /**
     * @brief Overload taking std::vector.
     */
    [[nodiscard]] juce::AudioBuffer<float> encodeToAudio(const std::vector<uint8_t>& data, double sampleRate) const
    {
        return encodeToAudio(data.data(), data.size(), sampleRate);
    }

    /**
     * @brief Detects presence of 1.3 kHz / 2.6 kHz Juno tape carrier tones in an incoming buffer.
     * @param buffer Incoming audio buffer to analyze.
     * @param sampleRate Audio sampling rate in Hz.
     * @param snrThresholdDb Minimum SNR in dB to confirm carrier detection (default: 6.0 dB).
     * @return Detection analysis result.
     */
    [[nodiscard]] JunoTapeCarrierDetection detectCarrier(const juce::AudioBuffer<float>& buffer,
                                                         double sampleRate,
                                                         float snrThresholdDb = 6.0f) const;

    /**
     * @brief Demodulates a Juno tape audio buffer back into raw bytes.
     * @param buffer Audio buffer with tape signal.
     * @param sampleRate Audio sampling rate in Hz.
     * @return Recovered payload bytes, or empty vector if sync or checksum fails.
     */
    [[nodiscard]] std::vector<uint8_t> decodeFromAudio(const juce::AudioBuffer<float>& buffer,
                                                       double sampleRate) const;

    /**
     * @brief Calculates Roland tape 7-bit two's complement checksum: (-sum) & 0x7F.
     * @param data Pointer to payload bytes.
     * @param size Number of bytes.
     * @return 7-bit checksum.
     */
    [[nodiscard]] static uint8_t calculateChecksum(const uint8_t* data, size_t size) noexcept;

    /**
     * @brief Evaluates tone magnitude/power at an exact frequency using Goertzel algorithm.
     */
    [[nodiscard]] static float computeGoertzelPower(const float* samples,
                                                    int numSamples,
                                                    double sampleRate,
                                                    float targetFreqHz) noexcept;

private:
    JunoTapeConfig config;
};

} // namespace abd::hw
