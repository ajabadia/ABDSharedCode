/*
  ==============================================================================

    DspFilterFamily.h
    LA FAMILIA DE FILTROS: el contrato comun y el idioma de sus parametros
    (namespace abd::dsp).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspCore).

    EL PROBLEMA QUE RESUELVE. En este repo hay (y va a haber) varios filtros que
    por dentro no se parecen en nada: una etapa TPT de dos estados, una tabla
    medida en un banco de pruebas, una ecuacion de escalera no lineal resuelta
    muestra a muestra. Que por dentro no se parezcan esta BIEN: son filtros
    distintos y suenan distinto. Lo que no puede pasar es que cada uno hable un
    idioma distinto, porque entonces un synth no puede cambiar uno por otro sin
    tocar su motor, y "tomar una pieza y engancharla" se convierte en "adaptarla
    cada vez", que es exactamente lo que se queria evitar.

    LO QUE ES ESTA FAMILIA. Un contrato por CONVENCION, igual que el de
    DspEffects/EffectPolicy.h: no hay clase base ni vtable en el lazo de audio,
    hay un juego de metodos que todo miembro honra y un rasgo (IsFilterStage) que
    lo comprueba en COMPILACION. Un filtro que no los tenga no es "un miembro mas
    flojo": no es de la familia, y eso se sabe al compilar y no al oirlo.

        void   prepare (double sampleRate) noexcept;    // una vez, y al cambiar fs
        void   reset () noexcept;                       // la nota no arranca con la cola anterior
        void   setCutoff (double hz) noexcept;          // corte en Hz, se recorta
        void   setResonance (double amount01) noexcept; // 0..1, ver el idioma de abajo
        bool   setMode (FilterMode) noexcept;           // false = no lo honra (no miente)
        double processSample (double input) noexcept;   // una muestra por dentro
        double getCutoff () const noexcept;             // el corte que SE ESTA usando
        double getResonance () const noexcept;          // la resonancia que SE ESTA usando
        bool   supportsMode (FilterMode) noexcept;      // que modos sabe hacer

    Y el BLOQUE sale de aqui una vez para todos (processBlock, mas abajo): es la
    misma frase para un buffer float de un host y para uno double interno, y no
    hay motivo para escribirla en cada implementacion.

    EL IDIOMA DE LOS PARAMETROS, QUE ES LA MITAD DE LA FAMILIA. Que todos tengan
    los mismos metodos no basta: si para uno "corte 0.5" son 1 kHz y para otro
    son 8 kHz, cambiar de filtro cambia la afinacion del panel, que es peor que
    no poder cambiar. Por eso:

      - el corte se pide en HZ, que es una unidad y no una interpretacion, y
      - la resonancia se pide en 0..1 CON EL MISMO SIGNIFICADO en todos: 0 es un
        filtro sin realce y 1 es el borde de la auto-oscilacion.

    La CURVA con la que cada topologia llega a ese borde es SUYA y la declara:
    una Q exponencial de 1/sqrt(2) a 20 en la familia TPT, la realimentacion de una
    escalera de transistores en la de ecuacion. Eso no es una
    inconsistencia, es la diferencia entre dos filtros; lo que la familia si fija
    es que el 0 y el 1 signifiquen lo mismo en los dos, y que el 0 de la Q sea
    EXACTAMENTE el mismo numero que el de la etapa compartida (filterMinQ es
    baseQ de ResonantFilterStage), para que "TPT de la familia" y "etapa
    compartida" sean el mismo filtro a resonancia 0 y no dos parecidos.

    QUE NO VIVE AQUI, Y POR QUE. La CASCADA (cuantas etapas, con que Q) es de la
    maquina y no del sustrato: lo dice DspResonantFilter.h y sigue siendo verdad.
    Aqui vive UN filtro, el que el producto enchufa en su voz. Encadenar etapas
    es del host.

    COMO SE ENGANCHA UNO, Y COMO SE CAMBIA. Un producto que quiere el TPT
    escribe `abd::dsp::FilterTpt`; uno que quiere la ecuacion, escribe
    `abd::dsp::FilterEquation`; uno que quiere la respuesta MEDIDA de una tabla
    de banco de pruebas, `abd::lutdsp::FilterLut`. Y el que quiera poder
    cambiarlo con un renglon:

        using VoiceFilter = abd::dsp::FilterEquation;   // y ya

    porque los tres honran el mismo contrato y el mismo idioma.

    LO QUE LA FAMILIA TODAVIA NO TRAE, DICHO EN VOZ ALTA. Un contenedor que
    guarde "uno de los tres" en TIEMPO DE EJECUCION. Eso necesita una
    representacion comun y una rama por muestra, y hoy ningun consumidor lo pide;
    el dia que lo pida se anade aqui, con su rama medida. Tampoco trae un
    contenedor para el miembro LUT: vive en LutDSP (que depende de este modulo y
    no al reves), asi que un `switch` de tiempo de ejecucion sobre los tres se
    escribe en el consumidor, que es el unico que ve las dos capas.

  ==============================================================================
*/

#pragma once

// DspMath.h Y NO DspCore.h, Y NO ES UN CAPRICHO DE ESTILO. Este fichero lo
// incluye DspCore.h (al final, como DspResonantFilter.h), y sus dos miembros
// (DspFilterTpt.h y DspFilterEquation.h) incluyen ESTE a su vez. Si aqui se
// incluyera DspCore.h, entrar por cualquiera de los miembros creaba un ciclo
// medido: el miembro -> la familia -> DspCore.h -> (al final) el miembro otra vez,
// y el compilador llegaba al cuerpo de la familia DESPUES de haber compilado el
// miembro, con `FilterMode` y las curvas todavia sin declarar. El ciclo se corta
// pidiendo lo UNICO que este fichero necesita (exp2/log2 de DspMath, que no
// incluye a nadie) en vez del paraguas entero.
#include "DspMath.h"

#include <cmath>
#include <type_traits>

namespace abd::dsp
{

//==============================================================================
/** El modo de un filtro de la familia. Tres, que son los que un synth pide de un
    filtro de voz; no esta la lista entera de un multimodo de plugin porque la
    familia describe VOZ, no pedalera. */
enum class FilterMode
{
    LowPass,
    HighPass,
    BandPass
};

/** El bit de un modo, para que un miembro declare los que sabe hacer en una sola
    constante: `filterModeBit (LowPass) | filterModeBit (BandPass)`. */
constexpr unsigned filterModeBit(FilterMode mode) noexcept
{
    return 1u << static_cast<unsigned>(mode);
}

//==============================================================================
/** De que esta hecho un miembro de la familia. Es un dato de PANEL y de
    DIAGNOSTICO (que filtro lleva puesta la voz), no un comportamiento: la
    familia no ramifica por esto en el lazo de audio. */
enum class FilterKind
{
    Tpt,     // etapa/cadena TPT: estado explicito, sin tabla y sin iteracion
    Lut,     // tabla medida: el corte sale de una curva de banco de pruebas
    Equation // ecuacion de circuito resuelta por muestra, con no linealidad
};

//==============================================================================
/** Los extremos del recorrido de corte que la familia considera suyo, en Hz. */
inline constexpr double filterMinCutoffHz = 20.0;
inline constexpr double filterMaxCutoffHz = 20000.0;

/** Recorta a 0..1. Un valor fuera de rango no es un error: es un mando en su
    tope o una automatizacion que se ha pasado. */
inline double filterClamp01(double value) noexcept
{
    return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}

/** De un mando de corte (0..1) a Hz, EXPONENCIAL entre 20 Hz y 20 kHz: es como
    se oye un filtro, y por eso la curva es de la familia y no de cada miembro.

    Las trascendentes son las de DspMath (float, deterministicas), no las de la
    libm: es la invariante de paridad nativo <-> WASM del modulo, y una curva de
    panel que no diese el mismo numero en los dos sitios seria una desafinacion
    distinta en cada build. */
inline double cutoffHzFromNormalized(double normalized) noexcept
{
    const auto ratio = static_cast<float>(filterMaxCutoffHz / filterMinCutoffHz);
    const auto t     = static_cast<float>(filterClamp01(normalized));

    return filterMinCutoffHz * static_cast<double>(exp2(static_cast<float>(log2(ratio) * t)));
}

/** El inverso de cutoffHzFromNormalized, para lo que llega en Hz y hay que buscar
    en una tabla indexada por la POSICION del mando (es el caso del miembro LUT). */
inline double normalizedFromCutoffHz(double hz) noexcept
{
    const auto ratio   = static_cast<float>(filterMaxCutoffHz / filterMinCutoffHz);
    const auto clamped = hz < filterMinCutoffHz ? filterMinCutoffHz
                                                : (hz > filterMaxCutoffHz ? filterMaxCutoffHz : hz);

    const auto v = static_cast<float>(clamped / filterMinCutoffHz);

    return filterClamp01(static_cast<double>(log2(v) / log2(ratio)));
}

/** La Q mas baja de la familia: 1/sqrt(2), el Butterworth de segundo orden.
    NO es un numero elegido aqui: es baseQ de ResonantFilterStage, y la igualdad
    es lo que hace que el miembro TPT de la familia y la etapa compartida sean el
    mismo filtro a resonancia 0. Hay un test que lo comprueba muestra a muestra,
    y si alguien cambia uno de los dos numeros, ese test lo dice. */
inline constexpr double filterMinQ = 0.70710678118654752;

/** La Q mas alta del recorrido de resonancia de la familia: el borde. */
inline constexpr double filterMaxQ = 20.0;

/** Resonancia 0..1 a Q, EXPONENCIAL: la Q se oye exponencialmente, asi que un
    mando lineal sobre ella se siente mal en la mitad baja del recorrido. */
inline double filterResonanceToQ(double resonance) noexcept
{
    const auto ratio = static_cast<float>(filterMaxQ / filterMinQ);
    const auto t     = static_cast<float>(filterClamp01(resonance));

    return filterMinQ * static_cast<double>(exp2(static_cast<float>(log2(ratio) * t)));
}

/** El inverso de filterResonanceToQ. Lo necesita un miembro que recibe una Q
    MEDIDA (una tabla) y tiene que hablarle a su nucleo en 0..1, que es el idioma
    de la familia. */
inline double filterQToResonance(double q) noexcept
{
    const auto ratio   = static_cast<float>(filterMaxQ / filterMinQ);
    const auto clamped = q < filterMinQ ? filterMinQ : (q > filterMaxQ ? filterMaxQ : q);

    const auto v = static_cast<float>(clamped / filterMinQ);

    return filterClamp01(static_cast<double>(log2(v) / log2(ratio)));
}

//==============================================================================
/** El rasgo que convierte "por convencion" en "lo comprueba el compilador".

    `static_assert (IsFilterStage<MiFiltro>::value, "no es de la familia")` es la
    forma de decir que un filtro nuevo no se ha quedado a medias: sin esto, el
    contrato se cumple por EDUCACION, y la educacion no se compila.

    Se comprueba la LLAMADA y no la firma exacta (el tipo de retorno se convierte
    solo): lo que importa es que la frase se pueda decir, y un `noexcept` o un
    `bool` de mas no hacen que un filtro deje de ser de la familia. */
template <typename T, typename = void>
struct IsFilterStage : std::false_type
{
};

template <typename T>
struct IsFilterStage<T, std::void_t<
                            decltype(std::declval<T&>().prepare(0.0)),
                            decltype(std::declval<T&>().reset()),
                            decltype(std::declval<T&>().setCutoff(0.0)),
                            decltype(std::declval<T&>().setResonance(0.0)),
                            decltype(std::declval<T&>().setMode(FilterMode::LowPass)),
                            decltype(std::declval<T&>().processSample(0.0)),
                            decltype(std::declval<const T&>().getCutoff()),
                            decltype(std::declval<const T&>().getResonance()),
                            decltype(std::declval<const T&>().supportsMode(FilterMode::LowPass))>> : std::true_type
{
};

//==============================================================================
/** El bloque, para todos, una vez: la misma frase para el buffer float de un host
    y para el double interno de quien no pasa por JUCE.

    Un miembro NO tiene que implementar processBlock para entrar en la familia; el
    bucle es de la familia, y asi ninguna implementacion puede olvidarse de los
    dos formatos ni de la conversion. */
template <typename FilterStage>
void processBlock(FilterStage& filter, float* buffer, int numSamples) noexcept
{
    if (buffer == nullptr)
        return;

    for (int i = 0; i < numSamples; ++i)
        buffer[i] = static_cast<float>(filter.processSample(static_cast<double>(buffer[i])));
}

template <typename FilterStage>
void processBlock(FilterStage& filter, double* buffer, int numSamples) noexcept
{
    if (buffer == nullptr)
        return;

    for (int i = 0; i < numSamples; ++i)
        buffer[i] = filter.processSample(buffer[i]);
}

} // namespace abd::dsp
