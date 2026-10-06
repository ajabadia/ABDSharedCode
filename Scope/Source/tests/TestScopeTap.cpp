#include <gtest/gtest.h>
#include "../Core/ScopeTap.h"
#include "../Core/ScopeTapType.h"

using namespace abd::scope;

TEST(ScopeTapTest, InactiveWriteNoOp) {
  ScopeTap tap("Test", ScopeTapType::MonoAudio, 512);
  EXPECT_FALSE(tap.isActive());

  float data[] = { 1.0f, 2.0f, 3.0f };
  tap.write(data, 3);

  EXPECT_EQ(tap.getAvailableRead(), 0);
}

TEST(ScopeTapTest, ActiveWriteRead) {
  ScopeTap tap("Test", ScopeTapType::MonoAudio, 512);
  tap.setActive(true);

  float data[] = { 0.1f, 0.2f, 0.3f, 0.4f };
  tap.write(data, 4);

  EXPECT_EQ(tap.getAvailableRead(), 4);

  float out[4] = { 0 };
  EXPECT_EQ(tap.read(out, 4), 4);

  for (int i = 0; i < 4; ++i) {
    EXPECT_FLOAT_EQ(out[i], data[i]);
  }
}

TEST(ScopeTapTest, StereoWriteRead) {
  ScopeTap tap("Stereo", ScopeTapType::StereoAudio, 512);
  tap.setActive(true);

  float left[] = { 1.0f, 2.0f, 3.0f };
  float right[] = { 4.0f, 5.0f, 6.0f };

  tap.writeStereo(left, right, 3);

  float outL[3] = { 0 };
  float outR[3] = { 0 };
  tap.read(outL, outR, 3);

  for (int i = 0; i < 3; ++i) {
    EXPECT_FLOAT_EQ(outL[i], left[i]);
    EXPECT_FLOAT_EQ(outR[i], right[i]);
  }
}

TEST(ScopeTapTest, ControlSignalType) {
  ScopeTap tap("CV", ScopeTapType::ControlSignal, 512);
  tap.setActive(true);

  EXPECT_EQ(tap.getType(), ScopeTapType::ControlSignal);

  float cv[] = { 0.0f, 0.5f, 1.0f };
  tap.write(cv, 3);
  EXPECT_EQ(tap.getAvailableRead(), 3);
}

TEST(ScopeTapTest, DeactivateClearsBuffers) {
  ScopeTap tap("Test", ScopeTapType::MonoAudio, 512);
  tap.setActive(true);

  float data[] = { 1.0f, 2.0f };
  tap.write(data, 2);
  EXPECT_EQ(tap.getAvailableRead(), 2);

  tap.setActive(false);
  EXPECT_FALSE(tap.isActive());
  EXPECT_EQ(tap.getAvailableRead(), 0);
}