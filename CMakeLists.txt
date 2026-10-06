cmake_minimum_required(VERSION 3.22)
project(ABDScope VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /wd4324 /permissive- /Zc:__cplusplus /Zc:inline)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_subdirectory(Source)

# Target Alias for clean consumer integration: ABDScope::ABDScopeCore
add_library(ABDScope::ABDScopeCore ALIAS ABDScopeCore)

# Optional Native JUCE GUI Standalone Demo.
# The JUCE probe runs ONLY when ABDScope is the top-level project (standalone
# dev sandbox). When consumed via add_subdirectory (ABDSharedCode/Scope, and
# from there any product), the consumer brings its own JUCE — or none, as in
# the Emscripten/WASM builds — and probing C:/JUCE from inside a consumer
# build would drag an unrelated JUCE tree into it.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  if(NOT TARGET juce::juce_core)
    if(EXISTS "C:/JUCE/CMakeLists.txt")
      add_subdirectory("C:/JUCE" _juce_build EXCLUDE_FROM_ALL)
    elseif(EXISTS "D:/desarrollos/juce/CMakeLists.txt")
      add_subdirectory("D:/desarrollos/juce" _juce_build EXCLUDE_FROM_ALL)
    endif()
  endif()
endif()

# Embedded WebUI Binary Assets for WebView2
if(COMMAND juce_add_binary_data)
  file(GLOB_RECURSE ABDSCOPE_WEB_ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/WebUI/src/*"
  )
  list(APPEND ABDSCOPE_WEB_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/WebUI/index.html")
  list(FILTER ABDSCOPE_WEB_ASSETS EXCLUDE REGEX "/tests/")
  list(FILTER ABDSCOPE_WEB_ASSETS EXCLUDE REGEX "/node_modules/")

  juce_add_binary_data(ABDScopeWebAssets
    HEADER_NAME "ABDScopeWebAssets.h"
    NAMESPACE "ABDScopeWebAssets"
    SOURCES ${ABDSCOPE_WEB_ASSETS}
  )
  target_link_libraries(ABDScopeCore INTERFACE ABDScopeWebAssets)
  add_library(ABDScope::ABDScopeWebAssets ALIAS ABDScopeWebAssets)
endif()

# Standalone C++ Smoke Test (MSVC Validation). Links the JUCE-free core headers
# target only: this test validates the pure DSP/serializer contract in any toolchain
# and must not pull in the JUCE glue (which needs JUCE + WebView2 to compile).
add_executable(ABDScope_CppSmoke Source/tests/StandaloneSmoke.cpp)
target_link_libraries(ABDScope_CppSmoke PRIVATE ABDScopeCoreHeaders)

# The demo app is a standalone-sandbox artifact too: gating it with the same
# top-level check keeps consumer builds (ABDSharedCode products) free of it.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND TARGET juce::juce_gui_extra)
  juce_add_gui_app(ABDScope_GuiDemo
    PRODUCT_NAME "ABDScope Native GUI Demo"
    VERSION 0.4.0
  )

  target_sources(ABDScope_GuiDemo PRIVATE
    Source/StandaloneDemo/Main.cpp
  )

  target_link_libraries(ABDScope_GuiDemo PRIVATE
    ABDScopeCore
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_dsp
  )
endif()