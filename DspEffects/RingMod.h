/*
  ==============================================================================

    RingMod.h
    Motor de modulacion en anillo, con el puente de diodos INYECTADO.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Ver DspEffects/EffectPolicy.h para el contrato.

    QUE ES Y QUE NO. Aqui vive la MAQUINA: el oscilador modulador con sus tres
    formas de onda, el LFO que le modula la frecuencia, la multiplicacion de
    la entrada por el modulador y la mezcla con la seca. NO vive aqui el
    recorte del puente de diodos (es la etapa inyectada), ni el suavizado, ni
    el mapeo 0..1 -> Hz: eso es del consumidor o del perfil.

    POR QUE EL RANGO DE FRECUENCIA ESTA EN EL PERFIL Y NO AQUI. Un modulador de
    pedal barre de 20 Hz a un par de kHz; un IRCAM de laboratorio llega a 8 kHz
    porque su oscilador era de laboratorio. Son numeros de la maquina, asi que
    van en el perfil (`RingModProfile`), no en el motor: asi el mismo motor da
    el pedal y el IRCAM, y ninguno de los dos sabe del otro.

    CONTRATO DE LLAMADA. Por cada muestra: processSample() para CADA canal y
    despues advance() UNA vez. El modulador es MONO de proposito (un anillo de
    verdad tiene un solo modulador y el segundo canal sale por el mismo), y la
    fase del LFO es comun a los dos canales: si se avanzara por canal, el
    stereo se descentrarla en cada bloque.

    POR QUE LA FRECUENCIA SOLO SE PIDE EN `advance`. Porque la frecuencia
    determina el INCREMENTO de fase del modulador, y el LFO que la modula
    avanza una vez por muestra, no una por canal. Pedirla tambien en
    `processSample` para no usarla seria el pie de heno de la API, y sobre
    todo haria creer que el resultado depende de ella. El motor no guarda
    copia de ningun control entre llamadas: lo que se le pasa es lo que usa.

    LA MEZCLA ES DEL CONSUMIDOR. Aqui sale la senal HUMEDA ya formada; el peso
    seco/humedo lo aporta quien la llama, por la regla del modulo.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "DspEffects/EffectPolicy.h"
#include "DspEffects/characters/DiodeBridge.h"

namespace abd::dsp
{

//==============================================================================
/** Rango de frecuencia del modulador y sus tres formas de onda, por maquina. */
struct RingModProfile
{
    static constexpr float minFrequencyHz = 20.0f;
    static constexpr float maxFrequencyHz = 8000.0f;
    static constexpr float lfoMinHz       = 0.1f;
    static constexpr float lfoMaxHz       = 10.0f;

    /** Umbrales de seleccion de forma de onda sobre el parametro 0..1. */
    static constexpr float sineUpper = 0.33f;
    static constexpr float sawUpper  = 0.66f;
};

//==============================================================================
/**
    Modulador en anillo: entrada por un oscilador y un puente de diodos.

    `Profile` son los numeros de la maquina; `Colour` es la no linealidad
    inyectada (DiodeBridge en un anillo de verdad, NullStage para un producto
    digital limpio).
*/
template <typename Profile = RingModProfile, typename Colour = DiodeBridge>
class RingMod
{
public:
    RingMod() = default;

    /** Fija el sample rate. */
    void prepare(double sampleRate)
    {
        dspAssert(sampleRate > 0.0);

        sampleRate_ = sampleRate;
        colour.prepareSampleRate(sampleRate);
        reset();
    }

    /** Vuelve a fase cero. Es estado de audio, no de control. */
    void reset() noexcept
    {
        oscPhase_ = 0.0f;
        lfoPhase_ = 0.0f;
        colour.reset();
    }

    /**
        Procesa UNA muestra de UN canal. Devuelve la senal HUMEDA: el peso
        seco/humedo lo pone el consumidor.

        Solo necesita la forma de onda. La frecuencia NO se lee aqui, y no es
        un descuido: la frecuencia determina el INCREMENTO de fase, y ese lo
        aplica `advance`. Pasarla tambien aqui solo para no usarla seria el
        pie de heno de la API y daria la impresion de que el resultado depende
        de ella cuando en realidad depende del `advance` que la accompanies.

        waveformNorm 0..1, selecciona la forma de onda del perfil.
    */
    float processSample(int channel, float input, float waveformNorm) noexcept
    {
        ignoreUnused(channel); // el modulador es mono: un anillo tiene uno

        const float modulator = oscillator(oscPhase_, waveformNorm);

        // El producto de dos senales es una campana de lados infinita; el
        // puente de diodos es lo que la hace sonar a modulacion y no a
        // multiplicacion. Con NullStage sale el producto limpio, que es lo
        // que quiere un producto digital.
        return colour.processSample(input * modulator, drive_, threshold_);
    }

    /**
        Avanza el modulador y el LFO. UNA vez por muestra, tras los canales.

        Aqui es donde la frecuencia se aplica de verdad, mapeada al rango del
        perfil con escala EXPONENCIAL. Es deliberado: barrido de forma lineal
        de 20 a 8000 Hz, el 90% del recorrido cae en el primer 10% del mando y
        los barridos utiles (el "duck" grave) quedan todos apretados al
        principio.
    */
    void advance(float frequencyNorm, float lfoRateNorm, float lfoDepthNorm) noexcept
    {
        const float twoPi = MathConstants<float>::twoPi;
        const float sr    = static_cast<float>(sampleRate_);

        oscPhase_ += twoPi * modulatedFrequency(frequencyNorm, lfoDepthNorm) / sr;

        // La fase se deja correr y se dobla cada 1000 vueltas: es un
        // acumulador, no un indice, y a partir de 2^13 vueltas el redondeo de
        // float se come el modulador. El `if` de una sola resta solo valia
        // mientras la incremento fuera menor que ese tope: con una frecuencia
        // modular por encima de 1000 veces el sample rate (o con un
        // `frequencyNorm` roto) la fase se iba sin limite. `wrapPhase` lo
        // envuelve en un paso, se comporte igual en el caso normal.
        oscPhase_ = dsp::wrapPhase(oscPhase_, twoPi * 1000.0f);

        const float lfoHz = Profile::lfoMinHz + (Profile::lfoMaxHz - Profile::lfoMinHz) * jlimit(0.0f, 1.0f, lfoRateNorm);

        lfoPhase_ += twoPi * lfoHz / sr;

        // El `lfoHz` sale de las constantes del perfil, asi que hoy no puede
        // pasar de 2*pi y el `if` de una sola resta bastaba. No hacia falta que
        // se quedara asi: un perfil futuro con un lfoMax por encima del sample
        // rate lo rompia en silencio, y la fase feeds del modulador, que esta
        // en la senal de audio. `wrapPhase` envuelve en un paso y ademas
        // reinicia el acumulador si algun parametro llega en NaN, que con el
        // `if` se quedaba envenenado para siempre.
        lfoPhase_ = dsp::wrapPhase(lfoPhase_, twoPi);
    }

    /** Ganancia del puente de diodos (0 lo apaga). */
    void setDrive(float drive) noexcept { drive_ = drive; }

    /** Umbral del puente de diodos. */
    void setThreshold(float threshold) noexcept { threshold_ = threshold; }

    /** La frecuencia del modulador SIN LFO, en Hz (para la interfaz). */
    float getFrequencyHz(float frequencyNorm) const noexcept
    {
        return baseFrequency(frequencyNorm);
    }

    /** Sample rate de la ultima llamada a `prepare`. */
    double getSampleRate() const noexcept { return sampleRate_; }

private:
    //==========================================================================
    /** La frecuencia del mando, mapeada al rango del perfil con escala
        exponencial (ver la nota del `processSample`). */
    static float baseFrequency(float normalised) noexcept
    {
        return Profile::minFrequencyHz * pow(Profile::maxFrequencyHz / Profile::minFrequencyHz,
                                             jlimit(0.0f, 1.0f, normalised));
    }

    /** La frecuencia de ESTA muestra: la del mando, ya modulada por el LFO.
        Modulación bipolar estándar: barre simétricamente alrededor de la frecuencia
        base (1.0 + mod) y se recorta en cero para evitar incrementos negativos. */
    float modulatedFrequency(float frequencyNorm, float lfoDepthNorm) const noexcept
    {
        const float mod = sin(lfoPhase_) * jlimit(0.0f, 1.0f, lfoDepthNorm);
        return baseFrequency(frequencyNorm) * jmax(0.0f, 1.0f + mod);
    }

    /** Las tres formas del perfil, sobre una fase en radianes. */
    static float oscillator(float phase, float waveform) noexcept
    {
        if (waveform < Profile::sineUpper)
            return sin(phase);

        const float normalised = phase - static_cast<float>(floorToInt(phase));

        if (waveform < Profile::sawUpper)
            return normalised * 2.0f - 1.0f; // sierra

        return normalised < 0.5f ? 1.0f : -1.0f; // cuadrada
    }

    Colour colour;

    double sampleRate_ = 44100.0;
    float oscPhase_    = 0.0f;
    float lfoPhase_    = 0.0f;
    float drive_       = 0.8f;
    float threshold_   = 0.3f;

    dspDeclareNonCopyableWithLeakDetector(RingMod)
};

} // namespace abd::dsp
