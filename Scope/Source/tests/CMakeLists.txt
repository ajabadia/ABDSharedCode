cmake_minimum_required(VERSION 3.20)

project(ABDScopeTests LANGUAGES CXX)

find_package(GTest REQUIRED)

add_executable(ABDScopeUnitTests
  TestSpscRingBuffer.cpp
  TestScopeTap.cpp
  TestTriggerDetector.cpp
)

target_link_libraries(ABDScopeUnitTests PRIVATE
  GTest::gtest_main
  ABDScopeCore
)

add_test(NAME ABDScopeUnitTests COMMAND ABDScopeUnitTests)