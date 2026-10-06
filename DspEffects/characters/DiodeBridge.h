/*
  ==============================================================================

    DiodeBridge.h
    Etapa de caracter: puente de diodos (recorte blando con umbral).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Es una ETAPA de caracter inyectable: ver
    DspEffects/EffectPolicy.h.

    QUE ES. La no linealidad de un puente de diodos: por debajo del umbral la
    senal pasa intacta, y por encima se comprime contra un techo. Es lo que
    hace que un ring modulator suene a modulacion y no a multiplicacion pura:
    sin el, el producto de dos senales es una campana infinita de lados que
    cualquier sintetizador digital reproduce con total limpieza y por eso suena
    sintetico.

    SIN ESTADO. A diferencia de TapeColour (que arrastra su siseo), esta etapa
    es una funcion pura: mismo valor de entrada, misma salida. Se puede
    entonces compartir entre el ring modulator de un pedal, el de un IRCAM y
    el de un sintetizador sin que ninguna tenga que saber de las otras.

    POR QUE NO ES `TapeColour`. La curva de la cinta es asimetrica y arrastra
    memoria (el bombeo magnetico); la del puente de diodos es simetrica y no
    arrastra nada. Comparten el `tanh` del sustrato y poco mas: unirlas seria
    guardar conmutadores en una etapa que ninguna de las dos necesita.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "DspEffects/EffectPolicy.h"

namespace abd::dsp
{

//==============================================================================
/**
    Puente de diodos: umbral de paso y techo blando.

    Sin estado y sin suavizado: el consumidor entrega el `drive` y el
    `threshold` de CADA muestra, porque el motor los lee en el lazo de
    muestras y ahi deben entrar.
*/
class DiodeBridge : public CharacterStage
{
public:
    /** Aplica el puente a UNA muestra.

        `drive`    ganancia antes del umbral.
        `threshold` hasta donde pasa recta. SE RECORTA A 0..1, que es el rango
                  documentado, y el recorte es nuevo: no cambia ni un bit a
                  ningun consumidor (el unico que hay, `RingMod`, nunca pasa de
                  0 a 1), y evita lo que hacia un valor mayor.

        Y LO QUE HACE `threshold = 1`, QUE NO ES LO QUE DECIA ANTES. El
        comentario de este metodo decia "1 = transparente", y es falso:
        `1 - threshold` es 0, el `tanh` se multiplica por cero, y la salida es
        exactamente `±1` para cualquier entrada por encima del umbral. MEDIDO:

            x = 2.0 con threshold 0.3 -> 0.998442
            x = 2.0 con threshold 1.0 -> 1.000000   (recorte duro a 1)
            x = 2.0 con threshold 1.5 -> 1.119203   (y AMPLIFICABA el tramo)

        Lo que si es transparente es lo que queda POR DEBAJO del umbral: con
        `threshold = 0.9` una entrada de 0.5 sale 0.5 clavado. Y por encima de 1
        la curva dejaba de ser monótona —subia hasta el umbral y luego bajaba,
        porque `(1 - threshold)` es negativo—, que es una de las razones por las
        que el recorte entra.

        El recorrido util es 0 (recorte puro) a 0.9 (casi transparente), y 1 es
        el extremo de recorte duro. */
    float processSample(float x, float drive = 1.0f, float threshold = 0.3f) noexcept
    {
        const float driven = x * drive;
        const float sign   = driven >= 0.0f ? 1.0f : -1.0f;
        const float mag    = driven >= 0.0f ? driven : -driven;

        const float lim = jlimit(0.0f, 1.0f, threshold);

        if (mag < lim)
            return driven; // todavia lineal: la senal pasa

        const float excess     = mag - lim;
        const float compressed = lim + tanh(excess * 2.0f) * (1.0f - lim);

        return sign * compressed;
    }

    /** Sin color: la etapa puente se puede apagar sin cambiar el motor. */
    void reset() noexcept {}
};

} // namespace abd::dsp