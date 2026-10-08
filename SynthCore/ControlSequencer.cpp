#include "ControlSequencer.h"
#include <algorithm>
#include <cmath>

namespace abd::synth
{
    //==============================================================================
    /**
     * NEGRAS POR PASO de cada divisor de reloj del manual.
     *
     * La tabla del manual da el valor como razón en notas enteras --"Four whole
     * notes", "Sixteenth note"-- y aquí se convierte a negras, que es la unidad
     * con la que se trabaja: una negra dura 60/BPM segundos, y un paso de R
     * notas enteras son 4R negras.
     *
     * SOLO LOS DIECISÉIS PRIMEROS: el rango de control NRPN es 0-15.
     */
    float ControlSequencer::quartersPerStep(int div)
    {
        static const float quarters[16] = {
            16.0f,       // 0   4 notas enteras
            12.0f,       // 1   3 notas enteras
            8.0f,        // 2   2 notas enteras
            4.0f,        // 3   1 nota entera
            2.0f,        // 4   1/2
            1.5f,        // 5   3/8 (negra con puntillo)
            4.0f / 3.0f, // 6   1/3
            1.0f,        // 7   1/4
            0.75f,       // 8   3/16
            2.0f / 3.0f, // 9   1/6
            0.5f,        // 10  1/8
            0.375f,      // 11  3/32
            1.0f / 3.0f, // 12  1/12
            0.25f,       // 13  1/16 (el valor por defecto)
            3.0f / 16.0f,// 14  3/64
            1.0f / 6.0f  // 15  1/24
        };
        return quarters[div < 0 ? 0 : (div > 15 ? 15 : div)];
    }

    //==============================================================================
    void ControlSequencer::prepare(double rate)
    {
        sampleRate = rate > 0.0 ? rate : 44100.0;
        phase = 0.0;
        stepIndex = 0;
        corrio = false;
        targetValue = steps[0];
        currentValue = steps[0];
        samplesPerStep = quartersPerStep(clockDivider) * 60.0 / masterBpm * sampleRate;
    }

    //==============================================================================
    void ControlSequencer::setStep(int stepOneBased, float bipolar)
    {
        if (stepOneBased < 1 || stepOneBased > 32)
            return;
        steps[stepOneBased - 1] = std::clamp(bipolar, -1.0f, 1.0f);

        // Editar el paso actual actualiza inmediatamente targetValue
        if (stepOneBased - 1 == stepIndex)
            targetValue = steps[stepIndex];
    }

    //==============================================================================
    void ControlSequencer::reset()
    {
        stepIndex = 0;
        phase = 0.0;
        corrio = false;
        targetValue = steps[0];
    }

    //==============================================================================
    float ControlSequencer::nextSample()
    {
        if (!enabled)
        {
            currentValue = 0.0f;
            targetValue = 0.0f;
            return 0.0f;
        }

        const double duracionBase = quartersPerStep(clockDivider) * 60.0 / masterBpm * sampleRate;

        // El swing reparte el paso: una mitad larga y otra corta que suman el
        // paso entero. Con 0 las dos valen la mitad.
        const double r = 0.5 + 0.25 * static_cast<double>(swingAmount);
        const double duracion = duracionBase * ((stepIndex % 2 == 0) ? r : (2.0 - r));

        phase += 1.0;
        if (phase >= duracion)
        {
            phase = 0.0;
            ++stepIndex;

            if (stepIndex >= length)
            {
                // Modo 1 del manual: "Key Sync On -- restarts on key press, does NOT loop"
                if (keyLoopMode == 1 && corrio)
                {
                    targetValue = 0.0f;
                }
                else
                {
                    stepIndex = 0;
                }
                corrio = true;
            }

            targetValue = steps[stepIndex];
        }

        // El slew suma el mando y la modulación de la matriz antes de recortar
        const float s = std::clamp(slewRate + slewMod, 0.0f, 1.0f);

        if (s <= 0.0f)
        {
            currentValue = targetValue;
        }
        else
        {
            const float tau = s * 1.0f; // segundos
            const float coef = 1.0f - std::exp(-1.0f / (tau * static_cast<float>(sampleRate)));
            currentValue += (targetValue - currentValue) * coef;
        }

        return currentValue;
    }
}
