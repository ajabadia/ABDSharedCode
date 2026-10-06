#include <gtest/gtest.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include "../Core/TriggerDetector.h"

using namespace abd::scope;

namespace {
constexpr double kPi = 3.14159265358979323846;
}

TEST(TriggerDetectorTest, SineWave440Hz) {
  const float sampleRate = 44100.0f;
  const float freq = 440.0f;
  const size_t numSamples = 4096;

  std::vector<float> buffer(numSamples);
  for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = std::sin(2.0 * kPi * freq * i / sampleRate);
  }

  TriggerResult result = TriggerDetector::process(buffer.data(), numSamples, sampleRate);

  EXPECT_GT(result.triggerIndex, 0);
  EXPECT_NEAR(result.estimatedFrequencyHz, freq, 2.0f);
  EXPECT_EQ(result.noteName, "A4");
}

TEST(TriggerDetectorTest, SineWave261Hz) {
  const float sampleRate = 44100.0f;
  const float freq = 261.63f;
  const size_t numSamples = 4096;

  std::vector<float> buffer(numSamples);
  for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = std::sin(2.0 * kPi * freq * i / sampleRate);
  }

  TriggerResult result = TriggerDetector::process(buffer.data(), numSamples, sampleRate);

  EXPECT_GT(result.triggerIndex, 0);
  EXPECT_NEAR(result.estimatedFrequencyHz, freq, 2.0f);
  EXPECT_EQ(result.noteName, "C4");
}

TEST(TriggerDetectorTest, SubBass55Hz) {
  const float sampleRate = 44100.0f;
  const float freq = 55.0f; // A1
  const size_t numSamples = 4096;

  std::vector<float> buffer(numSamples);
  for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = std::sin(2.0 * kPi * freq * i / sampleRate);
  }

  TriggerResult result = TriggerDetector::process(buffer.data(), numSamples, sampleRate);

  EXPECT_GT(result.triggerIndex, 0);
  EXPECT_NEAR(result.estimatedFrequencyHz, freq, 2.0f);
  EXPECT_EQ(result.noteName, "A1");
}

TEST(TriggerDetectorTest, SilenceReturnsZero) {
  float buffer[1024] = { 0.0f };
  TriggerResult result = TriggerDetector::process(buffer, 1024, 44100.0f);

  EXPECT_EQ(result.triggerIndex, 0);
  EXPECT_EQ(result.estimatedFrequencyHz, 0.0f);
  EXPECT_TRUE(result.noteName.empty());
}

TEST(TriggerDetectorTest, DCOffsetReturnsZero) {
  float buffer[1024];
  std::fill_n(buffer, 1024, 0.5f);
  TriggerResult result = TriggerDetector::process(buffer, 1024, 44100.0f);

  EXPECT_EQ(result.triggerIndex, 0);
  EXPECT_EQ(result.estimatedFrequencyHz, 0.0f);
}

TEST(TriggerDetectorTest, NullBufferReturnsZero) {
  TriggerResult result = TriggerDetector::process(nullptr, 1024, 44100.0f);
  EXPECT_EQ(result.triggerIndex, 0);
  EXPECT_EQ(result.estimatedFrequencyHz, 0.0f);
}

TEST(TriggerDetectorTest, SmallBufferReturnsZero) {
  float buffer[8] = { 0 };
  TriggerResult result = TriggerDetector::process(buffer, 8, 44100.0f);
  EXPECT_EQ(result.triggerIndex, 0);
  EXPECT_EQ(result.estimatedFrequencyHz, 0.0f);
}

TEST(TriggerDetectorTest, FrequencyToNoteName) {
  EXPECT_EQ(TriggerDetector::frequencyToNoteName(440.0f), "A4");
  EXPECT_EQ(TriggerDetector::frequencyToNoteName(261.63f), "C4");
  EXPECT_EQ(TriggerDetector::frequencyToNoteName(82.41f), "E2");
  EXPECT_EQ(TriggerDetector::frequencyToNoteName(110.0f), "A2");
  EXPECT_EQ(TriggerDetector::frequencyToNoteName(880.0f), "A5");
  EXPECT_EQ(TriggerDetector::frequencyToNoteName(55.0f), "A1");
  EXPECT_EQ(TriggerDetector::frequencyToNoteName(1000.0f), "B5");
}

TEST(TriggerDetectorTest, FrequencyToNoteNameOutOfRange) {
  EXPECT_TRUE(TriggerDetector::frequencyToNoteName(0.0f).empty());
  EXPECT_TRUE(TriggerDetector::frequencyToNoteName(-100.0f).empty());
  EXPECT_TRUE(TriggerDetector::frequencyToNoteName(50000.0f).empty());
}
