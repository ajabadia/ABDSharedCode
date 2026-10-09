/*
  ==============================================================================

    DspFilterEquation.h
    El miembro de ECUACION de la familia de filtros: una escalera de cuatro polos
    con realimentacion NO LINEAL, resuelta sin retardo en el lazo (namespace
    abd::dsp).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspCore). La
    familia a la que pertenece vive en DspFilterFamily.h.

    QUE ES. Cuatro etapas de un polo TPT en cascada, con la salida de la cuarta
    realimentada a la entrada, y el tanh de DspMath en el camino de
    realimentacion. Es la topologia de la escalera de transistores que un Moog
    hace con cuatro pares diferenciales, escrita como ECUACION y no como tabla ni
    como etapa lineal. Aqui "ecuacion" quiere decir que lo que suena sale de
    resolver el circuito cada muestra, no de leer una curva medida.

    LO QUE ES NUEVO AQUI, Y ES LO UNICO QUE IMPORTA. El lazo tiene realimentacion
    y no lleva retardo: la salida de la cuarta etapa entra en la primera. Con la
    forma clasica eso se resuelve dando un paso de retardo (usando la salida de la
    muestra ANTERIOR), que suena parecido pero no es lo mismo: un filtro con
    retardo en el lazo cambia su frecuencia de auto-oscilacion y su respuesta al
    mover la resonancia rapido. Aqui se resuelve de verdad, dos veces:

        y4 = G^4 * u + T          (la cascada, con T la contribucion de los estados)
        u  = x - k * tanh (y4)    (la realimentacion, no lineal por el tanh)

    El arranque es la solucion EXACTA del lazo LINEAL, que se despeja de una vez
    (u = (x - k*T) / (1 + k*G^4)), y despues se itera dos veces sobre el tanh. Dos
    iteraciones y no veinte: el tanh entra en saturacion enseguida, la iteracion
    converge en la primera decena de iteraciones solo cerca del umbral y el coste
    por muestra es el que es. Los numeros son constantes declaradas
    (kSolveIterations), no un "2" enterrado en un bucle.

    POR QUE EL tanh ES LO QUE HACE QUE ESTO NO SE DESMADRE. Sin el, a resonancia
    alta la realimentacion tiene ganancia mayor que uno a la frecuencia de corte y
    la oscilacion crece sin tope hasta el infinito en punto flotante. El tanh es lo
    mismo que hace el circuito real: cuando la señal es grande, el par diferencial
    deja de ser lineal y la ganancia efectiva del lazo baja hasta uno. La
    oscilacion se asienta, no se limita por un clip artificial.

    EL tanh ES EL DETERMINISTA DE DspMath, Y ESO NO ES UN DETALLE. La forma exacta
    de la saturacion no tiene que ser perfecta —una curva suave y monotona hace el
    trabajo, y la de DspMath lo es— pero SI tiene que ser LA MISMA en nativo y en
    WASM, porque si no el mismo patch suena distinto en cada build y no hay forma
    de comparar. Es la invariante del modulo (ver DspMath.h).

    QUE NO ESTA AQUI, DICHO EN VOZ ALTA, PORQUE ES TENTADOR AÑADIRLO. No estan los
    otros taps de la escalera (paso alto, banda, y las salidas de las etapas
    intermedias). Se pueden escribir, pero sus GANANCIAS hay que derivarlas y
    comprobarlas contra algo, y un tap con la ganancia inventada es peor que un tap
    que no existe: suena, y nadie sabe si suena bien. supportsMode dice que no, y
    setMode lo dice en la cara en vez de devolver un paso bajo disfrazado. Tampoco
    esta la saturacion de la ENTRADA de la escalera (la del par diferencial de
    entrada), por el mismo motivo: hay que medirla contra una referencia, y la
    referencia de este miembro es la ecuacion de la escalera, no un circuito
    concreto.

    CONTRATO DE LLAMADA. El de la familia: prepare(), reset() y processSample()
    por muestra, todo noexcept y sin asignaciones.

  ==============================================================================
*/

#pragma once

// La familia ANTES que el sustrato, por el mismo motivo que en DspFilterTpt.h:
// el modo y las curvas de la familia son el vocabulario de este fichero. Ver la
// nota de orden de cabeceras en DspFilterFamily.h.
#include "DspCore.h"
#include "DspFilterFamily.h"

#include <algorithm>
#include <cmath>

namespace abd::dsp
{

//==============================================================================
/** Una escalera de cuatro polos no lineal, miembro de la familia, de paso bajo.

    La resonancia 0..1 de la familia se traduce aqui a la realimentacion de una
    escalera de transistores. El umbral LINEAL de auto-oscilacion esta en k = 4
    (cuatro polos, cada uno con ganancia 1/sqrt(2) en el corte, o sea un cuarto de
    ganancia de lazo), y el tope del recorrido va un poco POR ENCIMA por una razon
    que se midio: con el tanh en el lazo, un k = 4 exacto NO canta, se apaga.
    Saturar baja la ganancia del lazo en cuanto la señal deja de ser infinitesimal,
    asi que en el umbral la oscilacion decae. Con el tope un poco mas arriba,
    resonancia 1 significa lo que la familia dice ("canta sola") y es el propio
    tanh el que le fija el nivel. Ver DspFilterFamily.h y maxFeedback.

    @tags{Audio}
*/
class FilterEquation
{
public:
    //==========================================================================
    /** De que esta hecho, para el panel y el diagnostico. */
    static constexpr FilterKind kind = FilterKind::Equation;

    /** Solo paso bajo: es la salida de la escalera. Los otros taps se pueden
        escribir, pero sus ganancias hay que derivarlas y comprobarlas, y mientras
        eso no pase este miembro dice que no (ver la cabecera del fichero). */
    static bool supportsMode(FilterMode mode) noexcept
    {
        return mode == FilterMode::LowPass;
    }

    //==========================================================================
    /** La frecuencia de muestreo. Deja el corte pendiente para forzar el
        recalculo de los coeficientes en la primera muestra. */
    void prepare(double sampleRate) noexcept
    {
        sampleRateHz = (sampleRate > 0.0) ? sampleRate : 44100.0;
        cutoffHz     = -1.0;
        setResonance(resonance);
    }

    /** Olvida el pasado. No toca corte, resonancia ni modo. */
    void reset() noexcept
    {
        for (auto& state : stageState)
            state = 0.0;
    }

    /** La frecuencia de corte en Hz, recortada por arriba igual que en el resto
        de la familia (la escalera tampoco es un filtro cerca de Nyquist). */
    void setCutoff(double hz) noexcept
    {
        const auto nyquist = sampleRateHz * 0.5;
        const auto limit   = nyquist * maxCutoffFraction;
        const auto wanted  = hz < minCutoffHz ? minCutoffHz : (hz > limit ? limit : hz);

        if (wanted == cutoffHz)
            return;

        cutoffHz = wanted;
        updateCoefficients();
    }

    /** La resonancia 0..1 de la familia, que aqui es la realimentacion 0..4. */
    void setResonance(double amount) noexcept
    {
        resonance = filterClamp01(amount);
        feedback  = maxFeedback * resonance;
    }

    /** El modo, si este miembro lo sabe hacer. Devuelve `false` sin cambiar nada
        cuando no: un paso alto disfrazado de paso bajo es un fallo que se oye y
        no se lee. */
    bool setMode(FilterMode mode) noexcept
    {
        if (!supportsMode(mode))
            return false;

        currentMode = mode;
        return true;
    }

    //==========================================================================
    /** Una muestra. Resuelve el lazo sin retardo y devuelve la salida de la
        cuarta etapa, que es el paso bajo de la escalera. */
    double processSample(double input) noexcept
    {
        if (cutoffHz <= 0.0)
            setCutoff(minCutoffHz);

        // Las contribuciones de los estados a la salida de la cascada. Cada etapa
        // es y_n = G*u_n + (1-G)*s_n, asi que (1-G)*s_n es lo que la etapa aporta
        // sin depender de su entrada.
        const auto g2 = G * G;
        const auto g3 = g2 * G;
        const auto g4 = g3 * G;

        const auto a1 = oneMinusG * stageState[0];
        const auto a2 = oneMinusG * stageState[1];
        const auto a3 = oneMinusG * stageState[2];
        const auto a4 = oneMinusG * stageState[3];

        const auto tail = g3 * a1 + g2 * a2 + G * a3 + a4;

        // El arranque: la solucion EXACTA del lazo con el tanh fuera. Es el valor
        // al que la iteracion converge cuando la señal es pequeña, asi que
        // empezar por ahi es empezar por el sitio bueno.
        auto u = (input - feedback * tail) / (1.0 + feedback * g4);

        // Y las iteraciones sobre el tanh, que es lo unico no lineal del lazo.
        for (int i = 0; i < kSolveIterations; ++i)
        {
            const auto y4 = g4 * u + tail;
            u             = input - feedback * static_cast<double>(tanh(static_cast<float>(y4)));
        }

        // Ahora si: se propaga la entrada ya resuelta por las cuatro etapas, y se
        // actualizan los estados con el MISMO valor de partida con el que se
        // calculo `tail` (si se actualizaran antes, la solucion del lazo dejaria
        // de corresponder con la cascada que se recorre).
        auto stageIn = u;

        for (int n = 0; n < kStages; ++n)
        {
            const auto v = (stageIn - stageState[n]) * G;
            const auto y = v + stageState[n];

            stageState[n] = y + v;
            stageIn       = y;
        }

        // Denormales fuera, como en el resto de la familia.
        for (auto& state : stageState)
            if (std::abs(state) < denormalFloor)
                state = 0.0;

        return stageIn;
    }

    //==========================================================================
    /** El corte que se esta usando, ya recortado, en Hz. */
    double getCutoff() const noexcept { return cutoffHz; }

    /** La resonancia que se esta usando, 0..1. */
    double getResonance() const noexcept { return resonance; }

    /** El modo que esta puesto. */
    FilterMode getMode() const noexcept { return currentMode; }

    /** La realimentacion del lazo. 0 es sin realce; el tope (maxFeedback) esta por
        encima del umbral lineal de 4 para que resonancia 1 cante de verdad. Es el
        numero con el que se oye la resonancia en esta topologia, asi que un panel
        lo usa y un test lo mide. */
    double getFeedback() const noexcept { return feedback; }

    //==========================================================================
    /** Cuantas etapas tiene la escalera. Es un dato de la topologia, no un
        parametro: cambiarlo cambia el filtro. */
    static constexpr int kStages = 4;

    /** Cuantas veces se itera sobre el tanh en cada muestra. Dos: el tanh satura
        enseguida y una segunda vuelta ya no mueve la muestra en el rango de uso,
        pero deja la resolucion bien planteada. */
    static constexpr int kSolveIterations = 2;

    /** La realimentacion que corresponde a resonancia 1. El umbral LINEAL de la
        escalera esta en 4.0 y este tope va un 12,5% por encima, que es una decision
        MEDIDA y no una licencia: con el tanh en el lazo, k = 4.0 exacto no canta,
        se apaga, porque saturar reduce la ganancia en cuanto la señal crece. Por
        debajo del umbral la escalera filtra; por encima, canta y el tanh le pone
        el nivel. 4.5 cae en el segundo lado, que es lo que un mando de resonancia
        al tope tiene que hacer. */
    static constexpr double maxFeedback = 4.5;

    /** El corte minimo que este miembro admite, en Hz. */
    static constexpr double minCutoffHz = 10.0;

    /** La fraccion de Nyquist a la que se recorta el corte, igual que en el resto
        de la familia. */
    static constexpr double maxCutoffFraction = 0.49;

    /** Por debajo de este valor una muestra se considera denormal y se anula. */
    static constexpr double denormalFloor = 1.0e-20;

private:
    //==========================================================================
    /** `g` de la forma TPT, con las mismas trascendentes y el mismo orden que en
        el resto de la familia, y de ahi `G = g/(1+g)`, que es la ganancia de un
        paso de la cascada. */
    void updateCoefficients() noexcept
    {
        const auto pi    = static_cast<float>(MathConstants<double>::pi);
        const auto angle = static_cast<float>(static_cast<double>(pi) * cutoffHz / sampleRateHz);

        const auto s = static_cast<double>(sin(angle));
        const auto c = static_cast<double>(cos(angle));

        const auto g = s / (std::abs(c) < 1.0e-6 ? 1.0e-6 : c);

        G         = g / (1.0 + g);
        oneMinusG = 1.0 - G;
    }

    //==========================================================================
    double sampleRateHz    = 44100.0;
    double cutoffHz        = -1.0;
    double resonance       = 0.0;
    FilterMode currentMode = FilterMode::LowPass;

    double G         = 0.0; // ganancia de cada paso de la cascada
    double oneMinusG = 1.0;
    double feedback  = 0.0; // k: 0..maxFeedback

    double stageState[kStages] = {0.0, 0.0, 0.0, 0.0};
};

} // namespace abd::dsp
