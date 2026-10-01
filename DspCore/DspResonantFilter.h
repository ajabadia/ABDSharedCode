/*
  ==============================================================================

    DspResonantFilter.h
    Etapa de paso bajo RESONANTE que se auto-oscila y se ASIENTA en un nivel
    (namespace abd::dsp).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspCore).
    Se incluye desde DspCore.h, igual que DspMath.h, y por eso puede usar las
    trascendentes deterministas de ahi en vez de la libm de cada plataforma.

    QUE ES. Un filtro de paso bajo de dos estados cuyo amortiguamiento se puede
    volver NEGATIVO: en ese punto los polos cruzan a la mitad derecha del plano y
    la etapa se auto-oscila, emitiendo un seno a la frecuencia de corte. Hasta
    ahi, un filtro resonante de los de toda la vida.

    LO QUE HACE ESTE Y NO HACE UNO NORMAL: que la oscilacion no crezca sin
    limite. Con el amortiguamiento negativo solo, cualquier ruido de la entrada
    hace crecer la oscilacion y nunca se detiene; un filtro de la vida real que
    se auto-oscila tiene que LIMITARLA, y lo hace con saturacion, con una
    GANANCIA AUTOMATICA o con un limitador explicito. Aqui es una AGC por
    potencia de banda, y el motivo esta medido y escrito abajo.

    LA AGC, Y POR QUE SUAVIZA LA POTENCIA EN VEZ DE USARLA INSTANTANEA.
    El amortiguamiento de cada muestra sube con la potencia de la salida de
    banda, promediada con un filtro de un polo:

        k[n] = k0 + beta * potencia[n]
        potencia[n] += follow * (banda[n]^2 - potencia[n])

    El equilibrio esta donde el crecimiento que aporta k0 < 0 se compensa con el
    amortiguamiento que aporta la potencia. De ahi sale el nivel al que se asienta
    y, por tanto, la prediccion que los tests comprueban:

        potencia de equilibrio = -k0 / beta
        amplitud de banda        = sqrt (-2 * k0 / beta)   (media de A^2 sin^2)

    Si en vez de promediar se usara banda^2 muestra a muestra, la amortiguacion
    oscilaria al doble de la frecuencia de corte, que es donde vive elGrowing
    exponente del oscilador de Rayleigh: sujetaria el nivel igual de bien pero
    doblaria el seno, ensuciandolo con armonicos. El promediado a ~2 ciclos de la
    frecuencia de corte mueve la amortiguacion demasiado despacio para doblar un
    ciclo, y la oscilacion sale limpia. Eso no es una opinion: hay un test que
    mide el tercer armónico de la oscilacion asentada.

    POR QUE TPT Y NO UN BIQUAD CLASICO. La forma de resolver el filtro (la
    transformada que conserva la topologia, de Zavalishin) tiene dos ventajas que
    aqui se pagan:

      - El mismo esquema sirve para el paso bajo, la banda y el alto con el mismo
        recorrido, y la banda es la senal de la que se saca la potencia, sin
        etapas extra.
      - El estado se mantiene al retunear, de modo que mover el corte no hace
        click. Con un biquad en forma directa habria que reentrar el estado para
        no hacerlo.

    A resonancia 0 esta etapa es, medida, el mismo filtro que un biquad de paso
    bajo de forma directa con la misma Q; hay un test que lo comprueba barrido a
    barrido en frecuencia, que es la forma de decir que el TPT no ha estropeado lo
    que ya se sabia hacer.

    LO QUE NO VIVE AQUI. La cascada de Butterworth de tres secciones con la que
    una maquina de verdad hace su filtro de 36 dB/octava es OTRO modulo: la Q de
    cada seccion y su orden son de esa maquina, no del sustrato compartido. Esta
    cabecera es la ETAPA, que es lo reutilizable. Encadenarla, o no, es del host.

    CONTRATO DE LLAMADA. prepare() una vez (tambien al cambiar la frecuencia de
    muestreo), reset() para que una nota no empiece con la cola de la anterior,
    y despues processSample() por muestra. Todo noexcept, sin asignaciones y
    sin ramificar en la cadena de realimentacion.

  ==============================================================================
*/

#pragma once

#include "DspCore.h"

#include <algorithm>
#include <cmath>

namespace abd::dsp
{

//==============================================================================
/** Una etapa de paso bajo resonante, con auto-oscilacion limitada en nivel.

    El amortiguamiento `k` es el inverso de la Q: positivo es un filtro que
    decae, cero es el umbral de la auto-oscilacion y negativo es un polo dentro
    del circulo unidad, o sea un oscilador. El recorrido de la resonancia (0..1)
    lleva `k` desde el valor del filtro sin realce hasta un suelo negativo, y de
    ahi en adelante la etapa canta sola a la frecuencia de corte.

    Que se ASIENTE en un nivel, en vez de crecer, es cosa del amortiguamiento
    adaptativo: sube con la potencia de la salida de banda, promediada. Ver la
    cabecera del fichero, que explica el equilibrio y de donde sale el nivel.

    @tags{Audio}
*/
class ResonantFilterStage
{
public:
    //==========================================================================
    /** La frecuencia de muestreo. Sin esto no hay nada que calcular: `g` sale
        de ella. Hay que llamar a `prepare()` otra vez si cambia.
    */
    void prepare (double sampleRate) noexcept
    {
        sampleRateHz = (sampleRate > 0.0) ? sampleRate : 44100.0;
        cutoffHz = -1.0;   // fuerza el retune del primer processSample
        setResonance (resonance);
    }

    //==========================================================================
    /** Olvida el pasado: la siguiente nota no empieza con la cola de la otra.
        No toca ni el corte ni la resonancia, que son ajustes, no estado.
    */
    void reset() noexcept
    {
        integrator1 = 0.0;
        integrator2 = 0.0;
        bandPower = 0.0;
    }

    //==========================================================================
    /** La frecuencia de corte, en Hz. El estado se conserva, asi que un corte
        que se mueve no hace click.

        @param hz  frecuencia de corte. Se recorta a un margen por debajo de
                   Nyquist: en la forma TPT `g = tan (pi * f / fs)` crece sin
                   limite al acercarse a Nyquist, y un `g` infinito no es un
                   filtro, es un NaN que se lleva por delante el estado.
    */
    void setCutoff (double hz) noexcept
    {
        const auto nyquist = sampleRateHz * 0.5;
        const auto limit = nyquist * maxCutoffFraction;
        const auto wanted = hz < 10.0 ? 10.0
                          : (hz > limit ? limit : hz);

        if (wanted == cutoffHz)
            return;

        cutoffHz = wanted;
        updateCoefficients();
    }

    //==========================================================================
    /** La resonancia, de 0 (sin realce) a 1 (auto-oscilacion al maximo).

        El recorrido es exponencial hasta el umbral porque la Q se oye
        exponencialmente, y lineal despues porque lo que hay que recorrer es el
        amortiguamiento NEGATIVO, que se oye de otra manera: cerca de cero, donde
        la AGC manda, cada paso de Q es un mundo; muy negativo, donde solo
        importa cuanto duro empuja, es casi lineal.

        @param amount  0..1. Se recorta: fuera de rango es un valor de panel, y
                      un valor de panel no puede meter un NaN en el filtro.
    */
    void setResonance (double amount) noexcept
    {
        resonance = std::min (1.0, std::max (0.0, amount));
        updateDamping();
    }

    //==========================================================================
    /** Corte y resonancia de golpe, que es como se explora un mando.

        @param hz       frecuencia de corte en Hz.
        @param amount   resonancia 0..1.
    */
    void setCutoffAndResonance (double hz, double amount) noexcept
    {
        const auto was = resonance;

        resonance = std::min (1.0, std::max (0.0, amount));
        cutoffHz = -1.0;
        setCutoff (hz);

        if (resonance != was)
            updateDamping();
    }

    //==========================================================================
    /** Una muestra por dentro de la etapa. Devuelve la salida de paso bajo.

        El orden de operaciones es el del TPT y no se reordena: la solucion de un
        paso a la vez es exacta para los valores del amortiguamiento de aqui, y
        cambiar el orden cambiaria el resultado bit a bit.

        @param input  la muestra de entrada.
        @returns      la salida de paso bajo de esta etapa.
    */
    double processSample (double input) noexcept
    {
        if (cutoffHz <= 0.0)
            setCutoff (10.0);

        // La potencia de la banda, promediada. El tiempo del promediado son dos
        // ciclos del corte, con suelo: por debajo de un suelo el suavizado se
        // vuelve tan rapido que vuelve a doblar el seno, que es justo lo que
        // este promediado evita.
        bandPower += follow * (band * band - bandPower);

        const auto k = std::min (maxDamping, k0 + beta * bandPower);
        const auto inv = 1.0 / (1.0 + g * (g + k));
        const auto a1 = inv;
        const auto a2 = g * a1;
        const auto a3 = g * a2;

        const auto v3 = input - integrator2;
        const auto v1 = a1 * integrator1 + a2 * v3;
        auto v2 = integrator2 + a2 * integrator1 + a3 * v3;

        integrator1 = 2.0 * v1 - integrator1;
        integrator2 = 2.0 * v2 - integrator2;

        band = v1;

        // Una voz que se apaga en silencio se va a denormales, que en x86 son
        // lentos. El suelo es donde ya no se oye nada.
        if (std::abs (integrator1) < denormalFloor) integrator1 = 0.0;
        if (std::abs (integrator2) < denormalFloor) integrator2 = 0.0;
        if (bandPower < denormalFloor)               bandPower = 0.0;

        return v2;
    }

    //==========================================================================
    // NOTA SOBRE LA PASABANDA: no hay compensacion aqui, y es que no hace
    // falta. Se Midio antes de escribirla —subiendo la resonancia de 0 a 0.99
    // con el corte en 1 kHz, la ganancia a 100 Hz se queda en 1.01 y a medio
    // corte sube de 0.95 a 1.33— o sea que este esquema REALZA la pasabanda al
    // subir el Q, que es lo que hace un filtro resonante, y no la apaga. Una
    // compensacion de pasabanda aqui no devolveria nada: lo multiplicaria por
    // 5.4. La unica concession es que la etapa no lleva ninguna, y que esta
    // frase dice por que.

    //==========================================================================
    /** La potencia de banda promediada ahora mismo. Es la variable de la que
        depende el equilibrio, asi que un medidor la usa y un test la mide. */
    double getBandPower() const noexcept  { return bandPower; }

    /** El amortiguamiento de este momento, ya con el efecto de la AGC. */
    double getDamping() const noexcept
    {
        return std::min (maxDamping, k0 + beta * bandPower);
    }

    /** El amortiguamiento SIN el efecto de la AGC, o sea el que decide si la
        etapa se auto-oscila. Negativo es que canta sola. */
    double getBaseDamping() const noexcept { return k0; }

    /** La resonancia actual, 0..1. */
    double getResonance() const noexcept { return resonance; }

    /** La frecuencia de corte actual, ya recortada, en Hz. */
    double getCutoff() const noexcept    { return cutoffHz; }

    //==========================================================================
    /** Cuanto manda la potencia de banda sobre el amortiguamiento, y por tanto
        el nivel al que se asienta la oscilacion. A CERO la AGC se apaga y la
        etapa es un resonador puro, cuya oscilacion CRECE: es lo que un filtro
        tiene que ser si nadie la limita, y esta aqui para poder compararlas.

        @param strength  0 desactiva el control de nivel; `agcStrength` es el
                         valor con el que nace. Los valores mas altos asientan
                         antes y mas abajo.
    */
    void setLevelControl (double strength) noexcept
    {
        beta = std::max (0.0, strength);
    }

    /** El nivel al que se asienta la oscilacion con el amortiguamiento actual,
        en amplitud de banda. Es `-k0 / beta` en potencia; aqui en amplitud, que
        es lo que un medidor muestra.

        CERO cuando la etapa no se auto-oscila (no hay oscilacion que asiente) y
        INFINITO con la AGC apagada, que es la respuesta correcta: sin nadie que
        la limite, la oscilacion no tiene tope. */
    double getSettledAmplitude() const noexcept
    {
        if (beta <= 0.0)
            return std::numeric_limits<double>::infinity();

        if (k0 >= 0.0)
            return 0.0;

        return std::sqrt (-2.0 * k0 / beta);
    }

    //==========================================================================
    /** El punto del recorrido de resonancia en el que el amortiguamiento cruza
        cero, o sea donde la etapa empieza a cantar sola.

        No es donde acaba el tramo exponencial: al final de ese tramo el
        amortiguamiento todavia es POSITIVO (1/maxQ), y se vuelve negativo en el
        tramo siguiente, interpolando entre 1/maxQ y el suelo. El cruce cae a
        una fraccion de ese tramo, proporcional a cuanto pesa el suelo frente al
        valor de partida.

        @returns  la resonancia 0..1 del umbral.
    */
    static double selfOscillationStart() noexcept
    {
        const auto kAtMaxQ = 1.0 / defaultMaxQ;
        const auto t = kAtMaxQ / (kAtMaxQ - dampingFloor);

        return threshold + (1.0 - threshold) * t;
    }

    //==========================================================================
    // Lo que se puede cambiar sin que cambie el sonido de golpe.
    //==========================================================================
    /** El techo de amortiguamiento que puede pedir la AGC. Existe para que un
        transitorio enorme no le pida al paso un salto que la solucion de una
        sola muestra no puede dar: con el techo puesto, la salida de un golpe a
        fondo de escala sigue siendo finita y acotada, y lo hay testeado. */
    static constexpr double maxDamping = 4.0;

    /** La Q mas alta del tramo exponencial, en la resonancia `threshold`. */
    static constexpr double defaultMaxQ = 20.0;

    /** Donde esta el suelo del amortiguamiento en resonancia 1, en valor de k.
        Negativo, que es lo que hace que la etapa se auto-oscila. */
    static constexpr double dampingFloor = -0.1;

    /** Cuanto manda la potencia de banda sobre el amortiguamiento, y por tanto
        el nivel al que se asienta la oscilacion: `potencia = -k0 / beta`, luego
        la amplitud es `sqrt (-2 * k0 / beta)`. */
    static constexpr double agcStrength = 0.5;

    /** Suelo del promediado de potencia, en segundos. */
    static constexpr double minFollowSeconds = 0.01;

    /** Ciclos del corte que dura el promediado de potencia. */
    static constexpr double followCycles = 2.0;

    /** Por debajo de este valor una muestra se considera denormal y se anula. */
    static constexpr double denormalFloor = 1.0e-20;

    /** La fraccion de Nyquist a la que se recorta el corte, para que `g` no se
        vaya con la frecuencia de muestreo. */
    static constexpr double maxCutoffFraction = 0.49;

private:
    //==========================================================================
    /** Los coeficientes que dependen de la frecuencia de corte. `g` sale de
        `tan (pi * f / fs)`, y como el sustrato no trae `tan` en sus
        deterministas, sale de `sin / cos` de las que si trae: mismo determinismo,
        una division mas.

        El angulo se pide en float porque las deterministas de DspMath lo toman,
        y a un angulo de este rango (|x| < pi/2) el error de float es del orden
        de 1e-7 relativo: dos centsimos de centavo de afinacion, que no se oye
        y si es el mismo numero en nativo y en WASM, que es lo que importa.
    */
    void updateCoefficients() noexcept
    {
        const auto pi = static_cast<float> (MathConstants<double>::pi);
        const auto angle = static_cast<float> (static_cast<double> (pi) * cutoffHz
                                                    / sampleRateHz);

        const auto s = static_cast<double> (sin (angle));
        const auto c = static_cast<double> (cos (angle));

        // `cos` no llega a cero con el recorte de arriba, pero el corte minimo y
        // una frecuencia de muestreo absurda podrian acercarse. El suelo mantiene
        // la division finita sin cambiar el filtro en el rango de uso.
        g = s / (std::abs (c) < 1.0e-6 ? 1.0e-6 : c);

        // La compensacion de pasabanda no existe (ver la nota de la clase), asi
        // que aqui solo queda el tiempo del promediado de potencia, que si
        // depende del corte.
        const auto seconds = std::max (minFollowSeconds, followCycles / cutoffHz);
        const auto tau = static_cast<float> (-1.0 / (seconds * sampleRateHz));

        // El recorte va AQUI y escrito, no repartido. `log2e` es `double` y
        // `exp2` toma `float`, asi que el producto salia en `double` y se
        // recortaba al entrar, sin que nadie lo hubiera pedido: era el unico
        // aviso C4244 de todo el modulo a /W4. Poner el `static_cast<float>`
        // delante de la llamada hace visible un recorte que ya estaba ahi, y no
        // cambia ni un bit: el valor final es el mismo que salia de
        // recortar y ensanchar. El `double` de abajo sobra, porque `exp2` ya
        // devuelve `float`.
        const auto target = exp2 (static_cast<float> (tau * log2e));

        follow = 1.0 - target;
    }

    //==========================================================================
    /** El amortiguamiento base `k0`, el que no depende de la potencia. */
    void updateDamping() noexcept
    {
        const auto kBase = 1.0 / baseQ;
        const auto kAtMaxQ = 1.0 / defaultMaxQ;

        if (resonance <= threshold)
        {
            // Exponencial de Q base a Q maxima, repartida en el tramo. El pow de
            // dos es el determinista de DspMath, no std::pow: mismo motivo que
            // antes, la paridad es la invariante del modulo.
            const auto t = static_cast<float> (resonance / threshold);
            const auto ratio = static_cast<double> (pow (static_cast<float> (kAtMaxQ / kBase), t));

            k0 = kBase * ratio;
        }
        else
        {
            // Del umbral al suelo: el amortiguamiento se vuelve negativo, y su
            // magnitud es lo que decide cuanto empuja la oscilacion.
            const auto t = (resonance - threshold) / (1.0 - threshold);

            k0 = kAtMaxQ + (dampingFloor - kAtMaxQ) * t;
        }
    }

    //==========================================================================
    /** El Q del filtro sin realce. Publico porque el host lo elige: una seccion
        de una cascada de un filtro concreto tiene una Q que es de ESE filtro. */
    double baseQ = 0.70710678118654752;   // 1/sqrt(2): Butterworth de segundo orden
    double sampleRateHz = 44100.0;
    double cutoffHz = -1.0;
    double resonance = 0.0;

    double g = 0.0;             // tan (pi * f / fs)
    double k0 = 0.0;            // amortiguamiento sin AGC
    double beta = agcStrength;  // cuanto manda la potencia (0 = AGC apagada)
    double follow = 0.0;        // coeficiente del promediado de potencia
    double integrator1 = 0.0;   // estado del TPT
    double integrator2 = 0.0;
    double band = 0.0;          // salida de banda de la ultima muestra
    double bandPower = 0.0;     // potencia de banda promediada

    /** Donde el amortiguamiento cruza cero, en resonancia 0..1. */
    static constexpr double threshold = 0.9;

    /** log2(e), para pasar de un tiempo en segundos a un exponente de exp2. */
    static constexpr double log2e = 1.4426950408889634;
};

} // namespace abd::dsp
