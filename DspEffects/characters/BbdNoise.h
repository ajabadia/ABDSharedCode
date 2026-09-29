/*
  ==============================================================================

    BbdNoise.h
    La degradacion que ensucia un coro BBD: fuga de las lineas, siseo del
    amplificador, y los clics que aparecen cuando el reloj acelera y vacia el
    deposito antes de tiempo.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Port de los generadores de
    ABDJUNiO601/Source/Synth/ChorusBBD.h (AnalogFloorNoise, LeakNoise, BBDClick).

    POR QUE ESTO ES UNA ETAPA Y NO PARTE DEL MOTOR. El motor `JunoBBD` es la
    maquina: filtros, lineas, reloj, mezclador. Esto es lo que le pasa a la
    maquina cuando lleva cuatro años en un armario con la tapa cerrada. Es
    estado (los generadores son recurrentes), asi que se inyecta como objeto, y
    se resuelve en compilacion. Con `BbdNullStage` el motor suena como un coro
    digital limpio, y se puede ESCUCHAR la diferencia entre el algoritmo y el
    chip, que es la unica forma de saber que parte de este fichero vale la pena.

    LOS TRES GENERADORES, y por que tres.

      1. SISEO DE FONDO (rosa). El amplificador del chip tiene ruido propio. Es
         rosa (6 polos) y con un realzador de agudas, porque el ruido de un
         transistor no es plano y el shelf es el "charlatán" del grabador.

      2. FUGA. Las lineas del BBD no son memoria perfecta: se les escapa carga a
         velocidad de reloj, y CUANTO mas escapa depende de DONDE esta el LFO.
         En el punto mas rapido del barrido las dos lineas estan mas frias, y por
         eso el motor les pasa la profundidad como el motor `leakAmount`.

      3. CLICS. Cuando el reloj acelera de golpe, vacia el deposito antes de
         llenarlo, y eso suena como un clic. El original genera DOS por canal
         (uno por el flanco de subida y otro por el de bajada, con signo
         contrario) porque el disparo es asimetrico. Cada uno tiene un anillo
         resonante en el motor que lo alarga.

    LOS SEMILLAS, Y UNA CURIOSIDAD QUE SE CONSERVA A PROPOSITO. Las dos lineas
    de fuga usan el MISMO generador con la MISMA semilla, o sea que generan
    ruido IDENTICO. En el original eso es lo que pasa (los dos `LeakNoise` se
    construyen con el mismo 0xDEADBEEF y no se vuelven a sembrar), y es
     probablemente un descuido: dos lineas de fuga con el mismo ruido son una
     imagen especular y no decorrelacionan nada. Se conserva porque cambiarlo
     es un cambio de sonido, y la nota esta para que quien lo encuentre sepa
     que es un descuido CONSERVADO a proposito, no uno olvidado.

    EL DESGASTE SE APLICA UNA VEZ aqui y DOS en el original, y no es un
    descuido de esta transcripcion: el original multiplica por `hissMultiplier`
    al generar el siseo de fondo y otra vez al cuadrar el conjunto, con lo que
    con desgaste 2.0 el siseo se va al 4x. Es un error de acumulacion y se ve.
    Corregirlo romperia la paridad a 0 ulps con el efecto ya publicado, y la
    paridad gana, asi que el doble se conserva. El motor es quien lo aplica dos
    veces (ver `JunoBBD.h`, "el desgaste se aplica DOS VEZES"), porque el
    desdoblar en dos sitios dentro de la etapa haria que el anillo, que solo lo
    lleva una vez, dejara de coincidir con la referencia.

  ==============================================================================
*/

#pragma once

#include "DspEffects/JunoBBD.h"
#include "DspEffects/profiles/JunoBbdProfile.h"

#include <cstdint>

namespace abd::dsp
{

namespace bbdnoise_detail
{

/**
    Un LCG de 32 bits. NO es un `rand()` ni un `juce::Random`: es el mismo
    generador del original (`semilla * 196314165 + 907633515`), porque el ruido
    del BBD tiene que ser el MISMO ruido, muestra a muestra, y un PRNG de la
    plataforma no lo garantiza.
*/
inline float nextWhite (std::uint32_t& seed) noexcept
{
    seed = seed * 196314165u + 907633515u;
    return 2.0f * static_cast<float> (seed) / static_cast<float> (0xFFFFFFFFu) - 1.0f;
}

/** Siseo de fondo del amplificador: rosa de 6 polos, con realzador de agudas. */
class FloorNoise
{
public:
    void prepare (double sampleRate, float lpCutoffHz, float shelfHz, float shelfDb) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float fc = lpCutoffHz < sr * 0.45f ? lpCutoffHz : sr * 0.45f;
        lpCoeff_ = 1.0f - junobbd_detail::expDet (-2.0f * dspmath_detail::kPi * fc / sr);

        const float sc = shelfHz < sr * 0.45f ? shelfHz : sr * 0.45f;
        shelfCoeff_ = 1.0f - junobbd_detail::expDet (-2.0f * dspmath_detail::kPi * sc / sr);
        shelfGain_  = pow (10.0f, shelfDb / 20.0f) - 1.0f;

        pink_ = true;
    }

    void setSeed (std::uint32_t s) noexcept { seed_ = s; }

    void reset() noexcept
    {
        seed_ = 0x12345678u;
        lpState_ = shelfState_ = 0.0f;
        p0_ = p1_ = p2_ = p3_ = p4_ = p5_ = p6_ = 0.0f;
    }

    float processSample (float whiteToPink) noexcept
    {
        const float white = nextWhite (seed_);

        float out;
        if (pink_)
        {
            p0_ = 0.99886f * p0_ + white * 0.0555179f;
            p1_ = 0.99332f * p1_ + white * 0.0750759f;
            p2_ = 0.96900f * p2_ + white * 0.1538520f;
            p3_ = 0.86650f * p3_ + white * 0.3104856f;
            p4_ = 0.55000f * p4_ + white * 0.5329522f;
            p5_ = -0.7616f * p5_ - white * 0.0168980f;

            const float pink = p0_ + p1_ + p2_ + p3_ + p4_ + p5_ + p6_ + white * 0.5362f;
            p6_ = white * 0.115926f;

            out = pink * 0.11f * (1.0f - whiteToPink) + white * whiteToPink;
        }
        else
        {
            lpState_ += lpCoeff_ * (white - lpState_);
            out = lpState_;
        }

        if (shelfGain_ != 0.0f)
        {
            shelfState_ += shelfCoeff_ * (out - shelfState_);
            return out + shelfGain_ * (out - shelfState_);
        }

        return out;
    }

private:
    std::uint32_t seed_ = 0x12345678u;
    bool pink_ = true;
    float lpCoeff_ = 0.0f, lpState_ = 0.0f;
    float shelfCoeff_ = 0.0f, shelfGain_ = 0.0f, shelfState_ = 0.0f;
    float p0_ = 0.0f, p1_ = 0.0f, p2_ = 0.0f, p3_ = 0.0f;
    float p4_ = 0.0f, p5_ = 0.0f, p6_ = 0.0f;
};

/** La fuga de las lineas: ruido de banda estrecha, y CUANTA depends del LFO. */
class LeakNoise
{
public:
    void prepare (double sampleRate, float hpHz) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        hpCoeff_ = 1.0f - junobbd_detail::expDet (-2.0f * dspmath_detail::kPi * hpHz / sr);
        lpCoeff_ = 1.0f - junobbd_detail::expDet (-2.0f * dspmath_detail::kPi * 4000.0f / sr);
    }

    void reset() noexcept
    {
        // Las DOS lineas comparten semilla: es lo que hace el original, y son
        // ruido especular. Ver la nota de la cabecera del fichero.
        seed_ = 0xDEADBEEFu;
        hpState_ = lpState_ = 0.0f;
    }

    float processSample() noexcept
    {
        const float white = nextWhite (seed_);
        hpState_ += hpCoeff_ * (white - hpState_);
        const float hp = white - hpState_;
        lpState_ += lpCoeff_ * (hp - lpState_);
        return lpState_;
    }

private:
    std::uint32_t seed_ = 0xDEADBEEFu;
    float hpCoeff_ = 0.0f, lpCoeff_ = 0.0f;
    float hpState_ = 0.0f, lpState_ = 0.0f;
};

/**
    Un clic de deposito vaciado. Se dispara cuando el LFO pasa de golpe el
    umbral, y su forma es asimetrica: un lobulo fuerte y rapido, y otro mas
    flojo y largo. Por eso el segundo canal del par va con signo contrario: son
    los dos flancos del mismo ciclo de reloj.
*/
class BbdClick
{
public:
    void prepare (double sampleRate, float threshold, float durationMs) noexcept
    {
        threshold_ = threshold;
        duration_  = static_cast<int> (durationMs * 0.001f * static_cast<float> (sampleRate));
    }

    void reset() noexcept { counter_ = -1; wasInZone_ = false; }
    void suppress() noexcept { counter_ = -1; wasInZone_ = true; }

    float processSample (float lfoForLine) noexcept
    {
        const bool inZone = lfoForLine > threshold_;

        if (inZone && ! wasInZone_) counter_ = 0;
        wasInZone_ = inZone;

        if (counter_ < 0 || counter_ >= duration_)
        {
            counter_ = -1;
            return 0.0f;
        }

        constexpr float kAsymPoint       = 0.20f;
        constexpr float kSecondLobeScale = 0.85f;
        constexpr float kLeadDecayRate   = 4.0f;
        constexpr float kTrailDecayRate  = 8.0f;

        const float t = static_cast<float> (counter_) / static_cast<float> (duration_);
        ++counter_;

        if (t < kAsymPoint)
        {
            const float u = t / kAsymPoint;
            return -junobbd_detail::expDet (-kLeadDecayRate * u) * sin (dspmath_detail::kPi * u);
        }

        const float u = (t - kAsymPoint) / (1.0f - kAsymPoint);
        return kSecondLobeScale * junobbd_detail::expDet (-kTrailDecayRate * u)
                                  * sin (dspmath_detail::kPi * u);
    }

private:
    int counter_ = -1;
    int duration_ = 0;
    bool wasInZone_ = false;
    float threshold_ = 0.95f;
};

} // namespace bbdnoise_detail

//==============================================================================
/**
    La etapa de caracter del coro BBD del Juno: siseo, fuga y clics.

    Estado POR CANAL, y el motivo por el que la API es `processLeft` /
    `processRight` y no un `processSample` que alterna: las dos lineas tienen
    generadores DISTINTOS con semillas distintas, y una alternancia implicita
    haria que el estado de un canal dependiera de si antes se llamo a un canal o
    al otro. Con dos entradas explicitas, "el canal L avanza una vez por
    muestra" es una propiedad estructural y se puede comprobar: mono y
    estereo-izquierdo tienen que dar IDENTICOS bit a bit. Ese es el mismo test que
    cayo en el eco multi-cabezal, y esta escrito para que no vuelva a caer.
*/
class BbdNoiseStage
{
public:
    void setProfile (const JunoBbdProfile& p) noexcept
    {
        profile_ = &p;
        prepared_ = false;
    }

    void prepareSampleRate (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        prepared_   = false;
    }

    double getSampleRate() const noexcept { return sampleRate_; }

    /** 0 = rosa, 1 = blanco. Es del panel, no de la maquina, asi que NO esta en
        el perfil: el perfil es `constexpr` y esto lo mueve el usuario. */
    void setHissColour (float c) noexcept { hissColor_ = jlimit (0.0f, 1.0f, c); }

    void reset() noexcept
    {
        // Las semillas de las dos lineas de fondo son DISTINTAS aqui, y las dos
        // lineas de fuga arrancan con la misma. Ver la cabecera del fichero.
        floorL_.reset(); floorL_.setSeed (0x12345678u);
        floorR_.reset(); floorR_.setSeed (0x87654321u);
        leakL_.reset();
        leakR_.reset();
        clickPriL_.reset();
        clickPriR_.reset();
        clickSloL_.reset();
        clickSloR_.reset();
    }

    /** Al ENCENDER un modo: un BBD que llevaba apagado no arrastra clics. */
    void suppressClicks() noexcept
    {
        clickPriL_.suppress();
        clickPriR_.suppress();
        clickSloL_.suppress();
        clickSloR_.suppress();
    }

    /** Entrada conforme al contrato de `EffectPolicy.h`; aqui no colorea nada. */
    float processSample (float, float = 0.0f, float = 0.0f) noexcept { return 0.0f; }

    /**
        `leakAmount` es la profundidad del LFO (CUANTA fuga hay en este punto),
        `clickScale` es el barrido relativo (un clic solo puede ser tan fuerte
        como el barrido que lo ha provocado) y `lfo` decide si el deposito se
        acaba de vaciar.

        `baseNoiseGain` y `hissMultiplier` los aplica la ETAPA y no el motor, y
        es un detalle que cuesta un dia si se pierde: el nivel de siseo del panel
        escala SOLO el ruido de fondo, no la fuga ni los clics, que ya traen sus
        propias constantes. Si el motor multiplica el conjunto, el siseo de fondo
        se multiplica dos veces mas de lo que debe. Con el barrido del LFO cerca
        del minimo (que es cuando la fuga manda) se oye; con el barrido alto
        (cuando manda el siseo) el error es de 1e-5 y no se oye, asi que un
        test de "suena parecido" no lo caza: solo lo caza la paridad a 0 ulps.
    */
    BbdStageOutput processLeft (float leakAmount, float lfo, float clickScale,
                                float baseNoiseGain, float hissMultiplier) noexcept
    {
        ensurePrepared();
        return one (floorL_, leakL_, clickPriL_, clickSloL_, leakAmount, -lfo, lfo,
                    clickScale, baseNoiseGain, hissMultiplier);
    }

    BbdStageOutput processRight (float leakAmount, float lfo, float clickScale,
                                 float baseNoiseGain, float hissMultiplier) noexcept
    {
        ensurePrepared();
        return one (floorR_, leakR_, clickPriR_, clickSloR_, leakAmount, lfo, -lfo,
                    clickScale, baseNoiseGain, hissMultiplier);
    }

private:
    BbdStageOutput one (bbdnoise_detail::FloorNoise&  floorN,
                        bbdnoise_detail::LeakNoise&   leakN,
                        bbdnoise_detail::BbdClick&    pri,
                        bbdnoise_detail::BbdClick&    slo,
                        float leakAmount,
                        float priLfo,
                        float sloLfo,
                        float clickScale,
                        float baseNoiseGain,
                        float hissMultiplier) noexcept
    {
        const JunoBbdProfile& p = *profile_;

        // El orden importa: es el orden del original, y el estado es recurrente.
        const float leakNoise = leakN.processSample();
        const float floor     = floorN.processSample (hissColor_);

        const float priValue = pri.processSample (priLfo) * p.clickGain * clickScale;
        const float sloValue = slo.processSample (sloLfo) * p.slowClickGain * clickScale;

        // El desgaste se aplica al conjunto, y el nivel de siseo SOLO al fondo.
        // Ver el comentario de `processLeft`.
        const float wetPink = floor * baseNoiseGain * hissMultiplier;

        BbdStageOutput out;
        out.click    = priValue;                 // el anillo lo usa sin escalar
        out.injected = (wetPink
                      + leakNoise * p.leakGain * leakAmount
                      + priValue
                      - sloValue) * hissMultiplier;

        return out;
    }

    void ensurePrepared()
    {
        if (prepared_ || profile_ == nullptr)
            return;

        const JunoBbdProfile& p = *profile_;

        floorL_.prepare (sampleRate_, p.noiseLpCutoffHz, p.noiseShelfHz, p.noiseShelfDb);
        floorR_.prepare (sampleRate_, p.noiseLpCutoffHz, p.noiseShelfHz, p.noiseShelfDb);
        hissColor_ = p.hissColor;
        leakL_.prepare  (sampleRate_, p.leakHpHz);
        leakR_.prepare  (sampleRate_, p.leakHpHz);

        clickPriL_.prepare (sampleRate_, p.clickThreshold, p.clickDurationMs);
        clickPriR_.prepare (sampleRate_, p.clickThreshold, p.clickDurationMs);
        clickSloL_.prepare (sampleRate_, p.clickThreshold, p.clickDurationMs);
        clickSloR_.prepare (sampleRate_, p.clickThreshold, p.clickDurationMs);

        prepared_ = true;
    }

    const JunoBbdProfile* profile_ = nullptr;
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    float hissColor_ = 0.4f;

    bbdnoise_detail::FloorNoise floorL_, floorR_;
    bbdnoise_detail::LeakNoise  leakL_, leakR_;
    bbdnoise_detail::BbdClick   clickPriL_, clickPriR_;
    bbdnoise_detail::BbdClick   clickSloL_, clickSloR_;
};

} // namespace abd::dsp
