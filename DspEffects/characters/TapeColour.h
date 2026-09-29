/*
  ==============================================================================

    TapeColour.h
    Etapa de caracter: color de cinta (curva de transferencia, siseo y deriva).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Es una ETAPA de caracter inyectable: ver
    DspEffects/EffectPolicy.h para el contrato.

    QUE ES Y QUE NO. Aqui viven las tres cosas que hacen que una maquina de
    cinta suene a cinta: la curva de transferencia asimetrica, el siseo de
    fondo y la deriva del transporte (wow lento y flutter rapido). NO viven
    aqui el retardo, ni los cabezales, ni el tanque, ni el suavizado de los
    controles: eso es el motor (ver DspEffects/MultiHeadEcho.h) o la politica de
    producto del consumidor.

    POR QUE ESTA SEPARADA. La misma etapa sirve para un eco de cinta, para un
    compresor de cinta y para el preamplificador de una maquina, y cada uno la
    usa con intensidades distintas. Pegada dentro del efecto del Space Echo,
    solo la podia usar el Space Echo.

    Y POR QUE LA DERIVA VIVE AQUI Y NO EN EL MOTOR, CUANDO EN UNA TANDA
    ANTERIOR LA HABIA SACADO AL MOTOR. Cambio de criterio, y el motivo esta
    escrito porque es facilisimo volver a equivocarse:

    La deriva depende de la POSICION DE ESCRITURA de la linea, que es estado
    del MOTOR. Pero la forma de la deriva (que osciladores, que amplitud, que
    redondeo) es de la MAQUINA, o sea de la etapa. Repartido al reves —que la
    etapa lleve su propio LFO de fase con su propia cuenta— el motor no puede
    llamarla sin que la deriva se descuadre de la posicion por la que van las
    muestras, y la paridad con el efecto de origen se pierde por un detalle de
    orden de llamadas.

    Asi que la costura queda donde es util: la etapa EVALUA (metodos `wow` y
    `flutter`, que no dependen de nada mas) y el motor APORTA la posicion. Es el
    mismo reparto que la politica de producto del modulo, aplicado a la
    emulacion: la forma va en la etapa, el estado va en el motor.

    EL LCG. `noiseSample` es un LCG de 32 bits (multiplicador 1664525,
    incremento 1013904223) y NO `dsp::Random`, que es de 48 bits. Es a
    proposito: la secuencia de ruido forma parte del SONIDO de la maquina
    emulada, y para poder afirmar paridad bit a bit con un efecto ya existente
    tiene que ser la misma secuencia, no una equivalente. Cambiarla aqui seria
    un cambio de sonido documentado, no una refactorizacion.

    DENORMALES. No se lava nada aqui: `ScopedNoDenormals` es del consumidor
    (cambia el modo de la FPU, no las muestras).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "DspEffects/EffectPolicy.h"

namespace abd::dsp
{

//==============================================================================
/**
    Color de cinta: la curva de transferencia asimetrica, el siseo y la deriva.

    Sin suavizado de parametros: el consumidor entrega los valores de CADA
    muestra, porque el motor los lee en el lazo de muestras y ahi deben entrar.
*/
class TapeColour : public CharacterStage
{
public:
    TapeColour()
    {
        reset();
    }

    /**
        Aplica el color a UNA muestra de la senal de cinta.

        `drive` 0 = TRANSPARENTE (la salida es exactamente la entrada), 1 = la
                curva completa de la cinta. El blend hacia la identidad es lo
                que hace que el mando sea usable: si `drive` 0 aplicase la curva
                igual, no habria forma de apagar el color sin cambiar de etapa.
        `hiss`  amplitud del siseo de fondo (0.15 tipico en una cinta de
                consumo, 0 en un digital). */
    float processSample (float x, float drive = 0.0f, float hiss = 0.0f) noexcept
    {
        const float amount = jlimit (0.0f, 1.0f, drive);
        const float shaped = tapeCurve (x * (1.0f + 3.0f * amount));

        return x + (shaped - x) * amount
             + hiss * noiseSample();
    }

    /** La curva de transferencia de la cinta, sin el siseo.

        Asimetrica al proposito: un latch magnetico satura la excursion positiva
        antes que la negativa, y esa asimetria es buena parte de por que una
        cinta no suena como un limitador. Los dos lados salen de la misma
        funcion para que la curva sea continua en el origen. */
    static float tapeCurve (float x) noexcept
    {
        if (x > 0.0f)
            return tanh (x);              // saturacion mas temprana
        else
            return 0.85f * tanh (x);      // la negativa aguanta algo mas
    }

    /**
        Una muestra del siseo.

        LCG de 32 bits, el mismo que usan las maquinas emuladas de la suite. El
        rango es aproximadamente [-1, 1). Va en la clase y no en `dsp::Random`
        porque la SECUENCIA es parte del sonido emulado (ver la cabecera): con
        otro generador el ruido seria distinto sample a sample y la paridad con
        el efecto de origen seria falsa.
    */
    float noiseSample() noexcept
    {
        seed_ = seed_ * 1664525u + 1013904223u;

        return static_cast<float> (static_cast<int32_t> (seed_)) * kNoiseScale;
    }

    /** El wow: la deriva lenta. Se evalua en la posicion de escritura.

        OJO, Y ESTO HAY QUE DECIRLO aunque el reparto de la cabecera no lo diga:
        `wowAt` y `flutterAt` son LA MISMA FUNCION y NO los llama nadie. MEDIDO:
        `|wowAt - flutterAt|` vale 0.000e+00 en 64 posiciones, y un grep de todo el
        arbol no encuentra ni una llamada. `MultiHeadEcho` genera su propia deriva
        en `advance()` con `Profile::wowHz` y `Profile::flutterAmount`, asi que el
        reparto que describe la cabecera ("la etapa EVALUA y el motor APORTA la
        posicion") no es el que esta implementado: hoy el motor hace las dos
        cosas.

        Se quedan, en vez de borrarse, porque son dos lineas y son el punto de
        extension para un consumidor que quiera la deriva atada a la posicion de
        escritura. Si se borran, esto es lo que hay que decir en su lugar: que
        la deriva es del motor y la etapa no la evalua. */
    float wowAt (float writePosition, float rate, float amplitude) const noexcept
    {
        return amplitude * sin (writePosition * rate);
    }

    /** El flutter: el aleteo rapido. Mismo cuerpo que `wowAt`; ver la nota de
        arriba, que es la misma. */
    float flutterAt (float writePosition, float rate, float amplitude) const noexcept
    {
        return amplitude * sin (writePosition * rate);
    }

    /** Devuelve la etapa al estado inicial (la semilla del siseo). */
    void reset() noexcept
    {
        seed_ = kNoiseSeed;
    }

    /** Cambia la semilla del siseo (varias instancias, o diagnostico). */
    void setNoiseSeed (uint32_t seed) noexcept
    {
        seed_ = seed;
    }

    dspDeclareNonCopyableWithLeakDetector (TapeColour)

private:
    /** 1/2^31: lleva los 32 bits con signo a aproximadamente [-1, 1). */
    static constexpr float kNoiseScale = 1.0f / 2147483648.0f;

    static constexpr uint32_t kNoiseSeed = 0x12345678u;

    uint32_t seed_ = kNoiseSeed;
};

} // namespace abd::dsp
