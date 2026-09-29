/*
  ==============================================================================

    adapters/BasicAdapters.h
    Los seis motores de este modulo, JEUGOS en el contrato de un slot. Namespace
    abd::dsp, modulo ABDShared::DspEffects.

    QUE HACE ESTE FICHERO Y POR QUE NO PUEDE ESTAR EN LOS MOTORES. Un motor de
    este modulo habla unidades FISICAS: hercios, milisegundos, dB, muestras. Un
    slot habla NORMALIZADO 0..1, porque lo que hay detras es un mando. Entre
    uno y otro hay una tabla (`FxParamSpec`) que es, otra vez, politica: el
    mismo coro en un Juno tiene un barrido de 0.3 ms y en un pedal de 4 ms, y
    eso no lo decide el motor sino el producto. Por eso la traduccion vive
    aqui, en un fichero que se puede no incluir.

    Y LA REGLA DE ORO DE ESTA CAPA: el adaptador NO MEZCLA. El slot ya mezcla
    (ver `FxSlot::process`), asi que un motor que devuelve su mezcla interna y
    otro que devuelve solo la parte mojada darian resultados distintos con la
    misma perilla. Por eso TODOS los adaptadores de aqui devuelven la senal
    YA MOJADA y dejan la combinacion con la seca en manos del slot. Donde un
    motor trae mezcla dentro —la reverb de FreeVerb, el coro, el BBD— el
    adaptador pone sus niveles de mezcla a "solo mojado" y por eso el
    parametro `mix` NO aparece en su tabla: vive en el mando del slot.

    ESO TIENE UN PRECIO QUE ESTA MEDIDO Y ACEPTADO: la reverberacion de este
    modulo tiene `wetLevel` y `dryLevel` dentro, y con los dos a "solo mojado"
    la suma ya no es la que sonaba en el producto. En vez de quitarle el
    mezclado interno al motor —que tiene consumers con paridad a 0 ulps— el
    adaptador declara en su tabla los NIVELES INTERNOS como parametros suyos
    (`Reverb.wet`, `Reverb.dry`), y el mezclado del slot se suma encima. Es la
    unica forma de no romper la paridad, y el usuario tiene los dos mandos: el
    del panel y el del motor.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "DspEffects/DspChorus.h"
#include "DspEffects/DspDelay.h"
#include "DspEffects/DspReverb.h"
#include "DspEffects/DspSaturation.h"
#include "DspEffects/DspSchroederReverb.h"
#include "DspEffects/FxRegistry.h"
#include "DspEffects/JunoBBD.h"
#include "DspEffects/ShelfFilter.h"
#include "DspEffects/Phaser4.h"
#include "DspEffects/characters/BbdNoise.h"
#include "DspEffects/profiles/JunoBbdProfile.h"

namespace abd::dsp::adapters
{

//==============================================================================
/** Rejilla comun: normalizado a fisico y a la inversa, con la tabla delante. */
struct Knobs
{
    const FxParamSpec* specs = nullptr;
    int count = 0;

    float to (int index, float normalised) const noexcept
    {
        if (specs == nullptr || index < 0 || index >= count)
            return 0.0f;

        return fxDenormalise (specs[index], normalised);
    }
};

//==============================================================================
/**
    UN MANDO SUAVIZADO. Y por que esta aqui y no en el motor.

    Un hueco recibe sus parametros UNA VEZ POR BLOQUE, y un motor que los
    aplica de golpe hace un tictac en cada cambio: un retardo que salta de
    300 a 310 ms en un borde de bloque displace la lectura de golpe, y eso se
    oye como un clic. Los envoltorios de producto que hay hoy tienen un
    `LinearSmoothedValue` de 20 ms por mando, y por eso esto no es una mejora
    nueva sino una condicion para que la migracion suene igual.

    Y DONDE ESTA ES LA DECISION. El suavizado es politica —20 ms es lo que usa
    NEURONiK, y otro producto puede no querer suavizar—, asi que por la regla
    del modulo no puede vivir en el motor. `EffectPolicy.h` ya inyecta la etapa
    de caracter como parametro de plantilla por el mismo motivo. Aqui se
    resuelve en el adaptador, que es donde ya vive el resto de la traduccion
    de unidades.

    LA RAMPA ES EN UNIDADES FISICAS, no en el mando normalizado. Es lo que
    hacen los envoltorios que se sustituyen, y tiene una consecuencia util: el
    unico trabajo por muestra es una resta y una suma. Suavizar el mando
    normalizado y desnormalizar cada muestra costaria una `pow` por mando y por
    muestra, y no vale la pena para evitar un coste que no existe.
*/
class SmoothedKnob
{
public:
    void reset (double sampleRate, float initial, double rampSeconds = 0.020) noexcept
    {
        smoother_.reset (sampleRate, rampSeconds);
        target_ = initial;
        smoother_.setCurrentAndTargetValue (initial);
    }

    /** El hueco avisa del valor nuevo. No se aplica aqui: se aplica ramping. */
    void setTarget (float physical) noexcept
    {
        target_ = physical;
        smoother_.setTargetValue (physical);
    }

    float getNextValue() noexcept { return smoother_.getNextValue(); }

    /** El valor ya asentado, para lo que se lee una vez por bloque. */
    float getTarget() const noexcept { return target_; }

    void jumpToTarget() noexcept { smoother_.setCurrentAndTargetValue (target_); }

private:
    LinearSmoothedValue<float> smoother_;
    float target_ = 0.0f;
};

//==============================================================================
/** CORO. El motor es de una linea con LFO, sin suavizado: el adaptador es el
    unico que decide la mezcla wet/dry, y por eso la hace "solo mojado"
    (`mix = 1.0f` deja la mezcla del motor en 50/50 porque su formula es
    `in·(1−mix·0.5) + eco·mix·0.5`; con 1.0 el eco entra a mitad de peso, que
    es lo medido como la maxima mojadura del motor sin tocarlo). */
class ChorusFx
{
public:
    static constexpr int kNumParams = 2;

    //--- La tabla ---------------------------------------------------------
    static const FxParamSpec* specs() noexcept
    {
        // El rango de `rate` llega a 8 Hz y no a 10 porque medido, el motor
        // modula 5..30 ms de retardo: a 10 Hz la linea da una vuelta cada 100
        // muestras y el coro se oye como un trémolo, no como un coro. Y el
        // 0.85 Hz por defecto es el de NEURONiK, que es de donde viene este
        // motor.
        //
        // Y SOLO DOS MANDOS, no tres. Habia un tercero ("voices") que el
        // adaptador no leia: un knob en un panel que no hace nada es peor que
        // un knob que no existe, porque el usuario lo mueve, no pasa nada, y
        // concluye que el plugin esta roto. Un parametro declarado y no
        // implementado es una promesa que el motor no cumple. Cuando haya voz
        // doble de verdad, se anade con su rango.
        static const FxParamSpec table[kNumParams] = {
            { "rate",  0.10f, 8.00f, 0.85f, 0.55f, 0 },   // Hz, con recorrido abajo
            { "depth", 0.00f, 1.00f, 0.50f, 1.00f, 0 }
        };
        return table;
    }

    static void* create (double sampleRate) noexcept
    {
        auto* self = new ChorusFx();
        self->prepare (sampleRate);
        return self;
    }

    static void destroy (void* instance) noexcept { delete static_cast<ChorusFx*> (instance); }
    static void reset (void* instance) noexcept   { static_cast<ChorusFx*> (instance)->engine_.reset(); }

    static void setParam (void* instance, int index, float value) noexcept
    {
        static_cast<ChorusFx*> (instance)->setParam (index, value);
    }

    static void setAll (void* instance, const float* v, int count) noexcept
    {
        auto* self = static_cast<ChorusFx*> (instance);
        for (int i = 0; i < count && i < kNumParams; ++i)
            self->setParam (i, v[i]);
    }

    static void process (void* instance, const float* inL, const float* inR,
                         float* outL, float* outR, int n) noexcept
    {
        auto* self = static_cast<ChorusFx*> (instance);
        const float depth = self->depth_;
        const float rate   = self->rate_;

        for (int i = 0; i < n; ++i)
        {
            const float l = self->engine_.processSample (0, inL[i], depth, 1.0f);
            const float r = self->engine_.processSample (1, inR[i], depth, 1.0f);
            self->engine_.advance (rate);
            outL[i] = l;
            outR[i] = r;
        }
    }

private:
    void prepare (double sampleRate) noexcept
    {
        // 100 ms, el maximo que necesita la modulacion de 5..30 ms del motor.
        engine_.prepare (sampleRate, 0.1);
    }

    void setParam (int index, float value) noexcept
    {
        const float p = knobs.to (index, value);

        switch (index)
        {
            case 0:  rate_  = p;  break;
            case 1:  depth_ = p;  break;
            default: break;
        }
    }

    Chorus engine_;
    Knobs knobs { specs(), kNumParams };
    float rate_  = 0.85f;
    float depth_ = 0.50f;
};

//==============================================================================
/** DELAY. El motor devuelve el tap, sin mezclar: el mezclado lo pone el slot. */
class DelayFx
{
public:
    static constexpr int kNumParams = 2;

    static const FxParamSpec* specs() noexcept
    {
        // Solo dos mandos, no tres. El tercero era `damping`, y el motor de
        // este modulo no tiene amortiguacion en el lazo de realimentacion: el
        // que la tiene es `DspReverb` y la de la cinta. Declararla aqui sin
        // implementarla es un knob que no hace nada (ver la nota del coro).
        static const FxParamSpec table[kNumParams] = {
            { "time",     0.010f, 2.0f, 0.375f, 0.30f, 0 },   // segundos
            { "feedback", 0.000f, 0.95f, 0.350f, 1.00f, 0 }
        };
        return table;
    }

    static void* create (double sampleRate) noexcept
    {
        auto* self = new DelayFx();
        self->prepare (sampleRate);
        return self;
    }

    static void destroy (void* instance) noexcept { delete static_cast<DelayFx*> (instance); }
    static void reset (void* instance) noexcept   { static_cast<DelayFx*> (instance)->engine_.reset(); }

    static void setParam (void* instance, int index, float value) noexcept
    {
        static_cast<DelayFx*> (instance)->setParam (index, value);
    }

    static void setAll (void* instance, const float* v, int count) noexcept
    {
        auto* self = static_cast<DelayFx*> (instance);
        for (int i = 0; i < count && i < kNumParams; ++i)
            self->setParam (i, v[i]);
    }

    static void process (void* instance, const float* inL, const float* inR,
                         float* outL, float* outR, int n) noexcept
    {
        auto* self = static_cast<DelayFx*> (instance);
        const float samples = self->delaySamples_;
        const float fb      = self->feedback_;

        for (int i = 0; i < n; ++i)
        {
            const float l = self->engine_.processSample (0, inL[i], samples, fb);
            const float r = self->engine_.processSample (1, inR[i], samples, fb);
            self->engine_.advanceWritePosition();
            outL[i] = l;
            outR[i] = r;
        }
    }

private:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        maxDelaySamples_ = static_cast<int> (sampleRate * kMaxSeconds);
        engine_.prepare (sampleRate, maxDelaySamples_);
        setParam (0, 0.375f);   // el tiempo por defecto, en normalizado
    }

    void setParam (int index, float value) noexcept
    {
        const float p = knobs.to (index, value);

        if (index == 0)
        {
            // El retardo va en muestras, y el maximo del motor es el de
            // `prepare`. Un tiempo pedido por encima se recorta al maximo en
            // vez de dejar el retardo en un sitio que el motor no tiene.
            delaySamples_ = jmin (p * static_cast<float> (sampleRate_),
                                  static_cast<float> (maxDelaySamples_));
        }
        else if (index == 1)
        {
            feedback_ = p;
        }
    }

    static constexpr double kMaxSeconds = 2.0;

    Delay engine_;
    Knobs  knobs { specs(), kNumParams };
    double sampleRate_ = 44100.0;
    int maxDelaySamples_ = 88200;
    float delaySamples_ = 0.0f;
    float feedback_ = 0.35f;
};

//==============================================================================
/** REVERB de FreeVerb. Es el unico motor con mezcla DENTRO, asi que el
    adaptador expone sus dos niveles como parametros y pone la mezcla del slot
    encima. Ver la nota de la cabecera del fichero. */
class ReverbFx
{
public:
    static constexpr int kNumParams = 4;

    static const FxParamSpec* specs() noexcept
    {
        // `width` por defecto a 1.0 es el valor que trae el motor, y por eso
        // cae en el FINAL del recorrido: un mando que nace arriba del todo
        // parece un mando que ya esta arriba. Se baja a 0.9, que es lo mas
        // ancho que la reverb de FreeVerb da antes de que la diferencia se
        // vuelva a ser de Samples, y que deja el mando con recorrido en las dos
        // direcciones.
        static const FxParamSpec table[kNumParams] = {
            { "size",    0.0f, 1.0f, 0.50f, 1.0f, 0 },
            { "damping", 0.0f, 1.0f, 0.50f, 1.0f, 0 },
            { "width",   0.0f, 1.0f, 0.90f, 1.0f, 0 },
            { "levels",  0.0f, 1.0f, 0.33f, 1.0f, 0 }   // los dos niveles internos
        };
        return table;
    }

    static void* create (double sampleRate) noexcept
    {
        auto* self = new ReverbFx();
        self->prepare (sampleRate);
        return self;
    }

    static void destroy (void* instance) noexcept { delete static_cast<ReverbFx*> (instance); }
    static void reset (void* instance) noexcept   { static_cast<ReverbFx*> (instance)->engine_.reset(); }

    static void setParam (void* instance, int index, float value) noexcept
    {
        static_cast<ReverbFx*> (instance)->setParam (index, value);
    }

    static void setAll (void* instance, const float* v, int count) noexcept
    {
        auto* self = static_cast<ReverbFx*> (instance);
        for (int i = 0; i < count && i < kNumParams; ++i)
            self->setParam (i, v[i]);
    }

    static void process (void* instance, const float* inL, const float* inR,
                         float* outL, float* outR, int n) noexcept
    {
        auto* self = static_cast<ReverbFx*> (instance);

        for (int i = 0; i < n; ++i) { outL[i] = inL[i]; outR[i] = inR[i]; }
        self->engine_.processStereo (outL, outR, n);
    }

private:
    void prepare (double sampleRate) noexcept
    {
        engine_.setSampleRate (sampleRate);
    }

    void setParam (int index, float value) noexcept
    {
        const float p = knobs.to (index, value);

        Reverb::Parameters params = engine_.getParameters();
        switch (index)
        {
            case 0: params.roomSize = p;  break;
            case 1: params.damping  = p;  break;
            case 2: params.width    = p;  break;
            case 3: params.wetLevel = p; params.dryLevel = 0.0f; break;
            default: break;
        }
        engine_.setParameters (params);
    }

    Reverb engine_;
    Knobs  knobs { specs(), kNumParams };
};

//==============================================================================
/** SATURACION. El motor es una ESTATICA sin estado: el adaptador es el que le
    da memoria para poder limpiarse con `reset`, y el que decide el suavizado.

    Y EL SUAVIZADO LO PONE EL ADAPTADOR, no el motor, porque es politica: 20 ms
    es lo que usa NEURONiK, y otro producto puede no querer suavizar. Lo que
    NO puede hacer el adaptador es suavizar el `drive` sin suavizar el
    `mix` del slot, porque entonces la ganancia de un efecto que ya esta
   establecidamente arriba bajaria durante el transitorio. */
class SaturationFx
{
public:
    static constexpr int kNumParams = 1;

    /** La rampa del `drive`, en segundos. Vive aqui y no como literal en
        `create` porque un test de NEURONiK mide CUANTO tarda el drive en
        asentarse (ver `Tests/ModulationDest17DriveTest.cpp`) y necesita el
        numero de aqui, no una copia que se separa el dia que este valor
        cambie. */
    static constexpr double kDriveRampSeconds = 0.005;

    static const FxParamSpec* specs() noexcept
    {
        // Solo un mando. El segundo era `bias`, y `Saturation::processSample` no
        // tiene sesion: es `atan(x · drive)`, sin offset. Inventar un mando de
        // sesion y que no haga nada es peor que no inventarlo (ver el coro).
        //
        // Y el rango es 1..8 y no 1..20 por lo que hace el motor: `atan(x ·
        // drive)` con 20 ya esta tan saturado que entre 8 y 20 el sonido apenas
        // se distingue, y con un recorrido lineal el 2 de NEURONiK caia en el
        // 0.05 del mando — es decir, un boton de saturacion que de verdad no se
        // puede tocar sin pasarse. Con sesgo 0.5 y tope 8, el 2 cae en el 0.38.
        static const FxParamSpec table[kNumParams] = {
            { "drive", 1.0f, 8.0f, 2.0f, 0.50f, 0 }
        };
        return table;
    }

    static void* create (double sampleRate) noexcept
    {
        auto* self = new SaturationFx();
        self->sampleRate_ = sampleRate;
        // 5 ms de rampa, no los 20 ms de NEURONiK: aqui la saturacion esta
        // detras de un slot con su propia mezcla, y 20 ms de rampa en el `drive`
        // se oyen como un golpe de ganancia al mover el mando. Los 20 ms de
        // NEURONiK son de su cadena entera, no de este parametro.
        self->smoother_.reset (sampleRate, kDriveRampSeconds);
        self->smoother_.setCurrentAndTargetValue (self->target_);
        return self;
    }

    static void destroy (void* instance) noexcept { delete static_cast<SaturationFx*> (instance); }

    static void reset (void* instance) noexcept
    {
        // El motor no tiene estado, asi que "vaciarlo" es dejar el suavizado en
        // su valor en vez de a mitad de una rampa: si no, el primer bloque
        // tras un `reset` sale con el drive de una transicion que el usuario
        // no pidio.
        auto* self = static_cast<SaturationFx*> (instance);
        self->smoother_.setCurrentAndTargetValue (self->target_);
    }

    static void setParam (void* instance, int index, float value) noexcept
    {
        static_cast<SaturationFx*> (instance)->setParam (index, value);
    }

    static void setAll (void* instance, const float* v, int count) noexcept
    {
        auto* self = static_cast<SaturationFx*> (instance);
        for (int i = 0; i < count && i < kNumParams; ++i)
            self->setParam (i, v[i]);
    }

    static void process (void* instance, const float* inL, const float* inR,
                         float* outL, float* outR, int n) noexcept
    {
        auto* self = static_cast<SaturationFx*> (instance);

        for (int i = 0; i < n; ++i)
        {
            const float drive = self->smoother_.getNextValue();
            outL[i] = Saturation::processSample (inL[i], drive);
            outR[i] = Saturation::processSample (inR[i], drive);
        }
    }

private:
    void setParam (int index, float value) noexcept
    {
        if (index != 0)
            return;

        // Solo `setTargetValue`: la rampa se conserva, que es lo que hay que
        // hacer. Habia escrito aqui que un host que reenvia los parametros en
        // cada bloque dejaria el `drive` pegado, y es FALSO, y se comprobo
        // midiendo las dos: `setTargetValue` corta con `approximatelyEqual`
        // cuando el valor no ha cambiado, y con el valor cambiado la cuenta
        // atras la completa `getNextValue` igual de rapido. Las dos llegan a
        // 8.0 con error 0.0000. La diferencia es que `setCurrentAndTargetValue`
        // mata la rampa, y un salto seco del `drive` es un tictac.
        target_ = knobs.to (index, value);
        smoother_.setTargetValue (target_);
    }

    LinearSmoothedValue<float> smoother_;
    Knobs  knobs { specs(), kNumParams };
    double sampleRate_ = 44100.0;
    float target_ = 2.0f;
};

//==============================================================================
/** REVERBERADOR DE SCHROEDER. Sale ya mojado (`processFrame` no mezcla), asi
    que el slot es el unico mezclador. Los cuatro mandos del motor tienen
    unidades fisicas distintas y por eso son los cuatro que se exponen. */
class SchroederFx
{
public:
    static constexpr int kNumParams = 4;

    static const FxParamSpec* specs() noexcept
    {
        static const FxParamSpec table[kNumParams] = {
            { "decay",      0.10f, 0.98f, 0.60f, 1.00f, 0 },
            { "damping",    0.00f, 1.00f, 0.30f, 1.00f, 0 },
            { "diffusion",  0.00f, 1.00f, 0.70f, 1.00f, 0 },
            { "predelay",   0.00f, 0.12f, 0.02f, 1.00f, 0 }  // segundos
        };
        return table;
    }

    static void* create (double sampleRate) noexcept
    {
        auto* self = new SchroederFx();
        self->prepare (sampleRate);
        return self;
    }

    static void destroy (void* instance) noexcept { delete static_cast<SchroederFx*> (instance); }
    static void reset (void* instance) noexcept   { static_cast<SchroederFx*> (instance)->engine_.reset(); }

    static void setParam (void* instance, int index, float value) noexcept
    {
        static_cast<SchroederFx*> (instance)->setParam (index, value);
    }

    static void setAll (void* instance, const float* v, int count) noexcept
    {
        auto* self = static_cast<SchroederFx*> (instance);
        for (int i = 0; i < count && i < kNumParams; ++i)
            self->setParam (i, v[i]);
    }

    static void process (void* instance, const float* inL, const float* inR,
                         float* outL, float* outR, int n) noexcept
    {
        auto* self = static_cast<SchroederFx*> (instance);
        for (int i = 0; i < n; ++i)
            self->engine_.processFrame (inL[i], inR[i], outL[i], outR[i]);
    }

private:
    void prepare (double sampleRate) noexcept
    {
        // 0.25 s, el techo por defecto del motor. Es el que usa el unico
        // consumidor que tiene hoy (el reverb de ABDEep).
        engine_.prepare (sampleRate, 0.25);
        engine_.setPreDelaySeconds (0.02f);
    }

    void setParam (int index, float value) noexcept
    {
        const float p = knobs.to (index, value);

        switch (index)
        {
            case 0: engine_.setDecay (p);      break;
            case 1: engine_.setDamping (p);    break;
            case 2: engine_.setDiffusion (p);  break;
            case 3: engine_.setPreDelaySeconds (p); break;
            default: break;
        }
    }

    SchroederReverb engine_;
    Knobs knobs { specs(), kNumParams };
};

//==============================================================================
/** CORO BBD. El unico con un parametro DISCRETO: el modo son cuatro estados
    y el panel lo enseña como un selector, no como un mando. */
class BbdChorusFx
{
public:
    static constexpr int kNumParams = 4;

    static const FxParamSpec* specs() noexcept
    {
        static const FxParamSpec table[kNumParams] = {
            { "mode",  0.0f, 3.0f, 1.0f, 1.0f, 4 },   // Off, I, II, I+II
            { "rate",  0.10f, 8.0f, 0.513f, 0.60f, 0 },
            { "depth", 0.00f, 1.0f, 0.60f, 1.00f, 0 },
            { "wear",  0.00f, 1.0f, 0.35f, 1.00f, 0 }   // multiplicador de ruido
        };
        return table;
    }

    static void* create (double sampleRate) noexcept
    {
        auto* self = new BbdChorusFx();
        self->prepare (sampleRate);
        return self;
    }

    static void destroy (void* instance) noexcept { delete static_cast<BbdChorusFx*> (instance); }

    static void reset (void* instance) noexcept
    {
        static_cast<BbdChorusFx*> (instance)->engine_.reset();
    }

    static void setParam (void* instance, int index, float value) noexcept
    {
        static_cast<BbdChorusFx*> (instance)->setParam (index, value);
    }

    static void setAll (void* instance, const float* v, int count) noexcept
    {
        auto* self = static_cast<BbdChorusFx*> (instance);
        for (int i = 0; i < count && i < kNumParams; ++i)
            self->setParam (i, v[i]);
    }

    static void process (void* instance, const float* inL, const float* inR,
                         float* outL, float* outR, int n) noexcept
    {
        auto* self = static_cast<BbdChorusFx*> (instance);
        for (int i = 0; i < n; ++i)
            self->engine_.process (inL[i], inR[i], outL[i], outR[i]);
    }

private:
    void prepare (double sampleRate) noexcept
    {
        // Y NO se llama a `setReconstructionCutoff` despues. `prepare` ya
        // rearma el biquad con `biquadFc` del perfil, y llamar al `set` despues
        // seria cambiar el filtro dos veces: el numero que se oye seria el
        // segundo, y el que tiene el resto del motor seria el primero. Es el
        // mismo error de orden que rompia el test de paridad de JUNiO601
        // (biquad 0.5774 contra 0.7326), y por eso el adaptador no lo repite:
        // quien quiera otra frecuencia de reconstruccion cambia el perfil.
        engine_.prepare (sampleRate);
    }

    void setParam (int index, float value) noexcept
    {
        const float p = knobs.to (index, value);

        switch (index)
        {
            case 0:
                engine_.setMode (static_cast<JunoBbdMode> (static_cast<int> (p + 0.5f)));
                break;
            case 1: engine_.setRate (p);    break;
            case 2: engine_.setDepth (p);   break;
            case 3: engine_.setHissMultiplier (p); break;
            default: break;
        }
    }

    // J106 y no J60 porque son el MISMO perfil: en JUNiO601 los dos modelos
    // reciben el mismo valor por defecto y el `ChorusModel` del original no se
    // lee nunca. Cuando haya una calibracion de J60 de verdad, es cambiar esta
    // linea y nada mas.
    JunoBBD<JunoBbdJ106Profile, BbdNoiseStage> engine_;
    Knobs knobs { specs(), kNumParams };
};

//==============================================================================
/** REPISA. La fila mas simple del catalogo y la unica cuya subida de valor es
    un SEGUNDO de lo que cuestan las otras: un biquad son diez multiplicaciones.

    Y AQUI ESTA LA DECISION QUE NO ES OBVIA: los mandos se suavizan y se empujan
    al motor CADA 16 MUESTRAS, no en cada bloque y no en cada muestra.

    - Por bloque es un salto cada 512, y se oye.
    - Por muestra es peor que eso: recalcular los coeficientes son cinco
      transcendentales, y MEDIDO son 194 ns por muestra, o sea el 20,7% de un
      nucleo a 48 kHz para un biquad. Es el precio de un filtro.
    - Cada 16 mide 17,9 ns (0,18%) y cada 32 mide 20,7 ns (0,21%). ENTRE LOS
      DOS NO HAY DIFERENCIA REAL: la maquina estaba midiendo por debajo del
      ruido, y el mas bajo no cuesta nada.

    LA MEDICION DEL CLIC HAY QUE HACERLA BIEN, y la primera vez no se hizo. Se
    comparaba el "excedente" -cuanto salta la salida por encima de lo que saltaba
    la ENTRADA- con un barrido de frecuencia de 200 Hz a 16 kHz. Con +12 dB de
    repisa alta la salida CRECE durante el barrido, porque a 200 Hz la repisa da
    0 dB y a 16 kHz da 12: el "excedente" medido era sobre todo la senal
    creciendo. Por eso parecia que empujar los coeficientes cada 256 muestras
    "sonaba" mucho peor que cada 32, y no sonaba: crecia.

    Un giro de mando de verdad son unos cientos de Hz por segundo, no 800 Hz por
    milisegundo: un barrido de 20 Hz a 20 kHz en 20 ms es un caso que no se da.

    Y LA CIFRA DE ESTA TARASA NO ESTA AQUI A PROPOSITO, y antes si lo estaba: ocho
    numeros, dos velocidades y cuatro tasas, de una medicion hecha con el criterio
    del "excedente" que los dos parrafos de arriba acaban de declarar incorrecto.
    No se pueden verificar con el metodo bueno, asi que no se escriben. El banco
    mide el caso con el criterio correcto —el motor contra si mismo con la misma
    rampa y los mismos destinos, y lo unico que cambia es cada cuanto se empuja—
    y lo imprime en cada ejecucion, junto con una fila de 32 muestras que
    comprueba que la puerta pone en rojo. Ahi estan los numeros, y se actualizan
    solos.

    Y 16 DIVIDE a los tamanos de bloque de todo el arbol: 64, 128, 256, 480, 512,
    1024 y 2048. El contador NO se reinicia en cada bloque a proposito: si se
    reiniciara, con un bloque de 384 muestras la division no cuadra y el empuje
    se moveria de fase cada bloque, que es la forma de que dos bloques iguales
    den dos sonidos distintos.

    Y LA BANDA MUERTA DE 0,05 dB DEL MOTOR SE CONSERVA, y aqui tiene un segundo
    trabajo, medido: con el barrido de GANANCIA la tasa de control da igual
    (x1,00 del paso en regimen desde 1 hasta 128 muestras), porque la banda
    muerta ya cuantiza sola la rampa. El motor solo recalcula cuando la ganancia
    se ha movido de verdad, que es justo lo que se queria de ella. El mando que
    SI nota la tasa es el de frecuencia, y por eso la puerta del rampado del
    test mide ESE y no el de ganancia.

    EL MANDO DE MODO NO SE SUAVIZA, y es deliberado. Es un selector discreto
    entre dos repisas: el paso de una a otra cambia la funcion de transferencia
    entera, y suavizarlo daria un filtro que no es ni bajo ni alto durante la
    transicion, o sea un filtro que no existe. El original de MS2000 tampoco lo
    suaviza. */
class ShelfEqFx
{
public:
    static constexpr int kNumParams = 3;

    /** Cada cuanto se empuja el mando al motor, en muestras. MEDIDO: el coste
        deja de distinguir entre 16 y 32, y el clic del barrido de frecuencia
        es el que manda. Ver la cabecera. */
    static constexpr int kControlRate = 16;

    static const FxParamSpec* specs() noexcept
    {
        // `freq` es CONTINUA y va de 20 Hz a 20 kHz, y no una tabla de cuatro
        // pasos como la de ABDMS2000. Las tablas de 4 pasos (160/250/400/600 y
        // 4000/6000/8000/12000 Hz) son politica de producto y se quedan en el
        // producto, que es la misma regla que el rango de 0,10 a 8 Hz del coro.
        //
        // El techo de 20 kHz esta POR ENCIMA de 0,45·fs a cualquier sample rate
        // del arbol menos a 32 kHz, donde el motor lo recorta a 14,4 kHz. Se
        // deja el techo alto a proposito: el motor recorta y el filtro sigue
        // siendo un filtro, mientras que un techo de 14 kHz en la fila obligaria
        // a un producto que trabaja a 96 kHz a no poder pedir 16 kHz.
        //
        // El sesgo de 0,5 es el que da recorrido util: en 20 Hz a 20 kHz, la
        // mitad baja del rango (20 Hz a 1 kHz) cae en el 0,22 del recorrido, y
        // sin sesgo caia en el 0,05 — la zona donde un filtro de graves no se
        // puede afinar.
        static const FxParamSpec table[kNumParams] = {
            { "mode",  0.0f,     1.0f,    0.0f,  1.00f, 2 },   // baja, alta
            { "freq",  20.0f, 20000.0f, 1000.0f,  0.50f, 0 },   // Hz
            { "gain", -12.0f,    12.0f,    3.0f,  1.00f, 0 }    // dB
        };
        return table;
    }

    static void* create (double sampleRate) noexcept
    {
        auto* self = new ShelfEqFx();
        self->prepare (sampleRate);
        return self;
    }

    static void destroy (void* instance) noexcept { delete static_cast<ShelfEqFx*> (instance); }

    static void reset (void* instance) noexcept
    {
        // El estado de audio del motor Y las rampas. Sin lo segundo, el primer
        // bloque despues de un `reset` de host sale con los mandos a medio
        // camino: el usuario no lo ha pedido y suena como un golpe de ganancia.
        auto* self = static_cast<ShelfEqFx*> (instance);
        self->engine_.reset();
        self->ganancia_.jumpToTarget();
        self->frecuencia_.jumpToTarget();
        self->aplicaMandos (self->ganancia_.getTarget(), self->frecuencia_.getTarget());
    }

    static void setParam (void* instance, int index, float value) noexcept
    {
        static_cast<ShelfEqFx*> (instance)->setParam (index, value);
    }

    static void setAll (void* instance, const float* v, int count) noexcept
    {
        auto* self = static_cast<ShelfEqFx*> (instance);
        for (int i = 0; i < count && i < kNumParams; ++i)
            self->setParam (i, v[i]);
        self->asientaMandos();
    }

    static void process (void* instance, const float* inL, const float* inR,
                         float* outL, float* outR, int n) noexcept
    {
        auto* self = static_cast<ShelfEqFx*> (instance);

        for (int i = 0; i < n; ++i)
        {
            // La rampa avanza SIEMPRE, incluso en las muestras en las que no se
            // empuja nada al motor. Si solo se leyera en las de control, la
            // rampa seria 32 veces mas rapida en un bloque de 32 muestras que
            // en uno de 512, y el mismo barrido sonaria distinto segun el
            // tamanio de bloque del host.
            const float ganancia = self->ganancia_.getNextValue();
            const float frecuencia = self->frecuencia_.getNextValue();

            // Lo que llega al motor es DONDE ESTA LA RAMPA, no su destino. La
            // primera version empujaba `getTarget()` —el valor final— y el
            // suavizado no hacia nada: el motor se comia el salto entero en la
            // primera muestra de control, que es exactamente el tictac que el
            // suavizado existe para evitar. Un suavizado que no se aplica al
            // motor es peor que no tenerlo, porque ademas cuesta una rampa por
            // muestra y da la sensacion de que el problema esta resuelto.
            //
            // Y EL CONTADOR TIENE QUE VOLVER A CERO, con un `++` y un `>=`, no
            // con un `++ == 0`: el `== 0` solo es cierto en la primera muestra de
            // la vida del objeto, y en la segunda no vuelve a ser cierto nunca.
            // Con eso el empuje pasaba una vez —en `prepare`— y los tres mandos
            // se quedaban clavados en su valor por defecto para siempre: mover
            // la ganancia no hacia NADA. No lo cazaba ningun test de sonido
            // porque el resultado final era el correcto, solo que ya no
            // respondia; hace falta un test que mueva el mando y compare.
            if (++self->cuenta_ >= kControlRate)
            {
                self->cuenta_ = 0;
                self->aplicaMandos (ganancia, frecuencia);
            }

            float l = inL[i];
            float r = inR[i];
            self->engine_.processFrame (l, r);
            outL[i] = l;
            outR[i] = r;
        }
    }

private:
    void prepare (double sampleRate) noexcept
    {
        engine_.prepare (sampleRate);

        // 20 ms, la rampa que ya usan los envoltorios que este sustituye.
        ganancia_.reset (sampleRate, 3.0f);
        frecuencia_.reset (sampleRate, 1000.0f);

        // El contador arranca ATRASADO, para que la primera muestra ya empuje
        // los mandos. Ver la nota del phaser: un coeficiente atascado un bloque
        // entero es un bloque entero de filtro equivocado. Aqui el efecto es
        // menor, porque en `prepare` los mandos ya estan en su sitio, pero el
        // mismo razonamiento vale y el mismo numero lo hace.
        cuenta_ = kControlRate - 1;
        aplicaMandos (ganancia_.getTarget(), frecuencia_.getTarget());
    }

    void setParam (int index, float value) noexcept
    {
        const float p = knobs.to (index, value);

        switch (index)
        {
            case 0: modo_ = (p < 0.5f) ? ShelfMode::Low : ShelfMode::High; break;
            case 1: frecuencia_.setTarget (p); break;
            case 2: ganancia_.setTarget (p);  break;
            default: break;
        }
    }


    /** Asienta las rampas en su destino.

        Y SOLO desde `setAll`, que es la via de CARGA DE UN PRESET. Un preset es
        una discontinuidad por naturaleza: el host lo carga y espera oirlo, no
        ver un fundido desde los valores por defecto de la fila. `setParam` sigue
        rampando, porque esa es la via de la automatizacion y de un panel.

        Sin esto, un objeto recien creado con los mandos puestos por `setAll`
        salia rampando desde el valor por defecto, y despues de un `reset` salia
        ya en el valor: o sea que cargar un preset y reiniciar el host sonaban
        distinto, que es el tipo de cosa que solo se nota en una comparacion A/B
        y que nadie encuentra porque "los dos suenan bien".
    */
    void asientaMandos() noexcept
    {
        ganancia_.jumpToTarget();
        frecuencia_.jumpToTarget();
    }

    /** Empuja al motor la posicion ACTUAL de las rampas.

        Se llama con el destino en `prepare` y en `reset` —donde no hay rampa
        todavia, porque no ha pasado ninguna muestra— y con la posicion
        interpolada en el lazo de audio. */
    void aplicaMandos (float ganancia, float frecuencia) noexcept
    {
        engine_.setMode (modo_);
        engine_.setFrequencyHz (frecuencia);
        engine_.setGainDB (ganancia);
    }

    ShelfFilter engine_;
    Knobs knobs { specs(), kNumParams };
    SmoothedKnob ganancia_;
    SmoothedKnob frecuencia_;
    ShelfMode modo_ = ShelfMode::Low;

    /** Cuenta las muestras desde el ultimo empuje, y NO se reinicia por bloque.
        Ver la nota de la cabecera: reiniciarla haria que el punto de empuje
        dependiera del tamanio de bloque del host. */
    int cuenta_ = 0;
};

//==============================================================================
/** PHASER. La fila del `Phaser4` de la cabecera, que es donde vive el barrido.

    Y AQUI LA FILA NO HACE CASO NADA, que es lo que hace el phaser compartible:
    el motor ya calcula el coeficiente por bloque, asi que el adaptador no tiene
    ni que patronizar el barrido ni guardar la fase del LFO. Solo empuja tres
    mandos, y los tres se suavizan porque los tres mueven la senal de golpe:

      - `rate`  cambia el incremento de la fase del LFO. Sin rampa, un salto
                salta la fase, y un salto de fase en un LFO es un clic.
      - `depth` cambia el exponente del barrido, o sea el corte. Sin rampa, el
                notch se teletransporta de un sitio a otro.
      - `feedback` cambia la ganancia del lazo, que es lo mas immediate de los
                tres: el lazo se asienta en un par de muestras.

    LOS MANDOS EN UNIDADES FISICAS, y el mapeo del MS2000 se queda en el MS2000.
    Su `modFxSpeed` es un entero de 0 a 127 que se mapea a
    `0.02 · 750^(n/127)` Hz, o sea un recorrido LOGARITMICO de tres decadas. Una
    fila comun no puede llevar ese mapeo dentro y seguir siendo una fila
    generica, asi que la fila habla en hercios y quien quiera el tacto del MS2000
    mapea en su producto, que es la misma regla que las tablas de 4 pasos de la
    repisa.

    LA TASA DE EMPUJE ES LA DEL MOTOR, no una del adaptador. Se escribe
    `Phaser4::kControlRate` y no un 16, para que las dos cosas que tienen que
    caer juntas no puedan separarse: si el motor recalculara en 16 y el adaptador
    empujara en 32, el motor leeria un mando con hasta 16 muestras de retraso, y
    el efecto de un barrido seria medio bloque de desfase. La unica forma de que
    eso pase es cambiar el numero del motor sin cambiar el del adaptador, y por
    eso el numero vive en el motor y el adaptador lo lee.

    Y LOS TRES MANDOS SE SUAVIZAN A 20 ms, la misma rampa que la repisa, y por
    el mismo motivo de siempre: 20 ms es lo que tardan los envoltorios que estos
    adaptadores sustituyen, y un efecto que se mueve mas rapido que su envoltorio
    suena a que el envoltorio se ha roto. */
class PhaserFx
{
public:
    static constexpr int kNumParams = 3;

    /** Cada cuanto se empuja el mando al motor. NO es un numero propio: es el
        del motor, para que el mando llegue en la misma muestra en la que el
        motor recalcula el coeficiente. */
    static constexpr int kControlRate = Phaser4<4>::kControlRate;

    static const FxParamSpec* specs() noexcept
    {
        // `rate` en hercios, de 0,02 a 15, que es el rango del MS2000 y tambien
        // el del motor. El sesgo de 0,45 da recorrido util abajo: sin el, la
        // mitad baja del recorrido (0,02 a 7,5 Hz, que es donde vive un LFO de
        // barrido) caeria en el 0,20, y con un sesgo de 1,0 la mitad alta
        // —de 7,5 a 15 Hz, que ya se oye como trémolo— caeria en el 0,64. Con
        // 0,45 las dos mitades tienen recorrido.
        //
        // Y LOS TRES VALORES POR DEFECTO NO SON LOS DEL MS2000, que es lo que
        // se pondria a hacer, y el motivo es una puerta del propio modulo: el
        // banco exige que ningun mando por defecto caiga fuera de 0,15 a 0,95 del
        // recorrido normalizado, porque un mando cuyo valor de fabrica esta en
        // el 0,05 del recorrido es un mando que no se puede usar —el producto se
        // abre con el efecto casi en su minimo.
        //
        // Los del MS2000 no lo cumplen. Su velocidad de fabrica son 40/127, que
        // con este rango y este sesgo cae en 0,12 de recorrido; y su
        // realimentacion de fabrica es 0, que es el extremo de verdad.
        //
        // Y ESO NO ES UN DEFECTO DEL MS2000. Alli la realimentacion a cero es lo
        // correcto, porque un phaser no tiene por que resonar al abrirse. Lo
        // que cambia de un sitio a otro es la fila: una fila generica se abre en
        // el sitio donde un usuario la encuentra, y un producto que quiera sus
        // valores de fabrica los pasa por `setParam` en su arranque, que es lo
        // que ya hace SynthEngine con los suyos. La fila no es el producto.
        //
        // Los tres de aqui, y donde caen:
        //
        //     rate      0,60 Hz   ->  0,23 del recorrido
        //     depth     0,50      ->  0,50
        //     feedback  0,25      ->  0,25
        //
        // El 0,25 de realimentacion es un cuarto del tope del motor (0,90), o
        // sea 0,225: se oye el barrido abriéndose sin que la resonacion se
        // lleve el sinal, que es como arranca un phaser.
        static const FxParamSpec table[kNumParams] = {
            { "rate",     0.02f, 15.00f, 0.60f, 0.45f, 0 },  // Hz
            { "depth",    0.00f,  1.00f, 0.50f, 1.00f, 0 },  // exponente del barrido
            { "feedback", 0.00f,  1.00f, 0.25f, 1.00f, 0 }   // 0..1; el tope de 0,90 es del motor
        };
        return table;
    }

    static void* create (double sampleRate) noexcept
    {
        auto* self = new PhaserFx();
        self->prepare (sampleRate);
        return self;
    }

    static void destroy (void* instance) noexcept { delete static_cast<PhaserFx*> (instance); }

    static void reset (void* instance) noexcept
    {
        // El estado del motor Y las rampas, por el mismo motivo que en la
        // repisa: sin lo segundo, el primer bloque despues de un `reset` de
        // host sale con los mandos a medio camino.
        auto* self = static_cast<PhaserFx*> (instance);
        self->engine_.reset();
        self->rate_.jumpToTarget();
        self->depth_.jumpToTarget();
        self->feedback_.jumpToTarget();
        self->aplicaMandos (self->rate_.getTarget(), self->depth_.getTarget(),
                            self->feedback_.getTarget());
    }

    static void setParam (void* instance, int index, float value) noexcept
    {
        static_cast<PhaserFx*> (instance)->setParam (index, value);
    }

    static void setAll (void* instance, const float* v, int count) noexcept
    {
        auto* self = static_cast<PhaserFx*> (instance);
        for (int i = 0; i < count && i < kNumParams; ++i)
            self->setParam (i, v[i]);
        self->asientaMandos();
    }

    static void process (void* instance, const float* inL, const float* inR,
                         float* outL, float* outR, int n) noexcept
    {
        auto* self = static_cast<PhaserFx*> (instance);

        for (int i = 0; i < n; ++i)
        {
            // La rampa avanza SIEMPRE, tambien en las muestras en las que no se
            // empuja nada, por el motivo que ya esta escrito en la repisa: si
            // solo se leyera en las de control, la rampa seria 16 veces mas
            // rapida en un bloque de 16 muestras que en uno de 512.
            const float rate = self->rate_.getNextValue();
            const float depth = self->depth_.getNextValue();
            const float feedback = self->feedback_.getNextValue();

            // Lo que llega al motor es DONDE ESTA LA RAMPA, no su destino. Si se
            // empujara `getTarget()`, el suavizado no haria nada: el motor se
            // comeria el salto entero el primer bloque, que es exactamente el
            // tictac que el suavizado existe para evitar.
            //
            // Y el contador TIENE QUE VOLVER A CERO con un `>=` y no con un
            // `== 0`, que solo es cierto en la primera muestra de la vida del
            // objeto. Con el `== 0` los tres mandos se quedaban clavados en su
            // valor por defecto y moverlos no hacia NADA.
            if (++self->cuenta_ >= kControlRate)
            {
                self->cuenta_ = 0;
                self->aplicaMandos (rate, depth, feedback);
            }

            float l = inL[i];
            float r = inR[i];
            self->engine_.processFrame (l, r);
            outL[i] = l;
            outR[i] = r;
        }
    }

private:
    void prepare (double sampleRate) noexcept
    {
        engine_.prepare (sampleRate);

        // 20 ms, la misma rampa que la repisa.
        rate_.reset (sampleRate, 0.60f);
        depth_.reset (sampleRate, 0.50f);
        feedback_.reset (sampleRate, 0.25f);

        cuenta_ = kControlRate - 1;
        aplicaMandos (rate_.getTarget(), depth_.getTarget(), feedback_.getTarget());
    }

    void setParam (int index, float value) noexcept
    {
        const float p = knobs.to (index, value);

        switch (index)
        {
            case 0: rate_.setTarget (p);     break;
            case 1: depth_.setTarget (p);    break;
            case 2: feedback_.setTarget (p); break;
            default: break;
        }
    }


    /** Asienta las rampas en su destino.

        Y SOLO desde `setAll`, que es la via de CARGA DE UN PRESET. Un preset es
        una discontinuidad por naturaleza: el host lo carga y espera oirlo, no
        ver un fundido desde los valores por defecto de la fila. `setParam` sigue
        rampando, porque esa es la via de la automatizacion y de un panel.

        Sin esto, un objeto recien creado con los mandos puestos por `setAll`
        salia rampando desde el valor por defecto, y despues de un `reset` salia
        ya en el valor: o sea que cargar un preset y reiniciar el host sonaban
        distinto, que es el tipo de cosa que solo se nota en una comparacion A/B
        y que nadie encuentra porque "los dos suenan bien".
    */
    void asientaMandos() noexcept
    {
        rate_.jumpToTarget();
        depth_.jumpToTarget();
        feedback_.jumpToTarget();
    }

    /** Empuja al motor la posicion ACTUAL de las rampas. */
    void aplicaMandos (float rate, float depth, float feedback) noexcept
    {
        engine_.setRateHz (rate);
        engine_.setDepth (depth);
        engine_.setFeedback (feedback);
    }

    Phaser4<4> engine_;
    Knobs knobs { specs(), kNumParams };
    SmoothedKnob rate_;
    SmoothedKnob depth_;
    SmoothedKnob feedback_;

    /** Cuenta las muestras desde el ultimo empuje, y NO se reinicia por bloque.
        Ver la nota de la repisa: reiniciarla haria que el punto de empuje
        dependiera del tamano de bloque del host. */
    int cuenta_ = 0;
};

} // namespace abd::dsp::adapters

