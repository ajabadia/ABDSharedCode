#include "Arpeggiator.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>

namespace abd::synth
{
//==============================================================================
void Arpeggiator::prepare(double rate)
{
    sampleRate        = rate > 0.0 ? rate : 44100.0;
    stepSamples       = sampleRate / stepRateHz;
    samplesToNextStep = 0.0;
    stepIndex         = 0;
    heldCount         = 0;
    playCount         = 0;
    poolCount         = 0;
    pendingCount      = 0;
    soundingCount     = 0;
}

//==============================================================================
void Arpeggiator::noteOn(int note, float velocity)
{
    // Una nota repetida no duplica la entrada: si el mismo número vuelve a
    // pulsar, lo que se actualiza es la velocidad, no el orden del ciclo.
    for (int i = 0; i < heldCount; ++i)
    {
        if (held[i].note == note)
        {
            held[i].velocity = velocity;
            goto anadida;
        }
    }

    if (heldCount >= kMaxHeld)
        return;

    held[heldCount] = {note, velocity};
    ++heldCount;

anadida:
    playOrder[playCount < kMaxHeld ? playCount : kMaxHeld - 1] = {note, velocity};
    if (playCount < kMaxHeld)
        ++playCount;

    // KEY SYNC: pulsar una tecla reinicia el ciclo al primer paso si está activo.
    if (keySync)
        resetStepCounter();

    buildPool();
}

//==============================================================================
void Arpeggiator::noteOff(int note)
{
    // Con HOLD enclavado, soltar la tecla no quita la nota del ciclo: sigue sonando.
    if (!hold)
    {
        for (int i = 0; i < heldCount; ++i)
        {
            if (held[i].note == note)
            {
                held[i] = held[heldCount - 1];
                --heldCount;
                break;
            }
        }
    }

    for (int i = 0; i < playCount; ++i)
    {
        if (playOrder[i].note == note)
        {
            playOrder[i] = playOrder[playCount - 1];
            --playCount;
            break;
        }
    }

    buildPool();

    if (heldCount == 0)
        resetStepCounter();
}

//==============================================================================
void Arpeggiator::allNotesOff()
{
    // SOLO LA LISTA DE NOTAS. Lo que esté sonando y lo que el gate tenga
    // pendiente NO se toca aquí: los apagados los emite generate() a través
    // de apagarTodo en el bloque siguiente, cuando ve que no queda ninguna
    // nota retenida. Vaciar esos contadores desde aquí sin emitir nada dejaba
    // notas colgadas.
    heldCount = 0;
    playCount = 0;
    poolCount = 0;
    resetStepCounter();
}

//==============================================================================
void Arpeggiator::resetStepCounter()
{
    stepIndex         = 0;
    samplesToNextStep = 0.0;
}

//==============================================================================
void Arpeggiator::buildPool()
{
    poolCount = 0;
    if (heldCount <= 0)
        return;

    HeldNote ordenadas[kMaxHeld];
    std::copy(held, held + heldCount, ordenadas);
    std::sort(ordenadas, ordenadas + heldCount,
              [](const HeldNote& a, const HeldNote& b) { return a.note < b.note; });

    const int cap = static_cast<int>(std::size(pool));
    for (int o = 0; o < octaveCount; ++o)
    {
        for (int i = 0; i < heldCount && poolCount < cap; ++i)
        {
            if (ordenadas[i].note + 12 * o > 127)
                continue; // el byte MIDI no da para octavas por encima de la máxima (127)
            pool[poolCount++] = {ordenadas[i].note + 12 * o, ordenadas[i].velocity};
        }
    }
}

//==============================================================================
int Arpeggiator::fillStepNotes(int step, HeldNote* out, int maxNotes)
{
    const int n = poolCount;
    if (n <= 0)
        return 0;

    const int cap      = std::min(maxNotes, kMaxNotesPerStep);
    const auto delPool = [&](int i) {
        return pool[i];
    };

    // Caso de una sola nota en el pool: todos los modos reproducen la misma nota.
    if (n == 1)
    {
        for (int i = 0; i < cap; ++i)
            out[i] = delPool(0);
        return std::min(n, cap);
    }

    switch (modeIndex)
    {
        case 0:  // Up: de menor a mayor
        case 10: // Chord: todas a la vez
            if (modeIndex == 10)
            {
                const int cuantas = std::min(n, cap);
                for (int i = 0; i < cuantas; ++i)
                    out[i] = delPool(i);
                return cuantas;
            }
            out[0] = delPool(step % n);
            return 1;

        case 1: // Down: de mayor a menor
            out[0] = delPool(n - 1 - (step % n));
            return 1;

        case 2: // Up&Down: sube y baja sin repetir extremos
        {
            const int periodo = 2 * n - 2;
            const int p       = (step % periodo) % periodo;
            out[0]            = delPool(p < n ? p : periodo - p);
            return 1;
        }

        case 3: // Up Inv: sube, y luego el acorde invertido vuelve a subir
        case 5: // Up&Down Inv: sube y baja atravesando inversiones
        {
            const int periodo = 2 * n - 2;
            const int p       = (step % periodo) % periodo;
            const int indice  = (modeIndex == 3) ? (step / periodo) % 2
                                                 : (p < n ? p : periodo - p);
            out[0]            = delPool(indice % n);
            return 1;
        }

        case 4: // Down Inv: baja, y luego la segunda inversión baja
        {
            const int periodo = 2 * n - 2;
            const int p       = (step % periodo) % periodo;
            const int base    = (p < n) ? (n - 1 - p) : (p - n + 1);
            out[0]            = delPool(base % n);
            return 1;
        }

        case 6: // Up Alt: el más bajo, el más alto, alternando
            out[0] = delPool(((step % 2) == 0) ? (step / 2) % n
                                               : (n - 1 - (step / 2) % n));
            return 1;

        case 7: // Down Alt: el más alto, el más bajo, alternando
            out[0] = delPool(((step % 2) == 0) ? (n - 1 - (step / 2) % n)
                                               : (step / 2) % n);
            return 1;

        case 8: // Random: azar determinista vía FastRng
            out[0] = delPool(rng.nextInt(n));
            return 1;

        case 9: // As Played: orden de pulsación
        default: {
            if (playCount <= 0)
            {
                out[0] = delPool(step % n);
                return 1;
            }
            const int i = step % playCount;
            out[0]      = {playOrder[i].note, playOrder[i].velocity};
            return 1;
        }
    }
}
} // namespace abd::synth
