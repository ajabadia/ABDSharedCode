/*
  ==============================================================================

    ArpeggiatorJuno.h
    Motor de arpegiador Roland Juno (Juno-6 / Juno-60) en C++20 puro.
    Modelado analógico del hardware:
      - Curva analógica de velocidad (RC clock: 470nF, 33kΩ + 1MΩ pot).
      - Límite de teclado físico (C1 a C6, notas 36 a 96 con plegado de octava).
      - Modos Up, Up/Down y Down.
      - Rango de 1 a 3 octavas.
      - Sincronización a host DAW por divisiones métricas.
      - Zero-alloc: buffer estático sin asignaciones dinámicas en audio thread.

    UBICACIÓN CANÓNICA: ABDSharedCode/SynthCore (namespace abd::synth).

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace abd::synth
{

//==============================================================================
// Divisiones métricas de sincronización con el host
//==============================================================================
enum class ArpDivision : int
{
    kDiv1 = 0,   // whole note
    kDiv2,       // 1/2 note
    kDiv4,       // 1/4 note
    kDiv4T,      // 1/4 triplet
    kDiv8,       // 1/8 note
    kDiv8T,      // 1/8 triplet
    kDiv16,      // 1/16 note
    kDiv16T,     // 1/16 triplet
    kDiv32,      // 1/32 note
    kNumArpDivisions
};

static constexpr int kNumArpDivisionsCount = static_cast<int>(ArpDivision::kNumArpDivisions);

static constexpr double kArpDivBeats[kNumArpDivisionsCount] = {
    4.0,         // 1/1
    2.0,         // 1/2
    1.0,         // 1/4
    2.0 / 3.0,   // 1/4T
    0.5,         // 1/8
    1.0 / 3.0,   // 1/8T
    0.25,        // 1/16
    1.0 / 6.0,   // 1/16T
    0.125        // 1/32
};

//==============================================================================
/** Motor de arpegiador Roland Juno (zero-alloc, RT-safe) */
class ArpeggiatorJuno
{
public:
    static constexpr int kMaxHeldNotes = 128;
    static constexpr int kMaxArpNote   = 96; // Límite de teclado físico Roland Juno (C7)

    bool    mEnabled       = false;
    int     mMode          = 0;       // 0=Up, 1=Up/Down, 2=Down
    int     mRange         = 0;       // 0=1oct, 1=2oct, 2=3oct
    float   mRate          = 120.f;   // BPM (steps per minute)
    float   mSampleRate    = 44100.f;

    // Estado de sincronización DAW
    bool    mSyncToHost    = false;
    bool    mHostPlaying   = false;
    double  mHostBPM       = 120.0;
    double  mHostBeatPos   = 0.0;
    int     mDivision      = static_cast<int>(ArpDivision::kDiv16);
    int64_t mLastSyncStep  = -1;

    int     mStepIndex     = 0;
    int     mDirection     = 1;       // 1=ascending, -1=descending
    float   mPhase         = 0.f;
    int     mLastNote      = -1;      // actualmente sonando
    std::atomic<uint32_t> mTickCount{0};

    bool    mLimitToKeyboard = true;

    ArpeggiatorJuno() noexcept = default;

    /** Curva de frecuencia analógica derivada del circuito RC del Juno-60 */
    static float arpRate(float t) noexcept
    {
        float pos = 1.f - t;
        float hz = 1.0f / (2.0f * (33000.0f + pos * 1000000.0f) * 0.47e-6f * 0.6633f);
        return hz * 60.f;
    }

    void SetSampleRate(float sr) noexcept { mSampleRate = sr; }

    void NoteOn(int note) noexcept
    {
        if (note < 0 || note > 127) return;

        auto* begin = mHeldNotes.data();
        auto* end   = begin + mHeldCount;
        auto* it    = std::lower_bound(begin, end, note);

        if (it != end && *it == note) return; // ya existe
        if (mHeldCount >= kMaxHeldNotes) return;

        // Inserción ordenada sin heap
        std::move_backward(it, end, end + 1);
        *it = note;
        ++mHeldCount;

        if (mHeldCount == 1)
        {
            mPhase     = 1.f;
            mStepIndex = 0;
            mDirection = 1;
        }
    }

    void NoteOff(int note) noexcept
    {
        auto* begin = mHeldNotes.data();
        auto* end   = begin + mHeldCount;
        auto* it    = std::find(begin, end, note);

        if (it != end)
        {
            std::move(it + 1, end, it);
            --mHeldCount;
        }

        if (mHeldCount == 0)
        {
            mStepIndex = 0;
            mDirection = 1;
            mPhase     = 0.f;
        }
    }

    void Reset() noexcept
    {
        mHeldCount    = 0;
        mStepIndex    = 0;
        mDirection    = 1;
        mPhase        = 0.f;
        mLastNote     = -1;
        mLastSyncStep = -1;
    }

    [[nodiscard]] int SeqLen() const noexcept
    {
        if (mLimitToKeyboard)
            return mHeldCount * (mRange + 1);

        int count = 0;
        int octaves = mRange + 1;
        for (int oct = 0; oct < octaves; ++oct)
        {
            for (int i = 0; i < mHeldCount; ++i)
            {
                if (mHeldNotes[i] + oct * 12 <= 127)
                    count++;
            }
        }
        return count;
    }

    [[nodiscard]] int SeqNote(int idx) const noexcept
    {
        int i = 0;
        int octaves = mRange + 1;
        for (int oct = 0; oct < octaves; ++oct)
        {
            for (int h = 0; h < mHeldCount; ++h)
            {
                int note = mHeldNotes[h] + oct * 12;
                if (mLimitToKeyboard)
                {
                    while (note > kMaxArpNote)
                        note -= 12;
                }
                else
                {
                    if (note > 127) continue;
                }

                if (i == idx) return note;
                i++;
            }
        }
        return -1;
    }

    int NextNote() noexcept
    {
        int len = SeqLen();
        if (len == 0) return -1;

        if (mStepIndex >= len) mStepIndex = 0;
        if (mStepIndex < 0)    mStepIndex = len - 1;

        int note = -1;
        switch (mMode)
        {
            case 0: // Up
                note = SeqNote(mStepIndex);
                mStepIndex = (mStepIndex + 1) % len;
                break;

            case 1: // Up/Down
                note = SeqNote(mStepIndex);
                if (len > 1)
                {
                    mStepIndex += mDirection;
                    if (mStepIndex >= len)
                    {
                        mStepIndex = len - 2;
                        mDirection = -1;
                    }
                    else if (mStepIndex < 0)
                    {
                        mStepIndex = 1;
                        mDirection = 1;
                    }
                }
                break;

            case 2: // Down
                note = SeqNote(len - 1 - mStepIndex);
                mStepIndex = (mStepIndex + 1) % len;
                break;

            default:
                note = SeqNote(0);
                break;
        }
        return note;
    }

    template <typename NoteOnF, typename NoteOffF>
    void Process(int nFrames, NoteOnF&& noteOn, NoteOffF&& noteOff)
    {
        if (!mEnabled || mHeldCount == 0)
        {
            if (mLastNote >= 0)
            {
                noteOff(mLastNote, 0);
                mLastNote = -1;
            }
            mLastSyncStep = -1;
            return;
        }

        if (mSyncToHost)
        {
            int div = std::clamp(mDivision, 0, kNumArpDivisionsCount - 1);
            double divBeats = kArpDivBeats[div];

            if (mHostPlaying)
            {
                double beatsPerSample = mHostBPM / (60.0 * static_cast<double>(mSampleRate));

                for (int s = 0; s < nFrames; ++s)
                {
                    double beatPos = mHostBeatPos + s * beatsPerSample;
                    int64_t stepNow = static_cast<int64_t>(std::floor(beatPos / divBeats));

                    if (stepNow != mLastSyncStep)
                    {
                        mLastSyncStep = stepNow;
                        mTickCount.fetch_add(1, std::memory_order_relaxed);

                        if (mLastNote >= 0)
                            noteOff(mLastNote, s);

                        int note = NextNote();
                        if (note >= 0)
                        {
                            noteOn(note, s);
                            mLastNote = note;
                        }
                    }
                }
                return;
            }

            float syncRate = static_cast<float>(mHostBPM / divBeats);
            float inc = syncRate / (60.f * mSampleRate);

            for (int s = 0; s < nFrames; ++s)
            {
                mPhase += inc;
                if (mPhase >= 1.f)
                {
                    mPhase -= 1.f;
                    mTickCount.fetch_add(1, std::memory_order_relaxed);

                    if (mLastNote >= 0)
                        noteOff(mLastNote, s);

                    int note = NextNote();
                    if (note >= 0)
                    {
                        noteOn(note, s);
                        mLastNote = note;
                    }
                }
            }
            return;
        }

        float inc = mRate / (60.f * mSampleRate);

        for (int s = 0; s < nFrames; ++s)
        {
            mPhase += inc;
            if (mPhase >= 1.f)
            {
                mPhase -= 1.f;
                mTickCount.fetch_add(1, std::memory_order_relaxed);

                if (mLastNote >= 0)
                    noteOff(mLastNote, s);

                int note = NextNote();
                if (note >= 0)
                {
                    noteOn(note, s);
                    mLastNote = note;
                }
            }
        }
    }

    [[nodiscard]] int  getHeldCount() const noexcept { return mHeldCount; }
    [[nodiscard]] bool hasHeldNotes() const noexcept { return mHeldCount > 0; }
    [[nodiscard]] int  getHeldNote(int idx) const noexcept
    {
        if (idx >= 0 && idx < mHeldCount)
            return mHeldNotes[static_cast<size_t>(idx)];
        return -1;
    }

private:
    std::array<int, kMaxHeldNotes> mHeldNotes{};
    int mHeldCount = 0;
};

} // namespace abd::synth
