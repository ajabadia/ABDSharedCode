/*
  ==============================================================================

    MS2000EqProfile.h
    El ecualizador de DOS REPISAS del MS2000 como PERFIL: solo numeros.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Viene de ABDMS2000, de
    `Source/DSP/Effects/Equalizer.{h,cpp}`.

    QUE ES. La tabla de posiciones del selector del hardware, en hercios. Ni una
    linea de audio. Es la misma dosis que `Re201Profile` (las doce posiciones de
    cabezal del RE-201) y que `ReverbProfile` (las diez variantes del DeepMind
    12): "que numeros son los de esta maquina".

        dsp::CascadeShelfEq<dsp::MS2000EqProfile> eq;
        eq.prepare (sampleRate);
        eq.setLowIndex  (1);        // 250 Hz, la posicion central
        eq.setHighIndex (2);        // 8.0 kHz, la posicion central
        eq.setLowGainDB (-3.0f);
        eq.processFrame (left, right);

    Y POR QUE ESTA AQUI Y NO EN EL PRODUCTO, CUANDO HACE UN TIEMPO ESTABA AL
    REVES. Es la pregunta que toca responder, porque se ha contestado dos veces
    en direcciones opuestas y las dos dejaron un incoherente.

      - La repisa suelta (`ShelfFilter`) esta en el modulo: el motor, la banda
        muerta y el biquad son maquina y los comparten todos.
      - Las DOS tablas de cuatro posiciones se quedaron en `ABDMS2000`, con el
        argumento de que "son politica de producto". Pero el modulo ya tiene las
        tablas del RE-201, del DeepMind 12 y de la Juno-106, que son exactamente
        el mismo tipo de cosa: numeros de un panel de hardware. Dejar las del
        MS2000 en el producto hacia que el mismo modulo tuviera tres maquinas
        representadas y una repartida, y que el motor de la cascada viviera en
        un shim de producto sin una sola comprobacion que lo mirara.

    Que la diferencia sea DONDE ESTA cada tabla, y no SI ESTA, es lo que decide
    esto. La tabla va aqui; la forma de la API que llama el panel --`setLow-
    FreqIndex`, `setHighGainDB`-- se queda en el producto, porque ahi la usan
    `SynthEngine`, `WasmBridge` y el WebUI y no tiene por que cambiar.

    LO QUE ESTA CONGELADO A PROPOSITO Y NO ES UN FALLO. El Q va fijo a
    Butterworth (0,7071) y no es un mando, igual que en el original y igual que
    en `ShelfFilter`. Anadir un Q variable seria un knob mas en la fila y una
    razon mas para que alguien ponga 0,9 y no suene a nada.

  ==============================================================================
*/

#pragma once

namespace abd::dsp
{

//==============================================================================
/**
    Los numeros del ecualizador de dos bandas del MS2000.

    SOLO DATOS. Ni una regla, ni un recorte, ni una llamada: las reglas viven en
    la maquina (`CascadeShelfEq`), que es quien sabe que hacer con un indice que
    se ha pasado y con una ganancia que no es un numero.

    LOS RANGOS DEL PANEL. La ganancia va de 0 a 127 con 64 en el centro, que es
    como la presenta el MS2000; el panel de ABDMS2000 ya lo traduce a dB antes de
    llamar, asi que aqui esta la tabla por si otro producto quiere el mapeo. Ojo
    con ese mapeo: el paso NO es de 1 dB (ver `gainKnobCentre`). Los indices van
    de 0 a 3.
*/
struct MS2000EqProfile
{
    //--- Las dos tablas del selector --------------------------------------
    static constexpr int numPositions = 4;

    static constexpr float lowFreqs[numPositions]  = {160.0f, 250.0f, 400.0f, 600.0f};
    static constexpr float highFreqs[numPositions] = {4000.0f, 6000.0f, 8000.0f, 12000.0f};

    //--- La posicion central es la de fabrica -------------------------------
    // Ni la primera ni la ultima: es donde el hardware se enciende, y por eso
    // un preset nuevo nace en el medio y no echado hacia un lado.
    static constexpr int defaultLowIndex  = 1; // 250 Hz
    static constexpr int defaultHighIndex = 2; // 8.0 kHz

    //--- Ganancia -----------------------------------------------------------
    static constexpr float gainMinDb         = -12.0f;
    static constexpr float gainMaxDb         = 12.0f;
    static constexpr float defaultLowGainDb  = 0.0f;
    static constexpr float defaultHighGainDb = 0.0f;

    //--- El mando del panel, de 0 a 127 con 64 en el centro ------------------
    //
    // MEDIDO que el tope del MS2000 es +-12 dB, no los +-15 de algunos
    // ecualizadores de la epoca.
    //
    // OJO CON LA ARITMETICA, QUE NO ES LO QUE PARECE. Antes esto decia "1 dB
    // por paso" y es FALSO. 0..127 son 128 pasos y solo hay 64 a cada lado del
    // centro, asi que 24 dB de recorrido repartidos en 64 pasos dan 0,1875 dB por
    // paso, no 1. Quien mapease el mando creyendo lo del comentario se
    // encontraria con un ecualizador de +-4 dB en vez de +-12, y como el tope
    // de `gainMaxDb` no avisa, sonaria raro sin romperse.
    //
    // El mapeo que SI sale de estos numeros es
    //     db = (valor - gainKnobCentre) * (gainMaxDb - gainMinDb) / gainKnobCentre
    // y por eso el denominador es el CENTRO, no `gainKnobSteps`.
    static constexpr int gainKnobSteps  = 127;
    static constexpr int gainKnobCentre = 64;
};

} // namespace abd::dsp
