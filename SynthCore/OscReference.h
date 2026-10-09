/*
  ==============================================================================

    OscReference.h
    Un oscilador de referencia para la familia de osciladores (namespace abd::synth).

    QUE ES. El miembro de REFERENCIA de la familia: el esqueleto del contrato que
    cualquier miembro nuevo debe poder compilar, declarar y probar, con el idioma
    de la familia (Hz, formas BASICAS, ancho 0..1 con el mismo significado, salida
    bipolar +-1) y sin innovar en la interfaz.

    Es lo MAS SIMPLE que resulta de la familia, no lo mas poderoso ni lo mas que
    suena bien: su trabajo es ser el ejemplo que el compilador verifica y que luego
    otro miembro pueda leer antes de escribir su propio nucleo. Por eso elige dos
    formas para empezar (diente de sierra y seno) y dice explicitamente cuales no
    sabe hacer todavia (triangulo y rectangulo): el contracto de la familia es que
    NO mienta, y este prototipo lo ejemplifica.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::SynthCore). La
    familia a la que pertenece vive en OscillatorFamily.h, que va ANTES que este
    fichero en cualquier unidad que lo incluya porque aqui se usan sus formas, su
    idioma de parametros y su tope de ancho de pulso.

    CONTRATO DE LLAMADA. prepare() una vez (y al cambiar la frecuencia de muestreo),
    reset() para que una nota no arranque con la cola de la anterior, y despues
    processSample() por muestra. Todo noexcept, sin asignaciones y sin ramificar en
    el camino caliente mas alla de la forma que este puesta.

  =============================================================================
*/

#pragma once

// La familia ANTES: sus formas (OscWaveform), su idioma (oscClampFrequencyHz,
// oscClampPulseWidth) y su tipo (OscKind) son el vocabulario de este fichero.
#include "OscillatorFamily.h"

#include <cmath>

namespace abd::synth
{

//==============================================================================
/** Un oscilador de referencia minimal pero completo de la familia.

    Acumulador de fase como todos los de la familia, pero deliberadamente limitado:
    solo diente de sierra y seno en esta version del prototipo, y ningun
    sobremuestreo (el factor 1 es el unico que honra, y lo dice en vez de
    aceptarlo y no usarlo). El triangulo y el rectangulo le faltan por escribir;
    que los rechace en vez de dibujarlos es el contrato.

    Su salida es bipolar y cabe en +-oscNominalPeak, sin recorte.

    @tags{Audio}
*/
class OscReference
{
public:
    //==========================================================================
    /** De que esta hecho, para el panel y el diagnostico. */
    static constexpr OscKind kind = OscKind::Phase;

    /** Las formas que este miembro sabe hacer EN ESTA VERSION DEL PROTOTIPO. El
        prototipo elige dos y declara que le falta el resto, que es lo que permite
        que un miembro nuevo de la familia no se quede a medias con un seno
        aproximado. */
    static bool supportsWaveform(OscWaveform waveform) noexcept
    {
        switch (waveform)
        {
            case OscWaveform::Sawtooth:
            case OscWaveform::Sine:
                return true;
            case OscWaveform::Triangle:
            case OscWaveform::Rectangle:
                return false; // le faltan; no dibuja una aproximacion
        }

        return false; // un valor fuera del enumerado no es una forma que se pueda dar
    }

    /** Este miembro del prototipo no sobremuestrea: ya es un oscilador de una
        muestra por muestra, y por eso soporta solo el 1 y lo dice. Un miembro real
        que agregue band limiting propio diria el factor que soporte. */
    static bool supportsOversampling(int factor) noexcept
    {
        return factor == 1;
    }

    //==========================================================================
    /** La frecuencia de muestreo. Sin esto no hay incremento que valga; hay que
        volver a llamarla si cambia. No toca ni la forma ni el ancho de pulso, y
        reaplica el tope de frecuencia de la familia. */
    void prepare(double sampleRate) noexcept
    {
        sampleRateHz = (sampleRate > 0.0) ? sampleRate : 44100.0;
        updateIncrement();
    }

    /** Olvida el pasado: la fase vuelve al origen y el seno con ella, asi que una
        nota no arranca con la cola de la anterior. No toca la forma ni la frecuencia
        ni el ancho, que son ajustes y no estado.

        En el origen cada forma empieza donde su propia definicion la pone: el
        diente de sierra en -1 (camina hacia +1 desde ahi) y el seno en 0. No se
        igualan a proposito: igualarlos seria mover las formas para que el reset
        quede bonito, y lo que se pide de un reset es que sea el mismo siempre. */
    void reset() noexcept
    {
        phase = 0.0;
    }

    /** La frecuencia en Hz, recortada al recorrido de la familia. Se guarda lo que
        se ha PEDIDO, para que un cambio de frecuencia de muestreo vuelva a aplicar
        el tope a lo pedido y no al ultimo recorte. */
    void setFrequency(double hz) noexcept
    {
        requestedHz = oscClampFrequencyHz(hz);
        updateIncrement();
    }

    /** La forma si este miembro la sabe hacer; devuelve `false` y NO cambia nada
        cuando no, porque un oscilador que dice que si y luego dibuja otra cosa es
        peor que uno que dice que no. */
    bool setWaveform(OscWaveform waveform) noexcept
    {
        if (!supportsWaveform(waveform))
            return false;

        currentWaveform = waveform;
        return true;
    }

    /** El factor de sobremuestreo, si este miembro lo honra. Solo el 1: devuelve
        `false` y NO cambia nada con cualquier otro, que es lo que permite pedirlo a
        ciegas a una pieza que venga de otra familia o de esta en otra version. */
    bool setOversampling(int factor) noexcept
    {
        return supportsOversampling(factor);
    }

    /** El ancho de pulso 0..1 de la familia (0.5 = cuadrada). Solo lo usa un
        rectangulo, y este prototipo aun no lo tiene: lo guarda por si llega un
        rectangulo despues, y por eso el idioma del ancho este donde debe estar. */
    void setPulseWidth(double duty01) noexcept
    {
        pulseWidth = oscClampPulseWidth(duty01);
    }

    //==========================================================================
    /** Una muestra. Devuelve la forma que este puesta.

        Un incremento que no se puede dibujar (dt <= 0 o dt >= 1) no se convierte
        en NaN: esta pieza puede acabar sumada en un buffer de host, y un NaN lo
        envenena entero. Por eso sale 0.0 en eso. */
    double processSample() noexcept
    {
        const auto dt = increment;

        if (!(dt > 0.0) || !(dt < 1.0))
            return 0.0;

        const auto t = phase;
        double out;

        switch (currentWaveform)
        {
            case OscWaveform::Sine:
                // El seno es continuo: no hay salto que corregir.
                out = std::sin(twoPi * t);
                break;

            case OscWaveform::Sawtooth:
                // Sube de -1 a +1 y cae de golpe en la fase 0 (un salto de -2).
                // Este prototipo no hace correccion polyBLEP todavia: a 1x y hasta
                // el tope de este miembro la imagen de la rampa alcanza Nyquist
                // (45% de Nyquist, ver maxFrequencyFractionOfNyquist), y por encima
                // el ruido que se mezcla es la razon por la que un miembro real
                // escribe la correccion.
                out = (2.0 * t) - 1.0;
                break;

            case OscWaveform::Triangle:
            case OscWaveform::Rectangle:
                // Le faltan: el prototipo no dibuja una aproximacion.
                // Devuelve lo que tendria el diente de sierra en esta fase, para no
                // dejar el buffer colgado con un NaN o un valor inerte; el test lo
                // comprueba al pedir ese waveform y ver que el miembro no lo hizo.
                out = (2.0 * t) - 1.0;
                break;
        }

        phase += dt;

        if (phase >= 1.0)
            phase -= 1.0;

        return out;
    }

    //==========================================================================
    /** La frecuencia que SE ESTA usando, ya recortada, en Hz. */
    double getFrequency() const noexcept { return frequency; }

    /** La forma que esta puesta. */
    OscWaveform getWaveform() const noexcept { return currentWaveform; }

    /** El factor de sobremuestreo que SE ESTA usando: siempre 1 en este miembro. */
    int getOversampling() const noexcept { return 1; }

    /** El ancho de pulso que SE ESTA usando, 0..1. Este prototipo no lo usa todavia,
        pero el idioma de la familia lo tiene en su lugar. */
    double getPulseWidth() const noexcept { return pulseWidth; }

    /** La fase 0..1: la fase de la muestra que se va a dibujar, para que el panel y
        el disparo de una nota lean lo mismo que se va a oir en la proxima llamada. */
    double getPhase() const noexcept { return phase; }

    /** Lo que esta pieza retrasa la salida: NADA. No hay diezmador ni linea de
        escalon aqui todavia, y el prototipo no inventa un retardo para parecer
        mas completito. */
    double getLatencySamples() const noexcept { return 0.0; }

    //==========================================================================
    /** El tope de frecuencia de este miembro, como fraccion de Nyquist. Por encima,
        la imagen de la rampa se mezcla con la rejilla; este prototipo se queda en
        0.45 de Nyquist, igual que el oscilador de fase existente, y lo declara. */
    static constexpr double maxFrequencyFractionOfNyquist = 0.45;

    /** Un ciclo en radianes, para el seno. Es el mismo numero que
        DSPUtils::TWO_PI (aqui en double), declarado aqui para no arrastrar
        DSPUtils.h entero por una constante. */
    static constexpr double twoPi = 6.283185307179586476925287;

    /** La frecuencia que trae puesta un oscilador recien hecho, antes de que nadie
        le diga otra: el LA de referencia. Una pieza que arrancase a 0 Hz callaria
        y el silencio se confundiria con una averia; y la voz siempre le dice su
        nota, asi que este numero es solo el suelo del banco de pruebas. */
    static constexpr double defaultFrequencyHz = 440.0;

private:
    //==========================================================================
    /** Aplica a lo pedido el tope de la familia y saca el incremento de fase por
        muestra. */
    void updateIncrement() noexcept
    {
        const auto ceiling = maxFrequencyFractionOfNyquist * sampleRateHz * 0.5;

        frequency = (requestedHz > ceiling) ? ceiling : requestedHz;
        increment = frequency / sampleRateHz;
    }

    //==========================================================================
    double sampleRateHz = 44100.0;
    double requestedHz  = defaultFrequencyHz; // lo que se ha pedido (antes de los topes)
    double frequency    = defaultFrequencyHz; // lo que SE ESTA usando
    double increment    = defaultFrequencyHz / sampleRateHz;
    double phase        = 0.0; // 0..1, la fase de la proxima muestra
    double pulseWidth   = 0.5; // 0..1, solo lo usa un rectangulo, que este prototipo aun no tiene
    OscWaveform currentWaveform = OscWaveform::Sawtooth;
};

} // namespace abd::synth
