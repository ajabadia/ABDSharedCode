// SynthCore unit tests — standalone executable, no JUCE dependency.
// Covers the shared DSP primitives extracted from ABDMS2000 (Phase 2 DRY),
// the VoiceAllocator converged from LutDSP, and the shared ModMatrix
// (the modulation-matrix core every synth will sit on top of).
//
// Style mirrors ABDMS2000/Source/Tests/DSPCoreTests.cpp (check() + counters).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

// Contador global de alojamientos: ArpeggiatorTests.inc mide el zero-alloc de
// generate() con el. Este binario de pruebas sobrescribe el operador new
// global para sumar uno por cada alojamiento; las variantes ALINEADAS no pasan
// por aqui (siguen su camino por defecto) y se liberan con su delete
// correspondiente, asi que no hay mezcla de allocators.
static long long gTestAllocations = 0;

void* operator new(std::size_t size)
{
    ++gTestAllocations;
    if (void* p = std::malloc(size != 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

#include "ADSREnvelope.h"
#include "Arpeggiator.h"
#include "AudioThreadSnapshot.h"
#include "ControlSequencer.h"
#include "DSPUtils.h"
#include "DriftEngine.h"
#include "EnvelopeAnalog.h"
#include "EnvelopeCurves.h"
#include "LFO.h"
#include "LfoAnalog.h"
#include "OscHalfbandDecimator.h"
#include "OscPolyBlep.h"
#include "OscVcoCa72.h"
#include "OscVcoCa72Profile.h"
#include "OscillatorFamily.h"
#include "PolyBLEP.h"
#include "PortamentoGlide.h"
#include "VoiceAllocator.h"
// Compat-shim coverage: the LutDSP path must keep aliasing the canonical type.
#include "LutDSP/VoiceAllocator.h"
#include "ModMatrix.h"
#include "S950Calibration.h"
#include "S950CalibrationHarness.h"
#include "S950Disk.h"
#include "S950EnvelopeBench.h"
#include "S950PatchFields.h"

namespace abd::synth::tests
{

static int testsPassed = 0;
static int testsFailed = 0;

static void check(bool condition, const char* testName)
{
    if (condition)
    {
        testsPassed++;
        printf("  [PASS] %s\n", testName);
    }
    else
    {
        testsFailed++;
        printf("  [FAIL] %s\n", testName);
    }
    fflush(stdout);
}

// La matriz de modulacion tiene su propio .inc: es un bloque autocontenido
// (incluye una COPIA LITERAL de la implementacion previa de ABDEep para el
// test de equivalencia, que no debe contaminar los includes de los demas).
#include "tests/ModMatrixTests.inc"

// Los motores del nucleo compartido tienen su propio bloque: arpeggio, secuencia,
// LFO analogico, envolvente analogica y ruido determinista. Cada .inc es
// autocontenido y solo depende de la cabecera de SynthCore que prueba.
#include "tests/ArpeggiatorTests.inc"
#include "tests/ControlSequencerTests.inc"
#include "tests/LFOAnalogTests.inc"
#include "tests/EnvelopeAnalogTests.inc"
#include "tests/DriftEngineTests.inc"

// El catalogo del S950 tambien va en su propio .inc: es un bloque de datos con
// sus propias reglas, y mezclarlo con el resto haria mas dificil ver que se
// esta probando.
#include "tests/S950CalibrationRenderTests.inc"
#include "tests/S950CalibrationTests.inc"
#include "tests/S950DiskTests.inc"
#include "tests/S950EnvelopeBenchTests.inc"
#include "tests/S950PatchFieldsTests.inc"

// La familia de osciladores tiene el suyo: el contrato (rasgo incluido), el
// idioma de los parametros y las dos piezas (el VCO de rampa y el de fase) con
// su fidelidad medida.
#include "tests/OscillatorFamilyTests.inc"

// ─────────────────────────── DSPUtils ───────────────────────────

static void testDSPUtils()
{
    printf("=== DSPUtils ===\n");

    check(DSPUtils::clamp(0.5f, 0.0f, 1.0f) == 0.5f, "clamp in range");
    check(DSPUtils::clamp(-0.5f, 0.0f, 1.0f) == 0.0f, "clamp below min");
    check(DSPUtils::clamp(1.5f, 0.0f, 1.0f) == 1.0f, "clamp above max");

    check(std::abs(DSPUtils::midiNoteToFrequency(69.0f) - 440.0f) < 0.01f, "A4 = 440 Hz");
    check(std::abs(DSPUtils::midiNoteToFrequency(81.0f) - 880.0f) < 0.1f, "A5 = 880 Hz");

    check(DSPUtils::softClip(0.0f) == 0.0f, "softClip zero");
    check(DSPUtils::softClip(100.0f) == 1.0f, "softClip saturates to +1");
    check(DSPUtils::softClip(-100.0f) == -1.0f, "softClip saturates to -1");

    float dist = DSPUtils::ampDistortion(0.3f, 0.8f);
    check(std::abs(dist) <= 1.0f / (0.5f + 0.8f * 0.5f), "ampDistortion bounded");
    check(DSPUtils::ampDistortion(0.0f, 1.0f) == 0.0f, "ampDistortion zero in/out");

    check(DSPUtils::convertSysExToCutoffHz(0.0f) == 20.0f, "cutoff min 20 Hz");
    check(std::abs(DSPUtils::convertSysExToCutoffHz(1.0f) - 20000.0f) < 1.0f, "cutoff max 20 kHz");

    check(std::abs(DSPUtils::decibelsToLinear(0.0f) - 1.0f) < 0.0001f, "0 dB = x1");
    check(std::abs(DSPUtils::decibelsToLinear(20.0f) - 10.0f) < 0.001f, "20 dB = x10");
    check(std::abs(DSPUtils::linearToDecibels(10.0f) - 20.0f) < 0.001f, "x10 = 20 dB");

    uint32_t rng = 0x12345678;
    float r1     = DSPUtils::randomBipolar(rng);
    float r2     = DSPUtils::randomBipolar(rng);
    check(r1 >= -1.0f && r1 <= 1.0f, "randomBipolar in [-1,1]");
    check(r1 != r2, "randomBipolar advances state");
}

// ─────────────────────────── PolyBLEP ───────────────────────────

static void testPolyBLEP()
{
    printf("=== PolyBLEP ===\n");

    const float dt = 0.05f;
    check(PolyBLEP::getResidual(0.5f, dt) == 0.0f, "residual zero mid-phase");
    check(std::abs(PolyBLEP::getResidual(0.0f, dt) + 1.0f) < 0.0001f, "residual at step start = -1");
    // Quadratic ramp 0 -> +1 across the last dt: midpoint evaluates to 0.25.
    check(std::abs(PolyBLEP::getResidual(1.0f - dt * 0.5f, dt) - 0.25f) < 0.0001f, "residual ramps quadratically before step");

    // Integrated residual stays within sane bounds
    bool bounded = true;
    for (int i = 0; i <= 100; ++i)
    {
        float t = static_cast<float>(i) / 100.0f;
        float r = PolyBLEP::getResidualIntegrated(t, 0.02f);
        if (std::abs(r) > 0.02f) bounded = false;
    }
    check(bounded, "integrated residual bounded by ~dt");
}

// ─────────────────────────── EnvelopeCurves + ADSREnvelope ───────────────────────────

static void testEnvelope()
{
    printf("=== EnvelopeCurves + ADSREnvelope ===\n");

    float at0 = EnvelopeCurves::getAttackTimeSeconds(0.0f);
    float at1 = EnvelopeCurves::getAttackTimeSeconds(1.0f);
    check(std::abs(at0 - 0.0005f) < 0.0001f, "attack min 0.5 ms");
    check(std::abs(at1 - 5.0f) < 0.01f, "attack max 5 s");
    check(EnvelopeCurves::getAttackTimeSeconds(0.1f) < EnvelopeCurves::getAttackTimeSeconds(0.9f), "attack monotonic");

    double multLong  = EnvelopeCurves::getDecayMultiplier(10.0, 44100.0);
    double multShort = EnvelopeCurves::getDecayMultiplier(0.002, 44100.0);
    // Per-sample multipliers: long decay ~0.99999, short decay ~0.949
    // (0.949^88.2 samples ~= e^-4.6 ~= 1% remaining at the 2 ms mark).
    check(multLong > 0.9999 && multLong < 1.0, "long decay mult near 1");
    check(multShort > 0.9 && multShort < 0.96, "short decay mult decays fast");
    check(EnvelopeCurves::getDecayMultiplier(1.0, 0.0) == 0.0, "invalid sampleRate -> 0");

    ADSREnvelope env;
    env.prepare(44100.0);
    env.setAttack(0.0f); // 0.5 ms
    env.setDecay(0.0f);
    env.setSustain(0.5f);
    env.setRelease(0.0f);
    env.noteOn(1.0f);

    float peak = 0.0f;
    for (int i = 0; i < 480; ++i) peak = env.getNextSample();
    check(std::abs(peak - 0.5f) < 0.05f, "reaches sustain after fast A+D");

    env.noteOff();
    float afterRelease = 0.0f;
    // Min release is 5 ms (~220 samples); give it 2000 to settle.
    for (int i = 0; i < 2000; ++i) afterRelease = env.getNextSample();
    check(afterRelease < 0.001f, "silences after fast release");
    check(env.isIdle(), "idle after release");

    // Velocity scaling
    ADSREnvelope env2;
    env2.prepare(44100.0);
    env2.setAttack(1.0f);
    env2.setDecay(1.0f);
    env2.setSustain(1.0f);
    env2.setRelease(1.0f);
    env2.noteOn(0.5f);
    float early = env2.getNextSample();
    check(early > 0.0f && early < 0.2f, "attack starts near zero (velocity-scaled)");

    // ── Las 4 etapas como contrato observable ──
    ADSREnvelope st;
    st.prepare(44100.0);
    st.setAttack(0.0f); st.setDecay(0.0f); st.setSustain(0.5f); st.setRelease(0.0f);
    check(st.getStage() == EnvelopeStage::Idle, "arranca en Idle");
    st.noteOn();
    check(st.getStage() == EnvelopeStage::Attack, "noteOn entra en Attack");
    st.getNextSample();
    check(st.getStage() == EnvelopeStage::Attack, "la primera muestra sigue en Attack (0,5 ms no han pasado)");
    for (int i = 0; i < 480; ++i) st.getNextSample();
    check(st.getStage() == EnvelopeStage::Sustain, "con A y D mínimos, a las 480 muestras ya está en Sustain");
    st.noteOff();
    check(st.getStage() == EnvelopeStage::Release, "noteOff entra en Release");
    for (int i = 0; i < 2000; ++i) st.getNextSample();
    check(st.getStage() == EnvelopeStage::Idle, "y tras el release mínimo vuelve a Idle");

    // ── El nivel de sustain se respeta, y su clampeo ──
    auto nivelSustain = [](float sus, float vel) {
        ADSREnvelope e;
        e.prepare(44100.0);
        e.setAttack(0.0f); e.setDecay(0.0f); e.setSustain(sus); e.setRelease(0.0f);
        e.noteOn(vel);
        float v = 0.0f;
        for (int i = 0; i < 480; ++i) v = e.getNextSample();
        return v;
    };
    check(std::abs(nivelSustain(0.8f, 1.0f) - 0.8f) < 1.0e-4f, "sustain 0,8 se sostiene exacto en 0,8");
    check(std::abs(nivelSustain(2.0f, 1.0f) - 1.0f) < 1.0e-4f, "setSustain se clampea arriba: 2,0 entra como 1,0");
    check(nivelSustain(-1.0f, 1.0f) < 1.0e-6f, "y abajo: −1,0 entra como 0,0 (decay hasta el suelo)");

    // ── La velocidad escala la SALIDA (velocityGain_), no el interior ──
    check(std::abs(nivelSustain(0.5f, 1.0f) - 0.5f) < 1.0e-4f, "vel 1,0: la salida es el sustain entero (0,5)");
    check(std::abs(nivelSustain(0.5f, 0.4f) - 0.2f) < 1.0e-4f, "vel 0,4: la salida es sustain × vel = 0,2");
    check(std::abs(nivelSustain(0.5f, 0.0f) - 0.05f) < 1.0e-4f, "vel 0 se clampea al suelo 0,1: la salida baja a 0,05");

    // ── Retrigger: noteOn en pleno release SUBE desde donde estaba ──
    ADSREnvelope rt;
    rt.prepare(44100.0);
    rt.setAttack(0.0f); rt.setDecay(0.0f); rt.setSustain(0.5f); rt.setRelease(1.0f); // release de 10 s
    rt.noteOn(1.0f);
    for (int i = 0; i < 480; ++i) rt.getNextSample();
    rt.noteOff();
    for (int i = 0; i < 4410; ++i) rt.getNextSample();   // 0,1 s: apenas ha bajado
    const float antesDelRetrigger = rt.getCurrentLevel();
    check(antesDelRetrigger > 0.4f, "tras 0,1 s de release lento la nota aún suena (medido, no supuesto)");
    rt.noteOn(1.0f);
    check(rt.getStage() == EnvelopeStage::Attack, "retrigger: noteOff seguido de noteOn vuelve a Attack");
    const float trasRetrigger = rt.getNextSample();
    check(trasRetrigger > antesDelRetrigger, "y SUBE desde donde estaba: no reinicia la envolvente en cero");

    // ── noteOff en pleno ataque: solo baja, y acaba en Idle ──
    ADSREnvelope na;
    na.prepare(44100.0);
    na.setAttack(1.0f);   // 5 s: a los 0,1 s va por ~0,18
    na.setDecay(0.0f); na.setSustain(1.0f); na.setRelease(0.0f);
    na.noteOn(1.0f);
    for (int i = 0; i < 4410; ++i) na.getNextSample();
    const float picoDelCorte = na.getCurrentLevel();
    check(picoDelCorte > 0.05f && picoDelCorte < 0.5f, "ataque lento: a los 0,1 s el nivel va por el camino (medido)");
    na.noteOff();
    check(na.getStage() == EnvelopeStage::Release, "noteOff en pleno ataque entra en Release");
    float maxTrasCorte = 0.0f;
    for (int i = 0; i < 2000; ++i)
    {
        const float v = na.getNextSample();
        if (v > maxTrasCorte) maxTrasCorte = v;
    }
    check(maxTrasCorte <= picoDelCorte + 1.0e-6f, "y el release solo BAJA: nunca supera el nivel del corte");
    check(na.isIdle(), "con el release mínimo (5 ms) ya está en Idle en 2000 muestras");

    // ── noteOff sin nota: no-op ──
    ADSREnvelope ni;
    ni.prepare(44100.0);
    ni.noteOff();
    check(ni.getStage() == EnvelopeStage::Idle, "noteOff sin nota activa es un no-op");
    check(ni.getNextSample() == 0.0f, "y la salida sigue siendo 0 exacto");

    // ── Fuera de rango: setters y velocidad se clampean, nada de NaN ──
    ADSREnvelope xr;
    xr.prepare(44100.0);
    xr.setAttack(5.0f); xr.setDecay(-3.0f); xr.setSustain(9.0f); xr.setRelease(-1.0f);
    xr.noteOn(1.5f);
    bool finito = true;
    for (int i = 0; i < 5000; ++i) { const float v = xr.getNextSample(); if (!std::isfinite(v)) finito = false; }
    xr.noteOff();
    for (int i = 0; i < 5000; ++i) { const float v = xr.getNextSample(); if (!std::isfinite(v)) finito = false; }
    check(finito, "parámetros y velocidad fuera de rango: todo el ciclo sale finito (sin NaN)");

    // ── prepare con sample rate inválido cae al fallback de 44100 ──
    ADSREnvelope p1, p2;
    p1.prepare(44100.0);
    p2.prepare(500.0);
    p1.setAttack(0.3f); p1.setDecay(0.3f); p1.setSustain(0.5f); p1.setRelease(0.3f);
    p2.setAttack(0.3f); p2.setDecay(0.3f); p2.setSustain(0.5f); p2.setRelease(0.3f);
    p1.noteOn(0.7f); p2.noteOn(0.7f);
    bool mismoRecorridoEnv = true;
    for (int i = 0; i < 500; ++i)
        if (p1.getNextSample() != p2.getNextSample()) mismoRecorridoEnv = false;
    check(mismoRecorridoEnv, "prepare(500) cae al fallback de 44100: recorrido bit a bit idéntico");
}

// ─────────────────────────── PortamentoGlide ───────────────────────────

static void testPortamento()
{
    printf("=== PortamentoGlide ===\n");

    PortamentoGlide glide;
    glide.prepare(44100.0);
    glide.reset(60.0f);
    glide.setGlideTime(0.0f);
    glide.setTargetNote(72.0f, true);
    check(std::abs(glide.getNextPitchSemitones() - 72.0f) < 0.001f, "glide time 0 = instant");

    PortamentoGlide glide2;
    glide2.prepare(44100.0);
    glide2.reset(60.0f);
    glide2.setGlideTime(0.8f);
    glide2.setTargetNote(72.0f, true);
    float first = glide2.getNextPitchSemitones();
    check(first > 60.0f && first < 72.0f, "glide starts between notes");
    check(glide2.getCurrentPitch() == first, "getCurrentPitch tracks");

    // Exponential glide converges asymptotically (never exactly arrives).
    // Empirical tau for param 0.8 ~ 7.5 s; practical contract: >96% of the
    // 12-semitone span covered after 25 s.
    float last = 0.0f;
    for (int i = 0; i < 44100 * 25; ++i) last = glide2.getNextPitchSemitones();
    check(last > 71.5f && last < 72.0f, "glide converges toward target (>96%)");

    // Glide disabled = snap
    glide2.setTargetNote(48.0f, false);
    check(std::abs(glide2.getNextPitchSemitones() - 48.0f) < 0.001f, "glide disabled snaps");
}

// ─────────────────────────── LFO ───────────────────────────

// Cuenta las vueltas de fase del diente de sierra: el salto de −1 a +1 marca
// un ciclo completo, así que mide la frecuencia REAL sin mirar internals.
static int contarVueltasSierra(LFO& lfo, int muestras, float modOctavas = 0.0f)
{
    int vueltas = 0;
    float prev = lfo.getNextSample(modOctavas);
    for (int i = 1; i < muestras; ++i)
    {
        const float v = lfo.getNextSample(modOctavas);
        if (v - prev > 1.5f) ++vueltas;   // el diente sube de golpe al envolver
        prev = v;
    }
    return vueltas;
}

static void testLFO()
{
    printf("=== LFO ===\n");

    check(std::abs(LFO::syncNoteToMultiplier(4) - 1.0f) < 0.0001f, "sync idx4 = 1/4 x1");
    check(std::abs(LFO::syncNoteToMultiplier(0) - 0.25f) < 0.0001f, "sync idx0 = 1/1 x0.25");
    check(LFO::syncNoteToMultiplier(14) > LFO::syncNoteToMultiplier(0), "sync monotonic up");
    check(LFO::syncNoteToMultiplier(-1) == LFO::syncNoteToMultiplier(4), "sync idx OOB -> default");
    check(LFO::syncNoteToMultiplier(99) == LFO::syncNoteToMultiplier(4), "sync idx high OOB -> default");

    // Triangle LFO1 at 1 Hz: one full cycle over 1 s at 44.1 kHz
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.reset(0.0f);
    lfo.setWaveformLFO1(LFOWaveform::Triangle);
    lfo.setFrequencyHz(1.0f);
    float peakPos = 0.0f, minPos = 0.0f;
    for (int i = 0; i < 44100; ++i)
    {
        float v = lfo.getNextSample();
        if (v > peakPos) peakPos = v;
        if (v < minPos) minPos = v;
    }
    check(std::abs(peakPos - 1.0f) < 0.01f, "triangle peaks +1");
    check(std::abs(minPos + 1.0f) < 0.01f, "triangle troughs -1");

    // Sine LFO2 stays bounded
    LFO lfo2;
    lfo2.prepare(44100.0);
    lfo2.setWaveformLFO2(LFOWaveformLFO2::Sine);
    lfo2.setFrequencyHz(5.0f);
    bool sineBounded = true;
    for (int i = 0; i < 44100; ++i)
    {
        float v = lfo2.getNextSample();
        if (v < -1.001f || v > 1.001f) sineBounded = false;
    }
    check(sineBounded, "LFO2 sine bounded [-1,1]");

    // Sample & Hold holds value until next cycle
    LFO lfo3;
    lfo3.prepare(44100.0);
    lfo3.setWaveformLFO1(LFOWaveform::SampleAndHold);
    lfo3.setFrequencyHz(1.0f);
    float s1 = lfo3.getNextSample();
    float s2 = lfo3.getNextSample();
    check(s1 == s2, "S&H holds within a cycle");

    // Key sync Voice mode resets phase
    LFO lfo4;
    lfo4.prepare(44100.0);
    lfo4.setWaveformLFO1(LFOWaveform::Square);
    lfo4.setKeySyncMode(2);
    lfo4.setFrequencyHz(0.1f);
    for (int i = 0; i < 10000; ++i) lfo4.getNextSample();
    lfo4.triggerKeySync(false);
    float vAfter = lfo4.getNextSample();
    check(vAfter > 0.5f, "voice key sync resets to square-high");

    // ── Frecuencia: los límites medidos del rango MS2000 (0,01 – 20 Hz) ──
    {
        LFO l;
        l.prepare(44100.0);
        l.setWaveformLFO1(LFOWaveform::Sawtooth);
        l.setFrequencyHz(5000.0f);      // clampeado al techo
        l.reset(0.0f);
        const int vueltas20 = contarVueltasSierra(l, 44100);
        check(vueltas20 >= 18 && vueltas20 <= 21, "setFrequencyHz se clampea al techo de 20 Hz (≈20 vueltas/s)");

        LFO s;
        s.prepare(44100.0);
        s.setWaveformLFO1(LFOWaveform::Sawtooth);
        s.setFrequencyHz(0.000001f);    // clampeado al suelo
        s.reset(0.0f);
        float v10s = 0.0f;
        for (int i = 0; i < 441000; ++i) v10s = s.getNextSample();   // 10 s de tiempo real
        // 10 s × 0,01 Hz = 0,1 de ciclo → diente = 1 − 2·0,1 = 0,8
        check(std::abs(v10s - 0.8f) < 0.01f, "setFrequencyHz se clampea al suelo de 0,01 Hz (0,8 tras 10 s)");
    }

    // ── Tempo sync: freq = BPM/60 × multiplicador, medida por vueltas ──
    {
        LFO s1;
        s1.prepare(44100.0);
        s1.setWaveformLFO1(LFOWaveform::Sawtooth);
        s1.setTempoSync(true, 6);       // 1/8 → ×2
        s1.setBpm(120.0);               // 120/60 × 2 = 4 Hz
        s1.reset(0.0f);
        const int v4 = contarVueltasSierra(s1, 44100 * 3);
        check(v4 >= 11 && v4 <= 13, "sync: BPM 120 × 1/8 = 4 Hz medidos (≈12 vueltas en 3 s)");

        LFO s2;
        s2.prepare(44100.0);
        s2.setWaveformLFO1(LFOWaveform::Sawtooth);
        s2.setTempoSync(true, 0);       // 1/1 → ×0,25
        s2.reset(0.0f);
        const int vMedia = contarVueltasSierra(s2, 44100 * 6);
        check(vMedia >= 2 && vMedia <= 4, "sync: BPM 120 × 1/1 = 0,5 Hz medidos (≈3 vueltas en 6 s)");

        LFO s3;
        s3.prepare(44100.0);
        s3.setWaveformLFO1(LFOWaveform::Sawtooth);
        s3.setTempoSync(true, 4);       // 1/4 → ×1
        s3.setBpm(999.0);               // fuera de rango → 120 por defecto
        s3.reset(0.0f);
        const int vBpm = contarVueltasSierra(s3, 44100 * 2);
        check(vBpm >= 3 && vBpm <= 5, "setBpm inválido vuelve a 120 (idx4 → 2 Hz, 4 vueltas en 2 s)");
    }

    // ── Con sync OFF la frecuencia manual manda, y setBpm no la toca ──
    {
        LFO s4;
        s4.prepare(44100.0);
        s4.setWaveformLFO1(LFOWaveform::Sawtooth);
        s4.setFrequencyHz(2.0f);
        s4.setTempoSync(false);
        s4.setBpm(300.0);               // sync apagado: la manual de 2 Hz no se mueve
        s4.reset(0.0f);
        const int vManual = contarVueltasSierra(s4, 44100 * 2);
        check(vManual >= 3 && vManual <= 5, "con sync OFF, setBpm(300) NO retunea: siguen las 2 Hz manuales");

        LFO s5;
        s5.prepare(44100.0);
        s5.setWaveformLFO1(LFOWaveform::Sawtooth);
        s5.setTempoSync(true, 6);       // se activa y se apaga
        s5.setTempoSync(false);
        s5.setFrequencyHz(1.0f);
        s5.reset(0.0f);
        const int vTrasSync = contarVueltasSierra(s5, 44100 * 2);
        check(vTrasSync >= 1 && vTrasSync <= 3, "tras desactivar el sync, setFrequencyHz(1) vuelve a mandar");
    }

    // ── Frecuencia modulada: el parámetro aplica 2^(2·oct) — +1 = ×4 ──
    {
        LFO b, m;
        b.prepare(44100.0); m.prepare(44100.0);
        b.setWaveformLFO1(LFOWaveform::Sawtooth);
        m.setWaveformLFO1(LFOWaveform::Sawtooth);
        b.setFrequencyHz(1.0f); m.setFrequencyHz(1.0f);
        b.reset(0.0f); m.reset(0.0f);
        const int vBase = contarVueltasSierra(b, 44100);
        const int vMod  = contarVueltasSierra(m, 44100, 1.0f);
        check(vBase >= 0 && vBase <= 2, "sin modulación, 1 Hz da una vuelta por segundo");
        check(vMod >= 3 && vMod <= 5, "getNextSample(+1) aplica 2^(2·1) = ×4: 4 vueltas por segundo");
    }

    // ── Key sync: los tres modos ──
    {
        LFO libre;
        libre.prepare(44100.0);
        libre.setWaveformLFO1(LFOWaveform::Square);
        libre.setKeySyncMode(0);        // Off: libre
        libre.setFrequencyHz(1.0f);
        for (int i = 0; i < 30000; ++i) libre.getNextSample();   // fase ≈ 0,68 → cuadrada en bajo
        libre.triggerKeySync(false);
        check(libre.getNextSample() < -0.5f, "key sync Off: triggerKeySync NO resetea (sigue en bajo)");

        LFO tim;
        tim.prepare(44100.0);
        tim.setWaveformLFO1(LFOWaveform::Square);
        tim.setKeySyncMode(1);          // Timbre
        tim.setFrequencyHz(1.0f);
        for (int i = 0; i < 30000; ++i) tim.getNextSample();
        tim.triggerKeySync(false);      // no es la primera nota del timbre
        check(tim.getNextSample() < -0.5f, "key sync Timbre: fuera de la primera nota NO resetea");
        tim.triggerKeySync(true);       // primera nota del timbre
        check(tim.getNextSample() > 0.5f, "key sync Timbre: con la primera nota SÍ resetea");
    }

    // ── reset(initialPhase): la fase inicial se respeta ──
    {
        LFO r;
        r.prepare(44100.0);
        r.setWaveformLFO1(LFOWaveform::Sawtooth);
        r.reset(0.5f);
        check(std::abs(r.getNextSample()) < 0.01f, "reset(0.5) arranca a media fase (diente ≈ 0)");
        r.reset(0.0f);
        check(r.getNextSample() > 0.99f, "reset(0) arranca al principio del ciclo (diente ≈ 1)");

        const float ultima = r.getNextSample();
        check(r.getCurrentValue() == ultima, "getCurrentValue() devuelve la última muestra emitida");
    }

    // ── S&H: sostiene DENTRO del ciclo y cambia al cruzarlo ──
    {
        LFO h;
        h.prepare(44100.0);
        h.setWaveformLFO1(LFOWaveform::SampleAndHold);
        h.setFrequencyHz(1.0f);
        const float v0 = h.getNextSample();
        float vNuevo = v0;
        for (int i = 0; i < 44102; ++i) vNuevo = h.getNextSample();  // cruza el wrap del segundo 1
        check(vNuevo != v0, "S&H cambia de valor al cruzar el ciclo");
    }

    // ── Duty: LFO1 Square fijo al 50 %, LFO2 Square+ aleatorio por ciclo ──
    {
        LFO cuad;
        cuad.prepare(44100.0);
        cuad.setWaveformLFO1(LFOWaveform::Square);
        cuad.setFrequencyHz(1.0f);
        cuad.reset(0.0f);
        int alto1 = 0, alto2 = 0;
        for (int i = 0; i < 44100; ++i) if (cuad.getNextSample() > 0.0f) ++alto1;
        for (int i = 0; i < 44100; ++i) if (cuad.getNextSample() > 0.0f) ++alto2;
        check(std::abs(alto1 - 22050) <= 3 && std::abs(alto2 - 22050) <= 3,
              "LFO1 Square: duty fijo al 50 % en dos ciclos seguidos");

        LFO sqp;
        sqp.prepare(44100.0);
        sqp.setWaveformLFO2(LFOWaveformLFO2::SquarePlus);
        sqp.setFrequencyHz(1.0f);
        sqp.reset(0.0f);                 // el reset pone PW inicial 0,5
        int duties[4] = {};
        for (int c = 0; c < 4; ++c)
        {
            int altos = 0;
            for (int i = 0; i < 44100; ++i) if (sqp.getNextSample() > 0.0f) ++altos;
            duties[c] = altos;
        }
        check(std::abs(duties[0] - 22050) <= 3, "Square+ arranca con el PW inicial 0,5 del reset");
        bool dentroDelRango = true;
        for (int c = 1; c < 4; ++c)
            if (duties[c] < 2200 || duties[c] > 41900) dentroDelRango = false;
        check(dentroDelRango, "Square+: cada ciclo su duty queda entre el 5 % y el 95 %");
        bool cambia = false;
        for (int c = 1; c < 4; ++c) if (duties[c] != duties[1]) cambia = true;
        check(cambia, "Square+: el duty CAMBIA de ciclo a ciclo (PW aleatoria, no fija)");
    }

    // ── prepare con sample rate inválido cae al fallback de 44100 ──
    {
        LFO p1, p2;
        p1.prepare(44100.0);
        p2.prepare(500.0);
        p1.setWaveformLFO1(LFOWaveform::Triangle);
        p2.setWaveformLFO1(LFOWaveform::Triangle);
        p1.setFrequencyHz(3.0f);
        p2.setFrequencyHz(3.0f);
        p1.reset(0.0f); p2.reset(0.0f);
        bool mismoRecorridoLfo = true;
        for (int i = 0; i < 5000; ++i)
            if (p1.getNextSample() != p2.getNextSample()) mismoRecorridoLfo = false;
        check(mismoRecorridoLfo, "prepare(500) cae al fallback de 44100: recorrido bit a bit idéntico");
    }
}

// ─────────────────────────── AudioThreadSnapshot ───────────────────────────

static void testAudioThreadSnapshot()
{
    printf("=== AudioThreadSnapshot ===\n");

    AudioThreadSnapshot snap;
    check(snap.voiceActive.size() == 32, "32 voice slots");
    check(snap.scopeBuffer.size() == 512, "512-sample scope buffer");
    check(snap.activeVoiceCount == 0 && !snap.isArpActive, "defaults zeroed");
    snap.vuLeft         = 0.5f;
    snap.voiceActive[3] = true;
    snap.voiceNote[3]   = 64;
    check(snap.voiceActive[3] && snap.voiceNote[3] == 64 && snap.vuLeft == 0.5f, "fields assignable");
}

// ─────────────────────────── VoiceAllocator ───────────────────────────

static void testVoiceAllocator()
{
    printf("=== VoiceAllocator ===\n");

    // Poly1 round-robin
    abd::synth::VoiceAllocator<4> alloc;
    check(alloc.getPolyMode() == PolyMode::Poly1, "default Poly1");

    auto a = alloc.allocateNoteOn(60, 1.0f);
    check(a.size() == 1 && a[0] == 0, "first noteOn -> voice 0");
    auto b = alloc.allocateNoteOn(62, 1.0f);
    check(b.size() == 1 && b[0] == 1, "second noteOn -> voice 1");
    check(alloc.getNumActiveVoices() == 2, "2 active after 2 noteOn");

    // Note off releases matching note only
    auto rel = alloc.allocateNoteOff(60);
    check(rel.size() == 1 && rel[0] == 0, "noteOff 60 releases voice 0");
    check(alloc.getNumActiveVoices() == 1, "1 active after noteOff");

    // Reuse happens round-robin: after v0/v1 the cursor sits at v2, so the
    // next noteOn takes v2 (the freed v0 is picked up later in the cycle).
    auto c = alloc.allocateNoteOn(64, 0.8f);
    check(c.size() == 1 && c[0] == 2, "round-robin continues cycle (takes v2)");

    // Stealing: fill all 4, then a 5th note steals the oldest
    abd::synth::VoiceAllocator<4> steal;
    steal.allocateNoteOn(40, 1.0f); // v0 (oldest)
    steal.allocateNoteOn(41, 1.0f); // v1
    steal.allocateNoteOn(42, 1.0f); // v2
    steal.allocateNoteOn(43, 1.0f); // v3
    auto s = steal.allocateNoteOn(44, 1.0f);
    check(s.size() == 1, "full polyphony: 5th note still allocated");
    check(steal.getNumActiveVoices() == 4, "still 4 active after steal");
    check(steal.getVoiceState(s[0]).midiNote == 44, "stolen voice carries new note");
    check(steal.getVoiceState(0).midiNote == 44, "oldest voice (v0) was the steal victim");

    // Poly2 re-triggers same note instead of allocating new voice
    abd::synth::VoiceAllocator<4> poly2;
    poly2.setPolyMode(PolyMode::Poly2);
    auto p1 = poly2.allocateNoteOn(60, 1.0f);
    auto p2 = poly2.allocateNoteOn(60, 1.0f);
    check(p1.size() == 1 && p2.size() == 1 && p1[0] == p2[0], "Poly2 reuses voice for repeated note");

    // Unison triggers all voices
    abd::synth::VoiceAllocator<4> uni;
    uni.setPolyMode(PolyMode::Unison);
    auto u = uni.allocateNoteOn(60, 1.0f);
    check(u.size() == 4, "unison allocates all voices");
    check(uni.getNumActiveVoices() == 4, "unison: all active");

    // allNotesOff clears everything
    uni.allNotesOff();
    check(uni.getNumActiveVoices() == 0, "allNotesOff clears");

    // Out-of-range voice state returns empty sentinel
    abd::synth::VoiceAllocator<2> small;
    check(small.getVoiceState(999).midiNote == -1, "OOB voice state = sentinel");

    // LutDSP compat shim: abd::lutdsp::VoiceAllocator aliases abd::synth's
    static_assert(std::is_same_v<abd::lutdsp::VoiceAllocator<4>, abd::synth::VoiceAllocator<4>>,
                  "LutDSP shim must alias the SynthCore canonical allocator");
    abd::lutdsp::VoiceAllocator<4> compat;
    compat.setPolyMode(abd::lutdsp::PolyMode::Unison);
    auto cu = compat.allocateNoteOn(60, 1.0f);
    check(cu.size() == 4, "abd::lutdsp compat shim operational");

    // ── MS2000-style steal ladder (generalized policy, voice-agnostic hints) ──
    {
        VoiceAllocator<4> ladder;
        ladder.setStealPolicy(StealPolicy::Ms2000Ladder);
        check(ladder.getStealPolicy() == StealPolicy::Ms2000Ladder, "steal policy configurable");
        check(VoiceAllocator<4>().getStealPolicy() == StealPolicy::LegacyTimestamp,
              "default policy stays LegacyTimestamp");

        // Engine state: v0 held(40), v1 latched(41, key up), v2 releasing(42),
        // v3 held(43) with the oldest trigger stamp.
        StealHint hints[4] = {
            {0, 40, true, true, false, false, 0.9f, 10},
            {1, 41, true, false, true, false, 0.8f, 11},
            {2, 42, true, false, false, true, 0.3f, 12},
            {3, 43, true, true, false, false, 0.9f, 8},
        };

        // 1. Repeated Note Protection: re-trigger the slot already playing the note
        int v = ladder.findVoiceToSteal(hints, 4, 40);
        check(v == 0, "ladder: repeated note re-triggers its slot");

        // 3. Latch stealing: sounding via HOLD with key released
        v = ladder.findVoiceToSteal(hints, 4, 50);
        check(v == 1, "ladder: latched key-up voice stolen first");

        // 4. Release-phase stealing: releasing voice closest to silence
        hints[1] = {1, 41, true, true, false, false, 0.8f, 11};
        v        = ladder.findVoiceToSteal(hints, 4, 50);
        check(v == 2, "ladder: release-phase (lowest amp) stolen next");

        // 5. FIFO sustain stealing: oldest key-held note
        hints[2] = {2, 42, true, true, false, false, 0.9f, 12};
        v        = ladder.findVoiceToSteal(hints, 4, 50);
        check(v == 3, "ladder: FIFO steals oldest held note");

        // 6. Fallback: after all held, FIFO would always win; the RR fallback is
        //    the guaranteed-slot net. Exercise it via the real engine flow: pick,
        //    apply, commitAllocation, then rebuild hints from live engine state.
        hints[3] = {3, 43, true, true, false, false, 0.9f, 9};
        int v1   = ladder.findVoiceToSteal(hints, 4, 50);
        check(v1 == 3, "fallback flow: FIFO picks oldest held (v3)");
        ladder.commitAllocation(v1, 50);
        hints[3] = {3, 50, true, true, false, false, 0.9f, 13}; // engine state post-commit
        int v2   = ladder.findVoiceToSteal(hints, 4, 51);       // 51: not already playing
        check(v2 == 0, "fallback flow: next pick moves to v0 (oldest stamp rotated)");
        ladder.commitAllocation(v2, 51);

        // Non-mutating: repeated queries return the same decision
        check(ladder.findVoiceToSteal(hints, 4, 51) == 0,
              "ladder query is non-mutating");

        // Hint-free fallback uses internal state (with retrigger protection)
        VoiceAllocator<3> internal;
        internal.setStealPolicy(StealPolicy::Ms2000Ladder);
        internal.allocateNoteOn(60, 1.0f);
        internal.allocateNoteOn(61, 1.0f);
        internal.allocateNoteOn(62, 1.0f);
        check(internal.findVoiceToSteal(nullptr, 0, 61) == 1,
              "hint-free: repeated note protection on internal state");
        check(internal.findVoiceToSteal(nullptr, 0, 70) == 0,
              "hint-free: full polyphony falls back to oldest voice");
    }
}

} // namespace abd::synth::tests

// ─────────────────────────── OscReference (prototipo de referencia de la familia) ───────────────────────────
//
// Este bloque se incluye DESPUES del de la familia porque el contrato usa
// OscVcoCa72 y OscPolyBlep como miembros de referencia. El .inc es
// autocontenido: abre y cierra su propio namespace abd::synth::tests e incluye
// su cabecera a nivel de fichero, asi que va FUERA del namespace de este archivo
// y antes de main(), donde se le llama.
#include "tests/OscReferenceTests.inc"

int main()
{
    printf("=== SynthCore Test Suite ===\n");
    abd::synth::tests::testDSPUtils();
    abd::synth::tests::testPolyBLEP();
    abd::synth::tests::testEnvelope();
    abd::synth::tests::testPortamento();
    abd::synth::tests::testLFO();
    abd::synth::tests::testAudioThreadSnapshot();
    abd::synth::tests::testVoiceAllocator();
    abd::synth::tests::testModMatrixAccumulation();
    abd::synth::tests::testModMatrixBounds();
    abd::synth::tests::testModMatrixGet();
    abd::synth::tests::testModMatrixEquivalenceWithAbdeep();
    abd::synth::tests::testModDestinationDescriptor();
    abd::synth::tests::testModMatrixZeroIdIsNotAlwaysInert();
    abd::synth::tests::testS950PatchFields();
    abd::synth::tests::testS950Disk();
    abd::synth::tests::testS950Calibration();
    abd::synth::tests::testS950EnvelopeBench();
    abd::synth::tests::testS950CalibrationRender();
    abd::synth::tests::testOscillatorFamilyContract();

    abd::synth::tests::testOscHalfbandDecimator();
    abd::synth::tests::testOscVcoCa72Frequency();
    abd::synth::tests::testOscVcoCa72Waveforms();
    abd::synth::tests::testOscVcoCa72BandLimiting();
    abd::synth::tests::testOscPolyBlep();
    abd::synth::tests::testOscillatorFamilyLanguage();

    abd::synth::tests::testOscReferenceContract();
    abd::synth::tests::testOscReferenceLanguage();
    abd::synth::tests::testOscReferenceResetAndProcessBlock();

    abd::synth::tests::testArpeggiatorModes();
    abd::synth::tests::testArpeggiatorNotesAndOctaves();
    abd::synth::tests::testArpeggiatorGateAndZeroAlloc();
    abd::synth::tests::testArpeggiatorRetrigger();

    abd::synth::tests::testControlSequencerBipolarSteps();
    abd::synth::tests::testControlSequencerClockTable();
    abd::synth::tests::testControlSequencerSwingAndSlew();

    abd::synth::tests::testLFOAnalogWaveforms();
    abd::synth::tests::testLFOAnalogRateRange();
    abd::synth::tests::testLFOAnalogDelayFade();

    abd::synth::tests::testEnvelopeAnalogCurves();
    abd::synth::tests::testEnvelopeAnalogPhases();

    abd::synth::tests::testDriftEngineDeterminism();
    abd::synth::tests::testDriftEngineAmplitudeScaling();

    printf("\n=== Results: %d passed, %d failed ===\n",
           abd::synth::tests::testsPassed, abd::synth::tests::testsFailed);
    return abd::synth::tests::testsFailed == 0 ? 0 : 1;
}
