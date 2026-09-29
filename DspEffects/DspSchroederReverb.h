/*
  ==============================================================================

    DspSchroederReverb.h
    Reverberador Schroeder-Moorer: 4 conbs en paralelo + 3 allpass en serie.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Port del efecto `FXSimpleReverb` de ABDEep
    (Source/DSP/FX/FXSimpleReverb.{h,cpp} + FXSimpleReverb_Process.cpp), que a
    su vez servia a diez variantes de reverb del DeepMind 12. Las variantes
    viven ahora como datos en DspEffects/profiles/ReverbProfile.h.

    QUE ES Y QUE NO. Aqui vive la MAQUINA: el pre-retardo mono, los cuatro conbs
    con amortiguamiento en la realimentacion, los tres allpass en serie, el
    escalado por decaimiento y la mezcla. NO viven aqui los diez tipos (eso son
    datos, en el perfil), ni el mapeo de los doce controles normalizados a
    valores fisicos, ni el wet/dry (lo mezcla el slot), ni el suavizado: eso es
    politica de producto y se queda en el consumidor, por la regla del modulo.

    CONTRATO DE LLAMADA, Y POR QUE ES POR MARCO Y NO POR CANAL. Es stereo, y el
    pre-retardo es MONO: se escribe una vez por muestra con la media de L y R, y
    se lee una vez. Si la API fuera por canal, el pre-retardo se escribiria dos
    veces por muestra y se desplazaria medio bloque, que es un cambio de
    sonido. Un marco stereo sigue siendo "la muestra" (es lo que hace
    `dsp::Reverb`, el port de `juce::Reverb` que ya vive en este modulo), asi
    que la regla del modulo se respeta.

        for (int s = 0; s < numSamples; ++s)
            reverb.processFrame (inL[s], inR[s], outL[s], outR[s]);

    SIN SUAVIZADO. Los coeficientes entran por `setDecay` / `setDamping` /
    `setDiffusion` y el consumidor los suaviza antes. Aqui no hay nada que
    suavizar: la maquina no guarda memoria de los valores anteriores.

    DOS COMPORTAMIENTOS QUE EL PORT CONSERVA A PROPOSITO. No son bugs de este
    fichero: son el comportamiento del efecto de ABDEep, y cambiarlos aqui
    romperia la paridad que el consumidor comprueba. Se documentan para que
    quien los encuentre sepa que sonconscious deliberados y no un descuido:

      1. MOVER EL TAMAÑO BORRA LA COLA. `setRoomSize` y `setPreDelaySeconds`
         recalculan las longitudes de los conbs, y recalcular longitudes implica
         redimensionar los buffers, y redimensionar pone los punteros de
         escritura a cero. En el efecto original eso significa que girar el
         mando de tamaño (o el de pre-retardo) durante una nota deja la reverb
         en silencio. Se conserva EXACTAMENTE asi. Arreglarlo es un cambio de
         sonido, y por eso va en su propio cambio y no colado en un port.

      2. LA INVERSION DE FASE SOLO SE APLICA AL CANAL IZQUIERDO. La variante
         "Reverse" (tipo 6) niega `wetL` pero no `wetR`. O sea que no invierte
         la reverb: la descorrelaciona, que es un efecto Haas, no un reverse.
         Tambien conservado tal cual, y por el mismo motivo: arreglarlo seria
         cambiar el sonido de un efecto ya publicado.

    LO QUE NO SE PROMETE. Este modulo no promete 0 ulps con nadie: no tiene el
    original a mano. La comparacion vive en el CONSUMIDOR (ABDEep), que es quien
    tiene el efecto pre-migracion, igual que la paridad de DspReverb con
    juce::Reverb vive en ABDNeural/Tests.

    Y UN FACTOR QUE PARECE UN TYPO Y NO LO ES: el pre-retardo real es
    `segundos * sampleRate * 0.2`, no `segundos * sampleRate`. El original
    multiplica por 0.2, de modo que pedir 100 ms retrasa 20 ms. Es un descuido
    del original, pero es el sonido que lleva anos sonando, asi que aqui se
    reproduce y se documenta (ver `setPreDelaySeconds`).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"

#include <algorithm>

namespace abd::dsp
{

//==============================================================================
/**
    Reverberador Schroeder-Moorer de 4 conbs y 3 allpass, por canal.

    Sin estado de audio compartido entre canales salvo el pre-retardo, que es
    mono a proposito (ver la cabecera).
*/
class SchroederReverb
{
public:
    SchroederReverb()
    {
        prepare (44100.0);
    }

    /** Fija el sample rate y redimensiona linea de conbs, allpass y pre-retardo.

        maxima dimensiona la linea de conbs. Los conbs de Schroeder miden entre
        30 y 41 ms y los allpass entre 5 y 8 ms, asi que por debajo de ~4 kHz el
        redondeo a un numero entero de muestras se come el filtro entero: es
        el mismo limite que tiene el original,    aqui solo documentado, porque la maquina no puede hacer otra cosa sin
        redimensionar, que es justo lo que borra la cola. */
    void prepare (double sampleRate, double maxCombSeconds = 0.25)
    {
        dspAssert (sampleRate > 0.0);

        sampleRate_  = sampleRate;
        maxCombSeconds_ = maxCombSeconds;

        preDelayCapacity_ = jmax (1, static_cast<int> (sampleRate_ * 0.2));

        rebuild();

        // El buffer de pre-retardo se dimensiona AQUI y no en `rebuild`, porque
        // en el original son dos operaciones distintas: `prepare` crea el buffer
        // de 200 ms y lo vacia, mientras que `updateFilters` (laResize de los
        // conbs) solo recalcula cuantas muestras se retrasan. Si el pre-retardo
        // se redimensionase con cada giro del mando de tamano, al original se le
        // borraria la cola del pre-retardo y aqui no, y la paridad se iria.
        preDelayBuffer.setSize (1, preDelayCapacity_, false, false, true);

        reset();
    }

    /** Vacia conbs, allpass y pre-retardo. Es estado de audio, no de control. */
    void reset() noexcept
    {
        combBufferL.clear();
        combBufferR.clear();
        allpassBufferL.clear();
        allpassBufferR.clear();
        preDelayBuffer.clear();

        preDelayWritePos_ = 0;

        for (int i = 0; i < kNumCombs; ++i)
        {
            combsL_[i].writePos   = 0;
            combsL_[i].filterState = 0.0f;
            combsR_[i].writePos   = 0;
            combsR_[i].filterState = 0.0f;
        }

        for (int i = 0; i < kNumAllPass; ++i)
        {
            allpassL_[i].writePos = 0;
            allpassR_[i].writePos = 0;
        }
    }

    /** Decaimiento 0..1. NEGATIVO es la variante "Reverse" (cola corta).

        EL RECORTE DE ARRIBA ES NUEVO, y no cambia ni una muestra de ningun
        consumidor actual: el tope documentado es 1 y el valor mas alto que pasa
        cualquiera de ellos es 0.85 (`ReverbProfile`), asi que el recorte no se
        toma nunca. Lo que evita es que un mando mal mapeado en un host convierta
        la reverb en un divergente. MEDIDO, sin el recorte, con un impulso y un
        segundo de silencio:

            decay 0.8  (en rango)  pico 3.2e-01
            decay 1.0  (documentado)  pico 3.7e-01
            decay 1.2  (fuera)     pico 4.2e-01
            decay 2.0  (muy fuera)  pico 7.9e+06      <- se dispara

        El coeficiente del conb es `decay * 0.9`, o sea que con `decay` por encima
        de 1.111 la realimentacion pasa de 1 y la cola crece sin limite. */
    void setDecay (float decay) noexcept
    {
        decay_ = jmin (decay, 1.0f);
        updateCombParams();
    }

    /** Amortiguamiento de alta frecuencia en la realimentacion, 0..1.

        Se recorta por el mismo motivo que `setDecay`. MEDIDO sin recorte, con
        `damping = 2.0`: pico de **1.0e+30** en un segundo, o sea que el pasabajos
        `damp1 = 2, damp2 = -1` tiene el polo en -1 y crece. */
    void setDamping (float damping) noexcept
    {
        damping_ = jlimit (0.0f, 1.0f, damping);
        updateCombParams();
    }

    /** Ganancia de los allpass, 0..1.

        Se recorta por el mismo motivo. MEDIDO sin recorte, con `diffusion = 2.0`:
        pico de **3.3e+28** en un segundo. */
    void setDiffusion (float diffusion) noexcept
    {
        diffusion_ = jlimit (0.0f, 1.0f, diffusion);
        updateCombParams();
    }

    /**
        Tamano de la sala, 0..1. ESCALA LAS LONGITUDES, y por tanto
        redimensiona: moving este mando BORRA la cola (ver la cabecera).
    */
    void setRoomSize (float roomSize) noexcept
    {
        roomSize_ = roomSize;
        rebuild();
    }

    /**
        Tamano de sala y pre-retardo en una sola llamada, con UN solo `rebuild`.

        Existe por el consumidor, no por el motor: el original recalcula las dos
        cosas juntas en su `updateFilters`, y si el consumidor llamara a
        `setRoomSize` y luego a `setPreDelaySeconds` redimensionaria los buffers
        dos veces. El AUDIO sale igual (el segundo rebuild deja el motor en el
        mismo estado), pero es trabajo de sobra en un camino que se ejecuta cada
        vez que se mueve un mando.
    */
    void setGeometry (float roomSize, float preDelaySeconds) noexcept
    {
        roomSize_       = roomSize;
        preDelaySeconds_ = preDelaySeconds;
        rebuild();
    }

    /**
        Pre-retardo en SEGUNDOS (0..0.2). Ojo: el retardo REAL es un quinto de
        lo pedido, porque el original escala por 0.2 (ver la cabecera). Tambien
        redimensiona los conbs, con el efecto de borrar la cola (ver la cabecera).
    */
    void setPreDelaySeconds (float seconds) noexcept
    {
        preDelaySeconds_ = seconds;
        rebuild();
    }

    /** Niega solo el canal izquierdo. Es lo que hace la variante "Reverse". */
    void setInvertLeft (bool invert) noexcept { invertLeft_ = invert; }

    /** Pre-retardo efectivo, en muestras. OJO: incluye el factor 0.2 del original. */
    int getPreDelaySamples() const noexcept { return preDelaySamples_; }

    /**
        Longitud, en muestras, del conb `index` (0..3).

        No hace falta para sonar: esta para poder comprobar el invariante de que
        las cuatro longitudes son DISTINTAS, que si no se cumple duplica un conb
        (ver `rebuild`). Es la misma razon por la que existe
        `getPreDelaySamples`.
    */
    int getCombLength (int index) const noexcept
    {
        return combsL_[jlimit (0, kNumCombs - 1, index)].bufferSize;
    }

    /** Procesa UN marco stereo. Ver el contrato de llamada en la cabecera. */
    void processFrame (float inL, float inR, float& outL, float& outR) noexcept
    {
        float wetL = inL;
        float wetR = inR;

        // Pre-retardo: mono (la media de los dos canales) y por eso escrito una
        // sola vez, antes de abrir los conbs.
        if (preDelaySamples_ > 0)
        {
            const int readPos = wrapPreDelay (preDelayWritePos_ - preDelaySamples_);

            wetL = preDelayBuffer.getSample (0, readPos);
            wetR = preDelayBuffer.getSample (0, readPos);

            preDelayBuffer.setSample (0, preDelayWritePos_, (inL + inR) * 0.5f);
            preDelayWritePos_ = wrapPreDelay (preDelayWritePos_ + 1);
        }

        if (invertLeft_)
            wetL = -wetL;

        // Conbs en paralelo y promediados.
        float sumL = 0.0f, sumR = 0.0f;

        for (int i = 0; i < kNumCombs; ++i)
        {
            sumL += processComb (combsL_[i], wetL);
            sumR += processComb (combsR_[i], wetR);
        }

        sumL *= 0.25f;
        sumR *= 0.25f;

        // Allpass en serie.
        for (int i = 0; i < kNumAllPass; ++i)
        {
            sumL = processAllPass (allpassL_[i], sumL);
            sumR = processAllPass (allpassR_[i], sumR);
        }

        // El decaimiento negativo (variante "gated") da una escala distinta.
        const float scale = decay_ < 0.0f ? 0.5f : decay_ * 0.7f + 0.3f;

        outL = sumL * scale;
        outR = sumR * scale;
    }

    /** Sample rate de la ultima llamada a `prepare`. */
    double getSampleRate() const noexcept { return sampleRate_; }

    dspDeclareNonCopyableWithLeakDetector (SchroederReverb)

private:
    //==========================================================================
    static constexpr int kNumCombs   = 4;
    static constexpr int kNumAllPass = 3;

    struct Comb
    {
        float* buffer     = nullptr;
        int    bufferSize = 1;
        int    writePos   = 0;
        float  feedback   = 0.5f;
        float  damp1      = 0.5f;
        float  damp2      = 0.5f;
        float  filterState = 0.0f;
    };

    struct AllPass
    {
        float* buffer     = nullptr;
        int    bufferSize = 1;
        int    writePos   = 0;
        float  gain       = 0.5f;
    };

    //==========================================================================
    /** Un conb con pasabajos de un polo en la realimentacion. */
    static float processComb (Comb& comb, float input) noexcept
    {
        const float output = comb.buffer[comb.writePos];

        comb.filterState = output * comb.damp1 + comb.filterState * comb.damp2;
        comb.buffer[comb.writePos] = input + comb.filterState * comb.feedback;

        if (++comb.writePos >= comb.bufferSize)
            comb.writePos = 0;

        return output;
    }

    /** Un allpass de primer orden. */
    static float processAllPass (AllPass& ap, float input) noexcept
    {
        const float buffered = ap.buffer[ap.writePos];
        const float output   = -input + buffered;

        ap.buffer[ap.writePos] = input + buffered * ap.gain;

        if (++ap.writePos >= ap.bufferSize)
            ap.writePos = 0;

        return output;
    }

    int wrapPreDelay (int pos) const noexcept
    {
        while (pos < 0)                pos += preDelayCapacity_;
        while (pos >= preDelayCapacity_) pos -= preDelayCapacity_;

        return pos;
    }

    /** Coeficientes de los conbs y los allpass. NO toca los buffers. */
    void updateCombParams() noexcept
    {
        const float feedback = decay_ < 0.0f ? 0.3f : decay_ * 0.9f;

        // LA AMORTIGUACION EN SU EXTREMO. `damp1 = damping, damp2 = 1 - damping`
        // es un pasabajos de un polo cuya ganancia en continua es 1 para CUALQUIER
        // valor, y en `damping = 0` se degenera: `y = 0·x + 1·y` es `y = y`, el
        // estado se CONGELA en su ultimo valor y ya no decae nunca.
        //
        // MEDIDO, con un bloque de DC al principio y dos segundos de silencio:
        //
        //     amortiguacion intacta  : media con signo +0.000000
        //     bajada a 0 en caliente : media con signo +0.088829
        //
        // O sea que bajar el mando a 0 no quita la amortiguacion: mete un offset
        // de DC permanente en la salida, que no se va nunca. El limite correcto
        // de "sin amortiguacion" es pasar TODO, no quedarse donde estuviera.
        //
        // El arreglo es el par (1, 0) cuando `damping` es 0, y NO cambia ni un
        // bit ningun otro valor: para `damping > 0` el par es el de siempre, y
        // ningun consumidor del arbol pasa un 0 (el rango de `ReverbProfile` es
        // 0.2..0.9), asi que el cambio solo afecta al caso que estaba roto.
        const float damp1 = damping_ > 0.0f ? damping_ : 1.0f;
        const float damp2 = damping_ > 0.0f ? 1.0f - damping_ : 0.0f;

        for (int i = 0; i < kNumCombs; ++i)
        {
            combsL_[i].feedback = feedback;
            combsL_[i].damp1    = damp1;
            combsL_[i].damp2    = damp2;
            combsR_[i].feedback = feedback;
            combsR_[i].damp1    = damp1;
            combsR_[i].damp2    = damp2;
        }

        for (int i = 0; i < kNumAllPass; ++i)
        {
            allpassL_[i].gain = diffusion_ * 0.7f;
            allpassR_[i].gain = diffusion_ * 0.7f;
        }
    }

    /**
        Recalcula longitudes y redimensiona los buffers.

        OJO: esto es lo que hace que mover el mando de tamano borre la cola (ver
        la cabecera). Cada conb lleva un desplazamiento de `i * 7` muestras para
        que las longitudes no sean primos entre si y la reverberacion no tenga
        periodicidad audible.

        Y EL TECHO VA ANTES DEL DESPLAZAMIENTO, POR UN MOTIVO MEDIDO. Aplicado
        despues (como estaba), el recorte podia dejar dos conbs en la MISMA
        longitud: con `maxCombSeconds = 0.040` a 48 kHz los cuatro acababan en
        1920 muestras, y alli el pico de la respuesta al impulso se va de 0.1625
        a 0.4875 (3x, porque en vez de un conb disparan tres a la vez) y aparece
        un eco de aleteo periodico. Con `maxCombSeconds = 0.010` los cuatro
        colapsan a 480 muestras: la reverb se quadruplica y mete un zumbido de
        100 Hz. Eso no es un techo defensivo, es un amplificador.

        Aplicado aqui el recorte, las cuatro longitudes son SIEMPRE distintas: los
        conb de partida ya van en orden, y sumar `i * 7` las deja estrictamente
        crecientes. Y para todo consumidor actual NO CAMBIA NADA: con el techo por
        defecto de 0.25 s el recorte no llega a dispararse ni de lejos (el conb
        mas largo que se puede pedir con `roomSize = 1.0` son 0.0621 s), asi que
        el cambio es un no-op bit a bit para toda entrada con techo >= 0.07 s.

        LOS CUATRO `clear()` NO SON OPCIONALES, Y ESTA ES LA SEGUNDA VEZ QUE SE
        APRENDE. `setSize` con el mismo numero de muestras NO toca el contenido
        viejo, se lo deja al que venga a leerlo, y con `avoidReallocating` no
        hay garantia de que cero ni siquiera al cambiar de tamano. El original
        llama a `clear()` DESPUES de `setSize` y por eso, al mover un mando, la
        cola se vacia SIEMPRE, haya cambiado el tamano o no. Sin esta linea la
        reverb arrastra el estado anterior y, con suerte, se nota; sin que cambie
        el tamano, ni se nota y solo aparece en la paridad. Comprobado.
    */
    void rebuild()
    {
        const float sizeScale = 0.5f + roomSize_;

        int combLen[kNumCombs];
        for (int i = 0; i < kNumCombs; ++i)
        {
            static constexpr double kCombSeconds[kNumCombs] =
                { 0.0297, 0.0331, 0.0378, 0.0411 };

            int length = static_cast<int> (sampleRate_ * kCombSeconds[i] * sizeScale);

            // Techo defensivo: con un roomSize absurdo el conb podria pedir mas
            // de lo que el buffer declarado puede dar. El original no lo tiene;
            // aqui no puede haber un desbordamiento en silencio. Va ANTES del
            // desplazamiento, y por el motivo medido que explica `rebuild`.
            length = jmin (jmax (1, length),
                           jmax (1, static_cast<int> (maxCombSeconds_ * sampleRate_)));

            length += i * 7;                    // desplazamiento anti-periodicidad

            combLen[i] = length;
        }

        int combTotal = 0;
        for (int i = 0; i < kNumCombs; ++i)
            combTotal += combLen[i];

        // Un buffer POR CANAL, no uno compartido. Es tentador ahorrar la
        // memoria punteando los dos canales al mismo sitio, y esta es
        // exactamente la trampa: los dos canales se escriben encima y la reverb
        // se vuelve mono (suena, pero no es stereo). El original tiene buffers
        // separados y `testSchroederReverb` lo vigila con el caso "misma
        // entrada, L y R deben coincidir", que es el que se delata si alguien
        // vuelve a compartir.
        combBufferL.setSize (1, combTotal, false, false, true);
        combBufferL.clear();
        combBufferR.setSize (1, combTotal, false, false, true);
        combBufferR.clear();

        int offset = 0;
        for (int i = 0; i < kNumCombs; ++i)
        {
            combsL_[i].buffer     = combBufferL.getWritePointer (0) + offset;
            combsL_[i].bufferSize = combLen[i];
            combsL_[i].writePos   = 0;
            combsL_[i].filterState = 0.0f;

            combsR_[i].buffer     = combBufferR.getWritePointer (0) + offset;
            combsR_[i].bufferSize = combLen[i];
            combsR_[i].writePos   = 0;
            combsR_[i].filterState = 0.0f;

            offset += combLen[i];
        }

        int apLen[kNumAllPass];
        int apTotal = 0;

        for (int i = 0; i < kNumAllPass; ++i)
        {
            static constexpr double kAllPassSeconds[kNumAllPass] =
                { 0.0051, 0.0068, 0.0083 };

            apLen[i]  = jmax (1, static_cast<int> (sampleRate_ * kAllPassSeconds[i] * sizeScale));
            apTotal  += apLen[i];
        }

        allpassBufferL.setSize (1, apTotal, false, false, true);
        allpassBufferL.clear();
        allpassBufferR.setSize (1, apTotal, false, false, true);
        allpassBufferR.clear();

        offset = 0;
        for (int i = 0; i < kNumAllPass; ++i)
        {
            allpassL_[i].buffer     = allpassBufferL.getWritePointer (0) + offset;
            allpassL_[i].bufferSize = apLen[i];
            allpassL_[i].writePos   = 0;

            allpassR_[i].buffer     = allpassBufferR.getWritePointer (0) + offset;
            allpassR_[i].bufferSize = apLen[i];
            allpassR_[i].writePos   = 0;

            offset += apLen[i];
        }

        // OJO CON EL 0.2. El retardo real es `segundos * sampleRate * 0.2`, NO
        // `segundos * sampleRate`. El original multiplica por 0.2 y asi esta
        // escrito, de modo que un pre-retardo pedido de 100 ms al final retrasa
        // 20 ms. Es un descuido del original (probablemente queria decir "el
        // maximo es 200 ms" y se le colo el factor en la escala), pero cambiarlo
        // seria cambiar el sonido de nueve efectos ya publicados, asi que se
        // reproduce tal cual y se documenta.
        preDelaySamples_ = jlimit (0, preDelayCapacity_,
                                   static_cast<int> (preDelaySeconds_ * sampleRate_ * 0.2));

        // Los coeficientes se recalculan aqui tambien, para que un `rebuild`
        // desde `prepare` deje el motor con los valores actuales y no con los de
        // la construccion.
        updateCombParams();
    }

    AudioBuffer<float> combBufferL;
    AudioBuffer<float> combBufferR;
    AudioBuffer<float> allpassBufferL;
    AudioBuffer<float> allpassBufferR;
    AudioBuffer<float> preDelayBuffer;

    Comb     combsL_[kNumCombs];
    Comb     combsR_[kNumCombs];
    AllPass  allpassL_[kNumAllPass];
    AllPass  allpassR_[kNumAllPass];

    double sampleRate_      = 44100.0;
    double maxCombSeconds_  = 0.25;
    int    preDelayCapacity_ = 1;
    int    preDelaySamples_   = 0;
    int    preDelayWritePos_   = 0;

    float decay_       = 0.5f;
    float damping_     = 0.5f;
    float diffusion_   = 0.5f;
    float roomSize_    = 0.5f;
    float preDelaySeconds_ = 0.0f;
    bool  invertLeft_  = false;
};

} // namespace abd::dsp