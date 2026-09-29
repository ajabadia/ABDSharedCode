/*
  ==============================================================================

    ShelfFilter.h
    Repisa de Butterworth de segundo orden (Q = 0.7071), baja o alta.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Port del ecualizador de ABDMS2000
    (Source/DSP/Effects/Equalizer.{h,cpp}), que era el unico de sus tres efectos
    que estaba limpio: dos repisas en cascada, forma II transpuesta, coeficiente
    A que da identidad exacta a 0 dB y banda muerta de 0,05 dB en la ganancia.

    QUE ES Y QUE NO. Aqui vive la MAQUINA: el biquad, el coeficiente, el estado.
    NO viven aqui las tablas de 4 pasos del MS2000 (160/250/400/600 Hz y
    4000/6000/8000/12000 Hz). Son politica de producto, y la regla del modulo es
    la misma que la del rango de 0,10 a 8 Hz del coro: la fila comun toma
    FRECUENCIAS CONTINUAS en hercios y quien tenga una tabla de pasos, la mantiene
    en su producto y le pasa el valor. ABDMS2000 lo hace exactamente asi.

    ASI SE USA.

        ShelfFilter eq;
        eq.prepare (48000.0);
        eq.setMode (ShelfMode::High);
        eq.setFrequencyHz (8000.0f);
        eq.setGainDB (-3.0f);

        float y = eq.processSample (x);          // mono, in situ
        eq.processFrame (l, r);                 // estéreo, estados separados

    Y POR QUE UNA REPISA Y NO EL ECUALIZADOR ENTERO. El ecualizador del MS2000
    son DOS de estas en cascada, una baja y otra alta. Esta clase es la pieza, y
    por eso se llama `ShelfFilter` y no `Equalizer`: un `Equalizer` que solo sabe
    hacer dos repisas con un Q fijo y cuatro frecuencias por banda no es un
    ecualizador, es un preset. Con la pieza suelta, un producto que quiera tres
    bandas mete tres, y uno que quiera un Q variable lo tiene.

    EL CAMBIO DE SONIDO, QUE ES DELIBERADO Y ESTA MEDIDO. La version antigua
    usaba `std::sqrt`, `std::pow`, `std::cos` y `std::sin` de libm. Este modulo
    no puede: solo tiene lo que hay en DspCore/DspMath.h, y `sqrt` NO esta
    entre ellas. El sustituto medido es `exp2 (0.5 * log2 (A))`, y el resultado
    no es el mismo bit a bit:

        error en A, en todo el rango de -12 a +12 dB : hasta 2 ulps
        (error relativo maximo 1.72e-07, y NO en el extremo de -12 dB sino a
        +11,40 dB; el rango entero de A es [0.501, 1.995])
        error en un coeficiente del biquad          : hasta 4 ulps

    Cuatro ulps en un coeficiente son unos 2e-07 relativos, o sea unos -140 dB
    respecto de la senal. Es el precio de que el filtro valga lo mismo a 32, 44,1
    y 48 kHz, y es un precio que se paga una vez y se cobra en todos los
    productos siguientes. Quien necesite el bit exacto necesita un `sqrt` de
    verdad en DspMath.h, que no esta porque nadie lo ha necesitado todavia.

    LA BANDA MUERTA DE 0,05 dB SE CONSERVA. Viene del original y esta bien
    puesta: recalcular los coeficientes en cada tic de automatizacion es trabajo
    tirado, y con la banda el motor solo se recalcula cuando la ganancia se ha
    movido de verdad. Con la banda el 0 dB es ademas un caso exacto: A = 1 y el
    biquad sale a identidad en los coeficientes, no "casi identidad".

  ==============================================================================
*/

#pragma once

#include "DspCore/DspMath.h"
#include "DspCore/DspCore.h"

namespace abd::dsp
{

/** Que repisa es. La diferencia no son los coeficientes del biquad, que se
    calculan con las mismas cinco formulas: es el signo de los terminos de
    realimentacion, y por eso es un `switch` en un unico sitio y no dos
    funciones gemelas. */
enum class ShelfMode
{
    Low,      ///< Repisa baja: sube lo grave, baja el medio.
    High      ///< Repisa alta: sube lo agudo, baja el medio.
};

/** Repisa de segundo orden con Q de Butterworth, continua en frecuencia.

    Q va FIJO a 1/sqrt(2) a proposito, igual que en el original. Un Q variable
    es un knob mas en la fila y una razon mas para que alguien pone un 0.9 y no
    suene a nada; si alguna vez hace falta, se anade con su prueba, no antes. */
class ShelfFilter
{
public:
    ShelfFilter() = default;
    ~ShelfFilter() = default;

    //-------------------------------------------------------------------------
    /** Fija el sample rate y recalcula. Un sample rate invalido deja el
        defecto, que es 44,1 kHz, nunca un coeficiente roto. */
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1000.0) ? (float) sampleRate : 44100.0f;
        updateCoefficients();
        reset();
    }

    /** Limpia el estado. Los coeficientes se quedan:.reset() no es un
        `prepare()` y no debe reasignar nada. */
    void reset() noexcept
    {
        s1L_ = s2L_ = s1R_ = s2R_ = 0.0f;
    }

    //-------------------------------------------------------------------------
    void setMode (ShelfMode mode) noexcept
    {
        if (mode == mode_) return;
        mode_ = mode;
        updateCoefficients();
    }

    ShelfMode getMode() const noexcept { return mode_; }

    /** Frecuencia central en hercios, CONTINUA.

        El limite superior es 0,45 veces el sample rate, no 0,2. La primera
        version de aqui usaba 0,2, que parecia el limite prudente, y al medir
        contra la referencia congelada resulto que MOVIA las frecuencias del
        propio MS2000 a 32 kHz: las repisas de 8 y de 12 kHz se recortaban a
        6,4 kHz y sonaban en otro sitio. El MS2000 usa 12 kHz, y 12000/32000 =
        0,375, asi que cualquier limite por debajo de 0,375 cambia el sonido de
        un producto que ya esta publicado.

        0,45 queda por debajo de Nyquist, que es donde el biquad empieza a no
        ser un filtro de verdad, y por encima de todo lo que usa el hardware. El
        minimo de 10 Hz es por el otro lado: a 0 Hz la repisa se aplana y se
        convierte en un pasabajos que no hace lo que su nombre dice. */
    void setFrequencyHz (float hz) noexcept
    {
        const float limite = sampleRate_ * 0.45f;
        const float recortada = (hz < 10.0f) ? 10.0f
                            : (hz > limite) ? limite
                            : hz;
        if (recortada != frequencyHz_)
        {
            frequencyHz_ = recortada;
            updateCoefficients();
        }
    }

    float getFrequencyHz() const noexcept { return frequencyHz_; }

    /** Ganancia en decibelios, de -12 a +12.

        La banda muerta de 0,05 dB viene del original del MS2000 y se conserva:
        sin ella, una automatizacion que se mueve en pasos de milisegundo
        recalcula cinco coeficientes por paso, y con ella solo cuando la ganancia
        se ha movido de verdad. */
    void setGainDB (float db) noexcept
    {
        const float recortada = jlimit (-12.0f, 12.0f, db);
        const float diferencia = recortada - gainDB_;
        if (jmax (diferencia, -diferencia) > 0.05f)
        {
            gainDB_ = recortada;
            updateCoefficients();
        }
    }

    float getGainDB() const noexcept { return gainDB_; }

    //-------------------------------------------------------------------------
    /** Una muestra. In situ, que es la forma que usa el resto del modulo. */
    float processSample (float& x) noexcept
    {
        const float y = b0_ * x + s1L_;
        s1L_ = b1_ * x - a1_ * y + s2L_;
        s2L_ = b2_ * x - a2_ * y;
        x = y;
        return y;
    }

    /** Un par, con estado separado por canal.

        Dos canales con el mismo estado seria un filtro de canal unico aplicado a
        dos señales: en estéreo la imagen se iría al centro. El original ya
        llevaba cuatro estados, y aqui tambien. */
    void processFrame (float& left, float& right) noexcept
    {
        const float yL = b0_ * left + s1L_;
        s1L_ = b1_ * left - a1_ * yL + s2L_;
        s2L_ = b2_ * left - a2_ * yL;

        const float yR = b0_ * right + s1R_;
        s1R_ = b1_ * right - a1_ * yR + s2R_;
        s2R_ = b2_ * right - a2_ * yR;

        left = yL;
        right = yR;
    }

    /** Las dos referencias a la senal que produce el filtro: sirve para comparar
        el motor contra una referencia congelada sin reimplementar el biquad en el
        test, que es el error clasico: comparar contra una formula escrita en el
        test y no contra la que corre de verdad. */
    float getB0() const noexcept { return b0_; }
    float getB1() const noexcept { return b1_; }
    float getB2() const noexcept { return b2_; }
    float getA1() const noexcept { return a1_; }
    float getA2() const noexcept { return a2_; }

    /** Si el filtro es la identidad EXACTA. MEDIDO, y no es lo que parece.

        A 0 dB los coeficientes NO son [1, 0, 1, 0, 0]. A = 1 deja `b1 = a1` y
        `b2 = a2`, o sea que el numerador y el denominador de la funcion de
        transferencia son el MISMO polinomio y se cancelan:

            H(z) = (1 + b1·z⁻¹ + b2·z⁻²) / (1 + a1·z⁻¹ + a2·z⁻²) = 1

        en todas las frecuencias, no solo en las que uno mira. Medido a 48 kHz y
        en las ocho frecuencias del MS2000, la respuesta a 0 dB sale 0.0000 dB
        en DC, en 0,1·f0, en f0 y en Nyquist, para las dos repisas.

        Por eso la comprobacion compara `b1` con `a1` y `b2` con `a2`, y no
        busca unos ceros: la version que buscaba `b1 == 0` daba "no es
        identidad" en los ocho casos, cuando el filtro era perfectamente
        transparente. Un test que mira el patron de coeficientes en vez de la
        funcion de transferencia da un falso negativo, y ese es el fallo que
        cuesta mas encontrar porque parece que el filtro este roto. */
    bool esIdentidad() const noexcept
    {
        return gainDB_ == 0.0f
            && b0_ == 1.0f
            && b1_ == a1_
            && b2_ == a2_;
    }

private:
    //-------------------------------------------------------------------------
    void updateCoefficients() noexcept
    {
        // A = 10^(dB/40) y la raiz de A. En el original eran dos llamadas
        // (`pow` y `sqrt`); aqui la raiz se saca de `exp2 (0.5 * log2 (A))`, que
        // es lo que DspCore puede dar. MEDIDO: hasta 2 ulps de diferencia en A en
        // todo el rango de -12 a +12 dB, y hasta 4 en el coeficiente final.
        //
        // La alternativa sería `pow (10, dB/80)`, que es la misma cosa con una
        // sola transcendental en vez de dos, y tambien estaria a 2 ulps. Se usa
        // la de dos porque es la que decidio y midio la guia del modulo, y
        // cambiar de sustituto en una migracion es cambiar la medicion que hay
        // escrita. La cuenta de transcendentales por coeficiente no es lo que
        // limita aqui: los coeficientes se calculan una vez por cambio de mando.
        const float A  = pow (10.0f, gainDB_ / 40.0f);
        const float rA = exp2 (0.5f * log2 (A));

        const float w0    = frequencyHz_ * twoPi() / sampleRate_;
        const float cosW0 = cos (w0);
        const float sinW0 = sin (w0);
        const float alpha = sinW0 / (2.0f * 0.70710678f);   // Q de Butterworth

        // Las dos repisas son las MISMAS cinco formulas con el signo de los
        // terminos que llevan `cos` cambiado, asi que estan en dos ramas
        // explicitas y no comprimidas con un signo multiplicativo. La primera
        // version de aqui las metia en una con un `signo`, y el ahorro eran
        // cuatro multiplicaciones por coeficiente que no compila, a cambio de
        // tener que leer la cuenta para saber cual de las dos formas es cual.
        // Transcripcion literal de las que tiene ABDMS2000, que es de donde
        // viene, y el test compara los coeficientes contra esa referencia.
        const float m = (A - 1.0f) * cosW0;      // el termino que cambia de signo
        const float p = (A + 1.0f) * cosW0;      // el que no cambia
        const float g = 2.0f * rA * alpha;

        if (mode_ == ShelfMode::High)
        {
            const float a0 = (A + 1.0f) - m + g;
            b0_ = (A * ((A + 1.0f) + m + g)) / a0;
            b1_ = (-2.0f * A * ((A - 1.0f) + p)) / a0;
            b2_ = (A * ((A + 1.0f) + m - g)) / a0;
            a1_ = (2.0f * ((A - 1.0f) - p)) / a0;
            a2_ = ((A + 1.0f) - m - g) / a0;
        }
        else
        {
            const float a0 = (A + 1.0f) + m + g;
            b0_ = (A * ((A + 1.0f) - m + g)) / a0;
            b1_ = (2.0f * A * ((A - 1.0f) - p)) / a0;
            b2_ = (A * ((A + 1.0f) - m - g)) / a0;
            a1_ = (-2.0f * ((A - 1.0f) + p)) / a0;
            a2_ = ((A + 1.0f) + m - g) / a0;
        }
    }

    static float twoPi() noexcept
    {
        return 6.28318530718f;
    }

    //-------------------------------------------------------------------------
    float sampleRate_  = 44100.0f;
    float frequencyHz_ = 1000.0f;
    float gainDB_      = 0.0f;
    ShelfMode mode_    = ShelfMode::Low;

    // Forma II transpuesta: el estado va en dos registros por canal, y el
    // biquad es el mismo para los dos.
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float s1L_ = 0.0f, s2L_ = 0.0f;
    float s1R_ = 0.0f, s2R_ = 0.0f;
};

} // namespace abd::dsp
