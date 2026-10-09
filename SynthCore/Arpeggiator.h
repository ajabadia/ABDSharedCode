#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace abd::synth
{
/**
 * @brief Motor de arpegiador determinista y portable en C++20.
 * Núcleo algorítmico agnóstico de framework sin dependencias de GUI ni de JUCE.
 */
class Arpeggiator
{
public:
    /** Nota retenida por el arpegiador: número MIDI y velocidad con que se tocó. */
    struct HeldNote
    {
        int note       = 0;
        float velocity = 0.0f;
    };

    /** Evento de nota emitido por el arpegiador (agnóstico de framework). */
    struct NoteEvent
    {
        int sampleOffset = 0;
        int note         = 0;
        float velocity   = 0.0f;
        bool isNoteOn    = false;
    };

    /** Generador pseudoaleatorio determinista portátil (Xorshift32). */
    struct FastRng
    {
        std::uint32_t state = 0x12345678;

        void setSeed(std::uint32_t seed) noexcept { state = seed != 0 ? seed : 0x12345678; }

        std::uint32_t next() noexcept
        {
            std::uint32_t x = state;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            return state = x;
        }

        int nextInt(int maxExclusive) noexcept
        {
            return maxExclusive > 0 ? static_cast<int>(next() % static_cast<std::uint32_t>(maxExclusive)) : 0;
        }
    };

    Arpeggiator() = default;

    void prepare(double sampleRate);

    // --- Configuración y parámetros de panel -----------------------------

    void setEnabled(bool on) { enabled = on; }
    void setMode(int mode) { modeIndex = std::clamp(mode, 0, 10); }
    /** Tasa de pulsos por segundo ya dividida por el divisor de reloj. */
    void setStepRateHz(float hz) { stepRateHz = std::clamp(hz, 0.01f, 500.0f); }
    /** Fracción del paso que dura la nota: 0 = muda, 1 = paso entero. */
    void setGate(float gate) { gateFraction = std::clamp(gate, 0.0f, 1.0f); }
    void setHold(bool on) { hold = on; }
    void setKeySync(bool on) { keySync = on; }
    /** Rango de octavas: 1 a 6. */
    void setOctaves(int octaves) { octaveCount = std::clamp(octaves, 1, 6); }
    /** Swing normalizado 0..1: 0 = recto (50%), 1 = máximo (75%). */
    void setSwing(float swing) { swingAmount = std::clamp(swing, 0.0f, 1.0f); }
    /** Semilla determinista para el modo Random (8). */
    void setRandomSeed(std::uint32_t s) { rng.setSeed(s); }

    // --- Gestión de notas de entrada ------------------------------------

    void noteOn(int note, float velocity);
    void noteOff(int note);
    void allNotesOff();

    bool isActive() const { return enabled && heldCount > 0; }
    bool isEnabled() const { return enabled; }
    int getMode() const { return modeIndex; }
    int getHeldCount() const { return heldCount; }
    float getGate() const { return gateFraction; }

    /**
     * Emite note-on y note-off de este bloque a un callback arbitrario (Zero-Alloc, sample-accurate).
     */
    template <typename EventConsumer>
    void generate(int numSamples, EventConsumer&& consumeEvent)
    {
        if (numSamples <= 0)
            return;

        // Primero lo pendiente del bloque anterior: si cae aquí, se emite, y si
        // no, se descuenta y sigue esperando.
        int quedan = 0;
        for (int i = 0; i < pendingCount; ++i)
        {
            pending[i].samplesLeft -= numSamples;
            if (pending[i].samplesLeft >= 0)
            {
                pending[quedan++] = pending[i];
            }
            else
            {
                const int sample = numSamples + pending[i].samplesLeft;
                consumeEvent(NoteEvent{sample, pending[i].note, 0.0f, false});
                for (int k = 0; k < soundingCount; ++k)
                {
                    if (sounding[k] == pending[i].note)
                    {
                        sounding[k] = sounding[soundingCount - 1];
                        --soundingCount;
                        break;
                    }
                }
            }
        }
        pendingCount = quedan;

        if (!enabled || heldCount == 0)
        {
            if (soundingCount > 0)
                apagarTodo(consumeEvent, 0);
            if (heldCount == 0)
                resetStepCounter();
            return;
        }

        stepSamples = sampleRate / stepRateHz;

        // El swing reparte el paso entre las dos notas de la pareja: una dura la
        // parte larga y la otra la corta, y las dos suman un paso entero. Con
        // swing 0 las dos mitades valen 0,5.
        const double r = 0.5 + 0.25 * static_cast<double>(swingAmount);

        // LAS MARCAS DE PASO
        static constexpr double kRedondeo = 1e-9;

        int pos         = 0;
        double restante = samplesToNextStep;

        while (restante <= static_cast<double>(numSamples - pos))
        {
            // La marca cae dentro de este bloque (o justo en su borde).
            const double suelo = std::floor(restante + kRedondeo);
            pos += static_cast<int>(suelo);
            restante -= suelo;

            HeldNote notas[kMaxNotesPerStep];
            const int cuantas = fillStepNotes(stepIndex, notas, kMaxNotesPerStep);

            // Lo que dura ESTE paso: par si el índice es par, impar si no.
            const double duracion = stepSamples * ((stepIndex % 2 == 0) ? r : (2.0 - r));

            for (int i = 0; i < cuantas; ++i)
            {
                // Si la misma nota sigue sonando, se apaga antes: es un
                // retrigger, no una voz superpuesta. Se QUITA de la lista:
                // la que queda sonando es la nueva, y el flush final apaga
                // exactamente una vez lo que realmente suena (sin repetidos).
                for (int k = 0; k < soundingCount; ++k)
                {
                    if (sounding[k] == notas[i].note)
                    {
                        consumeEvent(NoteEvent{pos, notas[i].note, 0.0f, false});
                        sounding[k] = sounding[soundingCount - 1];
                        --soundingCount;
                        break;
                    }
                }

                consumeEvent(NoteEvent{pos, notas[i].note, notas[i].velocity, true});

                if (soundingCount < kMaxNotesPerStep)
                    sounding[soundingCount++] = notas[i].note;

                // El gate es la fracción del paso que dura la nota. Con 0 no se
                // programa ningún apagado (0 = silence).
                if (gateFraction > 0.0f && pendingCount < kMaxPending)
                {
                    pending[pendingCount++] =
                        {pos + static_cast<int>(std::lround(duracion * gateFraction)), notas[i].note};
                }
            }

            restante = duracion;
            ++stepIndex;
        }

        samplesToNextStep = restante - static_cast<double>(numSamples - pos);
    }

private:
    static constexpr int kMaxNotesPerStep = 8;
    static constexpr int kMaxHeld         = 64;

    void buildPool();
    int fillStepNotes(int step, HeldNote* out, int maxNotes);

    template <typename EventConsumer>
    void apagarTodo(EventConsumer&& consumeEvent, int sample)
    {
        for (int i = 0; i < pendingCount; ++i)
            consumeEvent(NoteEvent{sample, pending[i].note, 0.0f, false});
        pendingCount = 0;

        for (int i = 0; i < soundingCount; ++i)
            consumeEvent(NoteEvent{sample, sounding[i], 0.0f, false});
        soundingCount = 0;
    }

    void resetStepCounter();

    bool enabled = false;
    bool hold    = false;
    bool keySync = false;

    int modeIndex      = 0;
    int octaveCount    = 1;
    float stepRateHz   = 8.0f;
    float gateFraction = 0.5f;
    float swingAmount  = 0.0f;

    double sampleRate        = 44100.0;
    double stepSamples       = 5512.5;
    double samplesToNextStep = 0.0;
    int stepIndex            = 0;

    HeldNote held[kMaxHeld];
    int heldCount = 0;

    HeldNote playOrder[kMaxHeld];
    int playCount = 0;

    HeldNote pool[kMaxHeld * 6];
    int poolCount = 0;

    struct PendingOff
    {
        int samplesLeft;
        int note;
    };
    static constexpr int kMaxPending = 32;
    PendingOff pending[kMaxPending];
    int pendingCount = 0;

    int sounding[kMaxNotesPerStep];
    int soundingCount = 0;

    FastRng rng;
};
} // namespace abd::synth
