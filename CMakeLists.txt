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

# --- DspCore Module ---
# JUCE-free substrate of the synths' engines (namespace abd::dsp), moved here
# from ABDNeural/Source/DSP (Fase 1 [5/6] of its de-JUCE plan) so the other
# synths stop porting the same juce_core / juce_audio_basics pieces separately:
#   DspCore.h                 maths/Range/SmoothedValue/HeapBlock/AudioBuffer/
#                             FloatVectorOperations/ScopedNoDenormals/Random
#   DspMidiMessage.h          literal port of juce::MidiMessage
#   DspMidiBuffer.h           literal port of juce::MidiBuffer
#   DspDebug.h                dspDbg (port of DBG)
#   DspLeakedObjectDetector.h dspLeakDetector / dspDeclareNonCopyableWithLeakDetector
# Header-only (everything is inline/template), so consumers only need the
# include path; there is nothing to link. C++17 on purpose: the consumer that
# motivated the move (ABDNeural) builds at 17, not 20.
#
# The bit-exact comparisons against the JUCE originals (juce::MidiMessage,
# juce::AudioBuffer, juce::Reverb) cannot live here: this module is JUCE-free by
# contract. They stay in the consumer (ABDNeural/Tests).
add_library(ABDShared_DspCore INTERFACE)

add_library(ABDShared::DspCore ALIAS ABDShared_DspCore)

target_include_directories(ABDShared_DspCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(ABDShared_DspCore INTERFACE cxx_std_17)

# Standalone unit tests for the module (no JUCE), same switch convention as
# SynthCore. Guard the invariants that break silently: HeapBlock element
# semantics, MIDI packing, buffer growth and seed determinism.
option(ABDSHAREDCODE_BUILD_DSPCORE_TESTS "Build DspCore standalone tests" ON)
if(ABDSHAREDCODE_BUILD_DSPCORE_TESTS)
    add_executable(ABDShared_DspCore_Tests
        DspCore/DspCoreTests.cpp
    )
    target_link_libraries(ABDShared_DspCore_Tests PRIVATE ABDShared_DspCore)
    target_compile_features(ABDShared_DspCore_Tests PRIVATE cxx_std_17)
endif()

# --- DspEffects Module ---
# Effects built on the DspCore substrate (namespace abd::dsp). JUCE-free: the
# ones that existed in JUCE are literal ports, and they keep the algorithm and
# tunings of the original.
#   DspReverb.h     - Freeverb, literal port of juce::Reverb
#   DspChorus.h     - modulated delay-line chorus engine (per-sample API)
#   DspDelay.h      - feedback delay engine (per-sample API)
#   DspSaturation.h - atan soft clipping (stateless shape)
# The engines take their parameters per sample on purpose: parameter smoothing
# and the mapping from the product's controls stay in the consumer, so the move
# does not change a single sample of the product (ABDNeural keeps its wrappers
# in Source/DSP/Effects/ and proves it with Tests/DspEffectsParityTest.cpp).
# Header-only: consumers only need the include path.
add_library(ABDShared_DspEffects INTERFACE)

add_library(ABDShared::DspEffects ALIAS ABDShared_DspEffects)

target_include_directories(ABDShared_DspEffects INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(ABDShared_DspEffects INTERFACE cxx_std_17)

target_link_libraries(ABDShared_DspEffects INTERFACE ABDShared::DspCore)

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

# --- WebView2Bridge Module ---
# Shared WebView2 scaffolding for every module that embeds a WebUI in a JUCE
# component: the resource provider pipeline (normalize -> embedded binary catalog
# -> optional ABDSharedAssets filesystem fallback) plus the JuceWebView2Component
# base (webview2 backend, native integration, theme + pageLoaded hook).
#
# INTERFACE, like the other shared modules: the sources propagate and are
# compiled into the consumer alongside its own JUCE, so consumers only need to
# fill a BinaryAssetsCatalog with their generated header and link this target.
# ABDScope already probes for it (`if(TARGET ABDShared::WebView2Bridge)`); until
# now nothing defined it, so that probe silently fell back to a bare include dir
# and nobody compiled WebView2ResourceProvider.cpp.
#
# Gated on NOT EMSCRIPTEN (same reason as ABDSHAREDCODE_BUILD_AUTOUPDATER): the
# bridge is juce_gui_extra, which does not exist in the WASM builds, and a
# propagated .cpp would break them at compile time.
if(NOT EMSCRIPTEN)
    add_library(ABDShared_WebView2Bridge INTERFACE)

    add_library(ABDShared::WebView2Bridge ALIAS ABDShared_WebView2Bridge)

    target_include_directories(ABDShared_WebView2Bridge INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_sources(ABDShared_WebView2Bridge INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/WebView2Bridge/JuceWebView2Component.h
        ${CMAKE_CURRENT_SOURCE_DIR}/WebView2Bridge/WebView2ResourceProvider.h
        ${CMAKE_CURRENT_SOURCE_DIR}/WebView2Bridge/WebView2ResourceProvider.cpp
    )

    target_link_libraries(ABDShared_WebView2Bridge INTERFACE
        juce_core
        juce_gui_extra
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
