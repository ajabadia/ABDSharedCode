/*
  ==============================================================================

    _Deprecados/LutDSP-JunoBBD.h
    UNICO COPIA EN EL ATICO. ESTO NO SE USA, Y NO SE USA A PROPOSITO.

    Que es. El borrador de un coro BBD del Juno que estuvo en
    `LutDSP/JunoBBD.h` y que se movió aquí el 2026-09-28, sin cambios en el
    cuerpo, cuando se extrajo el coro de verdad a `DspEffects/JunoBBD.h`.

    POR QUE NUNCA SE ADOPTÓ, que es lo que hace que este fichero exista en vez
    de estar borrado. No se quedó sin usar por descuido: se quedó sin usar
    porque el sitio donde estaba no era el suyo, y el motor bueno ya existe.

      1. ESTABA EN EL MODULO EQUIVOCADO. `LutDSP` es un modulo **JUCE-free** de
         tablas y evaluacion de LUT (namespace `abd::lutdsp`, cabeceras
         INTERFACE, cero fuentes compiladas). Un BBD es un retardo con reloj,
         con filtros, con saturacion y con generadores de ruido recurrentes: no
         tiene nada de una LUT. Y al estar en `LutDSP`, este fichero **tenia que
         incluir JUCE** (`<juce_audio_basics/...>`) para tener un
         `AudioBuffer`, lo cual rompia la regla del modulo entero. O sea que no
         era solo que estuviera en el sitio raro: era que para funcionar tenia
         que arrastrar la dependencia que ese modulo no puede tener. La guia ya
         lo senalo: "un BBD no es una LUT, es `DspEffects`".

      2. NO ERA EL BBD BUENO. Este borrador tiene linea de 256 etapas con
         interpolacion cubica, un pasabajos de reconstruccion de 9 kHz y dos
         LFO en cuadratura. Le faltan las tres cosas que hacen que un coro BBD
         suene a BBD y no a coro digital con un retardo: la **tolerancia de
         reloj** de las dos lineas (+/-1.5%, que es la asimetria que produce el
         batido caracteristico), la **perdida de transferencia de carga** (que
         hace que la linea pierda ganancia cuando el reloj va mas rapido) y los
         **clics** de deposito vaciado. El coro bueno es
         `ABDJUNiO601/Source/Synth/ChorusBBD.{h,cpp}` (681 lineas con
         `BBDFilter.h`), que si tiene las tres.

      3. LOS MODOS NO COINCIDEN CON EL PANEL. Aqui hay `ModeI` a 0.4 Hz,
         `ModeII` a 0.6 Hz y `ModeI_II` a 1.0 Hz. El Juno de verdad tiene
         0.513 Hz y 0.78 Hz, y el I+II no acelera a 1.0 Hz sino a 7.7 Hz. Un
         motor con numeros que no son los del panel no puede sustituir al otro
         sin cambiar el sonido de un efecto ya publicado, que es justo lo que no
         se quiere.

      4. Y AHORA HAY UN MOTOR DE VERDAD. `DspEffects/JunoBBD.h` es el coro BBD
         extraido de JUNiO601, con politica inyectada (`JunoBbdProfile` para los
         numeros de cada modelo, `BbdNoiseStage` para la degradacion) y con test
         de paridad a 0 ulps contra la referencia congelada en
         `ABDJUNiO601/Tests/ChorusBBDParityTest.cpp`. Este fichero ya no tiene
         a quien competir.

    QUE SE HIZO CON LAS DOS COPIAS QUE SI ERAN CODIGO MUERTO. La de
    `ABDJUNiO601/Source/Core/JunoBBD.h` (158 lineas) esta BORRADA: no la
    includia nadie, y era un cuarto modelo distinto de la misma maquina. La de
    `ABDAudioLab/src/dsp/JunoBBD.h` (un shim de 17 lineas que reexportaba esta)
    esta BORRADA tambien: su unico consumidor era un test que cubria este
    borrador, y ese test ahora cubre el motor de `DspEffects`.

    SI ALGUIEN LLEGA AQUI. No lo rescates ni lo adopts: lee
    `DspEffects/JunoBBD.h`. Si lo que quieres es un BBD, ese es el que tiene
    reloj con tolerancia, perdida de carga y clics. Este queda aqui solo para que
    quede escrito por que se fue.

  ==============================================================================
*/

// Y lo hace FALLAR en voz alta. Un fichero en el atico que compila solo es un
// fichero mas que alguien va a incluir dentro de seis meses sin mirar. Este
// prefieriere romperse.
#error "LutDSP-JunoBBD.h esta en el atico y no se usa: el coro BBD vive en DspEffects/JunoBBD.h"

/**
 * @file LutDSP-JunoBBD.h
 * @brief DESACTIVADO. Ver el bloque de arriba: no se usa y no se adopta.
 *
 * Models a 256-stage Bucket Brigade Delay line with cubic interpolation,
 * analog anti-aliasing / reconstruction low-pass filter (9 kHz),
 * and dual-LFO quadrature modulation for Modes I, II, and I+II.
 */

#pragma once

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace abd::lutdsp
{

class JunoBBD
{
public:
    enum class Mode
    {
        Off      = 0,
        ModeI    = 1, // Subtle chorus (0.4 Hz rate)
        ModeII   = 2, // Richer/deeper chorus (0.6 Hz rate)
        ModeI_II = 3  // Fast vibrato/flanging (1.0 Hz + higher depth)
    };

    JunoBBD()  = default;
    ~JunoBBD() = default;

    void prepare(double newSampleRate, int maxBlockSize = 512)
    {
        juce::ignoreUnused(maxBlockSize);
        sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;

        // 256-stage MN3009. Minimum clock gives ~15-20ms max delay.
        // Safety buffer of 100ms
        int bufferLength = static_cast<int>(sampleRate * 0.1) + 16;
        delayBufferL.assign(bufferLength, 0.0f);
        delayBufferR.assign(bufferLength, 0.0f);
        bufferSize = bufferLength;
        writePos   = 0;

        lfoPhase = 0.0;
        updateLfoRates();

        // 1-pole reconstruction filter (approx 9 kHz low-pass)
        calcFilterCoeffs(9000.0);
        filterStateL = 0.0f;
        filterStateR = 0.0f;
    }

    void reset() noexcept
    {
        std::fill(delayBufferL.begin(), delayBufferL.end(), 0.0f);
        std::fill(delayBufferR.begin(), delayBufferR.end(), 0.0f);
        writePos     = 0;
        lfoPhase     = 0.0;
        filterStateL = 0.0f;
        filterStateR = 0.0f;
    }

    void setMode(Mode newMode) noexcept
    {
        currentMode = newMode;
        updateLfoRates();
    }

    Mode getMode() const noexcept { return currentMode; }

    /**
     * @brief Processes a stereo block in-place with BBD chorus wet/dry mixing.
     */
    void processBlock(juce::AudioBuffer<float>& buffer)
    {
        if (currentMode == Mode::Off || buffer.getNumChannels() == 0 || bufferSize <= 0)
            return;

        const int numSamples = buffer.getNumSamples();
        float* leftChannel   = buffer.getWritePointer(0);
        float* rightChannel  = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;

        const double lfoInc = (juce::MathConstants<double>::twoPi * lfoRateHz) / sampleRate;

        for (int i = 0; i < numSamples; ++i)
        {
            float inL    = leftChannel[i];
            float inR    = rightChannel ? rightChannel[i] : inL;
            float monoIn = 0.5f * (inL + inR);

            // Write to ring buffers
            delayBufferL[writePos] = monoIn;
            delayBufferR[writePos] = monoIn;

            // Quadrature LFO modulations (Mode L & R are 180 degrees out of phase for stereo width)
            double modL = std::sin(lfoPhase);
            double modR = std::sin(lfoPhase + juce::MathConstants<double>::pi);

            float delayTimeMsL = baseDelayMs + static_cast<float>(modL) * lfoDepthMs;
            float delayTimeMsR = baseDelayMs + static_cast<float>(modR) * lfoDepthMs;

            float delaySamplesL = (delayTimeMsL * 0.001f) * static_cast<float>(sampleRate);
            float delaySamplesR = (delayTimeMsR * 0.001f) * static_cast<float>(sampleRate);

            float delayedL = readInterpolated(delayBufferL, delaySamplesL);
            float delayedR = readInterpolated(delayBufferR, delaySamplesR);

            // Apply reconstruction low-pass filter (9 kHz)
            delayedL = processFilter(delayedL, filterStateL);
            delayedR = processFilter(delayedR, filterStateR);

            // Juno hardware mixes dry + delayed wet
            leftChannel[i] = 0.5f * (inL + delayedL);
            if (rightChannel)
            {
                rightChannel[i] = 0.5f * (inR + delayedR);
            }

            // Advance state
            writePos = (writePos + 1) % bufferSize;
            lfoPhase += lfoInc;
            if (lfoPhase >= juce::MathConstants<double>::twoPi)
                lfoPhase -= juce::MathConstants<double>::twoPi;
        }
    }

private:
    void updateLfoRates() noexcept
    {
        switch (currentMode)
        {
            case Mode::ModeI:
                lfoRateHz   = 0.4;
                baseDelayMs = 4.0f;
                lfoDepthMs  = 1.5f;
                break;
            case Mode::ModeII:
                lfoRateHz   = 0.6;
                baseDelayMs = 5.0f;
                lfoDepthMs  = 2.5f;
                break;
            case Mode::ModeI_II:
                lfoRateHz   = 1.0;
                baseDelayMs = 3.5f;
                lfoDepthMs  = 3.0f;
                break;
            case Mode::Off:
            default:
                lfoRateHz   = 0.0;
                baseDelayMs = 0.0f;
                lfoDepthMs  = 0.0f;
                break;
        }
    }

    void calcFilterCoeffs(double cutoffHz) noexcept
    {
        // Simple standard exponential moving average coefficient for 1-pole low-pass:
        // alpha = 1 - exp(-2 * pi * fc / fs)
        double dt   = 1.0 / sampleRate;
        double rc   = 1.0 / (juce::MathConstants<double>::twoPi * cutoffHz);
        filterAlpha = static_cast<float>(dt / (rc + dt));
    }

    float processFilter(float input, float& state) noexcept
    {
        state += filterAlpha * (input - state);
        return state;
    }

    float readInterpolated(const std::vector<float>& buf, float delaySamples) const noexcept
    {
        float readPos = static_cast<float>(writePos) - delaySamples;
        while (readPos < 0.0f) readPos += static_cast<float>(bufferSize);

        int idx0   = static_cast<int>(readPos);
        float frac = readPos - static_cast<float>(idx0);

        int i_m1 = (idx0 - 1 + bufferSize) % bufferSize;
        int i_0  = idx0 % bufferSize;
        int i_p1 = (idx0 + 1) % bufferSize;
        int i_p2 = (idx0 + 2) % bufferSize;

        float y0 = buf[i_m1];
        float y1 = buf[i_0];
        float y2 = buf[i_p1];
        float y3 = buf[i_p2];

        // 4-point Hermite cubic interpolation
        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    double sampleRate = 44100.0;
    Mode currentMode  = Mode::Off;

    std::vector<float> delayBufferL;
    std::vector<float> delayBufferR;
    int bufferSize = 0;
    int writePos   = 0;

    double lfoPhase   = 0.0;
    double lfoRateHz  = 0.4;
    float baseDelayMs = 4.0f;
    float lfoDepthMs  = 1.5f;

    float filterAlpha  = 0.5f;
    float filterStateL = 0.0f;
    float filterStateR = 0.0f;
};

} // namespace abd::lutdsp
