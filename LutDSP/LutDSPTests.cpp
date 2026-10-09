/*
  ==============================================================================

    LutDSPTests.cpp
    Tests standalone del modulo LutDSP (namespace abd::lutdsp). SIN JUCE: se
    enlazan solo contra ABDShared::LutDSP (y, a traves suyo, ABDShared::DspCore),
    que es la propiedad que se quiere blindar: el modulo de las tablas medidas
    tiene que poder probarse sin arrastrar una GUI.

    Que se cubre aqui:

      1. El miembro LUT de la familia de filtros (LutFilter.h) ES de la familia:
         el rasgo de compilacion lo dice y sus modos se declaran.
      2. La tabla se lee de verdad: los numeros que salen son los de la tabla, con
         interpolacion BILINEAL, y no una curva aproximada. Se comprueba contra los
         valores escritos en el modelo (20 Hz y 20 kHz en los extremos del corte;
         Q 0.707 sin resonancia y 18.007 a tope), que es lo que hace que el test
         mida la lectura y no el redondeo.
      3. La geometria: el modelo que hay en el repo es 8 x 4, y si el generador
         cambia el tamano, el test lo dice en vez de leer fuera de la tabla.
      4. Sin tabla el miembro NO es una averia: se comporta como el analitico de la
         familia, y hasTable() lo declara. Una tabla que no se puede leer tampoco
         se acepta a medias: loadTable devuelve false y no deja media tabla.
      5. Y filtra: es un paso bajo, con el corte que dice la tabla.

    Uso: ABDShared_LutDSP_Tests  (no toma argumentos; 0 = OK)

  ==============================================================================
*/

#include "DspCore/DspFilterFamily.h"
#include "LutDSP/LutFilter.h"
#include "LutDSP/models/lut_mock_va_synth_moog_ladder.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

using abd::dsp::cutoffHzFromNormalized;
using abd::dsp::FilterKind;
using abd::dsp::filterMinQ;
using abd::dsp::FilterMode;
using abd::dsp::IsFilterStage;
using abd::lutdsp::FilterLut;
using abd::lutdsp::models::mock_va_synth_moog_ladder;
using abd::lutdsp::models::mock_va_synth_moog_ladder_SIZE;

int gChecks   = 0;
int gFailures = 0;

void check(bool ok, const char* what)
{
    ++gChecks;

    if (!ok)
    {
        std::printf("[FAIL] %s\n", what);
        ++gFailures;
    }
}

/** La geometria del modelo que hay en el repo: ocho posiciones de corte por fila
    y cuatro filas de resonancia. Se comprueba contra el tamano declarado por el
    propio modelo, para que un cambio del generador no pase en silencio. */
constexpr int kColumns = 8;
constexpr int kRows    = 4;

/** La energia (RMS) de la cola de una senal filtrada, saltandose el transitorio. */
template <typename FilterStage>
double filteredRms(FilterStage& filter, double freqHz, double sampleRate, int total, int measureTail)
{
    const auto step = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;

    double sum  = 0.0;
    int counted = 0;

    for (int i = 0; i < total; ++i)
    {
        const auto y = filter.processSample(std::sin(step * (double)i));

        if (i >= total - measureTail)
        {
            sum += y * y;
            ++counted;
        }
    }

    return counted > 0 ? std::sqrt(sum / (double)counted) : 0.0;
}

//==============================================================================
/** El miembro LUT es de la familia, y lo dice su rasgo (que es lo que comprueba
    el compilador, no un comentario). */
void testLutFilterIsAFamilyMember()
{
    static_assert(IsFilterStage<FilterLut>::value,
                  "FilterLut tiene que honrar el contrato de la familia de filtros");

    check(IsFilterStage<FilterLut>::value, "el miembro LUT es de la familia");
    check(FilterLut::kind == FilterKind::Lut, "y se declara LUT");

    check(FilterLut::supportsMode(FilterMode::LowPass), "la tabla mide una escalera: paso bajo si");
    check(!FilterLut::supportsMode(FilterMode::HighPass), "paso alto no: lo dice");

    FilterLut filter;
    filter.prepare(48000.0);

    check(!filter.setMode(FilterMode::HighPass), "setMode devuelve false cuando el modo no esta");
    check(filter.getMode() == FilterMode::LowPass, "y no cambia el modo al decir que no");
    check(filter.setMode(FilterMode::LowPass), "y honra el que si sabe");
}

//==============================================================================
/** La tabla se lee: los numeros son los de la tabla, y entre puntos se interpola
    (bilineal), no se escalona. */
void testLutFilterReadsTheMeasuredTable()
{
    // El modelo declara su tamano: si no coincide con la geometria que el test
    // pasa, el fallo no es del filtro, es que la tabla ha cambiado de forma.
    check(mock_va_synth_moog_ladder_SIZE == (size_t)(kColumns * kRows),
          "el modelo declara 32 puntos, que son las 8x4 que el test carga");

    FilterLut filter;
    filter.prepare(48000.0);

    check(filter.loadTable(mock_va_synth_moog_ladder, kColumns, kRows), "la tabla del modelo se carga");
    check(filter.hasTable(), "y el miembro dice que tiene tabla");
    check(filter.getTableColumns() == kColumns && filter.getTableRows() == kRows,
          "con la geometria que se le paso (8 columnas de corte, 4 filas de resonancia)");

    // Los extremos del corte: los DOS numeros que estan escritos en la tabla, en
    // Hz, y no los de la curva de la familia.
    filter.setResonance(0.0);
    filter.setCutoff(cutoffHzFromNormalized(0.0));

    check(std::abs(filter.getMeasuredCutoffHz() - 20.0) < 1.0e-3,
          "en la posicion 0 de corte la tabla mide 20 Hz");

    filter.setCutoff(cutoffHzFromNormalized(1.0));

    check(std::abs(filter.getMeasuredCutoffHz() - 20000.0) < 1.0e-2,
          "en la posicion 1 de corte la tabla mide 20 kHz");

    // La Q medida de la primera fila y de la ultima: 0.707 y 18.007 en la tabla.
    filter.setCutoff(cutoffHzFromNormalized(0.5));

    check(std::abs(filter.getMeasuredQ() - 0.707) < 1.0e-3,
          "sin resonancia la tabla mide Q 0.707");

    filter.setResonance(1.0);

    check(std::abs(filter.getMeasuredQ() - 18.007) < 1.0e-2,
          "a tope de resonancia la tabla mide Q 18.007");

    filter.setResonance(0.0);

    // Y entre dos puntos se INTERPOLA: a mitad de camino entre las posiciones 0 y
    // 1 de corte (20 Hz y 53.65392 Hz en la tabla) el corte medido es la media.
    filter.setCutoff(cutoffHzFromNormalized(0.5 / 7.0));

    check(std::abs(filter.getMeasuredCutoffHz() - 36.82696) < 1.0e-2,
          "a mitad de camino entre dos puntos, el corte medido es la media de los dos (bilineal)");

    // Lo mismo en el eje de la resonancia: a mitad entre la fila 0 (Q 0.707) y la
    // fila 1 (Q 2.62922) la Q medida es la media.
    filter.setResonance(0.5 / 3.0);

    check(std::abs(filter.getMeasuredQ() - 1.66811) < 1.0e-3,
          "y en el eje de resonancia tambien: la Q medida interpola entre las dos filas");

    // El corte MEDIDO crece con la posicion del mando: si la lectura se cruzara,
    // el mando iria al reves y el miembro quedaria peor que el analitico.
    filter.setResonance(0.0);

    auto monotonic = true;
    auto previous  = -1.0;

    for (int i = 0; i <= 40; ++i)
    {
        filter.setCutoff(cutoffHzFromNormalized((double)i / 40.0));

        const auto measured = filter.getMeasuredCutoffHz();

        if (!(measured >= previous))
            monotonic = false;

        previous = measured;
    }

    check(monotonic, "el corte medido nunca baja cuando el mando sube");
}

//==============================================================================
/** Sin tabla no es una averia y una tabla ilegible no se acepta a medias. */
void testLutFilterWithoutTable()
{
    FilterLut filter;
    filter.prepare(48000.0);

    check(!filter.hasTable(), "un miembro nuevo no tiene tabla");

    // Sin tabla, el miembro es el analitico de la familia: la curva de mando y la
    // Q exponencial. No es un fallback silencioso, es hasTable() diciendo que no.
    filter.setCutoff(1000.0);
    filter.setResonance(0.0);

    check(std::abs(filter.getMeasuredCutoffHz() - 1000.0) < 1.0e-9,
          "sin tabla el corte medido es el pedido, no un cero");
    check(std::abs(filter.getMeasuredQ() - filterMinQ) < 1.0e-12,
          "sin tabla la Q es la minima de la familia");

    // Una tabla con un solo punto por eje no se puede interpolar: se rechaza ENTERA
    // (no queda media tabla cargada, que seria una division por cero en la primera
    // consulta).
    check(!filter.loadTable(nullptr, kColumns, kRows), "una tabla nula se rechaza");
    check(!filter.loadTable(mock_va_synth_moog_ladder, 1, kRows), "una tabla con un solo punto de corte se rechaza");
    check(!filter.loadTable(mock_va_synth_moog_ladder, kColumns, 1), "y con una sola resonancia, tambien");
    check(!filter.hasTable(), "y despues de rechazarlas el miembro se queda SIN tabla, no con media");

    filter.setCutoff(2000.0);

    check(std::abs(filter.getMeasuredCutoffHz() - 2000.0) < 1.0e-9,
          "y sigue comportandose como el analitico de la familia");
}

//==============================================================================
/** Y filtra: es un paso bajo con el corte de la tabla. */
void testLutFilterFilters()
{
    constexpr double fs = 48000.0;

    FilterLut filter;
    filter.prepare(fs);
    filter.loadTable(mock_va_synth_moog_ladder, kColumns, kRows);
    filter.setResonance(0.3);
    filter.setCutoff(1000.0);

    const auto low = filteredRms(filter, 100.0, fs, 9600, 4800);

    filter.reset();

    const auto high = filteredRms(filter, 8000.0, fs, 9600, 4800);

    check(high < low * 0.2, "el miembro LUT es un paso bajo: atenua los agudos y deja los graves");

    // Los TRES numeros del corte, que no son el mismo y confundirlos es como se
    // miente sin querer: lo que se pide, lo que la tabla mide, y lo que el filtro
    // usa de verdad (recortado por Nyquist).
    filter.setCutoff(20000.0);

    check(std::abs(filter.getRequestedCutoff() - 20000.0) < 1.0e-9, "el corte pedido es el que se pidio");
    check(std::abs(filter.getMeasuredCutoffHz() - 20000.0) < 1.0e-2, "el medido sigue siendo el de la tabla");
    check(filter.getCutoff() < 20000.0,
          "y el que USA el filtro esta recortado por Nyquist: la tabla no promete lo que el filtro no puede");
    check(filter.getCutoff() > 0.0, "y es un corte de verdad, no un cero");

    // Un reset no deja cola y NO toca lo que es un ajuste: el corte que dice la
    // tabla sigue donde estaba (es la misma regla que la etapa compartida, cuyo
    // reset tampoco toca el corte ni la resonancia).
    filter.setCutoff(1000.0);

    const auto measuredBeforeReset = filter.getMeasuredCutoffHz();

    filter.reset();

    auto finite = true;

    for (int i = 0; i < 4096; ++i)
        if (!std::isfinite(filter.processSample(0.0)))
            finite = false;

    check(finite, "despues de un reset, con entrada cero, la salida es finita");
    check(filter.getMeasuredCutoffHz() == measuredBeforeReset,
          "y el corte medido no cambia: es un ajuste, no estado");
}

} // namespace

//==============================================================================
int main()
{
    testLutFilterIsAFamilyMember();
    testLutFilterReadsTheMeasuredTable();
    testLutFilterWithoutTable();
    testLutFilterFilters();

    if (gFailures == 0)
    {
        std::printf("[OK] LutDSP: %d comprobaciones\n", gChecks);
        return 0;
    }

    std::printf("[FALLO] LutDSP: %d de %d comprobaciones\n", gFailures, gChecks);
    return 1;
}
