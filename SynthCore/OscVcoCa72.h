/*
  ==============================================================================

    OscVcoCa72.h
    EL VCO DE RANPA: el miembro de CIRCUITO de la familia de osciladores
    (namespace abd::synth).

    QUE ES. Un oscilador que no cuenta fase: un condensador que integra una
    corriente, un disparador de Schmitt que lo reinicia cuando la rampa cruza su
    umbral (con el retardo del comparador y la espera del transistor) y tres
    conformadores de onda que salen del mismo nodo: el separador, su diente de
    sierra y el comparador del rectangulo con su histeresis. La frecuencia no sale
    de un incremento de fase sino de la CORRIENTE que carga el condensador, y por
    eso el nucleo tiene su propio tiempo dentro: el retardo del reinicio crece
    cuando la rampa se hace lenta, el techo baja un poco cuando corre, y el
    rectangulo conmuta tarde a proposito.

    DE DONDE VIENE. De la idea del VCO de un sinte analogico de los anos 70 tal
    como esta medido en el banco de pruebas del estudio: los numeros estan en
    OscVcoCa72Profile.h y el algoritmo esta escrito aqui de nuevo, sin copiar
    codigo de la referencia (que esta bajo GPL). Ver la nota de fidelidad de
    abajo, que es lo que se gana y lo que se deja fuera.

    EL IDIOMA DE LA FAMILIA, Y QUE CUESTA HABLARLO. La familia habla en Hz, y el
    aparato habla en corriente. Lo que la pieza hace es RESOLVER la corriente que
    su propio nucleo necesita para dar la frecuencia pedida: el modelo de periodo
    del nucleo es monotono en la corriente y `setFrequency` lo invierte por
    biseccion (cuarenta pasos, fuera del lazo de audio). El periodo que sale es el
    del modelo, que es el del nucleo: la resolucion no es una recta, es la ley del
    aparato. Y la puerta del circuito sigue abierta para quien la quiera:
    `setTimingCurrent` es la entrada del aparato (la corriente de temporizacion) y
    `getTimingCurrent` lo que esta usando.

    QUE SE QUEDA DEL CONVERTIDOR EXPONENCIAL, Y QUE NO. El convertidor (el que
    convierte voltios de teclado en corriente, con el par de transistores, los
    trimpots de escala y las derivas de temperatura) es lo que hace que un VCO
    analogico desafine de una manera concreta. Pero es exactamente el trozo que la
    familia SUSTITUYE: si se pide en Hz, el camino voltios -> corriente -> Hz ya lo
    ha recorrido quien pide, y volver a modelarlo seria meter una calibracion que
    nadie usa. Lo que si se queda, porque no es calibracion sino CARACTER, es lo
    que el convertidor le hace a la rampa: la corriente de temporizacion cae al
    caer la rampa (el efecto Early), y por eso la rampa es un poco concava y el
    diente de sierra no es una recta. Eso esta en `core.earlySlope`. Fuera quedan,
    dichos en voz alta: la cadena voltios->corriente (trimpots, R42, el par
    CA3046 y su temperatura), la deriva termica y el ruido.

    FIDELIDAD DE LA LEY DE DOS PUNTOS. `earlySlope` es UNA constante, la del
    centro del recorrido. En el banco de pruebas la pendiente crece hacia el tope
    (la relacion I(-4 V)/I(0 V) baja de ~0,94 a 0,881), asi que arriba del
    recorrido la curvatura de esta pieza se queda corta. No mueve la afinacion
    (setFrequency resuelve la corriente con la MISMA ley que usa el nucleo, y por
    eso el periodo sale clavado) y su peso en el reparto del ciclo es de
    centesimas: el retardo del comparador y la espera son tiempos ABSOLUTOS
    (microsegundos) y a 100 Hz son el 0,01% del periodo, a 4 kHz el 1%.

    LOS TRES CONFORMADORES CORREN SIEMPRE. Aunque la familia pida UNA salida
    (`processSample` devuelve la forma puesta), los tres conformadores y sus tres
    diezmadores corren en cada muestra. Es a proposito: cambiar de forma a mitad
    de nota no hace click ni reinicia el limitado de banda, que es lo que hace el
    aparato (los tres jacks estan vivos a la vez). El precio es el de tres
    diezmadores cuando hay sobremuestreo; a 1x no cuesta nada.

    LA TABLA DEL TRIANGULO. El pliegue del triangulo es una transferencia MEDIDA
    del aparato (un plegador de onda, no un generador de triangulo), y por eso
    puede inyectarse entera con `setTriangleTable`. Sin inyectar nada, el perfil
    trae los tres puntos de esa curva en el barrido del nucleo: su pliegue y sus
    dos extremos. Con esos tres puntos el triangulo sale asimetrico (sus dos picos
    no valen lo mismo y el pliegue no cae en la mitad del ciclo), que es como es.
    La interpolacion es LINEAL; la referencia interpola en cubica su tabla de 891
    puntos, y con la tabla completa inyectada la diferencia es la curvatura entre
    punto y punto.

    LO QUE LA PIEZA NO HACE, DICHO EN VOZ ALTA. No tiene SENO: `supportsWaveform`
    dice que no y `setWaveform` lo rechaza sin cambiar nada. No sobremuestrea a
    otro factor que 1, 2, 4 u 8. No compensa su propio retardo: lo declara en
    `getLatencySamples` y el host decide si lo resta (el comparador y el
    diezmador retrasan la salida y el modelo lo dice, en vez de mentir con un
    oscilador que suena medio periodo antes). Y no lleva su propio suavizado de
    parametros: `setFrequency` y `setPulseWidth` son instantaneos, como el panel
    del aparato; el glide y la modulacion los pone la voz.

  ==============================================================================
*/

#pragma once

#include "OscHalfbandDecimator.h"
#include "OscVcoCa72Profile.h"
#include "OscillatorFamily.h"

#include <algorithm>
#include <cmath>

namespace abd::synth
{

//==============================================================================
/** El VCO de rampa: miembro de CIRCUITO de la familia de osciladores.

    Da diente de sierra, triangulo y rectangulo (el seno no lo tiene), con el
    ancho de pulso del rectangulo en el idioma de la familia (0..1, 0.5 cuadrada)
    y una salida bipolar y nominalmente +-1, normalizada a partir de su PROPIO
    perfil (la nota de normalizacion esta en la cabecera del .cpp).

    @tags{Audio}
*/
class OscVcoCa72
{
public:
    //==========================================================================
    /** De que esta hecho, para el panel y el diagnostico. */
    static constexpr OscKind kind = OscKind::Circuit;

    /** Las tres formas del aparato. El seno NO esta, y no es un olvido: el nucleo
        no lo tiene y decir que si seria mentir en el idioma de la familia. */
    static constexpr unsigned supportedWaveforms = oscWaveformBit(OscWaveform::Sawtooth) | oscWaveformBit(OscWaveform::Triangle) | oscWaveformBit(OscWaveform::Rectangle);

    //==========================================================================
    /** El VCO del banco de pruebas a 44,1 kHz. */
    OscVcoCa72() noexcept;

    /** El VCO hecho del perfil que le den: el hardware entra como datos. */
    explicit OscVcoCa72(const OscVcoCa72Profile& profile) noexcept;

    //==========================================================================
    /** Cambia el perfil (se COPIA: el oscilador no guarda un puntero a el). */
    void setProfile(const OscVcoCa72Profile& profile) noexcept;

    /** El perfil que esta usando. */
    const OscVcoCa72Profile& getProfile() const noexcept;

    /** La transferencia del triangulo, medida o como sea, en tensiones de
        separador CRECIENTES. Los puntos NO se copian: tienen que vivir mas que el
        oscilador. `count <= 0` o `points == nullptr` vuelve a la del perfil. */
    void setTriangleTable(const OscVcoCa72TrianglePoint* points, int count) noexcept;

    //==========================================================================
    /** La frecuencia de muestreo. Como cambiar de fs no cambia ni una constante
        del nucleo (la rampa es de tiempo continuo), esto solo rehace el paso de
        integracion y los diezmadores: los ajustes y el estado se conservan. */
    void prepare(double sampleRate) noexcept;

    /** La nota arranca en el techo de la rampa, como despues de un reinicio. */
    void reset() noexcept;

    /** La frecuencia en Hz. Se recorta al recorrido de la familia y al de la
        pieza, y el resultado se puede preguntar con getFrequency(): la pieza
        devuelve lo que de verdad da, no lo que le pidieron. */
    void setFrequency(double hz) noexcept;

    /** La forma, si el aparato la sabe hacer. Devuelve `false` y NO cambia nada
        cuando no (el seno), que es lo que evita el fallo silencioso. */
    bool setWaveform(OscWaveform waveform) noexcept;

    /** El factor de sobremuestreo: 1, 2, 4 u 8 muestras internas por muestra de
        salida. Devuelve `false` y NO cambia nada con otro factor. Al cambiar, la
        rampa y sus sucesos siguen (no se mueve la fase), y los diezmadores
        arrancan de silencio, como en el aparato. */
    bool setOversampling(int factor) noexcept;

    /** El ancho de pulso 0..1 del rectangulo (0.5 cuadrada). Solo afecta al
        rectangulo; las otras dos formas lo ignoran y siguen igual.

        El aparato no llega a cualquier ancho: su comparador sube al reinicio
        solo si su umbral de subida queda por debajo del techo del separador, y
        mas estrecho que eso el rectangulo se quedaria ABAJO para siempre, que no
        es un pulso estrecho sino una averia. El minimo que da este VCO sale de su
        propio perfil (minPulseWidth) y aqui se recorta, en vez de sonar mal. */
    void setPulseWidth(double duty01) noexcept;

    //==========================================================================
    /** Una muestra. La forma que este puesta, bipolar y nominalmente +-1. */
    double processSample() noexcept;

    /** La frecuencia que SE ESTA usando, en Hz. */
    double getFrequency() const noexcept { return frequencyHz; }

    /** La forma que SE ESTA usando. */
    OscWaveform getWaveform() const noexcept { return waveform; }

    /** El factor de sobremuestreo que SE ESTA usando. */
    int getOversampling() const noexcept { return oversample; }

    /** El ancho de pulso que SE ESTA usando, 0..1. */
    double getPulseWidth() const noexcept { return pulseWidth; }

    /** La fase 0..1: cuanto le queda a la rampa de su caida, EN TIEMPO (0 en el
        techo, 1 en el umbral). Durante la espera del reinicio se queda en 0, como
        la rampa.

        En TIEMPO y no en tension a proposito: la rampa no es una recta (cae
        rapido al principio y se frena despues, porque la corriente baja con la
        tension), y la mitad de su RECORRIDO la alcanza en el 49,26% de su
        TIEMPO. Si la fase fuera la fraccion de tension, el ancho de pulso de la
        familia (que es una fraccion del ciclo, o sea de tiempo) mentiria un 0,7%
        en la mitad, y este es el numero que se comprueba. */
    double getPhase() const noexcept;

    /** Lo que la pieza retrasa su salida, en muestras de SALIDA. Lo ponen el
        diezmador (su retardo de grupo) y la linea de escalon limitado, que
        entrega la muestra ANTERIOR: a 1x vale 1,0 y sube con el sobremuestreo.
        El reinicio en si no retrasa nada: coloca el escalon en su instante. */
    double getLatencySamples() const noexcept;

    //==========================================================================
    /** Que formas sabe hacer. Estatica: se puede preguntar sin un oscilador. */
    static bool supportsWaveform(OscWaveform waveform) noexcept
    {
        if (static_cast<unsigned>(waveform) > static_cast<unsigned>(OscWaveform::Sine))
            return false;

        return (supportedWaveforms & oscWaveformBit(waveform)) != 0u;
    }

    /** Que factores de sobremuestreo sabe hacer. */
    static bool supportsOversampling(int factor) noexcept
    {
        return factor == 1 || factor == 2 || factor == 4 || factor == 8;
    }

    //==========================================================================
    /** LA PUERTA DEL CIRCUITO: la corriente de temporizacion (a rampa en 0 V), en
        amperios. Es lo que el aparato recibe de su convertidor; aqui sirve para
        modular en corriente (FM lineal) sin pasar por Hz, y para leer la
        calibracion que la pieza esta usando. Se recorta a (0, timingMax]. */
    void setTimingCurrent(double amps) noexcept;

    /** La corriente de temporizacion que SE ESTA usando, A. */
    double getTimingCurrent() const noexcept { return timingCurrent; }

    /** La tension de la rampa AHORA, V (el nodo que integra). Es lo que un
        osciloscopio veria en el condensador. */
    double getRampVoltage() const noexcept { return ramp; }

    /** La frecuencia mas alta que la pieza da: la que corresponde a la corriente
        maxima del perfil. Se recorta con ella, y por eso getFrequency() puede
        devolver menos de lo pedido. */
    double maxFrequencyHz() const noexcept;

    /** El ancho de pulso mas estrecho que el comparador de ESTA pieza puede dar,
        derivado de su perfil (unos 0,051 con el del banco de pruebas: el
        interruptor del aparato no baja de 0,18). */
    double minPulseWidth() const noexcept { return minDuty; }

    /** El ancho de pulso mas ancho: el tope de la familia, porque el comparador
        si sabe hacerlo. */
    double maxPulseWidth() const noexcept { return maxDuty; }

    /** El periodo del nucleo para una corriente de temporizacion, s: la ley del
        aparato (rampa + retardo del comparador + espera), sin invertir. */
    double periodSecondsForTimingCurrent(double amps) const noexcept;

private:
    //==========================================================================
    /** La ley de la rampa (C dv/dt = -(a + b v)) para una corriente de
        temporizacion: a y b en amperios y amperios por voltio, y c en faradios. */
    struct TimingLaw
    {
        double a;
        double b;
        double c;
    };

    TimingLaw timingLawFor(double amps) const noexcept;

    /** La linea de escalon limitado en banda: la misma correccion cuadratica del
        polyBLEP del modulo (ver PolyBLEP.h), en la forma que necesita un suceso
        que cae en un instante fraccionario dentro de una muestra. PolyBLEP.h esta
        escrito contra una FASE y devuelve un residuo que se resta; aqui el suceso
        trae su propio tamano y su fraccion, y los dos tramos se acumulan. Los
        valores salen UNA muestra tarde a proposito: el escalon entre las muestras
        n-1 y n corrige las dos, y por eso la n-1 se guarda hasta conocer la n. */
    struct BlepLine
    {
        double held    = 0.0; // la muestra anterior, corregida
        double pending = 0.0; // lo que le toca a la que viene

        void reset() noexcept
        {
            held    = 0.0;
            pending = 0.0;
        }

        /** Un escalon de `delta` a `frac` del camino entre la muestra anterior y
            la que viene (0 < frac <= 1). El reparto es el cuadratico del
            polyBLEP: `d` es lo que le queda por correr a la muestra que viene
            hasta el escalon, y las dos correcciones suman delta*(2d-1)/2, que es
            lo que el limitado de banda mueve alrededor del instante. En los dos
            extremos (el escalon clavado en una de las dos muestras) la que cae
            justo encima del escalon se lleva el medio escalon, que es el valor
            del limite en su propio instante. */
        void step(double delta, double frac) noexcept
        {
            const double d = std::min(std::max(1.0 - frac, 0.0), 1.0);

            held += delta * d * d * 0.5;
            pending -= delta * (1.0 - d) * (1.0 - d) * 0.5;
        }

        /** Un impulso de area `area` (valor por muestras) en el mismo instante:
            el limite de dos escalones opuestos que se cierran, repartido por su
            distancia, y con su area conservada exacta (held + pending == area). */
        void impulse(double area, double frac) noexcept
        {
            const double d = std::min(std::max(1.0 - frac, 0.0), 1.0);

            held += area * d;
            pending += area * (1.0 - d);
        }

        /** Empuja el valor ingenuo de la muestra nueva; devuelve la anterior
            corregida. */
        double push(double naive) noexcept
        {
            const double out = held;

            held    = naive + pending;
            pending = 0.0;

            return out;
        }
    };

    //==========================================================================
    /** Rehace lo que depende del perfil y del ancho de pulso: el barrido del
        separador y las tres normalizaciones del idioma de la familia. */
    void updateDerived() noexcept;

    /** La corriente de temporizacion que da el periodo pedido, por biseccion
        sobre periodSecondsForTimingCurrent (la ley es monotona). */
    double timingCurrentForFrequency(double hz) const noexcept;

    /** La pendiente de la corriente por volt de rampa para una corriente, A/V. */
    double timingSlopeFor(double amps) const noexcept
    {
        return amps * std::max(profile.core.earlySlope, 1.0e-6);
    }

    /** La tension del separador para una tension de rampa, V. */
    double bufferVoltageOf(double rampVolts) const noexcept
    {
        return profile.shaper.bufGain * rampVolts + profile.shaper.bufOffset;
    }

    /** La transferencia del triangulo en una tension de separador, V. */
    double triangleVoltageOf(double bufferVolts) const noexcept;

    /** La polarizacion de ancho, en voltios, que pone el umbral de bajada del
        comparador en la fase pedida: es la CURVA DE ANCHO de este miembro (la
        familia fija que 0.5 sea cuadrada; como se llega ahi es de la pieza). */
    double dutyToWidth(double duty) const noexcept;

    /** La tension de la rampa en una fase 0..1 (en tiempo, como getPhase) y su
        inversa. Es la ley del condensador despejada, y la usan la fase, la curva
        de ancho y el minimo de ancho: una sola cuenta para los tres, que es lo
        que impide que se separen. */
    double rampVoltageAtPhase(double phase) const noexcept;
    double phaseOfRampVoltage(double rampVolts) const noexcept;

    /** La fase a la que el comparador baja si su umbral esta en esta tension de
        separador: la inversa de dutyToWidth, que hace falta para saber hasta
        donde llega el aparato. */
    double dutyFromBufferVoltage(double bufferVolts) const noexcept;

    /** Los dos umbrales del comparador, en tension de separador, para el ancho
        que este puesto. */
    void updatePulseThresholds() noexcept;

    /** Un paso sobre-muestreado: la rampa y sus sucesos, tomados en orden de
        tiempo (el umbral, el reinicio tras el retardo, el fin de la espera, el
        paso por el umbral del rectangulo y sus flancos retrasados). */
    void step(double a, double b, double c, double vFall, double riseBuffer) noexcept;

    /** El reinicio: la rampa salta a su techo, los escalones del diente de sierra
        y del triangulo entran en sus lineas, y el rectangulo queda citado para
        subir. */
    void doReset(double a, double b, double c, double frac, double riseBuffer) noexcept;

    /** La rampa un tiempo despues: C dv/dt = -(a + b v), resuelta en cerrado. */
    double advanceRamp(double rampVolts, double seconds, double a, double b, double c) const noexcept
    {
        const double bigA = a / b;

        return -bigA + (rampVolts + bigA) * std::exp(-b * seconds / c);
    }

    /** Cuando la rampa pasa de `from` a `to`, s. */
    double crossingTime(double from, double to, double a, double b, double c) const noexcept
    {
        const double bigA = a / b;

        return (c / b) * std::log((from + bigA) / (to + bigA));
    }

    //==========================================================================
    OscVcoCa72Profile profile = oscVcoCa72ReferenceProfile();

    double sampleRateHz = 44100.0;
    double dt           = 1.0 / 44100.0;
    int oversample      = 1;

    double frequencyHz   = 440.0;
    double timingCurrent = 0.0; // A, la que el modelo necesita para frequencyHz
    double pulseWidth    = 0.5;
    OscWaveform waveform = OscWaveform::Sawtooth;

    // --- estado de la rampa
    double ramp       = 0.0; // V
    double holdLeft   = 0.0; // s
    double resetIn    = 0.0; // s, hasta el reinicio (retardo del comparador)
    double riseIn     = 0.0; // s, hasta que el rectangulo sube
    double fallIn     = 0.0; // s, hasta que el rectangulo baja
    bool resetPending = false;
    bool risePending  = false;
    bool fallPending  = false;
    bool rectIsLow    = false;
    int sub           = 0; // que paso sobre-muestreado va dentro de la muestra

    BlepLine sawLine;
    BlepLine triLine;
    BlepLine rectLine;

    OscHalfbandDecimator sawDecimator;
    OscHalfbandDecimator triDecimator;
    OscHalfbandDecimator rectDecimator;

    double outSaw  = 0.0;
    double outTri  = 0.0;
    double outRect = 0.0;

    // --- normalizaciones, derivadas del perfil (ver updateDerived)
    double sawScale    = 1.0;
    double triScale    = 1.0;
    double rectCenter  = 0.0;
    double rectHalf    = 1.0;
    double vFallBuffer = 0.0;              // tension de separador a la que el rectangulo baja
    double riseBuffer  = 0.0;              // tension de separador a la que el rectangulo sube
    double minDuty     = oscMinPulseWidth; // el estrecho que este comparador da
    double maxDuty     = oscMaxPulseWidth;
};

} // namespace abd::synth
