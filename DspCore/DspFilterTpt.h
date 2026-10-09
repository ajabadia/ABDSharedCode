/*
  ==============================================================================

    DspFilterTpt.h
    El miembro TPT de la familia de filtros: un filtro de VARIABLE DE ESTADO con
    los tres taps (paso bajo, paso alto y banda) del mismo par de estados
    (namespace abd::dsp).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspCore). La
    familia a la que pertenece vive en DspFilterFamily.h, que va ANTES que este
    fichero en DspCore.h porque aqui se usan sus modos y su idioma de parametros.

    QUE ES. La forma TPT de Zavalishin (la "transformada que conserva la
    topologia") resuelta en un paso, con los tres taps que un filtro de variable
    de estado da gratis: la banda es v1, el paso bajo es v2 y el paso alto es lo
    que queda, x - k*v1 - v2. Nada mas: ni tabla, ni iteracion, ni saturacion.

    POR QUE REPITE LAS ECUACIONES DE DspResonantFilter.h, Y POR QUE ESO ES
    CORRECTO. Aqui esta escrita la misma actualizacion de la etapa compartida, y
    hay un test que lo comprueba a 1e-12 muestra a muestra. La repeticion es una
    DECISION, no un descuido, y tiene dos motivos:

      - La etapa compartida (ResonantFilterStage) tiene una cosa que este NO
        tiene y no debe tener: la AGC, que hace que su auto-oscilacion se ASIENTE
        en un nivel en vez de crecer. Tocar su lazo para sacarle los tres taps
        seria cambiar el comportamiento de una pieza que los productos ya usan, y
        eso la tarea lo prohibe.
      - La familia necesita los TRES taps del mismo par de estados. La etapa
        compartida calcula la banda por dentro (v1) y solo la publica como
        potencia, porque su consumidor es una seccion de paso bajo. Sacarla desde
        fuera obligaria a reconstruirla, y una banda reconstruida es otro filtro.

    El precio de repetir se paga con un test que compara las dos salidas de paso
    bajo a resonancia 0, donde la AGC de la etapa esta apagada (su
    setLevelControl(0)) y las dos tienen que dar LO MISMO. Si la formulacion se
    separa, el test cae: la repeticion queda VIGILADA, que es lo unico que hace
    honesta una repeticion.

    EL MODO ES UN SERVICIO DE ESTE MIEMBRO, NO DEL CONTRATO. setMode devuelve
    `false` cuando no puede honrar el modo pedido y deja el que tenia: un filtro
    que dice que si y luego hace paso bajo es peor que uno que dice que no, porque
    el fallo se oye y no se lee. supportsMode se puede preguntar ANTES.

    CONTRATO DE LLAMADA. prepare() una vez (y al cambiar la frecuencia de
    muestreo), reset() para que una nota no empiece con la cola de la anterior, y
    despues processSample() por muestra. Todo noexcept, sin asignaciones y sin
    ramificar en la cadena de realimentacion (la rama del modo es una eleccion de
    tap, no del lazo).

  ==============================================================================
*/

#pragma once

// La familia ANTES que el sustrato, y en este orden a proposito: el modo y las
// curvas de la familia son el vocabulario de este fichero, y DspCore.h incluye a
// este miembro al final (de modo que entrar por DspCore.h es entrar con la familia
// ya leida). Ver la nota de orden de cabeceras en DspFilterFamily.h.
#include "DspCore.h"
#include "DspFilterFamily.h"

#include <algorithm>
#include <cmath>

namespace abd::dsp
{

//==============================================================================
/** Un filtro de variable de estado TPT con los tres taps, miembro de la familia.

    A resonancia 0 es el mismo filtro que ResonantFilterStage sin su control de
    nivel (misma formulacion, misma Q y mismo `g`), y subiendo la resonancia
    sube la Q de forma exponencial hasta 1/sqrt(2) * (20 / (1/sqrt(2))), o sea 20
    en el tope, que es el borde del recorrido de la familia (ver
    DspFilterFamily.h). NO se auto-oscila: para eso esta la etapa compartida, cuya
    AGC limita la oscilacion, o el miembro de ecuacion, cuyo tanh la limita.

    @tags{Audio}
*/
class FilterTpt
{
public:
    //==========================================================================
    /** De que esta hecho, para el panel y el diagnostico. */
    static constexpr FilterKind kind = FilterKind::Tpt;

    /** Los tres modos: son tres taps del MISMO par de estados, asi que no hay
        ninguno que este miembro no sepa dar. */
    static bool supportsMode(FilterMode mode) noexcept
    {
        ignoreUnused(mode);
        return true;
    }

    //==========================================================================
    /** La frecuencia de muestreo. Sin esto no hay `g` que valga; hay que volver a
        llamarla si cambia. Como la etapa compartida, deja el corte pendiente
        (`-1`) para forzar el recalculo de `g` en la primera muestra. */
    void prepare(double sampleRate) noexcept
    {
        sampleRateHz = (sampleRate > 0.0) ? sampleRate : 44100.0;
        cutoffHz     = -1.0;
        setResonance(resonance);
    }

    /** Olvida el pasado. No toca ni el corte ni la resonancia ni el modo: son
        ajustes, no estado. */
    void reset() noexcept
    {
        integrator1 = 0.0;
        integrator2 = 0.0;
    }

    /** La frecuencia de corte en Hz, recortada por arriba para que `g` no se vaya
        con la frecuencia de muestreo. El estado se conserva, asi que mover el
        corte no hace click. */
    void setCutoff(double hz) noexcept
    {
        const auto nyquist = sampleRateHz * 0.5;
        const auto limit   = nyquist * maxCutoffFraction;
        const auto wanted  = hz < minCutoffHz ? minCutoffHz : (hz > limit ? limit : hz);

        if (wanted == cutoffHz)
            return;

        cutoffHz = wanted;
        updateCoefficients();
    }

    /** La resonancia 0..1 de la familia: 0 sin realce (Q = filterMinQ) y 1 en el
        borde (Q = filterMaxQ). */
    void setResonance(double amount) noexcept
    {
        resonance = filterClamp01(amount);
        updateDamping();
    }

    /** El modo, si este miembro lo sabe hacer. Devuelve `false` y NO cambia nada
        cuando no; preguntar antes con supportsMode() es lo que evita el fallo
        silencioso. */
    bool setMode(FilterMode mode) noexcept
    {
        if (!supportsMode(mode))
            return false;

        currentMode = mode;
        return true;
    }

    //==========================================================================
    /** Una muestra. Devuelve el tap del modo que este puesto. */
    double processSample(double input) noexcept
    {
        if (cutoffHz <= 0.0)
            setCutoff(minCutoffHz);

        // La formulacion es la de la etapa compartida, con el mismo orden de
        // operaciones: es lo que hace que las dos salidas de paso bajo coincidan
        // (hay un test que lo mide, ver la cabecera del fichero).
        const auto inv = 1.0 / (1.0 + g * (g + k));
        const auto a1  = inv;
        const auto a2  = g * a1;
        const auto a3  = g * a2;

        const auto v3 = input - integrator2;
        const auto v1 = a1 * integrator1 + a2 * v3;
        const auto v2 = integrator2 + a2 * integrator1 + a3 * v3;

        integrator1 = 2.0 * v1 - integrator1;
        integrator2 = 2.0 * v2 - integrator2;

        // Una voz que se apaga en silencio se va a denormales, que en x86 son
        // lentos. El suelo es donde ya no se oye nada.
        if (std::abs(integrator1) < denormalFloor)
            integrator1 = 0.0;

        if (std::abs(integrator2) < denormalFloor)
            integrator2 = 0.0;

        switch (currentMode)
        {
            case FilterMode::HighPass:
                return input - k * v1 - v2;

            case FilterMode::BandPass:
                return v1;

            case FilterMode::LowPass:
            default:
                return v2;
        }
    }

    //==========================================================================
    /** El corte que se esta usando, ya recortado, en Hz. */
    double getCutoff() const noexcept { return cutoffHz; }

    /** La resonancia que se esta usando, 0..1. */
    double getResonance() const noexcept { return resonance; }

    /** El modo que esta puesto. */
    FilterMode getMode() const noexcept { return currentMode; }

    /** La Q que le corresponde a la resonancia actual en el idioma de la familia. */
    double getQ() const noexcept { return filterResonanceToQ(resonance); }

    //==========================================================================
    /** La fraccion de Nyquist a la que se recorta el corte, para que `g` no se
        vaya con la frecuencia de muestreo. El mismo numero que la etapa
        compartida: si no, a corte alto las dos formulaciones dejarian de
        coincidir justo donde el test las compara. */
    static constexpr double maxCutoffFraction = 0.49;

    /** El corte minimo que este miembro admite, en Hz. */
    static constexpr double minCutoffHz = 10.0;

    /** Por debajo de este valor una muestra se considera denormal y se anula. */
    static constexpr double denormalFloor = 1.0e-20;

private:
    //==========================================================================
    /** Los coeficientes que dependen del corte. `g` sale de `tan(pi*f/fs)`, y como
        el sustrato no trae `tan` en sus deterministas, sale de `sin/cos` de las
        que si trae: mismo determinismo y una division mas. Es la misma cuenta y
        el mismo orden que en la etapa compartida. */
    void updateCoefficients() noexcept
    {
        const auto pi    = static_cast<float>(MathConstants<double>::pi);
        const auto angle = static_cast<float>(static_cast<double>(pi) * cutoffHz / sampleRateHz);

        const auto s = static_cast<double>(sin(angle));
        const auto c = static_cast<double>(cos(angle));

        g = s / (std::abs(c) < 1.0e-6 ? 1.0e-6 : c);
    }

    /** El amortiguamiento, que es el inverso de la Q: `k = 1/Q`. A resonancia 0 da
        exactamente el 1/baseQ de la etapa compartida, porque filterMinQ ES baseQ
        (ver DspFilterFamily.h). */
    void updateDamping() noexcept
    {
        k = 1.0 / filterResonanceToQ(resonance);
    }

    //==========================================================================
    double sampleRateHz    = 44100.0;
    double cutoffHz        = -1.0;
    double resonance       = 0.0;
    FilterMode currentMode = FilterMode::LowPass;

    double g           = 0.0;                // tan (pi * f / fs)
    double k           = 1.4142135623730951; // 1 / filterMinQ
    double integrator1 = 0.0;                // estado del TPT
    double integrator2 = 0.0;
};

} // namespace abd::dsp
