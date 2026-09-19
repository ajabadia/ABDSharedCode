/*
  ==============================================================================

    DspChorus.h
    Chorus estereo de linea retardada modulada (lectura interpolada lineal).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Vivio en ABDNeural/Source/DSP/Effects/Chorus.h hasta la
    migracion de efectos; ahi queda solo el envoltorio de producto.

    QUE ES ESTE FICHERO (y que NO). Aqui vive SOLO la maquina: el buffer circular
    de 2 canales, el LFO de fase que modula la posicion de lectura entre 5ms y
    30ms, la lectura interpolada y la mezcla wet/dry. NO vive aqui el suavizado
    de parametros (dsp::LinearSmoothedValue, 20ms en NEURONiK), que es politica
    del producto y entra POR MUESTRA: asi estaba en el original (los smoothers se
    leian dentro del bucle de muestras) y asi se conserva la salida bit a bit.

    CONTRATO DE LLAMADA (lo unico no obvio de la API):
      por cada muestra -> processSample() para CADA canal y despues advance()
      UNA vez, con la frecuencia de ESA muestra. La fase del LFO y el puntero de
      escritura son comun a los canales: el canal entra en el buffer como
      `channel % 2`, igual que en el original (un buffer de 2 canales, siempre).

    DESVIACION DOCUMENTADA (la unica): prepare() pone el puntero de escritura a 0.
    El original no lo hacia, y era un bug latente: con un prepare() en caliente
    (cambio de sample rate) el puntero podia quedar por encima del buffer nuevo y
    la primera escritura se salia del rango. En el uso normal (un prepare antes de
    procesar) el puntero ya valia 0, asi que no cambia ningun resultado definido.
    reset(), en cambio, sigue sin tocarlo (solo vacia el buffer), como el original.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"

#include <cmath>

namespace abd::dsp
{

//==============================================================================
/**
    Chorus estereo de una linea retardada modulada por LFO.

    Sin suavizado de parametros ni mapeo: el consumidor aporta rate/depth/mix de
    cada muestra (ver el contrato en la cabecera).
*/
class Chorus
{
public:
    /** Capacidad por defecto: 2 canales x 100ms @ 44.1 kHz, como el original. */
    Chorus()
        : delayBuffer (2, 4096)
    {
        delayBuffer.clear();
    }

    /** Fija el sample rate y la capacidad del buffer (100ms por defecto, que es
        el maximo que necesita la modulacion de 5ms..30ms). */
    void prepare (double sampleRate, double maxDelaySeconds = 0.1)
    {
        dspAssert (sampleRate > 0.0);
        dspAssert (maxDelaySeconds > 0.0);

        sampleRate_ = sampleRate;
        delayBuffer.setSize (2, static_cast<int> (sampleRate * maxDelaySeconds));
        delayBuffer.clear();
        phase = 0.0f;
        writePos = 0;   // desviacion documentada: ver cabecera
    }

    /** Vacia el buffer sin tocar la fase ni el puntero de escritura (como el
        original; el envoltorio de producto se encarga de sus smoothers). */
    void reset() noexcept
    {
        delayBuffer.clear();
    }

    /** Procesa un sample de un canal con los valores de ESE sample.

        Orden identico al original: primero escribe la entrada en la posicion
        actual, luego lee la posicion modulada, y devuelve la mezcla wet/dry. */
    float processSample (int channel, float input, float depth, float mix) noexcept
    {
        const int chan = channel % 2;
        const int bufferSize = delayBuffer.getNumSamples();

        // Modulacion: LFO entre 5ms y 30ms
        const float mod = (std::sin (phase) + 1.0f) * 0.5f;   // 0 a 1
        const float delaySamples = (0.005f + mod * 0.025f * depth) * static_cast<float> (sampleRate_);

        delayBuffer.setSample (chan, writePos, input);

        float readPos = static_cast<float> (writePos) - delaySamples;
        if (readPos < 0.0f)
            readPos += static_cast<float> (bufferSize);

        const int index1 = static_cast<int> (readPos);
        const int index2 = (index1 + 1) % bufferSize;
        const float fraction = readPos - static_cast<float> (index1);

        const float delayedSample = (1.0f - fraction) * delayBuffer.getSample (chan, index1)
                                  + fraction * delayBuffer.getSample (chan, index2);

        return (input * (1.0f - mix * 0.5f)) + (delayedSample * mix * 0.5f);
    }

    /** Avanza la fase del LFO y el puntero de escritura. Una vez por muestra,
        tras recorrer los canales, con la frecuencia de ESA muestra. */
    void advance (float rateHz) noexcept
    {
        const float twoPi = MathConstants<float>::twoPi;

        const float phaseInc = twoPi * rateHz / static_cast<float> (sampleRate_);

        phase += phaseInc;
        if (phase >= twoPi)
            phase -= twoPi;

        if (++writePos >= delayBuffer.getNumSamples())
            writePos = 0;
    }

    /** Sample rate de la ultima llamada a prepare(). */
    double getSampleRate() const noexcept { return sampleRate_; }

private:
    AudioBuffer<float> delayBuffer;
    int writePos = 0;
    float phase = 0.0f;
    double sampleRate_ = 44100.0;

    dspDeclareNonCopyableWithLeakDetector (Chorus)
};

} // namespace abd::dsp
