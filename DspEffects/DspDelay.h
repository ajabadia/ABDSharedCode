/*
  ==============================================================================

    DspDelay.h
    Retardo estereo realimentado con buffer circular y lectura interpolada.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Vivio en ABDNeural/Source/DSP/Effects/Delay.h hasta la
    migracion de efectos; ahi queda solo el envoltorio de producto.

    QUE ES ESTE FICHERO (y que NO). Aqui vive SOLO la maquina del retardo: el
    buffer circular de 2 canales, la lectura interpolada lineal y la escritura de
    `input + delayed * feedback`. NO vive aqui la politica de producto:

      - el suavizado del tiempo y del feedback (dsp::LinearSmoothedValue, 50ms y
        20ms en NEURONiK) ni la conversion segundos -> muestras,
      - el recorte del feedback a 0.95,
      - la mezcla (wet fijo de 0.5 sumado al dry).

    Esa politica entra POR MUESTRA, que es exactamente como estaba en el efecto
    original (los smoothers se leian dentro del bucle de muestras). Por eso la
    API es processSample() + advanceWritePosition() y no un processBlock() con
    parametros por bloque: cambiarlo moveria la salida.

    CONTRATO DE LLAMADA (lo unico no obvio de la API):
      por cada muestra -> processSample() para CADA canal y despues
      advanceWritePosition() UNA vez. El puntero de escritura es comun a los
      canales: el canal entra en el buffer como `channel % 2`, igual que en el
      original (un buffer de 2 canales, siempre).

    DENORMALES: el original abre un dsp::ScopedNoDenormals en el processBlock de
    su envoltorio. Aqui no hay nivel de bloque (la API es por muestra), asi que
    esa responsabilidad es del consumidor y NEURONiK la mantiene en su Delay.h: el
    motor no la necesita para calcular igual (ScopedNoDenormals cambia el modo de
    la FPU, no las muestras), pero se queda donde estaba.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"

namespace abd::dsp
{

//==============================================================================
/**
    Linea de retardo estereo con realimentacion y lectura interpolada lineal.

    Sin suavizado de parametros ni mapeo: el consumidor aporta el retardo en
    muestras y el feedback de cada muestra (ver el contrato en la cabecera).
*/
class Delay
{
public:
    /** Capacidad por defecto: 2 canales x 2s @ 48 kHz, como el original. */
    Delay()
        : delayBuffer (2, 96000)
    {
        delayBuffer.clear();
    }

    /** Fija el sample rate y la capacidad del buffer.
        Se reservan `maxDelaySamples + 1024` muestras porque la lectura va por
        delante de la escritura (mismo margen que el original). */
    void prepare (double sampleRate, int maxDelaySamples)
    {
        dspAssert (sampleRate > 0.0);
        dspAssert (maxDelaySamples > 0);

        sampleRate_ = sampleRate;
        delayBuffer.setSize (2, maxDelaySamples + 1024);
        delayBuffer.clear();
        writePos = 0;
    }

    /** Vacia el buffer. No toca el puntero de escritura (como el original). */
    void reset() noexcept
    {
        delayBuffer.clear();
    }

    /** Lee la posicion interpolada del canal y escribe `input + delayed * feedback`
        en la posicion actual. Devuelve el tap leido, NO la mezcla.

        `delayInSamples` y `feedback` son los valores de ESTA muestra. */
    float processSample (int channel, float input, float delayInSamples, float feedback) noexcept
    {
        const int chan = channel % 2;
        const int bufferSize = delayBuffer.getNumSamples();

        const float readPos = wrapReadPosition (static_cast<float> (writePos) - delayInSamples,
                                                bufferSize);

        const int index1 = static_cast<int> (readPos);
        const int index2 = (index1 + 1) % bufferSize;
        const float fraction = readPos - static_cast<float> (index1);

        const float delayedSample = (1.0f - fraction) * delayBuffer.getSample (chan, index1)
                                  + fraction * delayBuffer.getSample (chan, index2);

        delayBuffer.setSample (chan, writePos, input + (delayedSample * feedback));

        return delayedSample;
    }

    /** Avanza el puntero de escritura. Una vez por muestra, tras todos los canales. */
    void advanceWritePosition() noexcept
    {
        if (++writePos >= delayBuffer.getNumSamples())
            writePos = 0;
    }

    /** Sample rate de la ultima llamada a prepare(). */
    double getSampleRate() const noexcept { return sampleRate_; }

private:
    //==========================================================================
    /** Lleva una posicion de lectura negativa (o anterior al inicio) al rango
        [0, bufferSize) sobre el buffer circular. */
    static float wrapReadPosition (float readPos, int bufferSize) noexcept
    {
        if (readPos < 0.0f)
            readPos += static_cast<float> (bufferSize);

        return readPos;
    }

    AudioBuffer<float> delayBuffer;
    int writePos = 0;
    double sampleRate_ = 44100.0;

    dspDeclareNonCopyableWithLeakDetector (Delay)
};

} // namespace abd::dsp
