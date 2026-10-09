/*
  ==============================================================================

    OscillatorFamily.h
    LA FAMILIA DE OSCILADORES: el contrato comun y el idioma de sus parametros
    (namespace abd::synth).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::SynthCore), que
    es donde ya vivian los primitivos de oscilador (PolyBLEP) y donde la voz
    comparte su ciclo de vida con la envolvente y el glide.

    EL PROBLEMA QUE RESUELVE. Un synth lleva osciladores que por dentro no se
    parecen en nada: un acumulador de fase con correcciones polyBLEP, un nucleo
    analogico de rampa integrada con un reinicio de comparador, una tabla de
    ondas, un oscilador de distorsion de fase. Que por dentro no se parezcan
    esta BIEN: son osciladores distintos y suenan distinto. Lo que no puede pasar
    es que cada uno hable un idioma distinto, porque entonces cambiar de
    oscilador obliga a tocar el motor de la voz, y "tomar una pieza y
    engancharla" se convierte en "adaptarla cada vez".

    LO QUE ES ESTA FAMILIA. Un contrato por CONVENCION, como el de la familia de
    filtros (DspCore/DspFilterFamily.h) y el de DspEffects/EffectPolicy.h: no hay
    clase base ni vtable en el lazo de audio, hay un juego de metodos que todo
    miembro honra y un rasgo (IsOscillator) que lo comprueba en COMPILACION. Un
    oscilador que no los tenga no es "un miembro mas flojo": no es de la familia,
    y eso se sabe al compilar y no al oirlo.

        void         prepare (double sampleRate) noexcept;   // una vez, y al cambiar fs
        void         reset () noexcept;                      // la nota no arranca con la cola anterior
        void         setFrequency (double hz) noexcept;      // Hz, se recorta a lo que la pieza da
        bool         setWaveform (OscWaveform) noexcept;     // false = no lo honra (no miente)
        bool         setOversampling (int factor) noexcept;  // 1/2/4/8; false = no lo honra
        void         setPulseWidth (double duty01) noexcept; // solo afecta al rectangulo
        double       processSample () noexcept;              // una muestra, bipolar
        double       getFrequency () const noexcept;         // la frecuencia que SE ESTA usando
        OscWaveform  getWaveform () const noexcept;          // la forma que SE ESTA usando
        int          getOversampling () const noexcept;      // el factor que SE ESTA usando
        double       getPulseWidth () const noexcept;        // el ancho que SE ESTA usando
        double       getPhase () const noexcept;             // 0..1, para sync y para el panel
        double       getLatencySamples () const noexcept;    // lo que la pieza retrasa la salida
        static bool  supportsWaveform (OscWaveform) noexcept;    // que formas sabe hacer
        static bool  supportsOversampling (int factor) noexcept; // que factores sabe hacer

    Y el BLOQUE sale de aqui una vez para todos (processBlock, mas abajo): es la
    misma frase para un buffer float de un host y para uno double interno, y no
    hay motivo para escribirla en cada implementacion.

    EL IDIOMA DE LOS PARAMETROS, QUE ES LA MITAD DE LA FAMILIA. Que todos tengan
    los mismos metodos no basta: si para uno "frecuencia 440" son 440 Hz y para
    otro son un numero de mando, cambiar de oscilador desafina la voz entera, que
    es peor que no poder cambiar. Por eso:

      - la frecuencia se pide en HZ, que es una unidad y no una interpretacion
        (y la pieza devuelve en getFrequency() la que de verdad esta usando: si
        le han pedido 30 kHz y su nucleo llega a 6, no dice que si);
      - la forma de onda se pide con un enumerado de formas BASICAS, y cada
        miembro dice con supportsWaveform() cuales sabe hacer de verdad (una
        tabla de ondas no tiene seno de circuito, y un nucleo de rampa no tiene
        por que tener seno: que lo diga es mas util que un seno aproximado);
      - el ancho de pulso se pide en 0..1 con EL MISMO SIGNIFICADO en todos: 0.5
        es un cuadrada, y 0 y 1 son los extremos que la pieza pueda dar;
      - la salida es BIPOLAR y nominalmente +-1 (oscNominalPeak), para que
        cambiar de oscilador no cambie el nivel que ve el mezclador;
      - el sobremuestreo se pide como factor 1/2/4/8, con el mismo significado:
        cuantas muestras internas por muestra de salida.

    La CURVA con la que cada miembro coloca su ancho de pulso, el pliegue de su
    triangulo o su propio caracter es SUYA y la declara: eso no es una
    inconsistencia, es la diferencia entre dos osciladores. Lo que la familia si
    fija es que 0.5 de ancho signifique cuadrada en los dos, y que +-1 signifique
    el mismo nivel en los dos.

    QUE NO VIVE AQUI, Y POR QUE. El MEZCLADOR (cuantos osciladores, con que
    niveles, con que desafinado) es de la maquina y no del sustrato, igual que la
    cascada de filtros: aqui vive UN oscilador, el que el producto enchufa en su
    voz. Tampoco vive aqui un contenedor que guarde "uno de los dos" en tiempo de
    ejecucion: necesita una representacion comun y una rama por muestra, y hoy
    ningun consumidor lo pide.

    COMO SE ENGANCHA UNO, Y COMO SE CAMBIA. Un producto que quiere el nucleo
    analogico de rampa escribe `abd::synth::OscVcoCa72`; uno que quiere un
    acumulador de fase con polyBLEP, `abd::synth::OscPolyBlep`. Y el que quiera
    poder cambiarlo con un renglon:

        using VoiceOsc = abd::synth::OscVcoCa72;   // y ya

    porque los dos honran el mismo contrato y el mismo idioma.

  ==============================================================================
*/

#pragma once

#include <cmath>
#include <type_traits>

namespace abd::synth
{

//==============================================================================
/** La forma BASICA de un oscilador de la familia. Las cuatro que un synth pide
    de un oscilador de voz; no esta la lista entera de un wavetable porque la
    familia describe VOZ, no coleccion de ondas. */
enum class OscWaveform
{
    Sawtooth,
    Triangle,
    Rectangle,
    Sine
};

/** El bit de una forma, para que un miembro declare las que sabe hacer en una
    sola constante: `oscWaveformBit (OscWaveform::Sawtooth) |
    oscWaveformBit (OscWaveform::Rectangle)`. */
constexpr unsigned oscWaveformBit(OscWaveform waveform) noexcept
{
    return 1u << static_cast<unsigned>(waveform);
}

//==============================================================================
/** De que esta hecho un miembro de la familia. Es un dato de PANEL y de
    DIAGNOSTICO (que oscilador lleva puesta la voz), no un comportamiento: la
    familia no ramifica por esto en el lazo de audio. */
enum class OscKind
{
    Phase,  // acumulador de fase: la muestra sale de la fase y una correccion
    Circuit // nucleo de circuito resuelto por muestra, con su tiempo dentro
};

//==============================================================================
/** Los extremos del recorrido de frecuencia que la familia considera suyo, en
    Hz. No son los que cada miembro da: son los del idioma (lo que se puede
    PEDIR). Un nucleo analogico de verdad empieza por encima de 0.05 Hz y un
    acumulador de fase llega hasta Nyquist; quien recorta a lo suyo es cada
    pieza, y lo dice devolviendo de getFrequency() lo que esta usando. */
inline constexpr double oscMinFrequencyHz = 0.05;
inline constexpr double oscMaxFrequencyHz = 20000.0;

/** Recorta a 0..1. Un valor fuera de rango no es un error: es un mando en su
    tope o una automatizacion que se ha pasado. */
inline double oscClamp01(double value) noexcept
{
    return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}

/** Recorta la frecuencia al recorrido de la familia, en Hz. */
inline double oscClampFrequencyHz(double hz) noexcept
{
    if (!(hz > oscMinFrequencyHz)) // (NaN y los negativos caen aqui)
        return oscMinFrequencyHz;

    return hz > oscMaxFrequencyHz ? oscMaxFrequencyHz : hz;
}

/** De una nota MIDI a Hz, con el LA 440 como referencia y la afinacion
    temperada igual que DSPUtils::midiNoteToFrequency.

    La trascendente es la de <cmath> y no la determinista de DspCore (DspMath.h)
    a proposito: SynthCore no depende de DspCore (son modulos hermanos, y
    arrastrar el sustrato entero por una curva de nota seria cambiar las
    dependencias del modulo por comodidad). Si algun dia un consumidor exige
    paridad bit a bit nativo <-> WASM en esta curva, la curva se muda a DspMath
    como se hizo con la del corte de la familia de filtros: el aviso esta aqui
    para que la decision se tome a proposito y no por sorpresa. */
inline double oscFrequencyHzFromNote(double midiNote) noexcept
{
    const auto semitones = static_cast<float>((midiNote - 69.0) / 12.0);

    return 440.0 * static_cast<double>(exp2(semitones));
}

/** El inverso de oscFrequencyHzFromNote, para lo que llega en Hz y hay que
    situar en el teclado (un panel que dibuja la nota, un afinador). */
inline double oscNoteFromFrequencyHz(double hz) noexcept
{
    const auto clamped = oscClampFrequencyHz(hz);
    const auto ratio   = static_cast<float>(clamped / 440.0);

    return 69.0 + 12.0 * static_cast<double>(log2(ratio));
}

/** El ancho de pulso mas estrecho que la familia admite. Un pulso de ancho 0 no
    es un pulso: es silencio, y una pieza que lo aceptara dejaria de sonar sin
    decir por que. */
inline constexpr double oscMinPulseWidth = 0.02;

/** El ancho de pulso mas ancho que la familia admite, por el mismo motivo. */
inline constexpr double oscMaxPulseWidth = 0.98;

/** Recorta un ancho de pulso (0..1) a lo que la familia admite. */
inline double oscClampPulseWidth(double duty) noexcept
{
    if (!(duty > oscMinPulseWidth)) // (NaN y los negativos caen aqui)
        return oscMinPulseWidth;

    return duty > oscMaxPulseWidth ? oscMaxPulseWidth : duty;
}

/** El pico nominal de la salida de la familia. La salida es bipolar y cabe en
    +-oscNominalPeak; que el 1 sea el MISMO 1 en todas las piezas es lo que hace
    que cambiar de oscilador no cambie el nivel que ve el mezclador. */
inline constexpr double oscNominalPeak = 1.0;

//==============================================================================
/** El rasgo que convierte "por convencion" en "lo comprueba el compilador".

    `static_assert (IsOscillator<MiOscilador>::value, "no es de la familia")` es
    la forma de decir que un oscilador nuevo no se ha quedado a medias: sin esto,
    el contrato se cumple por EDUCACION, y la educacion no se compila.

    Se comprueba la LLAMADA y no la firma exacta (el tipo de retorno se convierte
    solo): lo que importa es que la frase se pueda decir, y un `noexcept` o un
    `bool` de mas no hacen que un oscilador deje de ser de la familia. */
template <typename T, typename = void>
struct IsOscillator : std::false_type
{
};

template <typename T>
struct IsOscillator<T, std::void_t<
                           decltype(std::declval<T&>().prepare(0.0)),
                           decltype(std::declval<T&>().reset()),
                           decltype(std::declval<T&>().setFrequency(0.0)),
                           decltype(std::declval<T&>().setWaveform(OscWaveform::Sawtooth)),
                           decltype(std::declval<T&>().setOversampling(1)),
                           decltype(std::declval<T&>().setPulseWidth(0.5)),
                           decltype(std::declval<T&>().processSample()),
                           decltype(std::declval<const T&>().getFrequency()),
                           decltype(std::declval<const T&>().getWaveform()),
                           decltype(std::declval<const T&>().getOversampling()),
                           decltype(std::declval<const T&>().getPulseWidth()),
                           decltype(std::declval<const T&>().getPhase()),
                           decltype(std::declval<const T&>().getLatencySamples()),
                           decltype(T::supportsWaveform(OscWaveform::Sawtooth)),
                           decltype(T::supportsOversampling(1))>> : std::true_type
{
};

//==============================================================================
/** El bloque, para todos, una vez: la misma frase para el buffer float de un host
    y para el double interno de quien no pasa por JUCE.

    Un miembro NO tiene que implementar processBlock para entrar en la familia; el
    bucle es de la familia, y asi ninguna implementacion puede olvidarse de los
    dos formatos ni de la conversion. */
template <typename Oscillator>
void processBlock(Oscillator& oscillator, float* buffer, int numSamples) noexcept
{
    if (buffer == nullptr)
        return;

    for (int i = 0; i < numSamples; ++i)
        buffer[i] = static_cast<float>(oscillator.processSample());
}

template <typename Oscillator>
void processBlock(Oscillator& oscillator, double* buffer, int numSamples) noexcept
{
    if (buffer == nullptr)
        return;

    for (int i = 0; i < numSamples; ++i)
        buffer[i] = oscillator.processSample();
}

} // namespace abd::synth
