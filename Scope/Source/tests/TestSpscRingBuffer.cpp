#include <gtest/gtest.h>
#include "../Core/SpscRingBuffer.h"

using namespace abd::scope;

TEST(SpscRingBufferTest, WriteReadBasic) {
  SpscRingBuffer<float> buffer(1024);
  float data[] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
  float out[5] = { 0 };

  EXPECT_EQ(buffer.write(data, 5), 5);
  EXPECT_EQ(buffer.getAvailableRead(), 5);
  EXPECT_EQ(buffer.read(out, 5), 5);

  for (int i = 0; i < 5; ++i) {
    EXPECT_FLOAT_EQ(out[i], data[i]);
  }
}

TEST(SpscRingBufferTest, WriteBeyondCapacity) {
  SpscRingBuffer<float> buffer(8);
  float data[16] = { 1.0f };
  float out[16] = { 0 };

  EXPECT_EQ(buffer.write(data, 16), 8);
  EXPECT_EQ(buffer.getAvailableRead(), 8);
  EXPECT_EQ(buffer.read(out, 16), 8);
}

TEST(SpscRingBufferTest, WrapAround) {
  SpscRingBuffer<float> buffer(8);
  float data[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
  float out[10] = { 0 };

  EXPECT_EQ(buffer.write(data, 8), 8);
  EXPECT_EQ(buffer.read(out, 4), 4);
  EXPECT_EQ(buffer.write(data + 8, 2), 2);
  EXPECT_EQ(buffer.getAvailableRead(), 6);
  EXPECT_EQ(buffer.read(out, 10), 6);
}

TEST(SpscRingBufferTest, EmptyRead) {
  SpscRingBuffer<float> buffer(1024);
  float out[10] = { 0 };
  EXPECT_EQ(buffer.read(out, 10), 0);
}

TEST(SpscRingBufferTest, Reset) {
  SpscRingBuffer<float> buffer(1024);
  float data[] = { 1, 2, 3 };
  float out[3] = { 0 };

  buffer.write(data, 3);
  buffer.reset();
  EXPECT_EQ(buffer.getAvailableRead(), 0);
  EXPECT_EQ(buffer.read(out, 3), 0);
}