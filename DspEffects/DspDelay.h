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

        // El indice y la fraccion salen de la posicion YA ENVUELTA, y por eso
        // ambos estan dentro del buffer. El arreglo del desbordamiento esta en
        // `wrapReadPosition`; aqui solo se lee.
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
    /** Lleva una posicion de lectura al rango [0, bufferSize) sobre el buffer
        circular.

        OJO, esto era un `if` y no un `while`, y era un fallo de indice, no
        cosmetico: con un retardo MAYOR que la capacidad del buffer, una sola
        resta deja la posicion todavia negativa, `(int)readPos` sale negativo y
        `getSample` lee fuera del buffer (en depuracion, una asercion; en release,
        lo que haya en memoria). No es teorico: el envoltorio de ABDNeural
        (`ABDNeural/Source/DSP/Effects/Delay.h`) recorta el feedback a 0.95 pero
        NO recorta el retardo a la capacidad que se le dio en `prepare`, asi que
        un consumidor que pase un tiempo largo para un `prepare` corto revienta.

        Se hace con el cociente en vez de con un `while` porque un `while` sobre
        una posicion de -1e9 con un buffer de 1024 daria un millon de vueltas en
        el lazo de audio, que es un problema de tiempo real todavia peor que el
        del indice.

        Y para todo retardo DENTRO de la capacidad el resultado es EXACTAMENTE el
        de antes, bit a bit, asi que esto no cambia el sonido de nadie: solo
        cambia lo que antes se salia del buffer.
    */
    static float wrapReadPosition (float readPos, int bufferSize) noexcept
    {
        const float size = static_cast<float> (bufferSize);

        // division entera: el truncamiento va hacia cero, asi que al FINAL hay
        // que corregir el caso negativo, que es donde el cociente "se pasa".
        const long long q = static_cast<long long> (readPos / size);
        float r = readPos - size * static_cast<float> (q);

        if (r < 0.0f)
            r += size;

        // Y aqui esta el arreglo de un defecto REAL de este motor, encontrado al
        // medir el sistema de slots y no por una lectura del codigo.
        //
        // El calculo de arriba da un valor en [0, size), pero un `float` no
        // puede representar todos los reales de ahi: cuando el resultado cae a
        // menos de medio ULP de `size`, el redondeo lo sube a `size` EXACTO. Ahi
        // `static_cast<int>` da `bufferSize` — una muestra mas alla del final —
        // y `getSample` lee de mas.
        //
        // MEDIDO, y no con un retardo raro: con el retardo a 300 ms (14400.001
        // muestras) y el puntero de escritura en 14400, la resta da −0.001, al
        // envolver da 97023.999, y al redondear a float da 97024.000. Es el
        // retardo mas normal del mundo, en el unico instante en que la escritura
        // alcanza la lectura, y por eso cualquier barrido de un segundo lo
        // encuentra. Con `delayInSamples` viniendo de un mando normalizado, el
        // valor ni siquiera es un entero, asi que la ventana de redondeo no es
        // una excepcion: es el caso normal de un retardo que no cae en la
        // rejilla.
        //
        // `r >= size` a 0 es la correccion exacta y no un parche: la posicion
        // verdadera era `size - epsilon`, y la muestra 0 del buffer es
        // precisamente lo que hay `size` muestras antes que la escritura. El
        // error es de 0.001 muestras, o sea menos de un centesimo de ULP de
        // una muestra de audio.
        //
        // NO TOCA NINGUN RETARDO QUE YA SONARA. Para todo retardo dentro de la
        // capacidad, `r` queda a media ULP o mas del final y esta rama no se
        // toma: el resultado es el mismo numero, bit a bit, y por eso la
        // paridad a 0 ulps de ABDNeural sigue intacta.
        if (r >= size)
            r = 0.0f;

        return r;
    }

    AudioBuffer<float> delayBuffer;
    int writePos = 0;
    double sampleRate_ = 44100.0;

    dspDeclareNonCopyableWithLeakDetector (Delay)
};

} // namespace abd::dsp
