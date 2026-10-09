/*
  ==============================================================================

    MultiHeadEcho.h
    Motor de eco de multi-cabezal con la identidad de la maquina INYECTADA.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). El motor no es de ningun dispositivo: es "un eco que
    lee N cabezales de una sola linea de cinta y los suma". Quien lo instancia
    le pasa un PERFIL (los numeros de la maquina) y una ETAPA DE CARACTER (su
    degradacion). Ver DspEffects/EffectPolicy.h para el contrato.

    QUE ES Y QUE NO. Aqui vive la MAQUINA: la linea de cinta de dos canales, las
    lecturas interpoladas de los cabezales, la suma de sus ganancias, el
    tanque de la senal diferida, el control de tono y la mezcla de salida. NO
    vive aqui la identidad de la maquina (esas van en el perfil), ni el
    suavizado de los controles, ni el mapeo 0..1 -> unidades, ni la mezcla
    wet/dry del slot: eso es del consumidor, por la regla del modulo.

    POR QUE ESTA SEPARADO ASI. El Space Echo RE-201, un eco de cinta de estudio
    y un eco de maquina de discoteca son el MISMO motor con tres perfiles. En
    el codigo de origen el RE-201 traia su tabla de cabezales pegada al
    algoritmo, y por eso no se podia compartir con nadie: heredar de el era la
    unica forma de no perder los numeros del RE-201. Ahora los numeros son un
    `struct` de datos, y añadir una maquina es escribir un perfil.

    CONTRATO DE LLAMADA (lo unico no obvio de la API):
      por cada muestra -> processSample() para CADA canal y despues advance()
      UNA vez. El puntero de escritura y la deriva de la etapa de caracter son
      comunes a los canales: compartirlos es lo que evita que un eco que se
      reproduce en mono descentre el segundo canal. Mismo reparto que
      DspDelay.h y DspChorus.h.

    PARAMETROS POR MUESTRA. `setMode` NO es por muestra (elige una fila de la
    tabla del perfil y solo cambia cuando el usuario cambia de modo); el resto
    entra POR MUESTRA porque el motor los lee dentro del lazo de muestras, y ahi
    deben entrar para no mover una sola muestra de la salida.

    RELACION CON EL ORIGEN. Esta es una REDERIVACION con la costura compartida,
    no un port bit a bit del efecto de ABDEep: la estructura es la misma
    (tres cabezales sobre una linea, tanque diferido, control de tono) pero los
    coeficientes se han vuelto datos del perfil. La paridad con el efecto de
    ABDEep, si se quiere, se prueba en el CONSUMIDOR (que tiene el original a
    mano), igual que la paridad de DspReverb vive en ABDNeural/Tests. Este
    modulo no promete 0 ulps con nadie.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "DspEffects/EffectPolicy.h"
#include "DspEffects/characters/TapeColour.h"

namespace abd::dsp
{

//==============================================================================
/**
    Perfil generico: un eco de cinta de estudio, sin emular a nadie en concreto.

    Existe para que el motor se pueda usar como efecto GENERICO, que es la
    mitad del proposito de haberlo separado. Con `NullStage` no tiene color de
    cinta; con `TapeColour` es un eco de cinta discreto.
*/
struct StudioEchoProfile
{
    /** Una fila del perfil: que cabezales suenan y si entra el tanque. */
    struct Mode
    {
        bool head[3]; // cabezales activos
        bool reverb;  // el tanque entra en este modo
    };

    static constexpr int numModes = 3;

    static constexpr Mode modes[3] =
        {
            {{true, false, false}, true}, // un cabezal
            {{true, true, false}, true},  // dos
            {{true, true, true}, true}    // tres
    };

    // Donde esta cada cabezal, como multiplicador del retardo base.
    static constexpr float headRatio[3] = {0.20f, 0.45f, 0.70f};

    static float getHeadRatio(int /*mode*/, int head) noexcept
    {
        return headRatio[head];
    }

    static float getHeadGain(int /*mode*/, int /*head*/, float defaultGain) noexcept
    {
        return defaultGain;
    }

    // Escala de ganancia del canal derecho por cabezal: un cabezal real no
    // envia lo mismo a L que a R.
    static constexpr float headRightScale[3] = {0.95f, 0.90f, 0.92f};

    static constexpr float minDelaySeconds = 0.05f;
    static constexpr float maxDelaySeconds = 1.20f;
    static constexpr float dryGain         = 0.75f;
    static constexpr float headOutputGain  = 0.40f;
    static constexpr float toneFrequencyHz = 800.0f;
    static constexpr float tankTimeL       = 0.080f; // linea del tanque
    static constexpr float tankTimeR       = 0.110f;
    static constexpr float wowHz           = 0.7f;   // deriva lenta
    static constexpr float flutterHz       = 6.0f;   // aleteo rapido
    static constexpr float wowAmount       = 0.015f; // cuanto alarga
    static constexpr float flutterAmount   = 0.005f; // cuanto aletea
    static constexpr float colourDrive     = 0.25f;
    static constexpr float colourHiss      = 0.15f;
};

//==============================================================================
/**
    Eco de cinta de multi-cabezal.

    `Profile` son los numeros de la maquina (tabla de modos, rangos, ganancias);
    `Colour` es la etapa de degradacion. Los dos son parametros de plantilla:
    no hay indireccion en el lazo de audio y el motor no depende de ninguno.
*/
template <typename Profile, typename Colour = TapeColour>
class MultiHeadEcho
{
public:
    MultiHeadEcho() { prepare(44100.0); }

    /** Fija el sample rate y dimensiona la linea de cinta y el tanque. */
    void prepare(double sampleRate)
    {
        dspAssert(sampleRate > 0.0);

        sampleRate_ = sampleRate;

        // La linea de cinta tiene que aguantar el retardo MAS LARGO que puede
        // pedir un cabezal, no el retardo base. Con `Re201Profile` los cabezales
        // suenan a 1x, 2x y 3x (dicho en el propio perfil: "son retardos MAS
        // LARGOS, y por eso el buffer tiene que aguantar 3 veces el retardo
        // base"), asi que dimensionar a `maxDelaySeconds` se quedaba corto por
        // un factor 3 y el cabezal 3 leia fuera del buffer.
        float maxRatio = Profile::getHeadRatio(0, 0);

        for (int m = 0; m < Profile::numModes; ++m)
            for (int h = 0; h < 3; ++h)
                maxRatio = jmax(maxRatio, Profile::getHeadRatio(m, h));

        const float maxMod = 1.0f + Profile::wowAmount + Profile::flutterAmount;
        delaySamples_ = static_cast<int>(sampleRate * Profile::maxDelaySeconds * maxRatio * maxMod) + 32;
        tankSamples_  = static_cast<int>(sampleRate * Profile::maxDelaySeconds) + 1;

        tapeBuffer.setSize(2, delaySamples_, false, false, true);
        tankBuffer.setSize(2, tankSamples_, false, false, true);

        colour.prepareSampleRate(sampleRate);

        // Coeficiente del pasabajos de un polo del control de tono:
        // a = exp(-2*pi*f/sr), reescrito con exp2 porque dsp::exp2 es la
        // exponente que garantiza la paridad nativa <-> WASM (DspCore/DspMath.h)
        // y exp(-x) == exp2(log2(e) * -x).
        toneCoeff_ = exp2(-1.4426950408889634f * 6.283185307179586f * Profile::toneFrequencyHz / static_cast<float>(sampleRate));

        reset();
    }

    /** Vacia linea y tanque. NO toca el modo: es estado de audio, no de control. */
    void reset() noexcept
    {
        tapeBuffer.clear();
        tankBuffer.clear();
        writePos_     = 0;
        tankWritePos_ = 0;
        toneState_    = 0.0f;
        tankAccum_    = 0.0f;
        tankChannels_ = 0;
        wowPhase_     = 0.0f;
        flutterPhase_ = 0.0f;
        wow_          = 0.0f;
        flutter_      = 0.0f;
        colour.reset();
    }

    /** Elige la fila de la tabla del perfil. Por bloque, no por muestra. */
    void setMode(int mode) noexcept
    {
        mode_ = jlimit(0, Profile::numModes - 1, mode);
    }

    /** Modo activo. */
    int getMode() const noexcept { return mode_; }

    /**
        Procesa UNA muestra de UN canal.

        delaySeconds  retardo de la cabecera (el motor lo recorta al rango del
                      perfil: el recorte es de MAQUINA, no de politica).
        feedback      0..1, la mezcla por la que vuelve a escribirse.
        bass/treble   0..1, los dos controles de tono.
        tankMix       0..1, cuanto sale el tanque.
    */
    float processSample(int channel, float input,
                        float delaySeconds, float feedback,
                        float bass, float treble, float tankMix) noexcept
    {
        const int chan = channel & 1;

        // El retardo pedido, recortado al rango de MAQUINA. Es un recorte de
        // la maquina y no de politica: por eso va aqui y no en el consumidor.
        const float clampedDelay = jlimit(Profile::minDelaySeconds,
                                          Profile::maxDelaySeconds,
                                          delaySeconds);
        const float baseDelaySamples = clampedDelay * static_cast<float>(sampleRate_);

        // Deriva de la cinta: la lectura se alarga o se acorta con ella. Las
        // dos senales son del motor, no de la etapa de color (ver TapeColour.h).
        const float wowMod = 1.0f + wow_ * Profile::wowAmount + flutter_ * Profile::flutterAmount;

        const auto& mode = Profile::modes[mode_];

        // Reparto de la MAQUINA, no del perfil: con N cabezales encendidos
        // cada uno suena a 1/N, para que cambiar de combinacion no cambie el
        // nivel del eco. El perfil solo dice cuales estan encendidos.
        //
        // `headRightScale` se aplica ENCIMA de ese reparto, no en su lugar: es el
        // desvio de un cabezal real entre L y R (~5-10%), no una ganancia
        // absoluta. MEDIDO antes del arreglo, con Re201Profile: con un solo
        // cabezal R/L daba 0.95 (correcto), con dos daba entre 1.80 y 2.70, y con
        // los tres 2.70. O sea que el canal derecho era hasta 2.7 veces mas
        // fuerte que el izquierdo en cuanto se encendia un segundo cabezal, y
        // el desequilibrio CRECIA con cada cabezal que se anadia.
        int activeCount = 0;
        for (int h = 0; h < 3; ++h)
            if (mode.head[h]) ++activeCount;

        const float gainPerHead = activeCount > 0 ? 1.0f / static_cast<float>(activeCount) : 0.0f;

        // Suma de los cabezales. Solo lee los que estan encendidos.
        float headL = 0.0f, headR = 0.0f;

        for (int h = 0; h < 3; ++h)
        {
            if (mode.head[h])
            {
                // La lectura NO puede pasar del final de la linea de cinta. Es
                // RED, no un arreglo de un fallo medido: con los perfiles que hay
                // hoy el envuelto no llega a ocurrir.
                const float wanted   = Profile::getHeadRatio(mode_, h) * baseDelaySamples * wowMod;
                const float distance = jmin(static_cast<float>(delaySamples_ - 1), wanted);
                const float hGain    = Profile::getHeadGain(mode_, h, gainPerHead);

                headL += readTape(chan, distance) * hGain;
                headR += readTape(chan, distance) * hGain * Profile::headRightScale[h];
            }
        }

        const float heads  = (chan == 0 ? headL : headR);
        const float tankIn = (headL + headR) * 0.5f;

        // La realimentacion pasa por el color de cinta ANTES de volverse a
        // escribir: es ahi donde la cinta satura y sisea, no en la entrada.
        // El color de cinta solo se aplica al camino de REALIMENTACION, que es
        // donde una cinta satura y sisea. La entrada entra limpia: el
        // preamplificador del cabezal de grabacion no es el de reproduccion.
        const float fed = colour.processSample(heads * feedback,
                                               Profile::colourDrive,
                                               Profile::colourHiss);

        // Control de tono: un pasabajos de un polo, no dos estantes. El
        // original hacia lo mismo por el signo de la muestra, que es por donde
        // se distingue el graves del agudo sin un filtro de verdad.
        const float toneGain = (chan == 0) ? (0.5f + bass * 0.5f)
                                           : (0.5f + treble * 0.5f);
        const float toned    = fed * toneGain;

        tapeBuffer.setSample(chan, writePos_, input + toned);

        // Tanque diferido: dos lineas cruzadas que se restan entre si. Es lo
        // que da el "cantito" de muelle sin un resonador de muelles entero.
        const float tankReadL = readTank(0, static_cast<int>(sampleRate_ * Profile::tankTimeL));
        const float tankReadR = readTank(1, static_cast<int>(sampleRate_ * Profile::tankTimeR));
        const float tankDiff  = (tankReadL + tankReadR) * 0.5f;

        // El filtro de tono y la escritura al tanque son POR MUESTRA, no por
        // canal, asi que se acumulan aqui y los aplica `advance()` una sola vez
        // por muestra. Antes se hacian aqui, dentro de `processSample`, que se
        // llama una vez POR CANAL: en estereo el pasabajos corria al doble de la
        // frecuencia de muestreo y la segunda llamada pisaba la escritura de la
        // primera. MEDIDO: el canal izquierdo de un render estereo se separaba
        // del render mono del mismo material en 1.09e-1, o sea que el mismo motor
        // sonaba distinto segun cuantos canales le mandaras.
        const float tankNew = tankIn * 0.40f - tankDiff * 0.35f;
        tankAccum_ += tankNew;
        ++tankChannels_;

        const float tankOut = (chan == 0 ? tankReadL : tankReadR);

        // El flag del modo gobierna el tanque: en los cuatro primeros modos del
        // selector la reverb NO entra, y sin esto el modo "solo eco" seguiria
        // soltando con el tanque abierto.
        return input * Profile::dryGain + heads * Profile::headOutputGain + tankOut * tankMix * (mode.reverb ? 1.0f : 0.0f);
    }

    /** Avanza los punteros de escritura y la deriva del transporte.

        UNA vez por muestra, tras recorrer los canales. */
    void advance() noexcept
    {
        if (++writePos_ >= delaySamples_) writePos_ = 0;
        if (++tankWritePos_ >= tankSamples_) tankWritePos_ = 0;

        // Una vez por MUESTRA, con la media de los canales que han entrado: el
        // pasabajos de tono y la entrada del tanque son de la maquina, no de un
        // canal. En MONO (un solo `processSample` por muestra) la media es el
        // propio valor, asi que el caso mono queda BIT A BIT como era.
        if (tankChannels_ > 0)
        {
            const float tankMean = tankAccum_ / static_cast<float>(tankChannels_);

            toneState_ += (1.0f - toneCoeff_) * (tankMean - toneState_);

            tankBuffer.setSample(0, tankWritePos_, toneState_);
            tankBuffer.setSample(1, tankWritePos_, toneState_ * 0.97f);

            tankAccum_    = 0.0f;
            tankChannels_ = 0;
        }

        const float twoPi = MathConstants<float>::twoPi;
        const float sr    = static_cast<float>(sampleRate_);

        wowPhase_ += twoPi * Profile::wowHz / sr;
        flutterPhase_ += twoPi * Profile::flutterHz / sr;

        if (wowPhase_ > twoPi) wowPhase_ -= twoPi;
        if (flutterPhase_ > twoPi) flutterPhase_ -= twoPi;

        wow_     = sin(wowPhase_);
        flutter_ = sin(flutterPhase_);
    }

    /** Sample rate de la ultima llamada a `prepare`. */
    double getSampleRate() const noexcept { return sampleRate_; }

    /** Tamaño de la linea de cinta, en muestras, para el sample rate actual. */
    int delayBufferSize() const noexcept { return delaySamples_; }

private:
    //==========================================================================
    /** Lectura interpolada lineal de la linea de cinta a `distance` muestras. */
    float readTape(int channel, float distance) const noexcept
    {
        const int size = delaySamples_;

        // Misma defensa que en `DspDelay::wrapReadPosition`: una sola suma solo
        // envuelve mientras la distancia sea menor que el buffer. Con un perfil
        // cuyo `headRatio` pase de 1, o con una distancia manipulada, la suma
        // dejaba la posicion negativa y `% size` de un entero NEGATIVO sale
        // negativo en C++, o sea un indice fuera del buffer. Se resuelve con el
        // cociente, que envuelve cualquier magnitud.
        //
        // `prepare` ya dimensiona el buffer para el mayor `headRatio`, asi que
        // esto no deberia dispararse nunca; es la red por si un perfil futuro se
        // queda corto o si alguien llama a `processSample` con un retardo fuera
        // de rango.
        const float fsize = static_cast<float>(size);
        const float raw   = static_cast<float>(writePos_) - distance;
        const long long q = static_cast<long long>(raw / fsize);

        float readPos = raw - fsize * static_cast<float>(q);

        if (readPos < 0.0f)
            readPos += fsize;

        const int index0 = static_cast<int>(readPos) % size;
        const int index1 = (index0 + 1) % size;
        const float frac = readPos - static_cast<int>(readPos);

        return tapeBuffer.getSample(channel, index0) + frac * (tapeBuffer.getSample(channel, index1) - tapeBuffer.getSample(channel, index0));
    }

    float readTank(int channel, int distance) const noexcept
    {
        int readPos = tankWritePos_ - distance;

        while (readPos < 0) readPos += tankSamples_;
        while (readPos >= tankSamples_) readPos -= tankSamples_;

        return tankBuffer.getSample(channel, readPos);
    }

    AudioBuffer<float> tapeBuffer;
    AudioBuffer<float> tankBuffer;
    Colour colour;

    double sampleRate_  = 44100.0;
    int delaySamples_   = 1;
    int tankSamples_    = 1;
    int writePos_       = 0;
    int tankWritePos_   = 0;
    int mode_           = 0;
    float toneState_    = 0.0f;
    float tankAccum_    = 0.0f;
    int tankChannels_   = 0;
    float toneCoeff_    = 0.5f;
    float wowPhase_     = 0.0f;
    float flutterPhase_ = 0.0f;
    float wow_          = 0.0f;
    float flutter_      = 0.0f;

    dspDeclareNonCopyableWithLeakDetector(MultiHeadEcho)
};

} // namespace abd::dsp
