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
        self->smoother_.reset (sampleRate, 0.005);
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

} // namespace abd::dsp::adapters
