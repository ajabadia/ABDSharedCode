/**
 * @file VoiceAllocator.h
 * @brief Polyphonic voice allocation engine with Poly1 (round-robin), Poly2 (stealing), and Unison spread.
 * @author ABDSynths
 * @date 2026
 *
 * Provides decoupled polyphonic voice lifecycle management.
 * Compatible with custom Voice implementations without hard JUCE GUI dependencies.
 *
 * Canonical home: ABDSharedCode/SynthCore (namespace abd::synth).
 * ABDSharedCode/LutDSP/VoiceAllocator.h re-exports it under abd::lutdsp for
 * historical includes (compat shim).
 *
 * Stealing policy (generalized from ABDMS2000 VoiceManager::findVoiceToSteal):
 * engines can either consume the default legacy ladder (internal state) or feed
 * a per-slot StealHint table each query to activate the full MS2000 ladder
 * (repeated-note protection, latch stealing, release-phase stealing by
 * envelope level, FIFO sustain stealing) without this module knowing anything
 * about the host's Voice type.
 */

#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace abd::synth
{

enum class PolyMode
{
    Poly1 = 0,   // Natural round-robin voice cycling (Juno-106 standard)
    Poly2 = 1,   // Last-note reuse / legato prioritized allocation
    Unison = 2   // All available voices triggered simultaneously
};

struct VoiceState
{
    int midiNote = -1;
    float velocity = 0.0f;
    bool active = false;
    uint32_t noteOnTimestamp = 0;
};

/**
 * Voice-agnostic per-slot observation fed by the host engine (audio thread)
 * right before querying findVoiceToSteal(). Mirrors the data MS2000's
 * VoiceManager reads from its VoiceSlotState + Voice objects:
 *
 *  - isKeyHeld     : key is physically pressed (slotStates_.isKeyPressed)
 *  - isLatched     : sounding but sustained by HOLD/pedal (isHeldByLatch)
 *  - isReleasing   : envelope in release phase (Voice::isInRelease)
 *  - envelopeLevel : current VCA amplitude (Voice::getCurrentAmpLevel)
 *  - triggerStamp  : monotonic note-on counter (slotStates_.noteOnTime)
 */
struct StealHint
{
    int slotIndex = -1;
    int midiNote = -1;
    bool isVoiceActive = false;
    bool isKeyHeld = false;
    bool isLatched = false;
    bool isReleasing = false;
    float envelopeLevel = 0.0f;
    uint32_t triggerStamp = 0;
};

enum class StealPolicy : uint8_t
{
    LegacyTimestamp = 0, // repeated-note retrigger, free-from-RR, oldest timestamp
    Ms2000Ladder         // full ladder; complete only when hints are supplied
};

template <size_t MaxVoices = 8>
class VoiceAllocator
{
public:
    VoiceAllocator()
    {
        reset();
    }

    void reset() noexcept
    {
        for (size_t i = 0; i < MaxVoices; ++i)
        {
            voices[i] = VoiceState{};
        }
        roundRobinIndex = 0;
        timestampCounter = 0;
        mode = PolyMode::Poly1;
        policy = StealPolicy::LegacyTimestamp;
    }

    void setPolyMode(PolyMode newMode) noexcept { mode = newMode; }
    PolyMode getPolyMode() const noexcept { return mode; }

    void setStealPolicy(StealPolicy newPolicy) noexcept { policy = newPolicy; }
    StealPolicy getStealPolicy() const noexcept { return policy; }

    /**
     * @brief Allocates voice(s) for a Note-On event.
     * @return List of allocated voice indices.
     */
    std::vector<int> allocateNoteOn(int midiNote, float velocity)
    {
        std::vector<int> allocated;
        ++timestampCounter;

        if (mode == PolyMode::Unison)
        {
            for (size_t i = 0; i < MaxVoices; ++i)
            {
                voices[i].midiNote = midiNote;
                voices[i].velocity = velocity;
                voices[i].active = true;
                voices[i].noteOnTimestamp = timestampCounter;
                allocated.push_back(static_cast<int>(i));
            }
            return allocated;
        }

        if (mode == PolyMode::Poly2)
        {
            // Poly2: Check if same note is already playing to re-trigger it
            for (size_t i = 0; i < MaxVoices; ++i)
            {
                if (voices[i].active && voices[i].midiNote == midiNote)
                {
                    voices[i].velocity = velocity;
                    voices[i].noteOnTimestamp = timestampCounter;
                    allocated.push_back(static_cast<int>(i));
                    return allocated;
                }
            }
        }

        // 1. Pick a slot (ladder depends on the configured policy)
        int chosen = findVoiceToStealInternal(midiNote);

        // 2. Round-robin bookkeeping: advance the cursor on free-voice pickup
        //    and on steals; a repeated-note retrigger keeps the cursor.
        if (chosen >= 0)
        {
            const auto& st = voices[static_cast<size_t>(chosen)];
            const bool isRetrigger = st.active && st.midiNote == midiNote;
            if (!isRetrigger)
                roundRobinIndex = (static_cast<size_t>(chosen) + 1) % MaxVoices;
        }

        if (chosen >= 0)
        {
            voices[chosen].midiNote = midiNote;
            voices[chosen].velocity = velocity;
            voices[chosen].active = true;
            voices[chosen].noteOnTimestamp = timestampCounter;
            allocated.push_back(chosen);
        }

        return allocated;
    }

    /**
     * @brief Deallocates voice(s) matching a Note-Off event.
     * @return List of voice indices that were released.
     */
    std::vector<int> allocateNoteOff(int midiNote)
    {
        std::vector<int> released;
        for (size_t i = 0; i < MaxVoices; ++i)
        {
            if (voices[i].active && voices[i].midiNote == midiNote)
            {
                voices[i].active = false;
                released.push_back(static_cast<int>(i));
            }
        }
        return released;
    }

    void allNotesOff() noexcept
    {
        for (size_t i = 0; i < MaxVoices; ++i)
        {
            voices[i].active = false;
            voices[i].midiNote = -1;
        }
    }

    size_t getNumActiveVoices() const noexcept
    {
        size_t count = 0;
        for (size_t i = 0; i < MaxVoices; ++i)
        {
            if (voices[i].active) ++count;
        }
        return count;
    }

    const VoiceState& getVoiceState(size_t index) const noexcept
    {
        static const VoiceState emptyState;
        if (index >= MaxVoices) return emptyState;
        return voices[index];
    }

    //==========================================================================
    // Steal decision
    //==========================================================================

    /**
     * @brief Legacy ladder over internal state (pre-generalization behavior):
     *        free voice starting from the round-robin cursor, else the oldest
     *        active voice. Non-mutating: round-robin bookkeeping moved to
     *        allocateNoteOn.
     */
    int findVoiceToSteal() const noexcept
    {
        return findVoiceToStealInternal(-1);
    }

    /**
     * @brief MS2000-grade ladder over a host-supplied StealHint table.
     *
     * Non-mutating; the engine applies the returned slot itself. Ladder
     * (mirrors ABDMS2000 VoiceManager::findVoiceToSteal):
     *   1. Repeated Note Protection — re-trigger the slot already playing the note
     *   2. Free voice               — first inactive slot (hint order)
     *   3. Latch stealing           — sounding via HOLD/pedal with key released
     *   4. Release-phase stealing   — releasing or key-up slot closest to silence
     *   5. FIFO sustain stealing    — oldest triggerStamp among key-held slots
     *   6. Fallback                 — first active slot from the RR cursor
     *
     * @param hints       Host-refreshed table (slotIndex maps into this allocator).
     * @param hintCount   Number of entries (entries with slotIndex >= MaxVoices are ignored).
     * @param incomingNote MIDI note being allocated.
     */
    int findVoiceToSteal(const StealHint* hints, size_t hintCount, int incomingNote) const noexcept
    {
        if (hints == nullptr || hintCount == 0)
            return findVoiceToStealInternal(incomingNote);

        // 1. Repeated Note Protection (re-trigger same voice)
        for (size_t h = 0; h < hintCount; ++h)
        {
            const StealHint& s = hints[h];
            if (!validSlot(s) || !s.isVoiceActive) continue;
            if (s.midiNote == incomingNote)
                return s.slotIndex;
        }

        // 2. Free voice search (completely idle)
        for (size_t h = 0; h < hintCount; ++h)
        {
            const StealHint& s = hints[h];
            if (!validSlot(s)) continue;
            if (!s.isVoiceActive)
                return s.slotIndex;
        }

        // 3. Steal voice latched by HOLD/pedal whose key was physically released
        for (size_t h = 0; h < hintCount; ++h)
        {
            const StealHint& s = hints[h];
            if (!validSlot(s) || !s.isVoiceActive) continue;
            if (s.isLatched && !s.isKeyHeld)
                return s.slotIndex;
        }

        // 4. Release-phase stealing: releasing or key-up voice closest to silence
        int bestSlot = -1;
        float lowestAmp = 9999.0f;
        for (size_t h = 0; h < hintCount; ++h)
        {
            const StealHint& s = hints[h];
            if (!validSlot(s) || !s.isVoiceActive) continue;
            if (s.isReleasing || !s.isKeyHeld)
            {
                if (s.envelopeLevel < lowestAmp)
                {
                    lowestAmp = s.envelopeLevel;
                    bestSlot = s.slotIndex;
                }
            }
        }
        if (bestSlot != -1)
            return bestSlot;

        // 5. FIFO sustain stealing: oldest held note
        uint32_t oldestStamp = UINT32_MAX;
        bestSlot = -1;
        for (size_t h = 0; h < hintCount; ++h)
        {
            const StealHint& s = hints[h];
            if (!validSlot(s) || !s.isVoiceActive || !s.isKeyHeld) continue;
            if (s.triggerStamp < oldestStamp)
            {
                oldestStamp = s.triggerStamp;
                bestSlot = s.slotIndex;
            }
        }
        if (bestSlot != -1)
            return bestSlot;

        // 6. Fallback: first active slot from the round-robin cursor
        for (size_t offset = 0; offset < MaxVoices; ++offset)
        {
            size_t idx = (roundRobinIndex + offset) % MaxVoices;
            if (voices[idx].active)
                return static_cast<int>(idx);
        }
        return 0;
    }

    //==========================================================================
    // Host-driven state sync (hints-driven engines)
    //==========================================================================

    /**
     * @brief Records a note-on the host applied to a slot chosen via
     *        findVoiceToSteal(hints). Keeps the shadow state, trigger stamps
     *        and round-robin cursor in sync without re-running the ladder.
     *        Call once per applied note-on.
     */
    void commitAllocation(int slotIndex, int midiNote) noexcept
    {
        if (slotIndex < 0 || slotIndex >= static_cast<int>(MaxVoices)) return;
        ++timestampCounter;
        const auto& st = voices[static_cast<size_t>(slotIndex)];
        const bool isRetrigger = st.active && st.midiNote == midiNote;
        voices[static_cast<size_t>(slotIndex)].midiNote = midiNote;
        voices[static_cast<size_t>(slotIndex)].active = true;
        voices[static_cast<size_t>(slotIndex)].noteOnTimestamp = timestampCounter;
        if (!isRetrigger)
            roundRobinIndex = (static_cast<size_t>(slotIndex) + 1) % MaxVoices;
    }

    /**
     * @brief Marks a slot as released on the shadow state (host applied a
     *        note-off / force-stop). Keeps the free-voice tier honest.
     */
    void markSlotReleased(int slotIndex) noexcept
    {
        if (slotIndex < 0 || slotIndex >= static_cast<int>(MaxVoices)) return;
        voices[static_cast<size_t>(slotIndex)].active = false;
        voices[static_cast<size_t>(slotIndex)].midiNote = -1;
    }

private:
    /**
     * @brief Internal-state ladder. incomingNote < 0 disables repeated-note
     *        protection (legacy entry points); with Ms2000Ladder it is enabled.
     */
    int findVoiceToStealInternal(int incomingNote) const noexcept
    {
        // Repeated Note Protection (only meaningful with the MS2000 ladder and
        // a concrete incoming note)
        if (incomingNote >= 0 && policy == StealPolicy::Ms2000Ladder)
        {
            for (size_t i = 0; i < MaxVoices; ++i)
            {
                if (voices[i].active && voices[i].midiNote == incomingNote)
                    return static_cast<int>(i);
            }
        }

        // Free voice search (completely idle), starting from the RR cursor
        for (size_t offset = 0; offset < MaxVoices; ++offset)
        {
            size_t idx = (roundRobinIndex + offset) % MaxVoices;
            if (!voices[idx].active)
                return static_cast<int>(idx);
        }

        // Oldest active voice (smallest timestamp)
        int oldestVoice = 0;
        uint32_t oldestTs = UINT32_MAX;
        for (size_t i = 0; i < MaxVoices; ++i)
        {
            if (voices[i].noteOnTimestamp < oldestTs)
            {
                oldestTs = voices[i].noteOnTimestamp;
                oldestVoice = static_cast<int>(i);
            }
        }
        return oldestVoice;
    }

    static bool validSlot(const StealHint& s) noexcept
    {
        return s.slotIndex >= 0 && static_cast<size_t>(s.slotIndex) < MaxVoices;
    }

    std::array<VoiceState, MaxVoices> voices;
    PolyMode mode = PolyMode::Poly1;
    StealPolicy policy = StealPolicy::LegacyTimestamp;
    size_t roundRobinIndex = 0;
    uint32_t timestampCounter = 0;
};

} // namespace abd::synth
