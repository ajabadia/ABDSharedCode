cmake_minimum_required(VERSION 3.22)
project(ABDSharedCode VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- AutoUpdater Module ---
# JUCE-dependent (juce_core / juce_events), so it cannot exist in the Emscripten
# WASM builds: consumers with no JUCE in scope set this OFF (ABDMS2000/wasm
# does). Sin este option la libreria se construia siempre y el paso WASM de
# build.bat fallaba con "juce_core/juce_core.h file not found".
option(ABDSHAREDCODE_BUILD_AUTOUPDATER "Build AutoUpdater module" ON)
if(ABDSHAREDCODE_BUILD_AUTOUPDATER)
    add_library(ABDShared_AutoUpdater STATIC
        AutoUpdater/AutoUpdater.cpp
    )

    add_library(ABDShared::AutoUpdater ALIAS ABDShared_AutoUpdater)

    target_include_directories(ABDShared_AutoUpdater PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(ABDShared_AutoUpdater PUBLIC
        juce_core
        juce_events
    )
endif()

# --- SynthCore Module ---
# Shared, JUCE-free DSP primitives extracted from the synths (Phase 2 of the
# transversal DRY plan): PolyBLEP, DSPUtils, EnvelopeCurves, ADSREnvelope,
# PortamentoGlide, LFO, AudioThreadSnapshot and the generic VoiceAllocator
# (converged from LutDSP). Namespace: abd::synth.
add_library(ABDShared_SynthCore STATIC
    SynthCore/PolyBLEP.cpp
    SynthCore/ADSREnvelope.cpp
    SynthCore/PortamentoGlide.cpp
    SynthCore/LFO.cpp
)

add_library(ABDShared::SynthCore ALIAS ABDShared_SynthCore)

target_include_directories(ABDShared_SynthCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(ABDShared_SynthCore PUBLIC cxx_std_20)

# Standalone unit tests for the module (no JUCE). Disable when consuming
# ABDSharedCode from a project that does not want the extra test target.
option(ABDSHAREDCODE_BUILD_SYNTHCORE_TESTS "Build SynthCore standalone tests" ON)
if(ABDSHAREDCODE_BUILD_SYNTHCORE_TESTS)
    add_executable(ABDShared_SynthCore_Tests
        SynthCore/SynthCoreTests.cpp
    )
    target_link_libraries(ABDShared_SynthCore_Tests PRIVATE ABDShared_SynthCore)
    target_compile_features(ABDShared_SynthCore_Tests PRIVATE cxx_std_20)
endif()

# --- LutDSP Module ---
# Header-only LUT evaluation and analogue modelling (namespace abd::lutdsp):
# LutEvaluatorSimd (SSE intrinsics), AnalogLutFilterModule, VoiceDispersionModel,
# VoiceAllocator and JunoBBD. No compiled sources: consumers only need the
# include path (the SIMD headers compile per-TU, so there is nothing to link).
# WASM note: LutEvaluatorSimd.h compiles under Emscripten with the recipe
# -msimd128 -D__SSE__ -D__SSE2__ -include immintrin.h (see ABDCZ101/wasm),
# which maps the SSE intrinsics onto WASM SIMD128.
add_library(ABDShared_LutDSP INTERFACE)

add_library(ABDShared::LutDSP ALIAS ABDShared_LutDSP)

target_include_directories(ABDShared_LutDSP INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# --- HardwareDrivers Module ---
# Protocol-level hardware drivers shared by the synths (namespace abd::hw): the
# SysEx and NRPN codecs consumed by every synth's MIDI layer.
#
# The Casio CZ / Juno tape / FSK controllers that live in the same folder are
# deliberately NOT part of this target: ABDAudioLab compiles them directly from
# source and they require nlohmann/json, which would otherwise become a hard
# dependency for every consumer of the target.
#
# NOTE: it pulls juce_audio_basics, so the Emscripten WASM builds compile
# SysExCodec.cpp directly instead of linking this target.
option(ABDSHAREDCODE_BUILD_HARDWAREDRIVERS "Build HardwareDrivers module" ON)
if(ABDSHAREDCODE_BUILD_HARDWAREDRIVERS)
    add_library(ABDShared_HardwareDrivers STATIC
        HardwareDrivers/SysExCodec.cpp
        HardwareDrivers/NRPNParser.cpp
    )

    add_library(ABDShared::HardwareDrivers ALIAS ABDShared_HardwareDrivers)

    # Exposing the module root lets consumers write `#include <HardwareDrivers/...>`
    # (used by ABDJUNiO601) as well as relative includes.
    target_include_directories(ABDShared_HardwareDrivers PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_features(ABDShared_HardwareDrivers PUBLIC cxx_std_20)

    target_link_libraries(ABDShared_HardwareDrivers PUBLIC
        juce_core
        juce_audio_basics
    )
endif()

# --- HardwareMidiDetect Module ---
# Contract-driven MIDI hardware detection.
# Two consumable layers:
#   1. HardwareMidiDetector  - pure C++, contract-driven scanning (no GUI).
#   2. JuceHardwareMidiPicker- WebView2 component (ABDScope-style) whose host
#      injects a MidiHardwareBackend; the embedded WebUI does the picking UX.
add_library(ABDShared_HardwareMidiDetect INTERFACE)

add_library(ABDShared::HardwareMidiDetect ALIAS ABDShared_HardwareMidiDetect)

target_include_directories(ABDShared_HardwareMidiDetect INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect
)

target_sources(ABDShared_HardwareMidiDetect INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareContract.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareContractRegistry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareContractRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareMidiDetector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareMidiDetector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/MidiHardwareBackend.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/JuceHardwareMidiPicker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareMidiPickerResourceProvider.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareMidiPickerResourceProvider.cpp
)

target_link_libraries(ABDShared_HardwareMidiDetect INTERFACE
    juce_audio_devices
    juce_gui_extra
    juce_core
)

# nlohmann_json is used by HardwareContractRegistry. Prefer a fetched/bundled
# target, else fall back to a header-only include if available on the system.
if(NOT TARGET nlohmann_json::nlohmann_json AND NOT TARGET nlohmann_json)
    find_package(nlohmann_json QUIET)
endif()
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(ABDShared_HardwareMidiDetect INTERFACE nlohmann_json::nlohmann_json)
elseif(TARGET nlohmann_json)
    target_link_libraries(ABDShared_HardwareMidiDetect INTERFACE nlohmann_json)
endif()

# Optional: embed the picker WebUI as binary assets so hosts do not need a
# filesystem WebUI install. When the caller has NOT configured a consumer's
# CMake, these assets are generated only if JUCE's juce_add_binary_data is
# available in scope (i.e. when ABDSharedCode is consumed as a subdirectory
# after JUCE is added, or via the helper below).
if(COMMAND juce_add_binary_data)
    # Only embed index.html and any JS files in WebUI root.
    # Styles, models, brands are served from filesystem (ABDSharedAssets).
    file(GLOB HWD_WEB_ASSETS
        "${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/WebUI/index.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/WebUI/*.js"
    )
    list(FILTER HWD_WEB_ASSETS EXCLUDE REGEX "/node_modules/")
    if(HWD_WEB_ASSETS)
        juce_add_binary_data(HardwareMidiPickerAssets
            HEADER_NAME "HardwareMidiPickerAssets.h"
            NAMESPACE "HardwareMidiPickerAssets"
            SOURCES ${HWD_WEB_ASSETS}
        )

        target_include_directories(HardwareMidiPickerAssets INTERFACE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_HardwareMidiPickerAssets/JuceLibraryCode>"
        )
        target_link_libraries(ABDShared_HardwareMidiDetect INTERFACE HardwareMidiPickerAssets)
        add_library(ABDShared::HardwareMidiPickerAssets ALIAS HardwareMidiPickerAssets)
    endif()

    # StudioTopology WebUI Binary Assets
    file(GLOB TOPOLOGY_WEB_ASSETS
        "${CMAKE_CURRENT_SOURCE_DIR}/StudioTopology/resources/index.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/StudioTopology/resources/*.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/StudioTopology/resources/*.css"
    )
    if(TOPOLOGY_WEB_ASSETS)
        juce_add_binary_data(StudioTopologyAssets
            HEADER_NAME "StudioTopologyAssets.h"
            NAMESPACE "StudioTopologyAssets"
            SOURCES ${TOPOLOGY_WEB_ASSETS}
        )

        target_include_directories(StudioTopologyAssets INTERFACE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_StudioTopologyAssets/JuceLibraryCode>"
        )
        add_library(ABDShared::StudioTopologyAssets ALIAS StudioTopologyAssets)
    endif()
endif()
