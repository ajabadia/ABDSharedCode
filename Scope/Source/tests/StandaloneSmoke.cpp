#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include "../Core/SpscRingBuffer.h"
#include "../Core/ScopeTap.h"
#include "../Core/ScopeDataCollector.h"
#include "../Core/TriggerDetector.h"
#include "../Core/ScopeFrameSerializer.h"

namespace {

int g_failures = 0;

// Explicit runtime check that survives NDEBUG (Release) builds. Unlike assert(),
// these checks always run so build.bat's Release smoke run actually verifies.
void expect(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::cerr << "  [FAILED] " << expression << " (line " << line << ")" << std::endl;
        ++g_failures;
    }
}

} // namespace

#define CHECK(expr) expect((expr), #expr, __LINE__)

int main()
{
    std::cout << "============================================" << std::endl;
    std::cout << "  ABDScope Native C++ Standalone Sanity Test" << std::endl;
    std::cout << "============================================" << std::endl;

    // 1. Test SPSC Ring Buffer
    {
        std::cout << "[TEST] Testing SpscRingBuffer..." << std::endl;
        abd::scope::SpscRingBuffer<float> ring(128);
        std::vector<float> input = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
        CHECK(ring.write(input.data(), input.size()) == 5);
        CHECK(ring.getAvailableRead() == 5);

        std::vector<float> output(5, 0.0f);
        CHECK(ring.read(output.data(), 5) == 5);
        CHECK(output[0] == 1.0f && output[4] == 5.0f);
        CHECK(ring.getAvailableRead() == 0);
    }

    // 2. Test ScopeTap and ScopeDataCollector (multi-tap activation + id/slug selection)
    {
        std::cout << "[TEST] Testing ScopeDataCollector & Multi-Tap..." << std::endl;
        abd::scope::ScopeDataCollector collector;
        auto* tap1 = collector.registerTap("Master Out", abd::scope::ScopeTapType::StereoAudio, 4096, "master");
        auto* tap2 = collector.registerTap("Osc 1", abd::scope::ScopeTapType::StereoAudio, 4096, "osc1");
        auto* tap3 = collector.registerTap("Diagnostic 1kHz", abd::scope::ScopeTapType::StereoAudio, 4096, "diag_tone");
        CHECK(tap1 != nullptr && tap2 != nullptr && tap3 != nullptr);
        CHECK(collector.getTapCount() == 3);

        // First registered tap is auto-selected
        CHECK(tap1->isActive());
        CHECK(!tap2->isActive());

        // Selection by explicit slug id
        CHECK(collector.selectTap("osc1"));
        CHECK(!tap1->isActive() && tap2->isActive() && !tap3->isActive());

        // Selection by human-readable display name
        CHECK(collector.selectTap("Master Out"));
        CHECK(tap1->isActive() && !tap2->isActive() && !tap3->isActive());

        std::vector<float> lSamples(64, 0.5f);
        std::vector<float> rSamples(64, -0.5f);
        tap1->writeStereo(lSamples.data(), rSamples.data(), lSamples.size());
        CHECK(tap1->getAvailableRead() == 64);
    }

    // 3. Test TriggerDetector Sub-sample Precision & Pitch (octave-qualified note names)
    {
        std::cout << "[TEST] Testing TriggerDetector Sub-sample Lock & Pitch..." << std::endl;
        const float sampleRate = 44100.0f;
        const float targetFreq = 440.0f;
        const size_t numSamples = 4096;
        std::vector<float> sine(numSamples, 0.0f);

        for (size_t i = 0; i < numSamples; ++i)
        {
            sine[i] = std::sin(2.0f * 3.1415926535f * targetFreq * static_cast<float>(i) / sampleRate);
        }

        auto result = abd::scope::TriggerDetector::process(sine.data(), numSamples, sampleRate);
        CHECK(result.triggerIndex > 0);
        CHECK(result.triggerFraction >= 0.0f && result.triggerFraction < 1.0f);
        CHECK(std::abs(result.estimatedFrequencyHz - 440.0f) < 2.0f);
        CHECK(result.noteName == "A4");
        std::cout << "  Note: " << result.noteName << ", Freq: "
                  << result.estimatedFrequencyHz << " Hz" << std::endl;

        // Sub-bass sanity: 55 Hz (A1) requires a full 4096-sample window
        std::vector<float> sub(numSamples, 0.0f);
        for (size_t i = 0; i < numSamples; ++i)
        {
            sub[i] = std::sin(2.0f * 3.1415926535f * 55.0f * static_cast<float>(i) / sampleRate);
        }
        auto subResult = abd::scope::TriggerDetector::process(sub.data(), numSamples, sampleRate);
        CHECK(std::abs(subResult.estimatedFrequencyHz - 55.0f) < 2.0f);
        CHECK(subResult.noteName == "A1");
    }

    // 4. Test ScopeFrameSerializer JSON Wire-Protocol (real emitted keys)
    {
        std::cout << "[TEST] Testing ScopeFrameSerializer JSON Wire-Protocol..." << std::endl;
        abd::scope::ScopeDataCollector collector;
        auto* tap = collector.registerTap("Master Out", abd::scope::ScopeTapType::StereoAudio, 4096, "master");
        CHECK(collector.selectTap("master"));

        std::vector<float> l(256, 0.25f);
        std::vector<float> r(256, -0.25f);
        tap->writeStereo(l.data(), r.data(), 256);

        abd::scope::ScopeFrameSerializer serializer(256);
        std::string json = serializer.serializeActiveFrame(collector.getActiveTap(), 44100.0f);
        CHECK(!json.empty());

        // Canonical wire-protocol keys emitted by serializeActiveFrame()
        CHECK(json.find("\"signalType\":\"audio\"") != std::string::npos);
        CHECK(json.find("\"tapId\":\"master\"") != std::string::npos);
        CHECK(json.find("\"numSamples\":256") != std::string::npos);
        CHECK(json.find("\"timeDataL\":[") != std::string::npos);
        CHECK(json.find("\"timeDataR\":[") != std::string::npos);
        CHECK(json.find("\"rmsL\":") != std::string::npos);
        CHECK(json.find("\"peakL\":") != std::string::npos);
        // Legacy keys must not appear
        CHECK(json.find("\"time\":") == std::string::npos);
        CHECK(json.find("\"samplesL\":") == std::string::npos);
    }

    std::cout << "============================================" << std::endl;
    if (g_failures == 0)
    {
        std::cout << "  [SUCCESS] ALL C++ SMOKE TESTS PASSED!" << std::endl;
        std::cout << "============================================" << std::endl;
        return 0;
    }
    std::cout << "  [FAILURE] " << g_failures << " check(s) FAILED" << std::endl;
    std::cout << "============================================" << std::endl;
    return 1;
}
