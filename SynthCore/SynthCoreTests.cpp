// SynthCore unit tests — standalone executable, no JUCE dependency.
// Covers the shared DSP primitives extracted from ABDMS2000 (Phase 2 DRY)
// and the VoiceAllocator converged from LutDSP.
//
// Style mirrors ABDMS2000/Source/Tests/DSPCoreTests.cpp (check() + counters).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "DSPUtils.h"
#include "PolyBLEP.h"
#include "EnvelopeCurves.h"
#include "ADSREnvelope.h"
#include "PortamentoGlide.h"
#include "LFO.h"
#include "AudioThreadSnapshot.h"
#include "VoiceAllocator.h"
// Compat-shim coverage: the LutDSP path must keep aliasing the canonical type.
#include "LutDSP/VoiceAllocator.h"

namespace abd::synth::tests {

static int testsPassed = 0;
static int testsFailed = 0;

static void check(bool condition, const char* testName) {
    if (condition) {
        testsPassed++;
        printf("  [PASS] %s\n", testName);
    } else {
        testsFailed++;
        printf("  [FAIL] %s\n", testName);
    }
    fflush(stdout);
}

// ─────────────────────────── DSPUtils ───────────────────────────

static void testDSPUtils() {
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
    float r1 = DSPUtils::randomBipolar(rng);
    float r2 = DSPUtils::randomBipolar(rng);
    check(r1 >= -1.0f && r1 <= 1.0f, "randomBipolar in [-1,1]");
    check(r1 != r2, "randomBipolar advances state");
}

// ─────────────────────────── PolyBLEP ───────────────────────────

static void testPolyBLEP() {
    printf("=== PolyBLEP ===\n");

    const float dt = 0.05f;
    check(PolyBLEP::getResidual(0.5f, dt) == 0.0f, "residual zero mid-phase");
    check(std::abs(PolyBLEP::getResidual(0.0f, dt) + 1.0f) < 0.0001f, "residual at step start = -1");
    // Quadratic ramp 0 -> +1 across the last dt: midpoint evaluates to 0.25.
    check(std::abs(PolyBLEP::getResidual(1.0f - dt * 0.5f, dt) - 0.25f) < 0.0001f, "residual ramps quadratically before step");

    // Integrated residual stays within sane bounds
    bool bounded = true;
    for (int i = 0; i <= 100; ++i) {
        float t = static_cast<float>(i) / 100.0f;
        float r = PolyBLEP::getResidualIntegrated(t, 0.02f);
        if (std::abs(r) > 0.02f) bounded = false;
    }
    check(bounded, "integrated residual bounded by ~dt");
}

// ─────────────────────────── EnvelopeCurves + ADSREnvelope ───────────────────────────

static void testEnvelope() {
    printf("=== EnvelopeCurves + ADSREnvelope ===\n");

    float at0 = EnvelopeCurves::getAttackTimeSeconds(0.0f);
    float at1 = EnvelopeCurves::getAttackTimeSeconds(1.0f);
    check(std::abs(at0 - 0.0005f) < 0.0001f, "attack min 0.5 ms");
    check(std::abs(at1 - 5.0f) < 0.01f, "attack max 5 s");
    check(EnvelopeCurves::getAttackTimeSeconds(0.1f) < EnvelopeCurves::getAttackTimeSeconds(0.9f), "attack monotonic");

    double multLong = EnvelopeCurves::getDecayMultiplier(10.0, 44100.0);
    double multShort = EnvelopeCurves::getDecayMultiplier(0.002, 44100.0);
    // Per-sample multipliers: long decay ~0.99999, short decay ~0.949
    // (0.949^88.2 samples ~= e^-4.6 ~= 1% remaining at the 2 ms mark).
    check(multLong > 0.9999 && multLong < 1.0, "long decay mult near 1");
    check(multShort > 0.9 && multShort < 0.96, "short decay mult decays fast");
    check(EnvelopeCurves::getDecayMultiplier(1.0, 0.0) == 0.0, "invalid sampleRate -> 0");

    ADSREnvelope env;
    env.prepare(44100.0);
    env.setAttack(0.0f);          // 0.5 ms
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
}

// ─────────────────────────── PortamentoGlide ───────────────────────────

static void testPortamento() {
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

static void testLFO() {
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
    for (int i = 0; i < 44100; ++i) {
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
    for (int i = 0; i < 44100; ++i) {
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
}

// ─────────────────────────── AudioThreadSnapshot ───────────────────────────

static void testAudioThreadSnapshot() {
    printf("=== AudioThreadSnapshot ===\n");

    AudioThreadSnapshot snap;
    check(snap.voiceActive.size() == 32, "32 voice slots");
    check(snap.scopeBuffer.size() == 512, "512-sample scope buffer");
    check(snap.activeVoiceCount == 0 && !snap.isArpActive, "defaults zeroed");
    snap.vuLeft = 0.5f;
    snap.voiceActive[3] = true;
    snap.voiceNote[3] = 64;
    check(snap.voiceActive[3] && snap.voiceNote[3] == 64 && snap.vuLeft == 0.5f, "fields assignable");
}

// ─────────────────────────── VoiceAllocator ───────────────────────────

static void testVoiceAllocator() {
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
            { 0, 40, true, true,  false, false, 0.9f, 10 },
            { 1, 41, true, false, true,  false, 0.8f, 11 },
            { 2, 42, true, false, false, true,  0.3f, 12 },
            { 3, 43, true, true,  false, false, 0.9f, 8  },
        };

        // 1. Repeated Note Protection: re-trigger the slot already playing the note
        int v = ladder.findVoiceToSteal(hints, 4, 40);
        check(v == 0, "ladder: repeated note re-triggers its slot");

        // 3. Latch stealing: sounding via HOLD with key released
        v = ladder.findVoiceToSteal(hints, 4, 50);
        check(v == 1, "ladder: latched key-up voice stolen first");

        // 4. Release-phase stealing: releasing voice closest to silence
        hints[1] = { 1, 41, true, true, false, false, 0.8f, 11 };
        v = ladder.findVoiceToSteal(hints, 4, 50);
        check(v == 2, "ladder: release-phase (lowest amp) stolen next");

        // 5. FIFO sustain stealing: oldest key-held note
        hints[2] = { 2, 42, true, true, false, false, 0.9f, 12 };
        v = ladder.findVoiceToSteal(hints, 4, 50);
        check(v == 3, "ladder: FIFO steals oldest held note");

        // 6. Fallback: after all held, FIFO would always win; the RR fallback is
        //    the guaranteed-slot net. Exercise it via the real engine flow: pick,
        //    apply, commitAllocation, then rebuild hints from live engine state.
        hints[3] = { 3, 43, true, true, false, false, 0.9f, 9 };
        int v1 = ladder.findVoiceToSteal(hints, 4, 50);
        check(v1 == 3, "fallback flow: FIFO picks oldest held (v3)");
        ladder.commitAllocation(v1, 50);
        hints[3] = { 3, 50, true, true, false, false, 0.9f, 13 }; // engine state post-commit
        int v2 = ladder.findVoiceToSteal(hints, 4, 51);           // 51: not already playing
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

int main() {
    printf("=== SynthCore Test Suite ===\n");
    abd::synth::tests::testDSPUtils();
    abd::synth::tests::testPolyBLEP();
    abd::synth::tests::testEnvelope();
    abd::synth::tests::testPortamento();
    abd::synth::tests::testLFO();
    abd::synth::tests::testAudioThreadSnapshot();
    abd::synth::tests::testVoiceAllocator();

    printf("\n=== Results: %d passed, %d failed ===\n",
           abd::synth::tests::testsPassed, abd::synth::tests::testsFailed);
    return abd::synth::tests::testsFailed == 0 ? 0 : 1;
}
