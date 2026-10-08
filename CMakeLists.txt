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
#   --- la familia de osciladores, la hermana de la de filtros: mismo contrato por
#   --- convencion (no hay clase base ni vtable en el lazo) y mismo idioma de
#   --- parametros para todos sus miembros. Vive AQUI y no en DspCore porque los
#   --- primitivos de oscilador de este repo (PolyBLEP, DSPUtils) ya viven aqui, y
#   --- porque un oscilador no necesita el sustrato entero que un filtro si usa.
#   OscillatorFamily.h  LA FAMILIA DE OSCILADORES: el contrato que todo oscilador
#                       honra (prepare/reset/setFrequency en Hz/setWaveform/
#                       setOversampling/setPulseWidth/processSample y sus getters),
#                       el rasgo IsOscillator que lo comprueba al compilar, el
#                       idioma (frecuencia en Hz, formas BASICAS, ancho 0..1 con el
#                       mismo significado, salida bipolar +-1) y el processBlock
#                       (float y double) de la familia.
#   OscVcoCa72Profile.h EL VCO DE RANPA COMO DATOS: el nucleo (condensador de
#                       temporizacion, umbral del disparador de Schmitt, retardo del
#                       comparador, pendiente del efecto Early) y el conformador de
#                       ondas (separador, diente de sierra, comparador del rectangulo
#                       con su histeresis), a 25 C y sin carga. El hardware entra
#                       como perfil inyectado: el motor no lo sabe.
#   OscVcoCa72.h        EL miembro de CIRCUITO de la familia: la rampa integrada con
#   OscVcoCa72.cpp      su reinicio, sus tres conformadores y el limitado de banda
#                       (escalones polyBLEP en los sucesos y decimacion al bajar).
#                       Escrito de nuevo desde la idea del VCO del aparato de estudio
#                       (su referencia esta bajo GPL y no se copio codigo suyo); los
#                       numeros son los medidos del banco de pruebas. No tiene seno, y
#                       lo dice: supportsWaveform(Waveform::Sine) es false y
#                       setWaveform lo rechaza sin cambiar nada.
#   OscPolyBlep.h       El miembro de FASE: acumulador de fase con las cuatro formas y
#                       las discontinuidades corregidas por el PolyBLEP del modulo.
#                       Su tope de frecuencia y su ancho de pulso los declara.
#   OscHalfbandDecimator.h  La decimacion de media banda (etapas cortas delante y la
#                       larga al final, con los taps a distancia par del centro
#                       saltados) que usa el VCO al sobremuestrear. Ajeno a la familia
#                       a proposito: no sabe de formas ni de frecuencias, solo de
#                       muestras.
#
#   --- el miembro de REFERENCIA de la familia: el esqueleto del contrato que todo
#   --- nuevo miembro tiene que poder compilar, declarar y probar. No suena mejor
#   --- que los otros dos; su trabajo es ser el ejemplo que el compilador verifica
#   --- (rasgo IsOscillator) y que luego otro miembro pueda leer antes de escribir
#   --- su propio nucleo.
#   OscReference.h    El miembro de REFERENCIA minimal: acumulador de fase con dos
#                       formas (diente de sierra y seno) y lo que le falta dicho en
#                       voz alta (triangulo y rectangulo, ningun sobremuestreo). Es
#                       el prototipo estandarizado del que hablan README e
#                       INTEGRATION_GUIDE.
add_library(ABDShared_SynthCore STATIC
    SynthCore/PolyBLEP.cpp
    SynthCore/ADSREnvelope.cpp
    SynthCore/PortamentoGlide.cpp
    SynthCore/LFO.cpp
    SynthCore/OscVcoCa72.cpp
    SynthCore/Arpeggiator.cpp
    SynthCore/ControlSequencer.cpp
    SynthCore/LfoAnalog.cpp
    SynthCore/EnvelopeAnalog.cpp
    SynthCore/DriftEngine.cpp
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
#   DspFilterFamily.h         LA FAMILIA DE FILTROS: el contrato por convencion que
#                             todo filtro del sustrato honra (prepare/reset/setCutoff
#                             en Hz/setResonance 0..1/setMode/processSample), el
#                             rasgo IsFilterStage que lo comprueba al compilar, la
#                             curva de corte y la de Q que TODOS comparten (para que
#                             cambiar de filtro no cambie la afinacion del panel) y
#                             el processBlock (float y double) de la familia.
#   DspFilterTpt.h            FilterTpt: el miembro TPT. Los tres taps (paso bajo,
#                             paso alto, banda) del mismo par de estados.
#   DspFilterEquation.h       FilterEquation: el miembro de ecuacion. Escalera de
#                             cuatro polos con el lazo resuelto SIN retardo (ZDF) y
#                             tanh en la realimentacion, que es lo que la asienta.
#                             Los tres van AQUI y no fuera porque su contrato es del
#                             sustrato; el miembro LUT (tabla medida) vive en LutDSP,
#                             que depende de este modulo y no al reves.
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
#   LutFilter.h              el miembro LUT de la familia de filtros: el corte y la
#                             Q salen de una tabla MEDIDA (AbdBatchedPoint) y el
#                             nucleo que los toca es el TPT del sustrato. Depende de
#                             DspCore por eso mismo: el contrato de la familia y el
#                             nucleo son suyos, y el miembro LUT no puede vivir sin
#                             ellos. La dependencia va en un sentido solo (LutDSP ->
#                             DspCore), como la de DspEffects.
add_library(ABDShared_LutDSP INTERFACE)

add_library(ABDShared::LutDSP ALIAS ABDShared_LutDSP)

target_include_directories(ABDShared_LutDSP INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# El modulo NO declara un estandar propio: no lo declaraba antes y no es esta tarea
# la que lo decide (fijarlo aqui seria prometer que TODAS sus cabeceras compilan a
# ese estandar, y solo se ha comprobado con las que prueba su test).
target_link_libraries(ABDShared_LutDSP INTERFACE ABDShared::DspCore)

# Standalone unit tests for the module (no JUCE), same switch convention as
# SynthCore / DspCore / DspEffects. Lo que guardan: que el miembro LUT sea de la
# familia (el rasgo lo comprueba al compilar), que lea la tabla medida con sus
# numeros y su interpolacion —no una curva aproximada— y que sin tabla no sea una
# averia sino el analitico de la familia, dicho por hasTable().
option(ABDSHAREDCODE_BUILD_LUTDSP_TESTS "Build LutDSP standalone tests" ON)
if(ABDSHAREDCODE_BUILD_LUTDSP_TESTS)
    add_executable(ABDShared_LutDSP_Tests
        LutDSP/LutDSPTests.cpp
    )
    target_link_libraries(ABDShared_LutDSP_Tests PRIVATE ABDShared_LutDSP)
    # El test se compila a 17 a proposito: es el estandar del sustrato (DspCore) y
    # el que un consumidor viejo puede estar usando, asi que la familia tiene que
    # valer ahi. El project() raiz va a 20 y no estorba: las features solo suben.
    target_compile_features(ABDShared_LutDSP_Tests PRIVATE cxx_std_17)
endif()

# --- nlohmann_json: una sola resolucion para todo el repo -------------------------
# Dos cabeceras de este repo incluyen <nlohmann/json.hpp> y las dos son Superficie
# publica de un target:
#   HardwareDrivers/CasioCzVirtualController.h    -> ABDShared::HardwareDrivers
#   HardwareMidiDetect/HardwareContractRegistry.h -> ABDShared::HardwareMidiDetect
#
# Antes solo resolvia el segundo. El primero se quedaba sin la dependencia, y el
# sintoma NO es que este repo no compile: es un C1083 (Cannot open include file:
# nlohmann/json.hpp) en el CONSUMIDOR, en cuanto incluye esa cabecera. Se configura
# limpio aqui y revienta alli, que es el sitio mas caro de enterarse. Por eso el
# enlace va en el target que EXPONE la cabecera, no en el que la compila.
#
# Se resuelve una vez y se reutiliza en los dos sites, en vez de repetir el bloque
# if/find_package/if. El orden importa: esta resolucion tiene que correr ANTES
# que los targets que la enlazan, que es por eso que vive aqui arriba y no junto
# a HardwareMidiDetect, que es donde estaba.
#
# nlohmann_json es header-only: nlohmann_json::nlohmann_json solo aporta un include
# dir. Enlazarlo en PUBLIC no le cuesta nada a quien no incluya esas cabeceras,
# mientras que dejarlo fuera hace que.include falle segun que consumidor toque.
if(NOT TARGET nlohmann_json::nlohmann_json AND NOT TARGET nlohmann_json)
    find_package(nlohmann_json QUIET)
endif()
if(TARGET nlohmann_json::nlohmann_json)
    set(ABDSHAREDCODE_NLOHMANN_TARGET nlohmann_json::nlohmann_json)
elseif(TARGET nlohmann_json)
    set(ABDSHAREDCODE_NLOHMANN_TARGET nlohmann_json)
else()
    set(ABDSHAREDCODE_NLOHMANN_TARGET "")
endif()

# --- HardwareDrivers Module ---
# Protocol-level hardware drivers shared by the synths (namespace abd::hw): the
# SysEx and NRPN codecs consumed by every synth's MIDI layer.
#
# CasioCzVirtualController.cpp queda FUERA de este target, a proposito. La
# CABECERA si entra (ver target_sources mas abajo) porque es Superficie publica
# del modulo, pero el .cpp no: compilarlo aqui convertiria nlohmann/json en una
# dependencia dura de COMPILACION para todo el que enlace este target, que es el
# 90% de la suite y no lo necesita para nada. Lo compila ABDAudioLab desde
# fuente, que si hace FetchContent de nlohmann_json.
#
# El .cpp fuera NO significa que la cabecera se pueda incluir sola: por eso el
# target enlaza nlohmann_json en PUBLIC mas abajo. Antes no lo hacia, y el
# fallo era un C1083 en el consumidor. La razon de dejar fuera el .cpp era
# "nlohmann no esta aqui", que es exactamente la misma premisa que hacia fallar
# al que si incluia la cabecera.
#
# Los otros tres SI entran, y antes no estaban por confusion con el de Casio:
# JunoTapeModem, FskAudioModem y CasioNibbleCodec no dependen de nlohmann/json y
# compilan con lo que el target ya enlazaba. FskAudioModem lo documentaba el
# INTEGRATION_GUIDE.md como componente del modulo y no se podia enlazar: el
# sintoma era un undefined reference en el consumidor, no un error aqui.
#
# NOTE: it pulls juce_audio_basics, so the Emscripten WASM builds compile
# SysExCodec.cpp directly instead of linking this target.
option(ABDSHAREDCODE_BUILD_HARDWAREDRIVERS "Build HardwareDrivers module" ON)
if(ABDSHAREDCODE_BUILD_HARDWAREDRIVERS)
    add_library(ABDShared_HardwareDrivers STATIC
        HardwareDrivers/SysExCodec.cpp
        HardwareDrivers/NRPNParser.cpp
        HardwareDrivers/CasioNibbleCodec.cpp
        HardwareDrivers/FskAudioModem.cpp
        HardwareDrivers/JunoTapeModem.cpp
    )

    add_library(ABDShared::HardwareDrivers ALIAS ABDShared_HardwareDrivers)

    # Las cabeceras van en target_sources y no solo en el disco porque esto es un
    # paquete: lo que el guia promete son CLASES, y una clase cuya cabecera no
    # aparece aqui no se ve desde el IDE del consumidor ni cuando se inspecciona
    # el target. Las tres que se compilan ahora mismo y antes no (JunoTapeModem,
    # FskAudioModem, CasioNibbleCodec) estaban en disco y fuera del target, con
    # sus metodos definidos fuera de linea: se podian incluir y documentar, pero
    # no enlazar. El sintoma de eso es un undefined reference en el consumidor, no
    # un error aqui, que es el sitio mas caro de enterarse.
    # CasioCzVirtualController.h aparece igualmente porque es Superficie publica
    # del modulo, aunque su .cpp se compile fuera del target (ver nota de arriba).
    target_sources(ABDShared_HardwareDrivers PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/HardwareDrivers/AiraSysExController.h
        ${CMAKE_CURRENT_SOURCE_DIR}/HardwareDrivers/CasioCzVirtualController.h
        ${CMAKE_CURRENT_SOURCE_DIR}/HardwareDrivers/CasioNibbleCodec.h
        ${CMAKE_CURRENT_SOURCE_DIR}/HardwareDrivers/FskAudioModem.h
        ${CMAKE_CURRENT_SOURCE_DIR}/HardwareDrivers/HardwareController.h
        ${CMAKE_CURRENT_SOURCE_DIR}/HardwareDrivers/JunoTapeModem.h
        ${CMAKE_CURRENT_SOURCE_DIR}/HardwareDrivers/MidiCcController.h
        ${CMAKE_CURRENT_SOURCE_DIR}/HardwareDrivers/NRPNParser.h
        ${CMAKE_CURRENT_SOURCE_DIR}/HardwareDrivers/RoutingValidator.h
        ${CMAKE_CURRENT_SOURCE_DIR}/HardwareDrivers/SysexPresetGenerator.h
        ${CMAKE_CURRENT_SOURCE_DIR}/HardwareDrivers/SysExCodec.h
    )

    # Exposing the module root lets consumers write `#include <HardwareDrivers/...>`
    # (used by ABDJUNiO601) as well as relative includes.
    target_include_directories(ABDShared_HardwareDrivers PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_features(ABDShared_HardwareDrivers PUBLIC cxx_std_20)

    # juce_audio_devices no estaba y hace falta desde CasioCzVirtualController.h:
    # AiraSysExController.h, MidiCcController.h y CasioCzVirtualController.h
    # incluyen <juce_audio_devices/...> en su Superficie publica. Con solo
    # juce_core + juce_audio_basics, compilar CasioCzVirtualController.cpp —que
    # incluye su propia cabecera— falla en cuanto aparece ese include.
    # nlohmann_json va aqui, y no solo en HardwareMidiDetect, porque
    # CasioCzVirtualController.h es una cabecera PUBLIC de ESTE target y
    # necesita <nlohmann/json.hpp>. Sin este enlace, el que la incluya se come un
    # C1083 en su propia compilacion: ABDSharedCode se configura bien y el fallo
    # sale en el consumidor, que es donde nadie lo busca. En PUBLIC porque la
    # dependencia es de la Superficie, no de la implementacion: se propaga al
    # consumidor con target_link_libraries(ABDShared::HardwareDrivers) y por eso
    # basta con resolverlo una vez aqui arriba.
    target_link_libraries(ABDShared_HardwareDrivers PUBLIC
        juce_core
        juce_audio_basics
        juce_audio_devices
    )
    if(ABDSHAREDCODE_NLOHMANN_TARGET)
        target_link_libraries(ABDShared_HardwareDrivers PUBLIC ${ABDSHAREDCODE_NLOHMANN_TARGET})
    endif()
endif()

# --- BankManager Module ---
# El modulo embebible del Bank Manager, cortado desde ABDBankManager
# (DOCS/bank-manager-module-cut.md, §2b): core C++ (ValueTree v1, blobs Base64,
# IPC por callback), adaptador JSON <-> core sin dependencia de WebView2,
# loader de factory content y el protocolo SysEx del Behringer Pro800
# (buildDumpRequest/parseResponse/buildPatchDump, slot clamp 0-399).
#
# INTERFACE, igual que HardwareMidiDetect (el patron que el corte pide seguir):
# los .cpp se propagan y se compilan en el consumidor, que es quien tiene su
# JUCE y su catalogo de assets embebidos. El consumidor que quiera conservar su
# propio artefacto (ABDBankManager lo hace: ABDBankManagerCore.lib es el nombre
# que esperan su CI y su plugin) declara su estatico y enlaza este target: las
# fuentes propagadas se compilan dentro del suyo.
#
# Depende de HardwareDrivers (Pro800Midi delega el pack 8->7 y el unpack 7->8
# en abd::hw::SysExCodec), asi que este bloque va JUSTO DESPUES del de
# HardwareDrivers y no puede vivir sin el.
#
# Default OFF: solo lo activa quien consume el modulo (ABDBankManager lo fuerza
# en su bloque de integracion). Los demas consumidores del monorepo (p. ej. los
# builds WASM de los synths, que no enlazan este modulo) no cambian de
# comportamiento por el corte.
option(ABDSHAREDCODE_BUILD_BANKMANAGER "Build BankManager embeddable module" OFF)
if(ABDSHAREDCODE_BUILD_BANKMANAGER)
    add_library(ABDShared_BankManagerCore INTERFACE)

    add_library(ABDShared::BankManagerCore ALIAS ABDShared_BankManagerCore)

    target_include_directories(ABDShared_BankManagerCore INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Cabeceras y fuentes juntos: es un paquete, y la superficie publica del
    # modulo es la que el consumidor incluye como <BankManager/...>.
    target_sources(ABDShared_BankManagerCore INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/ABDBankManagerCore.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/ABDBankManagerCore.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/BankManagerWebViewAdapter.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/BankManagerWebViewAdapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/FactoryContentLoader.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/FactoryContentLoader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/Pro800Midi.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/Pro800Midi.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/HardwareMidiPipe.h
    )

    target_compile_features(ABDShared_BankManagerCore INTERFACE cxx_std_20)

    target_link_libraries(ABDShared_BankManagerCore INTERFACE
        ABDShared::HardwareDrivers
        juce_core
        juce_data_structures
        juce_cryptography
        juce_audio_devices
    )
endif()

# --- AudioComparator Module ---
# Motor de comparacion acustica A/B: alinea dos tomas en el tiempo (correlacion
# cruzada FFT), mide error temporal y espectral, y emite un veredicto formal
# (`pass`/`warn`/`fail`) contra una matriz de tolerancias.
#
# Lo que existia era el codigo entero y su seccion entera en el
# INTEGRATION_GUIDE.md, con su linea de CMake copiable
# (`target_link_libraries(... ABDShared::AudioComparator)`) y su ejemplo de uso,
# pero NINGUN target lo definia. Un consumidor que siguiera el guia no fallaba al
# configurar —que es cuando se nota un target que falta— sino al enlazar, y por
# el motivo equivocado.
#
# STATIC, no INTERFACE: son cuatro .cpp, y dos de ellos no son una unidad
# autonomous. `AudioABComparator.cpp`, `_Alignment.cpp` y `_Spectral.cpp` definen
# metodos de la MISMA clase `AudioABComparator` repartidos en tres unidades de
# traduccion. Compilar solo el primero deja los otros dos metodos sin definir.
#
# juce_dsp lo necesita `AudioABComparator_Spectral.cpp`, que es donde vive la
# correlacion cruzada FFT que el guia describe. No se hace INTERFACE porque el
# codigo ya existe y compilarlo es lo que lo hace cierto.
option(ABDSHAREDCODE_BUILD_AUDIOCOMPARATOR "Build AudioComparator module" ON)
if(ABDSHAREDCODE_BUILD_AUDIOCOMPARATOR)
    add_library(ABDShared_AudioComparator STATIC
        AudioComparator/AudioABComparator.cpp
        AudioComparator/AudioABComparator_Alignment.cpp
        AudioComparator/AudioABComparator_Spectral.cpp
        AudioComparator/AudioABVerdictEngine.cpp
    )

    add_library(ABDShared::AudioComparator ALIAS ABDShared_AudioComparator)

    target_sources(ABDShared_AudioComparator PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/AudioComparator/AudioABComparator.h
        ${CMAKE_CURRENT_SOURCE_DIR}/AudioComparator/AudioABVerdictEngine.h
    )

    # El modulo raiz, para que el consumidor escriba
    # `#include <AudioComparator/AudioABComparator.h>` como dice el guia.
    target_include_directories(ABDShared_AudioComparator PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_features(ABDShared_AudioComparator PUBLIC cxx_std_20)

    target_link_libraries(ABDShared_AudioComparator PUBLIC
        juce_core
        juce_audio_basics
        juce_dsp
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

# --- MidiKeyboard C++ Module ---
# EL LADO NATIVO de @abdsynths/midi-keyb. El paquete JS (`MidiKeyboard/`, sin
# target, se consume por pnpm workspace) es la otra mitad; aqui esta el
# componente JUCE que lo monta dentro de un plugin, con el WebUI del teclado
# servido desde un catalogo de binarios.
#
# Lo que estaba era una contradiccion, no un olvido: el INTEGRATION_GUIDE.md
# decia, en su seccion MidiKeyboard, "este modulo NO es CMake ... No tiene target
# C++ ni aparece en CMakeLists.txt", y en la misma carpeta havia cuatro
# ficheros C++ que no compilaba nadie. La frase era literalmente cierta y
# practicamente falsa, que es la peor forma de ser verdad: el lector se queda
# con la conclusion de que no hay nada que integrar, sin llegar a la de que
# hay algo que esta a medias.
#
# INTERFACE por el mismo motivo que WebView2Bridge: el .cpp se propaga y se
# compila en el consumidor, que es quien tiene su JUCE y su catalogo de assets.
# Depende de ese puente (`#include <WebView2Bridge/...>`), asi que este bloque
# va DESPUES de el y no puede vivir sin el.
#
# Gated on NOT EMSCRIPTEN, igual que WebView2Bridge y por el mismo motivo: es
# juce_gui_extra, que no existe en las builds WASM.
if(NOT EMSCRIPTEN)
    add_library(ABDShared_MidiKeyboardCpp INTERFACE)

    add_library(ABDShared::MidiKeyboardCpp ALIAS ABDShared_MidiKeyboardCpp)

    target_sources(ABDShared_MidiKeyboardCpp INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/MidiKeyboard/JuceMidiKeyboardComponent.h
        ${CMAKE_CURRENT_SOURCE_DIR}/MidiKeyboard/MidiKeyboardFloatingWindow.h
        ${CMAKE_CURRENT_SOURCE_DIR}/MidiKeyboard/MidiKeyboardResourceProvider.h
        ${CMAKE_CURRENT_SOURCE_DIR}/MidiKeyboard/MidiKeyboardResourceProvider.cpp
    )

    target_include_directories(ABDShared_MidiKeyboardCpp INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(ABDShared_MidiKeyboardCpp INTERFACE
        ABDShared::WebView2Bridge
        juce_core
        juce_audio_basics
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

# HardwareContractRegistry.h incluye <nlohmann/json.hpp> y es Superficie publica
# de este target. La resolucion ya esta hecha mas arriba, antes de
# HardwareDrivers, y se comparte: no se vuelve a buscar aqui. La variable se
# expande al valor, no al nombre de una variable de CMake.
if(ABDSHAREDCODE_NLOHMANN_TARGET)
    target_link_libraries(ABDShared_HardwareMidiDetect INTERFACE ${ABDSHAREDCODE_NLOHMANN_TARGET})
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

# --- Scope Module ---
# El osciloscopio analitico de la suite (subtree de ABDScope desde v0.4.0):
# core C++ header-only (ScopeTap, ScopeDataCollector, ScopeFrameSerializer,
# SpscRingBuffer, TriggerDetector) + glue JUCE (JuceWebScopeComponent,
# ScopeResourceProvider) + WebUI embebida como binary data.
#
# FUENTE UNICA DE VERDAD: el codigo vive AQUI (Scope/) y los consumidores
# enlazan `ABDShared::ScopeCore` en vez de hacer su propio
# `add_subdirectory(../ABDScope)`. Si funciona, funciona igual para todos; si
# se arregla, se arregla para todos; si se rompe, se rompe para todos. Y no
# puede haber DOS add_subdirectory del mismo modulo en un build: CMake muere
# con "add_library cannot create target ABDScopeCore because another target
# with the same name already exists", que es exactamente lo que pasaria si un
# consumidor siguiera anadiendo ../ABDScope al lado de este bloque.
#
# EL ORDEN IMPORTA: Scope/Source/CMakeLists.txt hace
# `if(TARGET ABDShared::WebView2Bridge)` para decidir entre enlazar el puente
# o caer a un simple include dir, asi que este bloque va DESPUES del de
# WebView2Bridge (arriba). Bajo EMSCRIPTEN el puente no existe (juce_gui_extra
# no esta en las builds WASM), el probe cae al fallback y el core header-only
# sigue disponible: cualquier TU WASM futura que incluya
# `<ScopeDataCollector.h>` (hoy ninguna lo hace: SynthEngine.cpp esta fuera de
# MS2000_DSP_SOURCES_WASM por juce_audio_processors) encuentra el include path.
#
# add_subdirectory sin condiciones: el CMakeLists de Scope ya trae sus propias
# compuertas (probe de JUCE solo en standalone, juce_add_binary_data solo si
# el comando existe, GuiDemo solo en standalone), asi que anadirlo aqui no
# arrastra JUCE a nadie ni anade targets de demo a los builds de producto.
add_subdirectory(Scope)

# Aliases ABDShared::Scope*: los targets reales conservan su nombre ABDScope*
# (heredado del repo original, que sigue existiendo como hermano hasta que se
# archive) y los alias son la interfaz que documenta el guia y enlazan los
# consumidores. ALIAS del target real, nunca del alias (ABDScope::ABDScopeCore
# es ya un alias; enlazar a traves de dos saltos no anade nada).
add_library(ABDShared::ScopeCoreHeaders ALIAS ABDScopeCoreHeaders)
add_library(ABDShared::ScopeCore ALIAS ABDScopeCore)

# La WebUI embebida solo existe si juce_add_binary_data esta en alcance (el
# consumidor trajo su JUCE antes de anadir ABDSharedCode). Sin JUCE — builds
# WASM, o consumidores que solo quieren el core— no hay target y la WebUI se
# sirve por filesystem, que es el fallback documentado del propio modulo.
if(TARGET ABDScopeWebAssets)
    add_library(ABDShared::ScopeWebAssets ALIAS ABDScopeWebAssets)
endif()
