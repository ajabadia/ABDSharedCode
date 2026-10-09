/*
  ==============================================================================

    LutFilter.h
    El miembro LUT de la familia de filtros: el corte y la Q salen de una TABLA
    MEDIDA en un banco de pruebas, y el nucleo que los toca es el TPT compartido
    (namespace abd::lutdsp).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::LutDSP), que es
    el modulo de las tablas medidas (LutEvaluatorSimd, AnalogLutFilterModule, los
    modelos de models/). Depende de ABDShared::DspCore, que es donde viven el
    contrato de la familia y el nucleo TPT; al reves no, y por eso el miembro LUT
    no puede vivir en DspCore.

    QUE ES, Y QUE NO ES. No es un filtro nuevo: es la MISMA variable de estado TPT
    de la familia, a la que se le da el corte y la Q que dice una tabla en vez de
    los que salen de la curva analitica del mando. Por eso no repite las
    ecuaciones (usaria las del nucleo compartido) y por eso lo que aporta es el
    MAPEO, que es justo lo que un banco de pruebas mide: que el mando de corte del
    aparato real no esta donde dice su serigrafia, y que su Q sube como sube.

    POR QUE EL MAPEO ES LO UNICO QUE HACE FALTA QUE HAGA. Una tabla medida es una
    correccion del MANDO, no de la matematica del filtro: los 20 puntos de "a
    cuantos Hz corta de verdad esta posicion del mando" no dicen nada sobre como
    resolver un paso bajo, y la Q medida es una Q. Lo que la tabla si trae y este
    miembro NO usa es la no linealidad (thd_percent, y la dispersion sigma): un
    filtro lineal no puede reproducir una distorsion medida, y fingir que si
    (aplicando un tanh de ganancia inventada, por ejemplo) seria sonar distinto sin
    que nadie sepa si suena bien. Se publican tal cual (getMeasuredThdPercent,
    getMeasuredDriftHz) para que un panel las muestre y para que el dia que alguien
    quiera un miembro NO lineal de verdad tenga el dato de donde tirar.

    LA TABLA, Y COMO SE LEE. Es la de AbdBatchedPoint que exporta ABDAudioLab
    (LutEvaluatorSimd), con la geometria que tiene el fichero generado:

      - `columns` puntos de CORTE por fila (la columna avanza con p1, la posicion
        del mando de corte), y
      - `rows` filas de RESONANCIA (la fila avanza con p2).

    Y se indexa en el orden en que el generador escribe: fila por fila de
    resonancia, y dentro de cada fila el corte creciendo. El miembro de models/ que
    hay en el repo (lut_mock_va_synth_moog_ladder) es 8 x 4: ocho posiciones de
    corte y cuatro de resonancia. La interpolacion es BILINEAL, que es lo que el
    propio modelo promete (una rejilla de medidas, no una rejilla de escalones), y
    la posicion del mando sale de la curva de corte de la FAMILIA
    (normalizedFromCutoffHz): la tabla esta indexada por posicion de mando y el
    consumidor pide Hz, asi que la conversion la hace la familia y no cada miembro.

    EL CORTE QUE SE PUBLICA, Y POR QUE HAY TRES. Porque son tres numeros distintos
    y confundirlos es como se miente sin querer:

      - getRequestedCutoff()      lo que pidio el panel, en Hz.
      - getMeasuredCutoffHz()     lo que la tabla dice que corta el aparato real en
                                  esa posicion (el dato del banco de pruebas).
      - getCutoff()               lo que el filtro esta usando AHORA, que es el
                                  medido ya recortado por Nyquist: un aparato que
                                  corta a 20 kHz no corta a 20 kHz a 44,1 kHz, y
                                  el miembro que dijera 20 kHz estaria describiendo
                                  la tabla y no el filtro que tiene delante.

    LO QUE NO TRAE, EN VOZ ALTA. Una tabla VACIA no es un error: es un miembro que
    se comporta como el analitico de la familia (curva de mando y Q exponencial),
    porque es lo mejor que se puede hacer sin medida. Se distingue con hasTable(),
    y el dia que alguien quiera que una tabla vacia calle en vez de aproximar, es
    una linea y una decision, no un accidente.

  ==============================================================================
*/

#pragma once

#include "LutEvaluatorSimd.h"

#include "DspCore/DspFilterFamily.h"
#include "DspCore/DspFilterTpt.h"

#include <cstddef>

namespace abd::lutdsp
{

//==============================================================================
/** El filtro de la familia que toma su corte y su Q de una tabla medida.

    Cumple el contrato de abd::dsp (ver DspCore/DspFilterFamily.h): prepare,
    reset, setCutoff en Hz, setResonance 0..1 y processSample, con el mismo
    idioma de parametros que los demas miembros, de modo que un synth pueda
    cambiar este filtro por el TPT o por el de ecuacion sin tocar su voz.

    @tags{Audio}
*/
class FilterLut
{
public:
    //==========================================================================
    /** De que esta hecho, para el panel y el diagnostico. */
    static constexpr abd::dsp::FilterKind kind = abd::dsp::FilterKind::Lut;

    /** Solo paso bajo: la tabla mide una escalera de 24 dB, que es un paso bajo.
        Un paso alto "medido" por la misma tabla seria una invencion. */
    static bool supportsMode(abd::dsp::FilterMode mode) noexcept
    {
        return mode == abd::dsp::FilterMode::LowPass;
    }

    FilterLut() noexcept
    {
        updateFromTable();
    }

    //==========================================================================
    /** Carga la tabla medida. `sizeP1` es el numero de puntos de corte por fila
        (columnas) y `sizeP2` el numero de filas de resonancia; el orden de los
        puntos es el del fichero generado (fila por fila).

        Devuelve `false` y deja el miembro SIN tabla (comportandose como el
        analitico de la familia) si la tabla no puede leerse: una rejilla de menos
        de dos puntos por eje no tiene nada que interpolar, y aceptarla seria
        dividir por cero en la primera consulta. El puntero NO se copia: la tabla
        tiene que vivir tanto como el filtro, que es lo que hace un modelo de
        models/ (`inline constexpr`). */
    bool loadTable(const AbdBatchedPoint* points, int sizeP1, int sizeP2) noexcept
    {
        if (points == nullptr || sizeP1 < 2 || sizeP2 < 2)
        {
            tablePoints = nullptr;
            columns     = 0;
            rows        = 0;
            updateFromTable();
            return false;
        }

        tablePoints = points;
        columns     = sizeP1;
        rows        = sizeP2;
        updateFromTable();
        return true;
    }

    //==========================================================================
    /** La frecuencia de muestreo del nucleo. */
    void prepare(double sampleRate) noexcept
    {
        core.prepare(sampleRate);
        applyToCore();
    }

    /** Olvida el pasado. No toca corte, resonancia ni tabla. */
    void reset() noexcept
    {
        core.reset();
    }

    /** El corte pedido, en Hz. Se traduce a posicion de mando con la curva de la
        familia y de ahi sale el corte MEDIDO que se le da al nucleo. */
    void setCutoff(double hz) noexcept
    {
        requestedHz = hz;
        updateFromTable();
    }

    /** La resonancia 0..1 de la familia. Lo que la tabla aporta aqui es la Q
        medida para esa posicion; el mando sigue siendo 0..1. */
    void setResonance(double amount) noexcept
    {
        resonance = abd::dsp::filterClamp01(amount);
        updateFromTable();
    }

    /** El modo, si este miembro lo sabe hacer. Devuelve `false` sin cambiar nada
        cuando no. */
    bool setMode(abd::dsp::FilterMode mode) noexcept
    {
        if (!supportsMode(mode))
            return false;

        return core.setMode(mode);
    }

    //==========================================================================
    /** Una muestra, por el nucleo compartido. */
    double processSample(double input) noexcept
    {
        return core.processSample(input);
    }

    //==========================================================================
    /** El corte que el filtro esta usando AHORA, en Hz: el medido, ya recortado
        por Nyquist. Es el numero que describe al filtro y no a la tabla. */
    double getCutoff() const noexcept { return core.getCutoff(); }

    /** La resonancia que se esta usando, 0..1 (el mando de la familia). */
    double getResonance() const noexcept { return resonance; }

    /** El modo que esta puesto. */
    abd::dsp::FilterMode getMode() const noexcept { return core.getMode(); }

    /** Lo que pidio el panel, en Hz, antes de pasar por la tabla. */
    double getRequestedCutoff() const noexcept { return requestedHz; }

    /** Lo que la tabla dice que corta el aparato en esa posicion, en Hz, tal cual
        lo dice (sin recortar por Nyquist). Sin tabla, el corte pedido. */
    double getMeasuredCutoffHz() const noexcept { return measuredHz; }

    /** La Q que la tabla mide en esa posicion. Sin tabla, la Q analitica de la
        familia para la resonancia actual. */
    double getMeasuredQ() const noexcept { return measuredQ; }

    /** La dispersion que la tabla mide (desviacion tipica del corte, en Hz). Este
        miembro NO la usa: se publica para un panel y para un futuro miembro no
        lineal que quiera de donde tirar (ver la cabecera del fichero). */
    double getMeasuredDriftHz() const noexcept { return measuredDriftHz; }

    /** La distorsion armonica que la tabla mide, en % (no usada tampoco). */
    double getMeasuredThdPercent() const noexcept { return measuredThdPercent; }

    /** Si hay tabla cargada. Sin ella el miembro trabaja con la curva analitica de
        la familia. */
    bool hasTable() const noexcept { return tablePoints != nullptr; }

    /** La geometria de la tabla cargada, para un panel o un test. */
    int getTableColumns() const noexcept { return columns; }
    int getTableRows() const noexcept { return rows; }

private:
    //==========================================================================
    /** Consulta la tabla y deja al nucleo con el corte y la Q medidos. */
    void updateFromTable() noexcept
    {
        if (tablePoints == nullptr)
        {
            // Sin medida, la mejor respuesta es la de la familia: la curva de
            // mando y la Q exponencial. No es un fallback silencioso, es
            // hasTable() diciendo que no hay tabla.
            measuredHz         = requestedHz;
            measuredQ          = abd::dsp::filterResonanceToQ(resonance);
            measuredDriftHz    = 0.0;
            measuredThdPercent = 0.0;

            applyToCore();
            return;
        }

        // La posicion en la rejilla: el corte por la curva de mando de la familia
        // (la tabla esta indexada por posicion de mando) y la resonancia directa.
        const auto x = abd::dsp::normalizedFromCutoffHz(requestedHz) * static_cast<double>(columns - 1);
        const auto y = resonance * static_cast<double>(rows - 1);

        int x0 = static_cast<int>(x);
        int y0 = static_cast<int>(y);

        if (x0 > columns - 2)
            x0 = columns - 2;

        if (y0 > rows - 2)
            y0 = rows - 2;

        const auto tx = x - static_cast<double>(x0);
        const auto ty = y - static_cast<double>(y0);

        const auto at = [this](int column, int row) -> const AbdBatchedPoint& {
            return tablePoints[static_cast<std::size_t>(row) * static_cast<std::size_t>(columns) + static_cast<std::size_t>(column)];
        };

        // Bilineal por campo: el mismo peso para los cuatro campos de la tabla, que
        // es exactamente lo que promete el modelo (una rejilla de medidas).
        const auto bilinear = [&, tx, ty](float AbdBatchedPoint::*field) noexcept -> double {
            const auto v00 = static_cast<double>(at(x0, y0).*field);
            const auto v10 = static_cast<double>(at(x0 + 1, y0).*field);
            const auto v01 = static_cast<double>(at(x0, y0 + 1).*field);
            const auto v11 = static_cast<double>(at(x0 + 1, y0 + 1).*field);

            const auto bottom = v00 + (v10 - v00) * tx;
            const auto top    = v01 + (v11 - v01) * tx;

            return bottom + (top - bottom) * ty;
        };

        measuredHz         = bilinear(&AbdBatchedPoint::mu);
        measuredQ          = bilinear(&AbdBatchedPoint::sec_mu);
        measuredDriftHz    = bilinear(&AbdBatchedPoint::sigma);
        measuredThdPercent = bilinear(&AbdBatchedPoint::thd_percent);

        applyToCore();
    }

    /** Le pasa al nucleo el corte medido y la Q medida. La Q viaja en el idioma de
        la familia (0..1) porque es el idioma que el nucleo habla; la vuelta
        Q -> resonancia -> Q es fiel hasta la precision de la curva compartida
        (float deterministico), que para una Q es muy por debajo de lo audible. */
    void applyToCore() noexcept
    {
        core.setCutoff(measuredHz);
        core.setResonance(abd::dsp::filterQToResonance(measuredQ));
    }

    //==========================================================================
    const AbdBatchedPoint* tablePoints = nullptr;
    int columns                        = 0;
    int rows                           = 0;

    double requestedHz        = 1000.0;
    double resonance          = 0.0;
    double measuredHz         = 1000.0;
    double measuredQ          = abd::dsp::filterMinQ;
    double measuredDriftHz    = 0.0;
    double measuredThdPercent = 0.0;

    abd::dsp::FilterTpt core;
};

} // namespace abd::lutdsp
