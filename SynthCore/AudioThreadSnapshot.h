#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace abd::synth {

struct AudioThreadSnapshot {
    static constexpr size_t kScopeBufferSize = 512;
    static constexpr size_t kMaxVoices = 32;

    float vuLeft{0.0f};
    float vuRight{0.0f};
    float peakLeft{0.0f};
    float peakRight{0.0f};

    uint32_t activeVoiceCount{0};
    std::array<bool, kMaxVoices> voiceActive{};
    std::array<uint8_t, kMaxVoices> voiceNote{};
    std::array<float, kMaxVoices> voiceVelocity{};
    std::array<float, kMaxVoices> voiceEnvelopeAmp{};
    std::array<float, kMaxVoices> voiceEnvelopeFilter{};

    // Oscilloscope circular buffer
    std::array<float, kScopeBufferSize> scopeBuffer{};
    uint32_t scopeWriteIndex{0};

    // Sequencer and Arp status
    int currentModSeqStep{0};
    int currentArpStep{0};
    bool isArpActive{false};
    bool isModSeqActive{false};
};

} // namespace abd::synth
