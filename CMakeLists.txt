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
# Headers, not sources, and each one earns its place by being something two
# synths would otherwise write twice:
#   S950PatchFields.h   the Akai S950 patch catalogue AS DATA: 38 keygroup
#                       fields + 18 Perform trims, with byte offset, panel
#                       range, encoding and human-readable name. Header-only
#                       and constexpr, so a panel and an engine can both link
#                       it and still say the same thing about the same byte
#                       (clean-room from the Mz950 study; nothing AGPL copied).
#   S950Disk.h          reads and writes the REAL bytes of an S950 programme —
#                       directory, allocation table, block chain — and resolves
#                       every field through the catalogue above. The stateful
#                       half of the pair, and the reason the catalogue exists:
#                       without it an importer hardcodes byte offsets.
#   S950Calibration.h   the measured-UNITS contract: which curves exist (envelope
#                       in s, LFO in Hz, warp in s, cutoff in Hz, filter-envelope
#                       octaves, sustain in dB), in what unit and over what
#                       panel range. DELIBERATELY WITH NO NUMBERS: a calibration
#                       table is experimental results, and read() returns
#                       std::nullopt — never 0 — for anything unmeasured, because
#                       a 0 envelope time is a click the engine will not suspect.
#   S950CalibrationHarness.h  turns a measurement session into that table:
#                       raw observations plus a known-answer reference model,
#                       correcting the bias of the analysis window and reporting
#                       when the session has not converged. It does not measure.
#   S950EnvelopeBench.h    the OTHER half of that sentence, the half the harness
#                       itself points at: it RENDERS an envelope whose times are
#                       known, runs it through the same moving-average analysis a
#                       probe would use, and returns observed - true as the bias
#                       the harness subtracts. That is the whole point: without a
#                       measured bias the harness subtracts a guess, and a guess
#                       that looks like a table. Two findings came out of writing
#                       it, both found by MEASURING and neither by reading the
#                       code: (1) the analytic truth of the exponential shape was
#                       right at threshold 0.5 and wrong by 58% at 0.25 and 0.75,
#                       because a fixed time constant never lifts the rise past
#                       the half-way point, so the render is now normalised; (2)
#                       with a window much wider than the envelope the analysis
#                       reports a 0.02 ms "duration" for a 5 ms pulse — a
#                       one-sample spike, not a measurement — so a reading is only
#                       trusted when the above-threshold run is at least as long
#                       as the window. The measured bias is +/-0.35% and it
#                       CHANGES SIGN with sweep speed, which is the proof that
#                       correcting per point is not over-engineering: a constant
#                       factor does not exist. Running this adds no point to
#                       S950Calibration.h, and a test checks that it does not.
#   (tests only) SynthCore/tests/S950CalibrationRenderTests.inc carries a
#                       minimal attack-decay renderer that ASKS the table how
#                       long an attack is, and checks the one thing that table
#                       being empty is FOR: that the degradation is silence and
#                       not a click. A click is not a weird sound, it is a fault
#                       the engine has no reason to suspect — with silence the
#                       first question is "is the instrument loaded?", which is
#                       the right question. A click is measured as the largest
#                       jump between two consecutive samples, and the same
#                       renderer on the same empty table gives 0.0 with a
#                       require-known policy and the full amplitude with a
#                       zero-fill one: the missing information is identical, only
#                       the decision taken on it differs. The test double lives in
#                       the .inc because it is not engine code; what is under test
#                       is the contract, not its sound.
#   --- the table above is also a PANEL contract, not just an engine one. It is
#   --- dumped to ABDSharedAssets/contracts/s950_calibration.json by
#   --- scripts/generate_s950_calibration_contract.py, so a panel draws the axes
#   --- and marks the unmeasured ones from the same declaration this file holds.
#   --- That export is what makes "no numbers here" checkable: the JSON carries
#   --- measured:false and points:[] on all six curves, and a --check run that
#   --- disagrees with this table fails. What the export cannot supply is the one
#   --- thing a log axis needs and nobody has: a measured minimum. Of the six
#   --- curves only lfoRate and filterEnvOctaves have a linear vertical axis;
#   --- the four logarithmic ones cannot draw it without a value that does not
#   --- exist yet. That count is the useful output, and it is a test.
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
#   DspMath.h                 deterministic transcendentals (the WASM<->native bit
#                             parity invariant of the substrate)
#   DspResonantFilter.h       ResonantFilterStage: a resonant lowpass SECTION
#                             whose damping can go negative, so it self-oscillates,
#                             and whose band-power AGC makes that oscillation
#                             settle at a level instead of growing (clean-room from
#                             the Mz950 study; nothing AGPL was copied)
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
# semantics, MIDI packing, buffer growth, seed determinism, and the resonant
# stage settling at a level instead of growing (which is asserted both with and
# without its level control, so the AGC is what the test is really about).
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
#
# And the ones whose sound is tied to a MACHINE. Those arrive through the
# injected-policy contract (EffectPolicy.h): the engine is a template over a
# data-only device PROFILE and a CHARACTER STAGE, so one engine serves every
# device and a device is a table of numbers instead of a class of its own.
#   EffectPolicy.h            - the contract: what a profile is, what a stage is
#   MultiHeadEcho.h           - multi-head tape echo engine (per-sample)
#   RingMod.h                 - ring modulator engine (per-sample)
#   DspSchroederReverb.h      - Schroeder-Moorer 4 comb + 3 all-pass (stereo FRAME)
#   JunoBBD.h                 - Juno bucket-brigade chorus engine (stereo FRAME)
#   characters/TapeColour.h   - asymmetric tape curve + hiss
#   characters/DiodeBridge.h  - diode-bridge soft clip
#   characters/BbdNoise.h     - 6-pole floor noise + drip + clock click for the BBD
#   profiles/Re201Profile.h   - the Space Echo RE-201 as pure data
#   profiles/ReverbProfile.h  - the ten DeepMind 12 reverbs as pure data
#   profiles/JunoBbdProfile.h - the J60/J106 as pure data
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

# Standalone unit tests for the module (no JUCE), same switch convention as
# DspCore and SynthCore. What they guard is mostly the POLICY contract rather
# than a tuning: a stage that grows a dependency on the engine, or an engine
# that stops reading its injected colour, are both silent failures that no
# amount of tuning regression would catch.
option(ABDSHAREDCODE_BUILD_DSPEFFECTS_TESTS "Build DspEffects standalone tests" ON)
if(ABDSHAREDCODE_BUILD_DSPEFFECTS_TESTS)
    add_executable(ABDShared_DspEffects_Tests
        DspEffects/DspEffectsTests.cpp
    )
    target_link_libraries(ABDShared_DspEffects_Tests PRIVATE ABDShared_DspEffects)
    target_compile_features(ABDShared_DspEffects_Tests PRIVATE cxx_std_17)
endif()

# --- LutDSP Module ---
# Header-only LUT evaluation and analogue modelling (namespace abd::lutdsp):
# LutEvaluatorSimd (SSE intrinsics), AnalogLutFilterModule, VoiceDispersionModel
# and VoiceAllocator. No compiled sources: consumers only need the
# include path (the SIMD headers compile per-TU, so there is nothing to link).
#
# NOTE: a JunoBBD.h used to live here. It was a PLACEHOLDER for the bucket
# brigade chorus, it was never adopted by anyone, and the engine that replaced it
# is DspEffects/JunoBBD.h (a real JUNiO601 port, proven sample-for-sample by
# ABDJUNiO601/Tests/ChorusBBDParityTest.cpp). The draft sits in
# _Deprecados/LutDSP-JunoBBD.h with a #error in it; it is not source code.
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

# --- LcdDisplay Module ---
# Universal character LCD (screen + menu state machine), C++ side. Pure
# component + data-driven menu tree with std::function hooks: the synth
# injects its tree and reacts; nothing here touches parameters or processors.
# INTERFACE (header-only) so consumers compile it alongside their JUCE.
# Gated on NOT EMSCRIPTEN (juce_gui_basics is absent from the WASM builds).
if(NOT EMSCRIPTEN)
    add_library(ABDShared_LcdDisplay INTERFACE)

    add_library(ABDShared::LcdDisplay ALIAS ABDShared_LcdDisplay)

    target_include_directories(ABDShared_LcdDisplay INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_sources(ABDShared_LcdDisplay INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/LcdDisplay/LcdDisplay.h
        ${CMAKE_CURRENT_SOURCE_DIR}/LcdDisplay/LcdMenuManager.h
    )

    target_link_libraries(ABDShared_LcdDisplay INTERFACE
        juce_gui_basics
    )
endif()

# --- Segmented Module ---
# Universal inline option switcher (radio group of flat buttons), C++ side.
# The native sibling of the shared Segmented component (ABDSharedAssets/
# components/segmented.js). INTERFACE (header-only) so consumers compile it
# alongside their JUCE. Gated on NOT EMSCRIPTEN (juce_gui_basics is absent
# from the WASM builds).
if(NOT EMSCRIPTEN)
    add_library(ABDShared_Segmented INTERFACE)

    add_library(ABDShared::Segmented ALIAS ABDShared_Segmented)

    target_include_directories(ABDShared_Segmented INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_sources(ABDShared_Segmented INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/Segmented/Segmented.h
    )

    target_link_libraries(ABDShared_Segmented INTERFACE
        juce_gui_basics
    )

    # Sonda opt-in de compilacion Y ejecucion (main propio, auto-verificable):
    # NO forma parte del paquete; es la manera de validar el header contra JUCE
    # real sin montar un plugin entero.
    add_executable(ABDShared_SegmentedProbe Segmented/SegmentedProbe.cpp)
    target_link_libraries(ABDShared_SegmentedProbe PRIVATE ABDShared_Segmented)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/MidiEndpointSafetyPolicy.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/MidiEndpointSafetyPolicy.cpp
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
