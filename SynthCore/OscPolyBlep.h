/*
  ==============================================================================

    OscPolyBlep.h
    El miembro de FASE de la familia de osciladores: un acumulador de fase con
    las formas basicas y sus discontinuidades corregidas por polyBLEP
    (namespace abd::synth).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::SynthCore). La
    familia a la que pertenece vive en OscillatorFamily.h, que va ANTES que este
    fichero en cualquier unidad que lo incluya porque aqui se usan sus formas, su
    idioma de parametros y su tope de ancho de pulso.

    QUE ES. El oscilador de fase de toda la vida: un contador que da la vuelta en
    0..1, una forma calculada en esa fase, y las dos o tres discontinuidades que
    tenga corregidas con el residuo polyBLEP del modulo (PolyBLEP.h, que ya
    existia y ya estaba probado). El seno no necesita correccion, el diente de
    sierra corrige su salto, el rectangulo corrige sus DOS flancos, y el
    triangulo se calcula INTEGRANDO su derivada ya banda-limitada.

    POR QUE EL TRIANGULO SALE DE SU DERIVADA, Y NO DEL RESIDUO INTEGRADO. El
    triangulo no tiene saltos de valor: lo que tiene es un cambio de pendiente, y
    corregirlo en el dominio del valor obliga a una segunda integral del residuo
    con su propia normalizacion y su propio signo, que es donde este tipo de
    oscilador se rompe en silencio (suena, pero con la esquina al reves). La
    derivada del triangulo es una cuadrada, y la cuadrada SI tiene saltos de
    valor: se le aplica la MISMA correccion que al rectangulo —el mismo camino ya
    probado, el mismo signo— y se integra. El resultado esta banda-limitado por
    construccion y no hay ninguna constante que ajustar a ojo.

    LO QUE ESTE MIEMBRO NO HACE, DICHO EN VOZ ALTA. No sobresamplea: ya esta
    banda-limitado a 1x, y por eso setOversampling() solo acepta el 1 y lo dice
    (devuelve false y no cambia nada). No tiene tablas: su caracter es el de las
    formas basicas exactas, que es lo que lo hace util como referencia de la
    familia —lo que suena no es un aparato, es la forma que se le pide—. Y no
    recorta la salida: el residuo polyBLEP se pasa unos pocos por ciento del +-1
    justo en las esquinas, y recortarlo ahi seria devolver distorsion por un
    problema de dibujo.

    SU PROPIO TOPE. La familia admite pedir hasta 20 kHz; este acumulador puede
    con casi todo, pero por debajo de Nyquist, y con margen: se queda en
    maxFrequencyFractionOfNyquist (0.45) de Nyquist, porque por encima de eso la
    correccion de un salto (que se extiende un periodo de muestreo a cada lado)
    abarca mas de medio ciclo y deja de corregir nada. getFrequency() devuelve el
    tope si le piden mas: la familia pide que la pieza no diga que si cuando no.

    CONTRATO DE LLAMADA. prepare() una vez (y al cambiar la frecuencia de
    muestreo), reset() para que una nota no arranque con la cola de la anterior, y
    despues processSample() por muestra. Todo noexcept, sin asignaciones y sin
    ramificar en el camino caliente mas alla de la forma que este puesta.

  ==============================================================================
*/

#pragma once

// La familia ANTES: sus formas (OscWaveform), su idioma (oscClampFrequencyHz,
// oscClampPulseWidth) y su tipo (OscKind) son el vocabulario de este fichero.
#include "OscillatorFamily.h"
#include "PolyBLEP.h"

#include <cmath>

namespace abd::synth
{

//==============================================================================
/** Un oscilador de fase con las cuatro formas basicas, miembro de la familia.

    La salida es bipolar y cabe en +-oscNominalPeak (el residuo polyBLEP se pasa
    unos pocos por ciento en las esquinas). El ancho de pulso solo lo usa la
    rectangular y va directo: 0.5 es una cuadrada, y no hay curva de por medio
    porque la fase YA es el parametro con el que se dibuja la forma.

    @tags{Audio}
*/
class OscPolyBlep
{
public:
    //==========================================================================
    /** De que esta hecho, para el panel y el diagnostico: su estado es la fase. */
    static constexpr OscKind kind = OscKind::Phase;

    /** Las cuatro formas de la familia: este miembro las sabe hacer todas. */
    static bool supportsWaveform(OscWaveform waveform) noexcept
    {
        switch (waveform)
        {
            case OscWaveform::Sawtooth:
            case OscWaveform::Triangle:
            case OscWaveform::Rectangle:
            case OscWaveform::Sine:
                return true;
        }

        return false; // un valor fuera del enumerado no es una forma que se pueda dar
    }

    /** Solo el 1: el miembro ya esta banda-limitado a una muestra por muestra y
        no tiene nada que hacer con mas. Lo dice en vez de aceptarlo y no usarlo. */
    static bool supportsOversampling(int factor) noexcept
    {
        return factor == 1;
    }

    //==========================================================================
    /** La frecuencia de muestreo. Sin esto no hay incremento que valga; hay que
        volver a llamarla si cambia. No toca ni la forma ni el ancho de pulso, y
        reaplica el tope de frecuencia (subir fs sube el tope). */
    void prepare(double sampleRate) noexcept
    {
        sampleRateHz = (sampleRate > 0.0) ? sampleRate : 44100.0;
        updateIncrement();
    }

    /** Olvida el pasado: la fase vuelve al origen y el triangulo con ella, asi que
        una nota no arranca con la cola de la anterior. No toca la forma ni la
        frecuencia ni el ancho, que son ajustes y no estado.

        En el origen cada forma empieza donde su propia definicion la pone: el
        diente de sierra y el triangulo en -1 (el triangulo sube desde ahi), la
        rectangular en +1 con un ancho de 0.5, y el seno en 0. No se igualan a
        proposito: igualarlos seria mover las formas para que el reset quede
        bonito, y lo que se pide de un reset es que sea el mismo siempre. */
    void reset() noexcept
    {
        phase            = 0.0;
        triangleIntegral = triangleStartValue;
    }

    /** La frecuencia en Hz, recortada a lo que la familia admite y despues a lo
        que este miembro da (0.45 de Nyquist). Se guarda lo que se ha PEDIDO, para
        que un cambio de frecuencia de muestreo vuelva a aplicar el tope a lo
        pedido y no al ultimo recorte. */
    void setFrequency(double hz) noexcept
    {
        requestedHz = oscClampFrequencyHz(hz);
        updateIncrement();
    }

    /** La forma si es una de las cuatro de la familia; devuelve `false` y NO
        cambia nada cuando no lo es, porque un oscilador que dice que si y luego
        dibuja otra cosa es peor que uno que dice que no. */
    bool setWaveform(OscWaveform waveform) noexcept
    {
        if (!supportsWaveform(waveform))
            return false;

        currentWaveform = waveform;
        return true;
    }

    /** El factor de sobremuestreo, si este miembro lo honra. Solo el 1: devuelve
        `false` y NO cambia nada con cualquier otro, que es lo que permite pedirlo
        a ciegas a una pieza que venga de otra familia. */
    bool setOversampling(int factor) noexcept
    {
        return supportsOversampling(factor);
    }

    /** El ancho de pulso 0..1 de la familia, recortado a lo que la familia admite
        (0.5 = cuadrada). Solo lo usa la forma rectangular; el resto lo guarda para
        cuando se pida esa forma. */
    void setPulseWidth(double duty01) noexcept
    {
        pulseWidth = oscClampPulseWidth(duty01);
    }

    //==========================================================================
    /** Una muestra. Devuelve la forma que este puesta, banda-limitada. */
    double processSample() noexcept
    {
        const auto dt = increment;

        // Un periodo de muestreo que no es un numero positivo y finito no se puede
        // dibujar: ni la fase avanza ni sale una muestra (y sale CERO, no un NaN,
        // porque esta pieza puede acabar dentro de un buffer que se suma).
        if (!(dt > 0.0) || !(dt < 1.0))
            return 0.0;

        const auto t = phase;
        double out;

        switch (currentWaveform)
        {
            case OscWaveform::Triangle: {
                // La derivada del triangulo es la cuadrada +-triangleSlope. Se
                // corrige como el rectangulo —un salto de 2*triangleSlope pide
                // medio, o sea triangleSlope— y se integra.
                double slope = (t < 0.5) ? triangleSlope : -triangleSlope;
                slope += triangleSlope * residual(t, dt);
                slope -= triangleSlope * residual(wrap01(t - 0.5), dt);
                triangleIntegral += slope * dt;
                out = triangleIntegral;
                break;
            }

            case OscWaveform::Rectangle: {
                // Dos flancos: sube en la fase 0 y baja en la fase del ancho. Cada
                // salto vale 2, y su correccion un residuo entero.
                const auto high = (t < pulseWidth) ? 1.0 : -1.0;
                out             = high + residual(t, dt) - residual(wrap01(t - pulseWidth), dt);
                break;
            }

            case OscWaveform::Sine:
                // El seno es continuo: no hay salto que corregir.
                out = std::sin(twoPi * t);
                break;

            case OscWaveform::Sawtooth:
            default:
                // Sube de -1 a +1 y cae de golpe en la fase 0: el salto baja 2, asi
                // que la correccion es restar un residuo.
                out = (2.0 * t) - 1.0 - residual(t, dt);
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

    /** El ancho de pulso que SE ESTA usando, 0..1. */
    double getPulseWidth() const noexcept { return pulseWidth; }

    /** La fase de la muestra que se va a dibujar, 0..1: la que tiene el acumulador
        antes de avanzar, para que el panel y el disparo de una nota lean lo mismo
        que se va a oir en la proxima llamada. */
    double getPhase() const noexcept { return phase; }

    /** Lo que esta pieza retrasa la salida: NADA. La correccion de un salto se
        calcula con la fase de la muestra que se esta dibujando (es no causal
        dentro de la muestra, no un bloque de retardo), y la integral del
        triangulo avanza en el mismo paso que la fase. Se declara para que la
        familia pueda comparar piezas sin adivinar. */
    double getLatencySamples() const noexcept { return 0.0; }

    //==========================================================================
    /** El tope de este miembro, como fraccion de Nyquist. Por encima, la
        correccion de un salto (que se extiende un periodo de muestreo a cada
        lado) abarcaria mas de medio ciclo y no corregiria nada. */
    static constexpr double maxFrequencyFractionOfNyquist = 0.45;

    /** La pendiente de la derivada del triangulo, en valor por unidad de fase: sube
        de -1 a +1 en medio ciclo. */
    static constexpr double triangleSlope = 4.0;

    /** Un ciclo en radianes, para el seno. Es el mismo numero que
        DSPUtils::TWO_PI (aqui en double), declarado aqui para no arrastrar
        DSPUtils.h entero por una constante. */
    static constexpr double twoPi = 6.283185307179586476925287;

    /** Donde arranca la integral del triangulo: su valor en la fase 0, que es el
        fondo del triangulo (el diente de sierra arranca ahi tambien). */
    static constexpr double triangleStartValue = -1.0;

    /** La frecuencia que trae puesta un oscilador recien hecho, antes de que nadie
        le diga otra: el LA de referencia. Una pieza que arrancase a 0 Hz callaria
        y el silencio se confundiria con una averia; y la voz siempre le dice su
        nota, asi que este numero es solo el suelo del banco de pruebas. */
    static constexpr double defaultFrequencyHz = 440.0;

private:
    //==========================================================================
    /** Aplica a lo pedido el tope de la familia y despues el de este miembro, y
        saca el incremento de fase por muestra. */
    void updateIncrement() noexcept
    {
        const auto ceiling = maxFrequencyFractionOfNyquist * sampleRateHz * 0.5;

        frequency = (requestedHz > ceiling) ? ceiling : requestedHz;
        increment = frequency / sampleRateHz;
    }

    /** El residuo polyBLEP del primitivo, en double para no obligar al que llama a
        saber que por dentro es float (es la firma que ya tenia el modulo). */
    static double residual(double t, double dt) noexcept
    {
        return static_cast<double>(PolyBLEP::getResidual(static_cast<float>(t), static_cast<float>(dt)));
    }

    /** Envuelve una fase a 0..1. Un solo renglon porque el incremento nunca llega
        al ciclo entero (el tope de frecuencia lo impide). */
    static double wrap01(double t) noexcept
    {
        return (t < 0.0) ? t + 1.0 : t;
    }

    //==========================================================================
    double sampleRateHz = 44100.0;
    double requestedHz  = defaultFrequencyHz; // lo que se ha pedido (antes de los topes)
    double frequency    = defaultFrequencyHz; // lo que SE ESTA usando
    // (Se saca de los dos de arriba, que ya estan inicializados: el orden de
    // declaracion es el que manda en un inicializador de miembro.)
    double increment            = defaultFrequencyHz / sampleRateHz;
    double phase                = 0.0; // 0..1, la fase de la proxima muestra
    double pulseWidth           = 0.5; // 0..1, solo lo usa la rectangular
    double triangleIntegral     = triangleStartValue;
    OscWaveform currentWaveform = OscWaveform::Sawtooth;
};

} // namespace abd::synth
