/*
  ==============================================================================

    FxEngine.h
    Cuatro slots de efecto y una matriz de ruteo. Namespace abd::dsp, modulo
    ABDShared::DspEffects.

    POR QUE ESTE MOTOR ESTA EN EL MODULO Y NO EN UN PRODUCTO. Porque hay dos
    productos que lo necesitan y porque la version de ABDEep era codigo JUCE:
    su `FXEngine::updateParameters` toma un `AudioProcessorValueTreeState` y su
    `process` toma un `juce::AudioBuffer`. ABDNeural compila su DSP a WebAssembly
    SIN JUCE, asi que copiar el motor obligaba a meter JUCE en el motor o a
    duplicarlo. Este es el motor de los dos: JUCE-free, sobre `AudioBuffer<float>`
    de `DspCore`, y con la politica de producto (que tabla de efectos, que
    nombres en el panel, que tabla de unidades) en manos del consumidor.

    QUE ES SEMANTICA Y QUE ES PRODUCTO. Los NUEVE modos de ruteo estan aqui, y el
    numero de slots tambien (4). No es que sean "de la maquina": es que son la
    TOPOLOGIA del ruteo, que no cambia entre sintetizadores, y con ella cambia el
    significado de un parametro —el `mix` de un slot en el 6 y en el 3 no vale
    lo mismo—. Lo que SI es de producto es que efectos hay, como se llaman y que
    unidades usan sus mandos, y eso entra por `setCatalogue`.

    LOS MODOS, con lo que hacen, porque los numeros solos no se aprenden:

       0  1 -> 2 -> 3 -> 4                serie
       1  (1 || 2) -> 3 -> 4              pareja en paralelo delante
       2  (1 || 2) || (3 || 4)            dos parejas en paralelo
       3  1 || 2 || 3 || 4                todo en paralelo
       4  (1 -> 2) || (3 -> 4)            dos cadenas en paralelo
       5  1 -> (2 || 3) -> 4              serie con el centro partido
       6  (1 || 2) -> (3 || 4)            el mas usado: mojado y seco en
                                          paralelo y luego en serie
       7  (1 -> 2 -> 3) || 4              cadena en serie y una adicion
       8  (1 || 2) -> 3 -> 4              la misma topologia que el 1
       9  1 -> 2 -> 3 -> 4 con realimentacion del conjunto

    Y EL MODO FX, que no es un ruteo sino donde se engancha: 0 insert, 1 send,
    2 bypass.

    UNA ADVERTENCIA SOBRE EL MODO 1 Y EL 8. Aqui hacen EXACTAMENTE lo mismo, los
    dos `(1 || 2) -> 3 -> 4`, y se conservan los dos numeros porque cambiar el
    significado de un numero que un panel ya muestra es peor que la duplicidad.

    OJO, QUE "HACEN LO MISMO" ES DE AQUI, NO DE ALLI. Se comprobo contra el
    codigo de ABDEep (`FXEngine_Routing.cpp`) y alli NO son el mismo diagrama:
    su modo 8 si es `(1∥2)→3→4`, y su modo 1 es `(1∥2)→(3∥4)`, que aqui es el
    6. O sea que el numero de ABDEep y el de aqui no son el mismo catalogo: el 1
    de ABDEep es el 6 de aqui y el 8 de ABDEep es el 1 de aqui. Y el 1 de ABDEep
    promedia la pareja (`1 / activos`) mientras que aqui se suma. Las dos
    diferencias estan escritas porque son el tipo de cosa que se da por buena
    sin mirar y luego pesa: un preset migrado de un producto al otro no suena
    igual por el numero del modo, y porque la suma y la media no son lo mismo.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "DspEffects/FxRegistry.h"
#include "DspEffects/FxSlot.h"

namespace abd::dsp
{

//==============================================================================
/** Los modos de ruteo, con nombre, porque un numero en un panel no se aprende. */
enum class FxRouting : int
{
    Series                 = 0,   // 1 -> 2 -> 3 -> 4
    ParallelFront          = 1,   // (1 || 2) -> 3 -> 4
    ParallelPairs          = 2,   // (1 || 2) || (3 || 4)
    FullParallel           = 3,   // 1 || 2 || 3 || 4
    DualSeriesParallel     = 4,   // (1 -> 2) || (3 -> 4)
    SeriesSplitMiddle      = 5,   // 1 -> (2 || 3) -> 4
    ParallelPairsSeries    = 6,   // (1 || 2) -> (3 || 4)
    SeriesChainPlusOne     = 7,   // (1 -> 2 -> 3) || 4
    ParallelFrontSeries    = 8,   // (1 || 2) -> 3 -> 4   (igual que 1)
    SeriesWithFeedback     = 9    // 1 -> 2 -> 3 -> 4 + realimentacion
};

/** Modo de insercion del motor entero. */
enum class FxMode : int
{
    Insert = 0,
    Send   = 1,
    Bypass = 2
};

//==============================================================================
class FxEngine
{
public:
    FxEngine() = default;

    //==============================================================================
    /** Prepara los cuatro slots. El catalogo es del producto: aqui solo se
        guarda el puntero, no se copia, y el producto vive mas que el motor. */
    void prepare (double sampleRate, int numChannels, int maxBlockSize) noexcept
    {
        sampleRate_   = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxBlockSize_ = maxBlockSize > 0 ? maxBlockSize : 512;
        (void) numChannels;

        ensureScratch (maxBlockSize_);

        for (int i = 0; i < kFxNumSlots; ++i)
            slots_[i].prepare (sampleRate_, 2, maxBlockSize_,
                               catalogue_, catalogueSize_);
    }

    /** Cambia el catalogo y reaplica el tipo de cada slot. Un producto con
        efectos propios (ABDEep tiene 48 mas) monta su tabla y la pasa aqui.

        OJO CON EL ORDEN: `prepare` en el slot ya recrea la instancia con la
        tabla nueva, asi que despues hay que VOLVER a poner el tipo que tenia.
        Sin ese `setType` de vuelta, cambiar de catalogo sacaria de todos los
        slots el efecto que el usuario habia elegido. */
    void setCatalogue (const FxEffectInfo* catalogue, int size) noexcept
    {
        catalogue_     = catalogue;
        catalogueSize_ = size;

        if (sampleRate_ <= 0.0)
            return;   // todavia no preparado: `prepare` cogera la tabla

        for (int i = 0; i < kFxNumSlots; ++i)
        {
            const int type = slots_[i].getType();
            slots_[i].prepare (sampleRate_, 2, maxBlockSize_, catalogue_, catalogueSize_);
            slots_[i].setType (type);
        }
    }

    FxSlot& getSlot (int index) noexcept
    {
        dspAssert (index >= 0 && index < kFxNumSlots);
        return slots_[jlimit (0, kFxNumSlots - 1, index)];
    }

    const FxSlot& getSlot (int index) const noexcept
    {
        return slots_[jlimit (0, kFxNumSlots - 1, index)];
    }

    void setRouting (FxRouting r) noexcept { routing_ = r; }
    FxRouting getRouting() const noexcept { return routing_; }

    void setMode (FxMode m) noexcept { mode_ = m; }
    FxMode getMode() const noexcept { return mode_; }

    /** Nivel de envio, para `FxMode::Send`. Un valor que no es un numero se
        ignora: en el 9 la realimentacion se queda en un buffer y ahi un NaN no
        se va nunca. */
    void setSendLevel (float level) noexcept { if (std::isfinite (level)) sendLevel_ = jlimit (0.0f, 1.0f, level); }
    float getSendLevel() const noexcept     { return sendLevel_; }

    /** Ganancia de la realimentacion global del ruteo 9. */
    void setFeedbackGain (float gain) noexcept { if (std::isfinite (gain)) feedbackGain_ = jlimit (0.0f, 0.95f, gain); }
    float getFeedbackGain() const noexcept    { return feedbackGain_; }

    void reset() noexcept
    {
        for (int i = 0; i < kFxNumSlots; ++i)
            slots_[i].reset();

        feedback_.clear();
    }

    //==============================================================================
    /**
        Procesa un bloque por el modo y el ruteo activos.

        `buffer` entra con la SECA y sale con el resultado.

        Y SI EL BLOQUE ES MAS GRANDE QUE EL PREPARADO, SE TROCEA AQUI, y no en
        el slot. Es la decision que sostiene todo lo demas: un `FxSlot` procesa
        como mucho `maxBlockSize_` muestras y devuelve el resto del buffer SIN
        TOCAR. Si el motor le pasara un bloque de 4096 muestras a un slot
        preparado a 512, el slot processing 512 y las otras 3584 saldirian
        SECAS: la mitad del bloque del host sin efecto nenhum, y en silencio. No
        es un caso raro, es el que pasa en cuanto un host anda con bloques
        grandes y el plugin se ha preparado a 256.

        Medido: con el motor preparado a 64 y un bloque de 512, la version que
        entregaba el bloque entero al slot diferia de la que lo troceaba en
        0.119 de pico, sobre una entrada de 0.2 — o sea, la mitad de la senal
        salia SECA. Troceando en el motor la diferencia es de 0 ulps, porque los
        motores de este modulo son todos POR MUESTRA y trocear no cambia ni un
        bit de lo que suena.
    */
    void process (AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        if (mode_ == FxMode::Bypass)
            return;

        const int channels = buffer.getNumChannels();
        const int available = jmin (numSamples, buffer.getNumSamples());
        if (available <= 0 || maxBlockSize_ <= 0)
            return;

        if (channels >= 2)
        {
            processStereoPath (buffer, available);
            return;
        }

        // MONO. El motor entero trabaja sobre dos canales, y la razon para no
        // hacerlo con un `if` en cada ruta es que escribir el canal 1 de un
        // buffer de un canal es escribir un nullptr: la primera suma del
        // ruteo 9 se va con el. En vez de eso la senal entra en una rebanada de
        // dos canales, se procesa como stereo y sale por el unico que hay.
        if (channels < 1)
            return;

        processMonoPath (buffer, available);
    }

private:
    //==============================================================================
    //  Los nueve ruteos. Todos leen del buffer principal y escriben en el, con
    //  buffers auxiliares para las copias. Los auxiliares estan pre-alocados.
    //==============================================================================

    /** Stereo: si el bloque cabe, se procesa donde esta, sin copiar ni una
        muestra. Si no cabe, se trocea — y ahi si hay copia, porque el motor
        trabaja siempre con un bloque entero en su propio buffer. */
    void processStereoPath (AudioBuffer<float>& buffer, int available) noexcept
    {
        if (available <= maxBlockSize_)
        {
            processInPlace (buffer, 2, available);
            return;
        }

        for (int offset = 0; offset < available; offset += maxBlockSize_)
        {
            const int n = jmin (maxBlockSize_, available - offset);

            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = buffer.getReadPointer (ch) + offset;
                float* dst = slice_.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                    dst[i] = src[i];
            }

            processInPlace (slice_, 2, n);

            for (int ch = 0; ch < 2; ++ch)
            {
                float* dst = buffer.getWritePointer (ch) + offset;
                const float* src = slice_.getReadPointer (ch);
                for (int i = 0; i < n; ++i)
                    dst[i] = src[i];
            }
        }
    }

    /** Mono: el mismo troceado, con la entrada replicada en los dos canales. */
    void processMonoPath (AudioBuffer<float>& buffer, int available) noexcept
    {
        for (int offset = 0; offset < available; offset += maxBlockSize_)
        {
            const int n = jmin (maxBlockSize_, available - offset);

            const float* mono = buffer.getReadPointer (0) + offset;
            float* l = slice_.getWritePointer (0);
            float* r = slice_.getWritePointer (1);
            for (int i = 0; i < n; ++i) { l[i] = mono[i]; r[i] = mono[i]; }

            processInPlace (slice_, 2, n);

            float* dst = buffer.getWritePointer (0) + offset;
            for (int i = 0; i < n; ++i)
                dst[i] = l[i];
        }
    }

    void processInPlace (AudioBuffer<float>& buffer, int channels, int n) noexcept
    {
        // Sin ningun efecto en la cadena no se toca el buffer. En el modo de
        // envio tambien: si no hay nada que enviar, la mezcla final es
        // `seca·(1−nivel) + mojada·nivel` con las dos cosas iguales, que es la
        // seca; devolverla sin tocar es exactamente ese numero y sin gastar el
        // paso de copia.
        if (! hayEfectoEnLaCadena())
            return;

        if (mode_ == FxMode::Send)
        {
            processSend (buffer, channels, n);
            return;
        }

        switch (routing_)
        {
            case FxRouting::FullParallel:         routeFullParallel (buffer, n); break;
            case FxRouting::ParallelPairs:        routeParallelPairs (buffer, n); break;
            case FxRouting::DualSeriesParallel:   routeDualSeriesParallel (buffer, n); break;
            case FxRouting::SeriesSplitMiddle:    routeSeriesSplitMiddle (buffer, n); break;
            case FxRouting::ParallelPairsSeries:  routeParallelPairsSeries (buffer, n); break;
            case FxRouting::ParallelFront:
            case FxRouting::ParallelFrontSeries:  routeParallelFrontSeries (buffer, n); break;
            case FxRouting::SeriesChainPlusOne:   routeSeriesChainPlusOne (buffer, n); break;
            case FxRouting::SeriesWithFeedback:   routeSeriesWithFeedback (buffer, n); break;
            case FxRouting::Series:
            default:                              routeSeries (buffer, n); break;
        }
    }

    /**
        SI NO HAY NINGUN EFECTO EN LA CADENA, EL MODULO ES TRANSPARENTE.

        Y NO ES UNA PIEDADAD: sin este paso, cinco de los nueve ruteos
        devolvian la senal ROTA con los cuatro huecos en bypass, y no lo hacia
        cualquiera. El 2 y el 6 la BORRAN -- la salida es silencio -- y el 4 y el
        7 la multiplican por tres, y el 5 por dos. Medido con una entrada de
        1.0: sale 0, 0, 3, 2 y 3.

        La causa es que los cinco ruteos acaban en `copyTo (acumulador, buffer)`.
        El acumulador lo siembran con una suma de ramas paralelas, y una rama
        que no tiene ningun efecto NO aporta la seca: `runParallel` se salta los
        huecos inactivos. Asi que el acumulador se queda sin la entrada, y al
        volcarlo al buffer la signal se va con el.

        O sea: enrutando "dos parejas en paralelo" y dejando el rack vacio, el
        plugin se queda mudo. Y no lo hacia ver ningun test porque el unico que
        miraba el bypass lo miraba con el ruteo de serie, que es el unico que no
        pasa por el acumulador.

        Y POR QUE SE ARREGLA AQUI Y NO EN LOS CINQUE RUTEOS. Metiendo la seca
        en cada rama parallel[a] para que la cuenta cuadre, cada preset con
        efectos en paralelo sonaria OTRA cosa: es cambiar el sonido de lo que ya
        esta en produccion. Con este paso solo cambia el caso en el que no hay
        ningun efecto en la cadena, que antes era silencio o un multiplicador.
    */
    bool hayEfectoEnLaCadena() const noexcept
    {
        for (int i = 0; i < kFxNumSlots; ++i)
            if (slots_[i].isActive())
                return true;

        return false;
    }

    /**
        EL MODO ENVIO, y por que mezcla al FINAL en vez de tocar los `mix` de los
        slots.

        La primera version de esto hacia `setMix (sendLevel_)` en cada slot antes
        de procesar. Suena casi igual, y esta MAL: `setMix` es un mando del
        usuario, asi que un bloque de envio se comia el `mix` que el usuario
        habia puesto en ese slot, y no lo devolvia. Con un bloque de envio en
        medio de una reproduccion el mezclado se movia solo.

        Lo correcto es lo que hace ABDEep, y es lo de aqui: se guarda la seca,
        se procesa la cadena con los mandos de cada slot intactos, y la mezcla
        final es `seca·(1−nivel) + procesado·nivel`. El envio es una mezcla del
        BUS, no un cambio de un mando de un efecto.
    */
    void processSend (AudioBuffer<float>& buffer, int channels, int n) noexcept
    {
        copyTo (buffer, dry_, n, channels);

        switch (routing_)
        {
            case FxRouting::FullParallel:         routeFullParallel (buffer, n); break;
            case FxRouting::ParallelPairs:        routeParallelPairs (buffer, n); break;
            case FxRouting::DualSeriesParallel:   routeDualSeriesParallel (buffer, n); break;
            case FxRouting::SeriesSplitMiddle:    routeSeriesSplitMiddle (buffer, n); break;
            case FxRouting::ParallelPairsSeries:  routeParallelPairsSeries (buffer, n); break;
            case FxRouting::ParallelFront:
            case FxRouting::ParallelFrontSeries:  routeParallelFrontSeries (buffer, n); break;
            case FxRouting::SeriesChainPlusOne:   routeSeriesChainPlusOne (buffer, n); break;
            case FxRouting::SeriesWithFeedback:   routeSeriesWithFeedback (buffer, n); break;
            case FxRouting::Series:
            default:                              routeSeries (buffer, n); break;
        }

        const float level = sendLevel_;
        const float seca = 1.0f - level;

        for (int ch = 0; ch < channels; ++ch)
        {
            float* out = buffer.getWritePointer (ch);
            const float* dry = dry_.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                out[i] = dry[i] * seca + out[i] * level;
        }
    }

    /** 1 -> 2 -> 3 -> 4. El mas simple y el de referencia. */
    void routeSeries (AudioBuffer<float>& buffer, int n) noexcept
    {
        for (int i = 0; i < kFxNumSlots; ++i)
            slots_[i].process (buffer, n);
    }

    /** (1 || 2) -> 3 -> 4: pareja en paralelo delante, y los dos de atras en
        serie.

        ESTE ERA EL BUG QUE NO SE OIA. Los modos 1 y 8 estan desde el principio
        escritos en la cabecera de este fichero como "(1 || 2) -> 3 -> 4", y en
        el `switch` caian en el `default` de la serie: hacian 1 -> 2 -> 3 -> 4.
        La pareja del principio no se oia nunca, porque la serie la tapaba. No
        lo cazaba ningun test porque los que habia solo pedian numeros finitos y
        que el 1 y el 8 dieran lo mismo --y lo dang, siendo los dos la misma
        serie. Lo que faltaba era comprobar la CUENTA de cada modo, y con una
        sonda de cuatro constantes se ve a la primera: el diagrama pide 5.625 y
        el motor daba 5.375, que es exactamente lo que da la serie.

        Que la suma de la pareja sea una suma y no una media es decision de este
        modulo y no se cambia aqui: ver la nota sobre ABDEep al final de la
        cabecera, que reparte los numeros de otra manera. */
    void routeParallelFrontSeries (AudioBuffer<float>& buffer, int n) noexcept
    {
        runParallel (buffer, accum_, 0, 1, n);
        copyTo (accum_, buffer, n);

        slots_[2].process (buffer, n);
        slots_[3].process (buffer, n);
    }

    /** (1 || 2) || (3 || 4): dos parejas en paralelo. */
    void routeParallelPairs (AudioBuffer<float>& buffer, int n) noexcept
    {
        runParallel (buffer, accum_, 0, 1, n);
        runParallel (buffer, parallel_, 2, 3, n);
        addTo (accum_, parallel_, n);
        copyTo (accum_, buffer, n);
    }

    /** 1 || 2 || 3 || 4: cada slot con la misma entrada, y todo junto. */
    void routeFullParallel (AudioBuffer<float>& buffer, int n) noexcept
    {
        copyTo (buffer, accum_, n);
        for (int i = 0; i < kFxNumSlots; ++i)
        {
            if (!slots_[i].isActive())
                continue;
            runOne (buffer, scratch_, i, n);
            addTo (accum_, scratch_, n);
        }
        copyTo (accum_, buffer, n);
    }

    /** (1 -> 2) || (3 -> 4): dos cadenas completas en paralelo. */
    void routeDualSeriesParallel (AudioBuffer<float>& buffer, int n) noexcept
    {
        copyTo (buffer, accum_, n);

        copyTo (buffer, parallel_, n);
        slots_[0].process (parallel_, n);
        slots_[1].process (parallel_, n);
        addTo (accum_, parallel_, n);

        copyTo (buffer, parallel_, n);
        slots_[2].process (parallel_, n);
        slots_[3].process (parallel_, n);
        addTo (accum_, parallel_, n);

        copyTo (accum_, buffer, n);
    }

    /** 1 -> (2 || 3) -> 4: serie con el centro partido. */
    void routeSeriesSplitMiddle (AudioBuffer<float>& buffer, int n) noexcept
    {
        copyTo (buffer, scratch_, n);
        slots_[0].process (scratch_, n);

        // Los dos del medio en paralelo, cada uno con la MISMA entrada (la
        // salida del slot 1), y sus resultados sumados.
        copyTo (scratch_, parallel_, n);
        slots_[1].process (parallel_, n);
        copyTo (scratch_, accum_, n);

        copyTo (scratch_, parallel_, n);
        slots_[2].process (parallel_, n);
        addTo (accum_, parallel_, n);

        copyTo (accum_, scratch_, n);
        slots_[3].process (scratch_, n);
        copyTo (scratch_, buffer, n);
    }

    /** (1 || 2) -> (3 || 4): mojado y seco en paralelo, y luego en serie. */
    void routeParallelPairsSeries (AudioBuffer<float>& buffer, int n) noexcept
    {
        runParallel (buffer, accum_, 0, 1, n);
        runParallel (accum_, parallel_, 2, 3, n);
        addTo (accum_, parallel_, n);
        copyTo (accum_, buffer, n);
    }

    /** (1 -> 2 -> 3) || 4: cadena en serie y una adicion. */
    void routeSeriesChainPlusOne (AudioBuffer<float>& buffer, int n) noexcept
    {
        copyTo (buffer, accum_, n);

        copyTo (buffer, scratch_, n);
        slots_[0].process (scratch_, n);
        slots_[1].process (scratch_, n);
        slots_[2].process (scratch_, n);
        addTo (accum_, scratch_, n);

        copyTo (buffer, scratch_, n);
        slots_[3].process (scratch_, n);
        addTo (accum_, scratch_, n);

        copyTo (accum_, buffer, n);
    }

    /**
        1 -> 2 -> 3 -> 4 con REALIMENTACION del conjunto.

        Y LA SEMANTICA ES LA DE ABDEEP, que ya esta en produccion, y se copia
        literal: la realimentacion es **la salida del bloque ANTERIOR**, que se
        suma a la ENTRADA antes de entrar en la cadena. Es decir
        `y[n] = cadena (x[n] + g · y[n-1])`: un retardo realimentado de un
        bloque de latencia.

        La primera version de este metodo realimentaba la seca del bloque en
        curso, que es otra topologia distinta —un `y = cadena (x + g·x)`, que no
        realimenta nada porque `x` no depende de `y`— y se cambio al comparar
        con el original. Queda escrito porque es el tipo de error que se cuela
        al "mejorar" un codigo que ya funciona: el cambio parecia mas correcto y
        era otro.
    */
    void routeSeriesWithFeedback (AudioBuffer<float>& buffer, int n) noexcept
    {
        if (feedbackGain_ <= 0.0f)
        {
            routeSeries (buffer, n);
            return;
        }

        if (feedback_.getNumSamples() >= n)
        {
            const float* fbL = feedback_.getReadPointer (0);
            const float* fbR = feedback_.getReadPointer (1);
            float* outL = buffer.getWritePointer (0);
            float* outR = buffer.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                outL[i] += fbL[i] * feedbackGain_;
                outR[i] += fbR[i] * feedbackGain_;
            }
        }

        for (int i = 0; i < kFxNumSlots; ++i)
            slots_[i].process (buffer, n);

        // Y la salida de este bloque es la realimentacion del siguiente.
        if (feedback_.getNumSamples() >= n)
            copyTo (buffer, feedback_, n);
    }

    /** Procesa dos slots en paralelo sobre `buffer` y suma el resultado. */
    void runParallel (AudioBuffer<float>& buffer, AudioBuffer<float>& sum,
                      int a, int b, int n) noexcept
    {
        sum.clear();
        for (int s = a; s <= b; ++s)
        {
            if (!slots_[s].isActive())
                continue;
            runOne (buffer, scratch_, s, n);
            addTo (sum, scratch_, n);
        }
    }

    /** Un slot suelto sobre una copia de `buffer`, escribiendo en `dst`. */
    void runOne (AudioBuffer<float>& buffer, AudioBuffer<float>& dst, int slot, int n) noexcept
    {
        copyTo (buffer, dst, n);
        slots_[slot].process (dst, n);
    }

    //==============================================================================
    //  Copias de bloque. Todas van solo por los dos primeros canales, que es lo
    //  que el motor procesa; un host con mas canales los deja intactos.
    //==============================================================================

    void copyTo (AudioBuffer<float>& from, AudioBuffer<float>& to, int n, int channels = 2) noexcept
    {
        const int nch = jmin (channels, from.getNumChannels());
        for (int ch = 0; ch < nch; ++ch)
        {
            const float* src = from.getReadPointer (ch);
            float* dst = to.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                dst[i] = src[i];
        }
    }

    void addTo (AudioBuffer<float>& dst, AudioBuffer<float>& src, int n) noexcept
    {
        const int nch = jmin (2, jmin (dst.getNumChannels(), src.getNumChannels()));
        for (int ch = 0; ch < nch; ++ch)
        {
            float* d = dst.getWritePointer (ch);
            const float* s = src.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] += s[i];
        }
    }

    /** Los buffers auxiliares. Se dimensionan al bloque PREPARADO y no crecen
        despues: es justo el tamano que el motor nunca pasa de un troceo, y un
        `process` que reservase memoria seria una asignacion en el lazo de
        audio. Un bloque mayor del host se trocea, no se agranda. */
    void ensureScratch (int blockSize) noexcept
    {
        if (blockSize < 1)
            blockSize = 1;

        parallel_.setSize (2, blockSize);
        accum_.setSize (2, blockSize);
        scratch_.setSize (2, blockSize);
        feedback_.setSize (2, blockSize);
        dry_.setSize (2, blockSize);
        slice_.setSize (2, blockSize);

        parallel_.clear();
        accum_.clear();
        scratch_.clear();
        feedback_.clear();
        dry_.clear();
    }

    //--- Estado ------------------------------------------------------------
    FxSlot slots_[kFxNumSlots];

    const FxEffectInfo* catalogue_ = nullptr;
    int catalogueSize_ = 0;

    FxRouting routing_ = FxRouting::Series;
    FxMode    mode_ = FxMode::Insert;

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 512;

    float sendLevel_ = 0.5f;
    float feedbackGain_ = 0.0f;

    AudioBuffer<float> parallel_;
    AudioBuffer<float> accum_;
    AudioBuffer<float> scratch_;
    AudioBuffer<float> feedback_;
    AudioBuffer<float> dry_;
    AudioBuffer<float> slice_;
};

} // namespace abd::dsp
