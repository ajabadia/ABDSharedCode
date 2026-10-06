/*
  ==============================================================================

    JunoBBD.h
    Coro con Bucket Brigade Device: retardo con reloj propio, ruido de
    transferencia de carga y mezclador con ganancias asimetricas.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Port del coro de ABDJUNiO601
    (Source/Synth/ChorusBBD.{h,cpp} + Source/Synth/BBDFilter.h), que emula el
    MN3009. Los numeros de cada modelo son datos, en profiles/JunoBbdProfile.h.

    QUE ES Y QUE NO, y por que esto no es "un chorus". Un chorus digital es un
    LFO sobre una linea de retardo. Este es un retardo CUYO RELOJ TIENE TOLERANCIA
    (las dos lineas van a frecuencias de reloj distintas, +/-0.015), que pierde
    eficiencia al transferir carga (y con ella ganancia, en funcion de la
    frecuencia de reloj), y que se mezcla con ganancias asimetricas por canal.
    Eso es lo que hacia que la identidad del MN3009 estuviera PEGADA al
    algoritmo, en la misma clase, y por eso no se podia compartir: quitar la
    identidad obligaba a heredar de la clase. Aqui la identidad son DATOS
    (el perfil) y la degradacion es una ETAPA.

    LA INYECCION DE POLITICA, en las dos dosis de `EffectPolicy.h`:

        dsp::JunoBBD<dsp::JunoBbdJ106Profile, dsp::BbdNoiseStage> chorus;

      - EL PERFIL dice que numeros son los de esta maquina (retardos, barridos,
        frecuencias de reloj, tolerancias, ganancias del mezclador,shelf del
        siseo...). Cero indireccion: son `constexpr`.
      - LA ETAPA mete la degradacion que ensucia el BBD: fuga, siseo rosa, los
        dos clics por canal y el anillo que resuena cuando uno disparan. Es
        estado, asi que es un objeto, y se inyecta como parametro de plantilla:
        la llamada se resuelve en compilacion y no hay vtable en el lazo de audio.

    Y con `NullStage` el mismo motor es un coro digital limpio, sin siseo, sin
    clics y sin fuga. Ese es el otro extremo del patron y no es un caso
    trivial: es lo que hace que el motor sirva a dos politicas.

    LO QUE NO VIVE AQUI (por la regla del modulo, se queda en el consumidor):
    el mapeo de los mandos del panel a estos valores, el suavizado, el recorte
    y la mezcla wet/dry del SLOT. Aqui hay un mezclador porque el IC6 del Juno es
    parte de la maquina (ganancias asimetricas documentadas), no del slot.

    LAS DOS TRANSCENDENTALES QUE ESTE MOTOR NECESITA Y DspMath NO TRAE. El
    original usa `std::tan` y `std::exp`, y este modulo es libm-free. Se
    calculan con lo que si hay, y el error se ha MEDIDO en los rangos que usa
    este motor (ver `tanDet` y `expDet`):

        tan(x) = sin(x) / cos(x)   peor error RELATIVO 6.6e-07 en (0, 0.45*pi]
        exp(y) = exp2(y * log2(e))  peor error RELATIVO 6.1e-07 en [-8, 0]

    En los DOS coeficientes que el filtro calcula de verdad (fc 8000 y 20000 Hz
    contra 22050/44100/48000/96000 Hz) el error relativo de `tan` esta entre
    3e-08 y 2.8e-07: por debajo del ruido de un float, y en cualquier caso
    identico en el motor y en la referencia congelada, que es lo que sostiene la
    paridad a 0 ulps. NO se anade `tan` ni `exp` a DspCore/DspMath.h a proposito:
    son identidades exactas, no aproximaciones nuevas que mantener.

    LA PARIDAD. Este motor no promete 0 ulps con nadie por su cuenta: no tiene el
    original a mano. La comparacion vive en el CONSUMIDOR
    (ABDJUNiO601/Tests/ChorusBBDParityTest.cpp), contra una REFERENCIA CONGELADA
    que es el coro original con las MISMAS transcendentales deterministas. Igual
    que hace NEURONiK en ABDNeural/Tests/DspEffectsParityTest.cpp, y por el
    mismo motivo: sustituir la libm de la plataforma por una implementacion
    determinista es un cambio de sonido DELIBERADO (el motor es pre-1.0) y es lo
    que cierra la paridad nativa <-> WASM. Ese test ya no prueba ESE cambio
    (seria circular): prueba que la extraccion fue fiel, aritmetica incluida.
    Y esa aritmetica incluye cosas que no son "el algoritmo", sino los numeros
    que el original tiene por costumbre. Dos que costaron una sesion de medir:
    el 2*pi del LFO es el de 32 bits porque el original multiplica un `kPi`
    float por dos (ver `kTwoPiDouble`), y la calibracion se lee DENTRO de
    `prepare()`, no despues (ver `prepare` y `setReconstructionCutoff`). Un
    motor extraido que "mejora" la aritmetica del original no es una extraccion.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "DspEffects/EffectPolicy.h"
#include "DspEffects/profiles/JunoBbdProfile.h"

#include <cstdint>

namespace abd::dsp
{

//==============================================================================
/** Los modos del boton del Juno. */
enum class JunoBbdMode
{
    Off,
    ChorusI,
    ChorusII,
    ChorusBoth
};

/** Cuantos pasos tiene el BBD. Es el reloj, no una constante de DSP. */
static constexpr int kJunoBbdStages = 256;

/**
    2*pi en DOBLE, y no el `kPi` del modulo (que es float) prolongado.

    El 2*pi del LFO NO es el 2*pi exacto, y no es un descuido: se midio. El
    original calcula el incremento de fase como
    `kPi * 2.0 * rate / sr`, y `kPi` es un `float`. O sea que su 2*pi es el de 32
    bits (6.2831854820251465) ampliado a doble, NO el exacto (6.2831853071795862).
    La diferencia es de 2.8e-08 relatives, y con ella el LFO se desincroniza de la
    referencia poco a poco hasta que el retardo de la linea L se va 1 ulp y la
    salida se separa 2e-05: el fallo aparecia en la muestra 85 de los tres modos
    y en NINGUN otro sitio.

    La version anterior de esta constante era el 2*pi exacto en doble, con un
    comentario que decia que el original usaba `juce::MathConstants<double>
    ::twoPi`. Eso no era una medicion, era una suposicion, y era falsa: aqui se
    asumia precision que el original no tiene. Un motor extraido que "mejora" la
    aritmetica del original no es una extraccion, es una reescritura, y por eso
    la constante sale de `kPi` (float) y no de un literal de doble precision.
*/
static constexpr double kTwoPiDouble = 2.0 * static_cast<double>(dspmath_detail::kPi);

//==============================================================================
namespace junobbd_detail
{

/** log2(e), para llevar `exp` a `exp2`. */
constexpr float kLog2E = 1.4426950408889634f;

/** `tan` sin libm. Medido: error relativo <= 6.6e-07 en (0, 0.45*pi]. */
inline float tanDet(float x) noexcept
{
    return sin(x) / cos(x);
}

/** `exp` sin libm. Medido: error relativo <= 6.1e-07 en [-8, 0]. */
inline float expDet(float y) noexcept
{
    return exp2(y * kLog2E);
}

/**
    Un filtro de reconstruccion del BBD: SVF de 2º orden en cascada con un
    polo de inclinacion de agudas. TPT (topology-preserving transform), que es
    lo que lo hace estable a cualquier frecuencia y sample rate sin tocar los
    coeficientes.
*/
class ReconstructionFilter
{
public:
    void prepare(double sampleRate, float biquadFc, float biquadQ, float poleFc) noexcept
    {
        sampleRate_ = static_cast<float>(sampleRate);
        biquadQ_    = biquadQ;
        poleFc_     = poleFc;

        const float fc = biquadFc < sampleRate_ * 0.45f ? biquadFc : sampleRate_ * 0.45f;
        gBiquad_       = tanDet(dspmath_detail::kPi * fc / sampleRate_);

        a1_ = 1.0f / (1.0f + gBiquad_ / biquadQ_ + gBiquad_ * gBiquad_);

        const float fcP = poleFc < sampleRate_ * 0.45f ? poleFc : sampleRate_ * 0.45f;
        gPole_          = tanDet(dspmath_detail::kPi * fcP / sampleRate_);

        reset();
    }

    void reset() noexcept { ic1_ = ic2_ = pole_ = 0.0f; }

    float processSample(float input) noexcept
    {
        // SVF de 2º orden, pasabajos.
        const float v3 = input - ic2_;
        const float v1 = a1_ * ic1_ + a1_ * gBiquad_ * v3;
        const float v2 = ic2_ + gBiquad_ * v1;
        ic1_           = 2.0f * v1 - ic1_;
        ic2_           = 2.0f * v2 - ic2_;

        // Polo de inclinacion de agudas.
        const float v  = (v2 - pole_) * gPole_ / (1.0f + gPole_);
        const float lp = pole_ + v;
        pole_          = lp + v;

        return lp;
    }

private:
    float sampleRate_ = 44100.0f;
    float biquadQ_    = 0.7071f;
    float poleFc_     = 20000.0f;
    float gBiquad_    = 0.0f;
    float a1_         = 0.0f;
    float gPole_      = 0.0f;
    float ic1_ = 0.0f, ic2_ = 0.0f, pole_ = 0.0f;
};

/**
    La linea de retardo del BBD: buffer circular de potencia de dos (por eso la
    mascara en vez de un modulo), interpolacion de Hermite para el reloj no
    entero, y la saturacion que le da el "calor".

    La saturacion se APLICA al escribir, no al leer: en el BBD la perdida se
    produce cuando la carga se deposita, no cuando sale. Por eso el drive se
    suaviza muestra a muestra, que es como llega al chip de verdad.
*/
class BbdLine
{
public:
    void prepare(double sampleRate, float minSeconds, float fc, float q, float poleFc) noexcept
    {
        int minLen = static_cast<int>(sampleRate * minSeconds) + 4;
        int len    = 1;
        while (len < minLen) len <<= 1;

        buffer_.setSize(1, len, false, false, true);
        buffer_.clear();
        mask_ = len - 1;

        pre_.prepare(sampleRate, fc, q, poleFc);
        post_.prepare(sampleRate, fc, q, poleFc);

        sampleRate_ = static_cast<float>(sampleRate);
    }

    void reset() noexcept
    {
        buffer_.clear();
        writePos_ = 0;
        pre_.reset();
        post_.reset();

        // OJO, aqui NO se toca `satDriveSmooth_`, y es a proposito. El original
        // tampoco lo toca en su `Clear()`: el drive suavizado sobrevive a un
        // vaciado. Ponerlo a cero (o a `satDrive_`) en el `reset()` parece lo
        // logico y cambia la primera muestra: el drive suavizado arranca en 0.12
        // y no en el valor que se le asigne despues. Con la saturacion
        // desactivandose cerca de 0.01, esa primera muestra se oye. Se conserva
        // el comportamiento del original y se escribe para que quien lo
        // encuentre sepa que es conscious, no un olvido.
    }

    /** Drive de saturacion. Lo fija el motor desde el perfil. */
    void setSatDrive(float drive) noexcept { satDrive_ = drive; }

    /** Suavizado del drive, por muestra. */
    void setSatSlew(float slew) noexcept { satSlew_ = slew; }

    float processSample(float input, float delaySamples, float injectedNoise) noexcept
    {
        const float withNoise = input + injectedNoise;
        const float filtered  = pre_.processSample(withNoise);

        satDriveSmooth_ += (satDrive_ - satDriveSmooth_) * satSlew_;
        const float sd  = satDriveSmooth_;
        const float sat = sd > 0.01f ? tanh(filtered * sd) / sd : filtered;

        const int w                   = writePos_ & mask_;
        buffer_.getWritePointer(0)[w] = sat;

        const float wet = readHermite(delaySamples);

        writePos_ = (writePos_ + 1) & mask_;

        return post_.processSample(wet);
    }

private:
    static float hermite(float frac, float y0, float y1, float y2, float y3) noexcept
    {
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    float readHermite(float delaySamples) const noexcept
    {
        const float* b = buffer_.getReadPointer(0);

        float rPos = static_cast<float>(writePos_) - delaySamples;
        if (rPos < 0.0f) rPos += static_cast<float>(mask_ + 1);

        const int i1     = static_cast<int>(rPos);
        const float frac = rPos - static_cast<float>(i1);

        return hermite(frac,
                       b[(i1 - 1) & mask_],
                       b[i1 & mask_],
                       b[(i1 + 1) & mask_],
                       b[(i1 + 2) & mask_]);
    }

    AudioBuffer<float> buffer_;
    int mask_         = 0;
    int writePos_     = 0;
    float sampleRate_ = 44100.0f;

    // Los dos arrancan en 0.12, como los miembros del original. El `0.1` del
    // perfil es la BASE antes del boost de saturacion (el original calcula
    // `0.1f * calSatBoost`), y son cosas distintas: confundirlas cambia la
    // primera muestra.
    float satDrive_       = 0.12f;
    float satDriveSmooth_ = 0.12f;
    float satSlew_        = 0.001f;
    ReconstructionFilter pre_, post_;
};

/**
    El anillo resonante que despierta un clic. Es un resonator de dos polos, y va
    en el MOTOR, no en la etapa, por un motivo concreto: se suma a la salida de
    la linea DESPUES del filtro de reconstruccion, y lo que lo excita es el
    clic, que genera la etapa. Si el anillo viviera en la etapa, esta tendria que
    devolver dos cosas por muestra y el motor no tendria forma de colocar el
    anillo en su sitio sin depender de un orden de llamadas que nadie firma.
*/
class ClickRing
{
public:
    void prepare(double sampleRate, float resHz, float q) noexcept
    {
        freqCoeff_ = 2.0f * sin(dspmath_detail::kPi * resHz / static_cast<float>(sampleRate));
        damp_      = 1.0f / q;
        reset();
    }

    void reset() noexcept
    {
        low_  = 0.0f;
        band_ = 0.0f;
    }

    float processSample(float input) noexcept
    {
        low_ += freqCoeff_ * band_;
        const float high = input - low_ - damp_ * band_;
        band_ += freqCoeff_ * high;
        return low_;
    }

private:
    float freqCoeff_ = 0.0f;
    float damp_      = 0.0f;
    float low_ = 0.0f, band_ = 0.0f;
};

/**
    El zumbido de la red electrica que entra por el BBD. Onda completa de red
    (2 x 60 Hz) con tres armonicos, que es como suena de verdad un trafo bajo
    carga: el fundamental y los primeros armonicos, no una senoidal.

    Va en el MOTOR y no en la etapa a proposito: es UN solido por muestra
    compartido por los dos canales, no dos generadores de ruido. Meterlo en la
    etapa obligaria a llamarla una vez por muestra para el zumbido y otra por
    canal para el resto, y a depender de un orden de llamadas que nadie firma.
*/
class MainsRipple
{
public:
    void prepare(double sampleRate, float mainsHz, float a1, float a2, float a3) noexcept
    {
        // Onda completa: 2 x la frecuencia de red. No es un error, es lo que
        // rectifica el puente del medidor de corriente del chip.
        inc_ = 2.0f * mainsHz / static_cast<float>(sampleRate);
        a1_  = a1;
        a2_  = a2;
        a3_  = a3;
    }

    void reset() noexcept { phase_ = 0.0f; }

    float processSample() noexcept
    {
        phase_ += inc_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        const float tp = 2.0f * dspmath_detail::kPi * phase_;
        return a1_ * sin(tp) + a2_ * sin(2.0f * tp) + a3_ * sin(3.0f * tp);
    }

private:
    float inc_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float phase_ = 0.0f;
};

} // namespace junobbd_detail

//==============================================================================
/**
    Lo que devuelve una etapa de caracter del BBD en una muestra y un canal.

    DOS VALORES, y no uno, por el sitio donde se suman: `injected` entra en la
    linea ANTES del filtro de reconstruccion (es ruido de la senal, no de la
    salida), y `click` sale DESPUES, porque excita el anillo resonante que el
    motor suma al final. Aplanarlos en un solo float obligaria a mover el anillo
    dentro de la linea y cambiaria el sonido.
*/
struct BbdStageOutput
{
    float injected = 0.0f;
    float click    = 0.0f;
};

//==============================================================================
/**
    Etapa de caracter NULA para el BBD: el coro digital limpio, sin siseo de
    linea, sin fuga, sin clics y sin anillo. La usan el motor con
    `JunoBbdJ60Profile, BbdNullStage` / `JunoBbdJ106Profile, BbdNullStage` para
    escuchar la maquina sin su degradacion, que es como se separa "lo que hace
    el algoritmo" de "lo que hace el chip".

    Encaja en el contrato de `EffectPolicy.h` por el motivo general: devuelve
    float y acepta el par con valores por defecto. Aqui la primera dosis
    (`drive`) es 0 porque una etapa ADITIVA no colorea una senal: no hay senal
    que colorear, solo algo que sumar. Ver la nota del motor.
*/
class BbdNullStage
{
public:
    float processSample(float, float = 0.0f, float = 0.0f) noexcept { return 0.0f; }

    BbdStageOutput processLeft(float, float, float, float, float) noexcept { return {}; }
    BbdStageOutput processRight(float, float, float, float, float) noexcept { return {}; }

    void prepareSampleRate(double sr) noexcept { sampleRate_ = sr; }
    double getSampleRate() const noexcept { return sampleRate_; }
    void reset() noexcept {}
    void suppressClicks() noexcept {}
    void setHissColour(float) noexcept {}
    void setProfile(const JunoBbdProfile&) noexcept {}

private:
    double sampleRate_ = 44100.0;
};

//==============================================================================
/**
    El coro BBD: UN motor, la identidad de la maquina inyectada.

        dsp::JunoBBD<dsp::JunoBbdJ106Profile, dsp::BbdNoiseStage> chorus;

    `Profile` es una tabla de constantes (ver profiles/JunoBbdProfile.h) y
    `Stage` es la degradacion. Los dos se inyectan como parametros de
    plantilla, asi que no hay vtable ni indireccion en el lazo de audio.

    CONTRATO POR MARCO, no por canal, y por que: las dos lineas del coro son
    independientes y cada una avanza una vez por muestra, pero comparten el
    LFO, el zumbido de red y el reloj. Una API por canal obligaria a recalcular
    el LFO dos veces (y a que las dos lineas se desfasaran si se compartiese el
    estado, que es el fallo que se corrigio en el eco multi-cabezal). Un marco
    sigue siendo "la muestra", asi que la regla del modulo se respeta.
*/
template <typename Profile, typename Stage>
class JunoBBD
{
public:
    JunoBBD()
    {
        prepare(44100.0);
    }

    /** Fija el sample rate, dimensiona las dos lineas y deja el motor listo. */
    /**
        Prepara para una frecuencia de muestreo. `prepare()` pone los mandos de
        calibracion en los numeros de fabrica del PERFIL, no en cero, y el
        consumidor los sobreescribe DESPUES con los `set...` (ganancias,
        retardos, corte, saturacion). Es al reves que en el original, que los
        leia de su tabla antes de que existiera el motor, y por eso aqui
        cualquier `set...` posterior vale.
    */
    void prepare(double sampleRate)
    {
        dspAssert(sampleRate > 0.0);

        sampleRate_             = sampleRate;
        const JunoBbdProfile& p = Profile::value;

        lineL_.prepare(sampleRate, p.lineMinSeconds, p.biquadFc, p.biquadQ, p.poleFc);
        lineR_.prepare(sampleRate, p.lineMinSeconds, p.biquadFc, p.biquadQ, p.poleFc);

        ripple_.prepare(sampleRate, p.mainsHz, p.mainsA1, p.mainsA2, p.mainsA3);
        clickRingL_.prepare(sampleRate, p.clickRingHz, p.clickRingQ);
        clickRingR_.prepare(sampleRate, p.clickRingHz, p.clickRingQ);

        stage_.setProfile(p);
        stage_.prepareSampleRate(sampleRate);

        reset();
    }

    /** Vacia lineas, etapa y fase. Es estado de audio, no de control. */
    void reset() noexcept
    {
        lineL_.reset();
        lineR_.reset();
        ripple_.reset();
        clickRingL_.reset();
        clickRingR_.reset();
        stage_.reset();
        const JunoBbdProfile& p = Profile::value;

        lfoPhase_       = 0.0;
        wetMix_         = jlimit(0.0f, 1.0f, p.defaultMix);
        depth_          = jlimit(0.0f, 1.0f, p.defaultDepth);
        lfoRate_        = p.defaultRate;
        hissLvlDb_      = p.hissLevelDb;
        hissMultiplier_ = p.hissMultiplier;

        // Y los mandos de CALIBRACION arrancan en los numeros de fabrica del
        // perfil, no en cero. Es lo que evita la trampa de tener un motor mudo:
        // con `calGainWet_ = 0` la mezcla de mojado es cero y el efecto no hace
        // NADA hasta que el consumidor empuja su tabla de calibracion. Un motor
        // que nace callado y hay que despertar es un fallo de diseno, no una
        // garantia: el consumidor puede sobrescribir los seis cuando quiera.
        calDelayI_   = p.delayI;
        calDelayII_  = p.delayII;
        calModDepth_ = p.modDepthScale;
        calBothRate_ = p.rateBoth;
        calGainDry_  = p.gainDry;
        calGainWet_  = p.gainWet;

        setSaturation(p.satBoost);
    }

    void setMode(JunoBbdMode m) noexcept
    {
        if (m != mode_)
        {
            mode_ = m;

            // Al ENCENDER un modo se callan los clics: un BBD que lleva
            // apagado no puede tener un clic acumulado de la vez anterior.
            if (mode_ != JunoBbdMode::Off)
                stage_.suppressClicks();
        }
    }

    JunoBbdMode getMode() const noexcept { return mode_; }

    void setRate(float hz) noexcept { lfoRate_ = jlimit(0.1f, 15.0f, hz); }
    void setDepth(float d) noexcept { depth_ = jlimit(0.0f, 1.0f, d); }
    void setMix(float w) noexcept { wetMix_ = jlimit(0.0f, 1.0f, w); }
    void setHissLevelDb(float db) noexcept { hissLvlDb_ = jlimit(-96.0f, -40.0f, db); }
    void setHissMultiplier(float m) noexcept { hissMultiplier_ = jlimit(0.0f, 2.0f, m); }

    /** 0 = siseo rosa, 1 = blanco. Lo consume la etapa, no el motor. */
    void setHissColour(float c) noexcept { stage_.setHissColour(jlimit(0.0f, 1.0f, c)); }

    /** Ganancias del mezclador IC6. El consumidor las toma de su calibracion. */
    void setMixerGains(float dry, float wet) noexcept
    {
        calGainDry_ = dry;
        calGainWet_ = wet;
    }

    /** Retardos base de cada modo, en ms. Vienen de la calibracion del panel. */
    void setBaseDelaysMs(float delayI, float delayII) noexcept
    {
        calDelayI_  = delayI;
        calDelayII_ = delayII;
    }

    /** Barrido maximo en ms, y la frecuencia del modo I+II. */
    void setModulation(float modDepthMs, float bothRateHz) noexcept
    {
        calModDepth_ = modDepthMs;
        calBothRate_ = bothRateHz;
    }

    /** Saturacion de las lineas y corte del filtro de reconstruccion. */
    void setSaturation(float satBoost) noexcept
    {
        lineL_.setSatDrive(Profile::value.satDrive * satBoost);
        lineR_.setSatDrive(Profile::value.satDrive * satBoost);
    }

    void setReconstructionCutoff(float hz) noexcept
    {
        lineL_.prepare(sampleRate_, Profile::value.lineMinSeconds, hz,
                       Profile::value.biquadQ, Profile::value.poleFc);
        lineR_.prepare(sampleRate_, Profile::value.lineMinSeconds, hz,
                       Profile::value.biquadQ, Profile::value.poleFc);
    }

    /** Procesa UN marco stereo. Ver el contrato en la cabecera. */
    void process(float inL, float inR, float& outL, float& outR) noexcept
    {
        if (mode_ == JunoBbdMode::Off)
        {
            outL = inL;
            outR = inR;
            return;
        }

        const JunoBbdProfile& p = Profile::value;
        const float sr          = static_cast<float>(sampleRate_);

        //--- Cuanto barre el LFO en este modo, y a que velocidad ----------//
        float baseDepthMs = p.depthI;
        if (mode_ == JunoBbdMode::ChorusII)
            baseDepthMs = p.depthII;
        else if (mode_ == JunoBbdMode::ChorusBoth)
            baseDepthMs = p.depthBoth;

        const float delayDepth = baseDepthMs * (calModDepth_ / p.modDepthScale) * depth_;

        const float centreDelayMs = mode_ == JunoBbdMode::ChorusII ? calDelayII_ : calDelayI_;
        const float currentRate   = mode_ == JunoBbdMode::ChorusBoth ? calBothRate_ : lfoRate_;
        const double phaseInc     = kTwoPiDouble * currentRate / sampleRate_;

        const float baseNoiseGain = pow(10.0f, hissLvlDb_ / 20.0f);
        const float leakMin       = p.leakMinFrac;
        const float invDepth      = delayDepth > 1.0e-9f ? 1.0f / delayDepth : 0.0f;

        //--- Estado del LFO, COMUMPARTIDO por los dos canales ---------------//
        float lfo;
        if (mode_ == JunoBbdMode::ChorusBoth)
        {
            lfo = sin(static_cast<float>(lfoPhase_));
        }
        else
        {
            // Triangular: el LFO del Juno es un triángulo, no una senoidal.
            //
            // Y OJO, la aritmetica va en DOBLE hasta el final, como en el
            // original: `norm` es `double` ahi, la resta contra 0.5 es en
            // `double`, y solo el resultado se pasa a `float`. Hacer la resta en
            // float parece lo mismo y no lo es: `norm` a 32 bits redondea la
            // fase antes de restar, y el error se ve amostra 85 (la primera en la
            // que dispara un clic) y grows desde ahi. Es de los sitios donde la
            // paridad a 0 ulps se gana o se pierde.
            const double norm    = lfoPhase_ / kTwoPiDouble;
            const double centred = norm - 0.5;
            lfo                  = 1.0f - 4.0f * static_cast<float>(centred < 0.0 ? -centred : centred);
        }

        //--- Retardo de cada linea, con la tolerancia de su reloj ----------//
        const float trim = p.clockTrim;
        float delay0Ms   = (centreDelayMs + delayDepth * lfo) * (1.0f - trim);
        float delay1Ms   = (centreDelayMs - delayDepth * lfo) * (1.0f + trim);

        if (delay0Ms < p.minDelayMs) delay0Ms = p.minDelayMs;
        if (delay1Ms < p.minDelayMs) delay1Ms = p.minDelayMs;

        const float delay0samp = delay0Ms * 0.001f * sr;
        const float delay1samp = delay1Ms * 0.001f * sr;

        //--- Frecuencia de reloj de cada linea (256 etapas, vuelta completa) -//
        float clock0 = static_cast<float>(kJunoBbdStages) / (2.0f * delay0Ms * 0.001f);
        float clock1 = static_cast<float>(kJunoBbdStages) / (2.0f * delay1Ms * 0.001f);
        if (clock0 < p.minClockHz) clock0 = p.minClockHz;
        if (clock1 < p.minClockHz) clock1 = p.minClockHz;

        //--- Cuanta fuga hay en cada linea en ESTE punto del LFO -----------//
        const float lfo0     = (delay0Ms - centreDelayMs) * invDepth;
        const float lfo1     = (delay1Ms - centreDelayMs) * invDepth;
        const float lfoNorm0 = (lfo0 + 1.0f) * 0.5f;
        const float lfoNorm1 = (lfo1 + 1.0f) * 0.5f;

        const float leak0 = delayDepth * (leakMin + (1.0f - leakMin) * lfoNorm0);
        const float leak1 = delayDepth * (leakMin + (1.0f - leakMin) * lfoNorm1);

        //--- La etapa aporta el ruido, la fuga y los clics -----------------//
        const float clickScale      = delayDepth / p.depthI;
        const BbdStageOutput stageL = stage_.processLeft(leak0, lfo, clickScale,
                                                         baseNoiseGain, hissMultiplier_);
        const BbdStageOutput stageR = stage_.processRight(leak1, lfo, clickScale,
                                                          baseNoiseGain, hissMultiplier_);

        float wet0 = lineL_.processSample(inL, delay0samp, stageL.injected);
        float wet1 = lineR_.processSample(inR, delay1samp, stageR.injected);

        //--- Perdida de transferencia de carga: el BBD pierde ganancia cuando
        //--- el reloj va mas rapido, y el trim compensa la asimetria L/R.
        const float gain0 = (1.0f + p.gainTrim) * (1.0f - p.cteCoeff * (1.0f / clock0 - p.cteInvClockCentre));
        const float gain1 = (1.0f - p.gainTrim) * (1.0f - p.cteCoeff * (1.0f / clock1 - p.cteInvClockCentre));

        wet0 *= gain0;
        wet1 *= gain1;

        //--- El anillo que despierta el clic, DESPUES de la linea -----------//
        wet0 += clickRingL_.processSample(stageL.click) * p.clickRingGain * hissMultiplier_;
        wet1 += clickRingR_.processSample(stageR.click) * p.clickRingGain * hissMultiplier_;

        //--- Zumbido de red, UNO por muestra, a los dos canales ------------//
        const float ripple = ripple_.processSample() * hissMultiplier_;
        wet0 += ripple;
        wet1 += ripple;

        //--- IC6: el mezclador con ganancias asimetricas -------------------//
        const float dryMix       = 1.0f - wetMix_ * (1.0f - calGainDry_);
        const float wetMixAmount = wetMix_ * calGainWet_;

        outL = dryMix * inL + wetMixAmount * wet0;
        outR = dryMix * inR + wetMixAmount * wet1;

        // Y el fase NO pasa por `wrapPhase`, aunque la regla del modulo diga que
        // el envuelto de fase se hace con el. `wrapPhase` recibe y devuelve
        // FLOAT, y aqui la fase se acumula en DOBLE (como en el original), asi
        // que usarlo obliga a truncar a 32 bits en cada muestra. Con rates bajos
        // no se nota hasta la muestra 83, que es la primera en la que dispara
        // un clic, y ahi la diferencia ya es de 1 ulp en el retardo. La fase de
        // un LFO que dura horas en doble no es un capricho: es lo que evita que
        // el retardo derive de forma audible. La resta entera de una vez es
        // exactamente lo que hace `wrapPhase`, pero sin perder los bits.
        lfoPhase_ += phaseInc;
        if (lfoPhase_ >= kTwoPiDouble) lfoPhase_ -= kTwoPiDouble;
    }

    dspDeclareNonCopyableWithLeakDetector(JunoBBD)

        private : double sampleRate_ = 44100.0;

    JunoBbdMode mode_{JunoBbdMode::Off};

    junobbd_detail::BbdLine lineL_, lineR_;
    junobbd_detail::MainsRipple ripple_;
    junobbd_detail::ClickRing clickRingL_, clickRingR_;
    Stage stage_;

    double lfoPhase_ = 0.0;

    // Los valores por defecto de los mandos salen del PERFIL, no de aqui: los
    // numeros de fabrica de la maquina viven en la tabla, y repetir un -68.0f
    // aqui seria una segunda verdad que se desincroniza en cuanto la tabla
    // cambie. Los valores de calibracion que SI fija el consumidor (ganancias
    // del mezclador, retardos base) se quedan en 0 y los pone `set...` antes de
    // que suene nada.
    float lfoRate_        = 0.0f;
    float depth_          = 0.0f;
    float wetMix_         = 0.0f;
    float hissLvlDb_      = 0.0f;
    float hissMultiplier_ = 1.0f;

    float calDelayI_   = 0.0f;
    float calDelayII_  = 0.0f;
    float calModDepth_ = 0.0f;
    float calBothRate_ = 0.0f;
    float calGainDry_  = 0.0f;
    float calGainWet_  = 0.0f;
};

} // namespace abd::dsp
