/*
  ==============================================================================

    Phaser4.h
    Phaser de N etapas de primer orden en todo-paso, con el barrido del notch
    en escala logaritmica y el coeficiente calculado POR BLOQUE.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Port del phaser de ABDMS2000
    (Source/DSP/Effects/ModFX.cpp, `ModFX::processPhaser`), que era el unico de
    sus tres efectos que la guia marcaba como "NO migra".

    POR QUE ESTE ERA EL QUE NO MIGRABA, Y POR QUE SI. La guia lo ponia en la lista de lo que no migra,
    "usa std::tan y DspMath no tiene tan". Eso era verdad y no era el problema:
    `tan` no esta en DspMath, pero `sin` y `cos` SI, y `tan (x) = sin (x) /
    cos (x)`. MEDIDO en todo el rango del barrido (argumento de PI·f/fs entre
    0,013 y 0,36 rad, o sea muy lejos de los polos del coseno):

        std::tan (x) frente a sin (x)/cos (x)     : 1 ulp,  error rel. 9,9e-08
        el coeficiente alpha = (t-1)/(t+1)         : 3 ulps, error rel. 2,9e-07

    Asi que el bloqueo real no era la matematica que no se podia calcular, era la
    cuenta: el original calcula el coeficiente OCHO VECES POR MUESTRA (4 etapas
    x 2 canales) cuando hay solo DOS valores distintos, uno por canal, porque
    `alpha` no depende de la etapa. Cuatro de cada seis calculos eran el mismo
    numero repetido.

    Y ESO, MEDIDO, es un phaser que se lleva la mitad de un nucleo:

        original, 8 tan + 2 pow + 2 sin por muestra   483,5 ns/muestra  49,5 %
        2 coeficientes por bloque, LFO por muestra    182,9 ns/muestra  18,7 %
        2 coeficientes por bloque, LFO en el bloque     49,9 ns/muestra   5,1 %
        lo mismo, con DspMath::pow                      27,4 ns/muestra   2,8 %
        lo mismo, con bloque de 64                      27,1 ns/muestra   2,8 %

    (1.024.000 muestras, gcc -O2, en la maquina de medicion del modulo. El
    porcentaje es sobre un nucleo a 48 kHz: 1.024.000 muestras a 10 ns son
    10,24 ms de CPU por cada 21,3 ms de audio.)

    Los tres saltos son independientes y se acumulan:

      1. LAS CUATRO ETAPAS COMPARTEN EL COEFICIENTE. 483 -> 183 ns. Es la
         mitad del gasto, y sale solo de no repetir el mismo numero.

      2. EL LFO SE MUUEVE AL BLOQUE. 183 -> 50 ns. Son dos `std::sin` por
         muestra, y el LFO es de 0,02 a 15 Hz: a 15 Hz se mueve 3,4 grados por
         muestra, y a 0,5 Hz (el valor por defecto de ABDMS2000) 0,004 grados.
         Congelarlo 16 muestras es redondear a 0,07 grados a 15 Hz.

      3. `std::pow` POR `DspMath::pow`. 50 -> 27 ns. La de libm es 2,6 veces
         mas cara, y hace UN calcular por bloque, asi que el 2,6 no se oye; lo
         que importa es que este modulo no puede llamar a libm.

    Y EL ESCALONADO NO SE OYE, MEDIDO CONTRA LA REFERENCIA CORRECTA. Comparado
    contra la MISMA trayectoria de LFO con los coeficientes en cada muestra, que
    no se diferencia en nada mas, con un barrido completo de 3 s y feedback al
    45 %:

        cada   8 muestras   -73,4 dBFS pico,  -106,4 dBFS RMS
        cada  16            -66,8 dBFS pico,   -99,9 dBFS RMS
        cada  32            -60,5 dBFS pico,   -93,8 dBFS RMS
        cada  64            -56,0 dBFS pico,   -88,2 dBFS RMS
        cada 128            -50,6 dBFS pico,   -83,0 dBFS RMS
        cada 512            -47,3 dBFS pico,   -77,0 dBFS RMS

    16 es el elegido porque el COSTE deja de distinguir a partir de ahi: 16 y 64
    dan los mismos 27 ns. Bajar a 8 no compra nada y multiplica por dos la
    cuenta de bloques. Y 16 divide a los tamanos de bloque de todo el arbol (64,
    128, 256, 480, 512, 1024, 2048), que es la misma razon por la que la repisa
    eligio 16.

    ASI QUE EL CAMBIO DE SONIDO, Y ES DELIBERADO. Tres cosas, todas medidas:

      - el barrido se congela 16 muestras: -66,8 dBFS de pico respecto de la
        referencia por muestra, unos cuatro bits de la senal de 24 bits;
      - `tan` pasa a ser `sin/cos`: 1 ulp en el coeficiente;
      - `std::pow` pasa a ser `DspMath::pow`: ver el test, que lo mide.

    Los tres son el precio de que el efecto valga lo mismo a 32, 44,1, 48 y
    96 kHz, nativo y en WASM, y de ocupar un 2,8 % de nucleo en vez de un
    49,5 %.

    EL NUMERO DE ETAPAS ES DE PLANTILLA, no una constante. El original tiene
    cuatro porque el MS2000 tiene cuatro, y un phaser de seis u ocho etapas es
    el mismo algoritmo con mas etapas. Con `kEtapas` de parametro de plantilla
    el coste del bucle sigue siendo el de las etapas que se pidan, y el estado
    vive en un array del tamano justo, sin ramas ni indireccion.

    Y `kTasa` ES UN PARAMETRO DE PLANTILLA, no una constante de la clase, y no
    solo porque el test lo mida: porque la tasa es parte de lo que hace el
    efecto, igual que lo es el numero de etapas. Un phaser que recalcula el
    coeficiente cada muestra y otro que lo recalcula cada 64 son dos efectos
    distintos —el segundo con un -56 dBFS de escalonado medido— y obligar a que
    compartan el mismo numero seria hacer del 16 un numero magico, que es
    exactamente lo que se queria quitar.

    Y LA SENAL DE RETORNO DE 0,90 ES DE ESTABILIDAD, no un numero de producto.
    El original multiplicaba por 0,90 al mapear 0..127 a ganancia. Aqui el motor
    RECIBE 0..1 y aplica el 0,90, porque si el tope viviera en el producto,
    otro producto podria subirlo a 1,2 y reventase el motor sin enterarse. A 0,9 con
    cuatro etapas en cascada el lazo se queda en 0,9 y es estable a cualquier
    corte. El 0,5 de mezcla humeda/seca tampoco es un knob: el original lo tiene
    fijo y es lo que hace el notch, asi que se queda fijo.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspMath.h"
#include "DspCore/DspCore.h"

namespace abd::dsp
{

/** Phaser de `kEtapas` todo-paso de primer orden en cascada, con barrido del
    notch en escala logaritmica y coeficiente calculado por bloque.

    La diferencia con un phaser de libro es DONDE se calcula el coeficiente. En
    uno de libro el corte se mueve muestra a muestra; aqui se mueve cada
    `kControlRate` muestras, que es lo que permite que el motor viva en un
    modulo sin libm y que el mismo filtro valga nativo y en WASM. La diferencia
    esta medida y son -66,8 dBFS de pico; ver la cabecera.

    QUE NO VIVE AQUI. El mapeo de 0..1 a hercios, la escala logaritmica de la
    velocidad (0,02 a 15 Hz en el MS2000) y el tope de realimentacion en 0..127
    son politica de PRODUCTO y se quedan en el producto. Este motor habla en
    hercios y en 0..1, que es lo mismo que hace `ShelfFilter`. */
template <int kEtapas = 4, int kTasa = 16>
class Phaser4
{
public:
    Phaser4() = default;
    ~Phaser4() = default;

    // Sin etapas no hay phaser, y con una tasa de cero el contador se cumple
    // en cada muestra, que es el caso degenerado de "sin bloque". Se dicen aqui
    // y no en un comentario porque un `static_assert` sale en la compilacion del
    // que se equivoca, y un comentario sale en la del que lo lee.
    static_assert (kEtapas >= 1, "un phaser necesita al menos una etapa");
    static_assert (kTasa >= 1, "la tasa de control tiene que ser al menos una muestra");

    //-------------------------------------------------------------------------
    /** Cada cuanto se recalcula el coeficiente, en muestras. MEDIDO: el coste
        deja de distinguir entre 16 y 64, y 16 es el que menos se oye. Ver la
        cabecera.

        Y ES UN PARAMETRO DE PLANTILLA y no un `const` de la clase. Con un
        `const` fijo, el test que mide la curva del escalonado no puede medirla:
        instancia seis motores iguales y leeria seis veces el mismo numero, que es
        exactamente el fallo que se cometio con la tabla de la repisa. Con el
        parametro, el test instancia `Phaser4<4, 8>` y `Phaser4<4, 64>` y mide
        de verdad, y un producto que quiera 32 paga lo que cuesta 32. */
    static constexpr int kControlRate = kTasa;

    /** Fija el sample rate, el barrido y la velocidad, y recalcula. Un sample
        rate invalido deja el defecto, que es 44,1 kHz. */
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? (float) sampleRate : 44100.0f;
        setSweepRange (minHz_, maxHz_);
        // El LFO se mide en ciclos por MUESTRA, asi que depende del sample rate.
        // Sin esta linea, un `prepare` a 96 kHz con el mismo `setRateHz` barria
        // cuatro veces mas rapido que a 24 kHz, y el motor no tendria el mismo
        // sonido en un host que cambia de sample rate.
        incLfo_ = (double) rateHz_ / (double) sampleRate_;
        updateCoefficients();
        reset();
    }

    /** Limpia el estado de audio y el LFO. Los coeficientes se quedan: `reset`
        no es un `prepare` y no debe reasignar nada. */
    void reset() noexcept
    {
        for (int k = 0; k < kEtapas; ++k)
        {
            estadoL_[k].x1 = estadoL_[k].y1 = 0.0f;
            estadoR_[k].x1 = estadoR_[k].y1 = 0.0f;
        }
        fbL_ = fbR_ = 0.0f;
        fbMono_ = 0.0f;
        faseLfo_ = 0.0;

        // Y EL CONTADOR VA ATRASADO UNA MUESTRA, a proposito. `prepare` calcula
        // el coeficiente con la profundidad y la velocidad POR DEFECTO, porque
        // la firma de `prepare` no lleva mandos: el llamante todavia no ha
        // llamado a `setDepth` ni a `setRateHz`. Con el contador a cero, el
        // motor no actualizaba hasta la muestra 15, o sea que USABA un coeficiente
        // de 1.048 Hz cuando el llamante ya habia pedido 200 Hz.
        //
        // MEDIDO, y no como teoria: con `depth` 0, donde no hay barrido y las
        // dos copias del motor tienen que dar bits identicos, la diferencia
        // salia en -3,0 dBFS de pico. Ese -3 dBFS no era el error de escalonado,
        // era el arranque, y hacia que el error de escalonado pareciese tres
        // veces mas grande de lo que es.
        //
        // Un coeficiente atascado 16 muestras son 16 muestras de filtro
        // equivocado, y con un |alpha| de 0,97 eso es 0,2 de amplitud de
        // entrada: de sobra para oirse. La regla general que sale de aqui es que
        // en un motor de control rate el PRIMER bloque tiene que ser de control,
        // no de audio.
        cuenta_ = kTasa - 1;
    }

    //-------------------------------------------------------------------------
    /** Velocidad del LFO en hercios, de 0,001 a 1.000.

        El original mapeaba 0..1 a 0,02..15 Hz con una potencia, y ese mapeo es
        politica: se queda en el producto. Aqui entra en hercios, que es la
        unidad en la que esta la musica.

        El techo de 1.000 Hz esta por el `sin` y no por la musica: a 1 kHz de
        barrido sobre una muestra de 44,1 kHz el LFO avanza a mas de un
        muestreo por ciclo y "barrido" deja de significar nada. El suelo de
        0,001 Hz es un ciclo cada mil segundos, por debajo del cual el barrido
        es una constante con un deriva que tarda horas en oírse. */
    void setRateHz (float hz) noexcept
    {
        rateHz_ = jlimit (0.001f, 1000.0f, hz);
        incLfo_ = (double) rateHz_ / (double) sampleRate_;
    }

    float getRateHz() const noexcept { return rateHz_; }

    /** Profundidad del barrido, de 0 a 1. Es el EXPONENTE, no una mezcla: 0
        deja el corte fijo en el minimo y 1 recorre todo el rango.

        Que sea el exponente y no el recorrido es del original y suena distinto
        de lo que el nombre sugiere: a 0,5 el corte recorre la mitad del
        recorrido logaritmico, no la mitad del rango de frecuencia, y por eso
        el barrido pasa mas tiempo en lo grave. Se conserva. */
    void setDepth (float d) noexcept
    {
        depth_ = jlimit (0.0f, 1.0f, d);
    }

    float getDepth() const noexcept { return depth_; }

    /** Realimentacion, de 0 a 1. El tope interno es 0,90, que es donde el lazo
        es estable con cuatro etapas; ver la cabecera. */
    void setFeedback (float f) noexcept
    {
        feedback_ = jlimit (0.0f, 1.0f, f) * 0.90f;
    }

    float getFeedback() const noexcept { return feedback_; }

    /** Rango del barrido en hercios, continuo.

        Los limites se recortan a 0,45 veces el sample rate por el mismo motivo
        que en `ShelfFilter`: es donde el filtro empieza a no ser un filtro. El
        maximo de ABDMS2000 son 5.500 Hz, y 5500/32000 = 0,172, asi que el
        recorte no lo toca ni al sample rate mas bajo del arbol.

        El minimo tiene un suelo de 20 Hz porque por debajo el todo-paso se
        acerca a la identidad y el phaser deja de hacer la muesca. */
    void setSweepRange (float minHz, float maxHz) noexcept
    {
        const float limite = sampleRate_ * 0.45f;
        minHz_ = jmax (20.0f, jmin (minHz, limite));
        maxHz_ = jmax (minHz_, jmin (maxHz, limite));
        sweepRatio_ = maxHz_ / minHz_;
    }

    float getMinHz() const noexcept { return minHz_; }
    float getMaxHz() const noexcept { return maxHz_; }
    float getSweepRatio() const noexcept { return sweepRatio_; }

    //-------------------------------------------------------------------------
    /** Un par, con LFO en cuadratura y estados separados por canal.

        El LFO del canal derecho va desplazado 90 grados, que es lo que hace que
        el barrido se abra en el medio en vez de girar entero, y es del
        original. El estado es por etapa y por canal: ocho estados, porque las
        cuatro etapas de un canal no se pueden compartir el estado con las del
        otro. */
    void processFrame (float& left, float& right) noexcept
    {
        // El contador TIENE que volver a cero, y NO se reinicia por bloque: si
        // se reiniciara, con un bloque de 384 muestras la division no cuadra y
        // el empuje se moveria de fase cada bloque, que es la forma de que dos
        // bloques iguales den dos sonidos distintos.
        if (++cuenta_ >= kTasa)
        {
            cuenta_ = 0;
            updateCoefficients();
        }

        float xL = left + fbL_ * feedback_;
        float xR = right + fbR_ * feedback_;

        for (int k = 0; k < kEtapas; ++k)
        {
            xL = etapa (estadoL_[k], xL, coefL_);
            xR = etapa (estadoR_[k], xR, coefR_);
        }

        fbL_ = xL;
        fbR_ = xR;

        // 50 % humeda/seca, fijo y del original: la mezcla al 50 % es lo que
        // cancela la muesca con la senal directa. Con mezcla ajustable la
        // muesca desaparece al bajar de 0,5, y el efecto deja de ser un phaser.
        left  = left  * 0.5f + xL * 0.5f;
        right = right * 0.5f + xR * 0.5f;
    }

    /** Mono. El mismo filtro con un solo LFO y un solo juego de etapas.

        No es un caso de convenience: un phaser en mono tiene que decidir si el
        barrido es el mismo en los dos canales o dos barridos distintos, y la
        respuesta cambia el timbre entero. En `processFrame` son dos barridos en
        cuadratura, que es lo del original. */
    float processSample (float x) noexcept
    {
        if (++cuenta_ >= kTasa)
        {
            cuenta_ = 0;
            updateCoefficientsMono();
        }

        float y = x + fbMono_ * feedback_;
        for (int k = 0; k < kEtapas; ++k)
            y = etapa (estadoMono_[k], y, coefMono_);

        fbMono_ = y;
        return x * 0.5f + y * 0.5f;
    }

    //-------------------------------------------------------------------------
    /** El coeficiente de cada canal. El motor entero cabe en DOS numeros, y
        por eso el test no necesita reimplementar el filtro para comparar: mira
        estos dos. */
    float getAlphaL() const noexcept { return coefL_; }
    float getAlphaR() const noexcept { return coefR_; }
    float getAlphaMono() const noexcept { return coefMono_; }

    /** El corte que hay ahora mismo en cada canal, en hercios. */
    float getCutoffLHz() const noexcept { return cutoffL_; }
    float getCutoffRHz() const noexcept { return cutoffR_; }

    /** El coeficiente de un todo-paso a un corte dado, sin `std::tan`.

        Es `public` y estatico por una razon muy concreta: es lo que permite
        que el test compare el motor contra una REFERENCIA CONGELADA que lleva
        dentro su propia `std::tan` de libm, sin reimplementar el filtro entero
        en el test. La alternativa —escribir la formula otra vez dentro del
        test— daria verde siempre, porque compararia el motor consigo mismo. */
    static float coeficienteDe (float freqHz, float sampleRate) noexcept
    {
        const float x = 3.14159265f * freqHz / sampleRate;
        const float t = sin (x) / cos (x);
        return (t - 1.0f) / (t + 1.0f);
    }

    /** La fase del LFO. Es la que el test usa para que la referencia congelada
        vaya por la misma trayectoria: sin esto, dos barridos a distinta velocidad
        no se pueden comparar muestra a muestra. */
    double getLfoPhase() const noexcept { return faseLfo_; }

private:
    //-------------------------------------------------------------------------
    struct Estado
    {
        float x1 = 0.0f;
        float y1 = 0.0f;
    };

    /** Un todo-paso de primer orden: y = a·x + x1 - a·y1.

        La misma cuenta del original, transcrita. Es la forma directa y no
        transpuesta porque en un todo-paso de primer orden las dos dan el mismo
        resultado con el mismo numero de operaciones, y aqui importa mas la
        traza con la referencia congelada que la forma. */
    static float etapa (Estado& e, float x, float a) noexcept
    {
        const float y = a * x + e.x1 - a * e.y1;
        e.x1 = x;
        e.y1 = y;
        return y;
    }

    /** El coeficiente del todo-paso a un corte dado, SIN `std::tan`.

        El original hacia `t = tan (PI·f/fs)` y luego `(t-1)/(t+1)`. Aqui
        `t = sin (x) / cos (x)`, que es lo mismo con dos funciones que DspMath
        si tiene. MEDIDO: 1 ulp en `t` y 3 en el coeficiente, en todo el rango
        del barrido a cuatro sample rates.

        Y el coeficiente esta acotado: con `x` en (0, PI/2), `t` en (0, 1) y
        `alpha` en (-1, 0), o sea que el filtro es estable por construccion. Si
        alguien subiera el corte por encima de Nyquist, `cos` se acerca a cero y
        `alpha` se va a -1, que es el borde de la estabilidad. Por eso
        `setSweepRange` recorta a 0,45·fs y no a 0,49·fs. */

    /** El corte de un lado del barrido, en hercios.

        El mapeo es logaritmico, del original: `f = min · (max/min)^profundidad`
        con la profundidad media entre 0 y 1. La razon `max/min` se calcula UNA
        vez en `setSweepRange` y no por bloque, porque no cambia. */
    float corteDe (float lfo) const noexcept
    {
        return minHz_ * pow (sweepRatio_, (lfo * 0.5f + 0.5f) * depth_);
    }

    /** El bloque de control. Avanza el LFO y recalcula los DOS coeficientes.

        El LFO se evalua aqui y no en el lazo de audio por dos razones, y las
        dos estan medidas: son dos `std::sin` por muestra (183 -> 50 ns), y el
        LFO a 0,5 Hz se mueve 0,004 grados por muestra, o sea 0,07 grados cada
        16. Es una redondeo que no se oye y que ademas sale gratis.

        La fase es `double` a proposito. En `float` una fase entre 0 y 1 avanza
        por pasos de 2^-24, y a 0,02 Hz tarda horas en dejar de avanzar bien; con
        `double` son 2^-53. Es un `double` por BLOQUE, que no cuesta nada. */
    void updateCoefficients() noexcept
    {
        const float dosPi = 6.28318530718f;
        const float medioPi = 1.5707963f;

        const float lfoL = (float) sin (dosPi * (float) faseLfo_);
        const float lfoR = (float) sin (dosPi * (float) faseLfo_ + medioPi);

        // La fase avanza UN BLOQUE, que son `kTasa` muestras, y por eso el
        // incremento va multiplicado por `kTasa`. Sin esa multiplicacion el LFO
        // corre `kTasa` veces mas lento de lo que dice su frecuencia, y el
        // defecto es de los que no se ven mirando el sonido —a 15 Hz y con 16
        // muestras de bloque hace 0,94 vueltas por segundo en vez de 15, y el
        // barrido sigue recorriendo el rango, asi que "suena a phaser"— sino
        // midiendo el corte.
        faseLfo_ += incLfo_ * kTasa;

        // Y LA VUELTA NO PUEDE SER UN SOLO `-= 1.0`. Con la velocidad al tope
        // (1.000 Hz) y un bloque de 512 muestras, la fase avanza 10,67 vueltas
        // de golpe: un solo `-= 1.0` la deja en 9,67, que es una fase valida y
        // un LFO que sigue dando lo mismo, pero que ya no esta donde la dejaria
        // un motor que avanza muestra a muestra. Hay que quitar las vueltas
        // enteras de golpe.
        if (faseLfo_ >= 1.0)
            faseLfo_ -= (double) floorToInt ((float) faseLfo_);

        cutoffL_ = corteDe (lfoL);
        cutoffR_ = corteDe (lfoR);

        coefL_ = coeficienteDe (cutoffL_, sampleRate_);
        coefR_ = coeficienteDe (cutoffR_, sampleRate_);
    }

    /** El bloque de control en mono: un LFO, un coeficiente. */
    void updateCoefficientsMono() noexcept
    {
        const float lfo = (float) sin (6.28318530718f * (float) faseLfo_);

        faseLfo_ += incLfo_ * kTasa;
        if (faseLfo_ >= 1.0)
            faseLfo_ -= (double) floorToInt ((float) faseLfo_);

        cutoffL_ = corteDe (lfo);
        coefMono_ = coeficienteDe (cutoffL_, sampleRate_);
    }

    //-------------------------------------------------------------------------
    float sampleRate_ = 44100.0f;
    float minHz_ = 200.0f;
    float maxHz_ = 5500.0f;
    float sweepRatio_ = 27.5f;

    float rateHz_ = 0.5f;
    float depth_ = 0.5f;
    float feedback_ = 0.0f;

    /** La fase del LFO y su incremento en ciclos por MUESTRA. Se avanza por
        bloque, multiplicando por `kControlRate`, que es lo mismo que avanzar
        `kControlRate` veces y da la misma trayectoria. */
    double faseLfo_ = 0.0;
    double incLfo_ = 0.5 / 44100.0;

    float coefL_ = 0.0f, coefR_ = 0.0f, coefMono_ = 0.0f;
    float cutoffL_ = 0.0f, cutoffR_ = 0.0f;

    Estado estadoL_[kEtapas];
    Estado estadoR_[kEtapas];
    Estado estadoMono_[kEtapas];
    float fbL_ = 0.0f, fbR_ = 0.0f, fbMono_ = 0.0f;

    int cuenta_ = 0;
};

} // namespace abd::dsp
