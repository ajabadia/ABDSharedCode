/*
  ==============================================================================

    DspSaturation.h
    Saturacion por soft-clipping (atan): la FORMA del efecto, sin politica de
    parametros.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Vivio en ABDNeural/Source/DSP/Effects/Saturation.h
    hasta la migracion de efectos; ahi queda solo el envoltorio de producto.

    QUE ES ESTE FICHERO (y que NO). El efecto puro es una forma estatica:

        y = atan (x * drive) * 0.63661977236f

    No tiene estado: ni buffer, ni fase, ni suavizado. En el producto su unico
    "estado" era un dsp::LinearSmoothedValue sobre la ganancia de drive, y eso
    es politica de parametros de NEURONiK, asi que se queda alli, junto con el
    mapeo amount -> drive (1.0 + amount * 4.0) y con la puerta de "drive ~ 1.0 =
    bypass", que tambien es politica de producto: la forma NO es la identidad en
    drive = 1.

    A diferencia de DspReverb.h (port literal de juce::Reverb, que arrastra sus
    propios SmoothedValue porque formaban parte del original ajeno), aqui no hay
    original que portar: la aritmetica es la de NEURONiK y se mueve literal, con
    el mismo literal y el mismo orden de operaciones.

    El literal 0.63661977236f NO se "mejora" a 2/PI con mas precision: es el que
    calculaba el efecto antes de la migracion y la paridad bit a bit contra la
    referencia congelada (ABDNeural/Tests/DspEffectsParityTest.cpp) depende de
    el.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"

#include <cmath>

namespace abd::dsp
{

//==============================================================================
/**
    Soft-clipping con atan: la forma del efecto de saturacion, sin estado.

    Es una utilidad estatica a proposito (Saturation::processSample (x, drive)):
    la ganancia de drive entra POR MUESTRA, de modo que cada consumidor conserva
    su propia politica (suavizado, mapeo de parametro, bypass) sin que este
    modulo imponga ninguna.
*/
class Saturation
{
public:
    /** Normalizacion de atan (2/PI), literal historico del efecto. Ver cabecera. */
    static constexpr float atanNormalisation = 0.63661977236f;

    /** Un sample saturado con la ganancia de drive dada. */
    static float processSample (float input, float drive) noexcept
    {
        return dsp::atan (input * drive) * atanNormalisation;
    }

    /** Aplica la saturacion a todo el buffer con una ganancia constante.
        Para ganancias que cambian muestra a muestra (el caso del suavizado de
        parametros) hay que llamar a processSample. */
    static void processBlock (AudioBuffer<float>& buffer, float drive) noexcept
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* samples = buffer.getWritePointer (ch);

            for (int s = 0; s < numSamples; ++s)
                samples[s] = processSample (samples[s], drive);
        }
    }

private:
    Saturation() = delete;   // utilidad estatica: no se instancia
};

} // namespace abd::dsp
