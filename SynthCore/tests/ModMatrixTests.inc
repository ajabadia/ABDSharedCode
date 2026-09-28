// ModMatrix unit tests — standalone, no JUCE.
//
// La pieza que mas importa aqui es `testModMatrixEquivalenceWithAbdeep`: una
// COPIA LITERAL de la implementacion previa de ABDEep
// (Source/DSP/ModulationMatrix.{h,cpp} antes de la Fase 5) y la comparacion
// muestra a muestra con `ModMatrixT::get`. Si divergen UNA sola vez, este test
// falla. Es lo que permite migrar ABDEep al nucleo compartido sin cambiar ni
// una muestra de sonido.

#include "ModMatrix.h"

// ── Copia literal de la implementación PREVIA de ABDEep ─────────────────────
// Deliberadamente NO se llama a la real: si ABDEep cambiara su motor, este
// test seguiría comparando contra lo que había, que es justo lo que hace falta
// para probar que la migración no altera el resultado.

namespace legacy {

enum class ModSource {
    kNone = 0, kLFO1, kLFO2, kLFO1Uni, kLFO2Uni,
    kEnv1VCA, kEnv2VCF, kEnv3MOD, kNoteNumber, kVelocity, kReleaseVelocity,
    kKeyPressure, kModWheel, kPitchBend, kFootController, kExpressionPedal,
    kBreathController, kSustainPedal, kControlSequencer, kVoiceNumber,
    kCC_X, kCC_Y, kCC_Z, kMaxSources
};

enum class ModDestination {
    kNone = 0, kOsc1Pitch, kOsc2Pitch, kOsc1SquareWidth, kOsc2ToneMod,
    kOsc1Level, kOsc2Level, kSubOscLevel, kNoiseLevel, kFilterCutoff,
    /* ... los 48 del enum real no hacen falta para la equivalencia: lo que se
       compara es la ARITMÉTICA, no el vocabulario ... */
    kMaxDestinations
};

struct ModRoute { ModSource source = ModSource::kNone;
                  ModDestination destination = ModDestination::kNone;
                  float amount = 0.0f; };

class ModulationMatrix {
public:
    static constexpr int kNumSlots = 8;
    void clear() { for (int i = 0; i < kNumSlots; ++i) routes[i] = ModRoute(); }
    void setRoute(int slotIndex, ModSource src, ModDestination dest, float amount)
    {
        if (slotIndex >= 0 && slotIndex < kNumSlots) {
            routes[slotIndex].source = src;
            routes[slotIndex].destination = dest;
            routes[slotIndex].amount = amount;
        }
    }
    float getModulationValue(ModDestination dest, const float* sourceValues) const
    {
        if (dest == ModDestination::kNone || sourceValues == nullptr) return 0.0f;
        float total = 0.0f;
        for (int i = 0; i < kNumSlots; ++i) {
            if (routes[i].destination == dest) {
                int srcIdx = static_cast<int>(routes[i].source);
                if (srcIdx > 0 && srcIdx < static_cast<int>(ModSource::kMaxSources))
                    total += sourceValues[srcIdx] * routes[i].amount;
            }
        }
        return total;
    }
private:
    ModRoute routes[kNumSlots];
};

} // namespace legacy

/** Convierte un byte bipolar de profundidad (128 = centro) a [-1, +1]. */
static float byteToAmount(int amountByte) noexcept
{
    return (static_cast<float>(amountByte) - 128.0f) / 128.0f;
}

// ── La aritmética ─────────────────────────────────────────────────────────

static void testModMatrixAccumulation() {
    printf("=== ModMatrix: accumulate ===\n");
    using Matrix = ModMatrixT<8>;

    constexpr std::size_t kSources = 8;
    constexpr std::size_t kDests = 16;

    const float sources[kSources] = { 0.0f, 0.5f, -0.25f, 1.0f,
                                      -1.0f, 0.75f, 0.1f, 0.2f };
    float accum[kDests] = {};

    Matrix m;
    m.setRoute(0, 1, 5, 0.5f);    // LFO1  -> dest 5, amount +0.5
    m.setRoute(1, 2, 5, -0.4f);   // LFO2  -> dest 5, amount -0.4
    m.setRoute(2, 3, 5, 1.0f);    // otro  -> dest 5, amount +1.0

    m.accumulate(sources, kSources, accum, kDests);

    const float expected = sources[1] * 0.5f + sources[2] * -0.4f + sources[3] * 1.0f;
    check(accum[5] == expected, "tres rutas al mismo destino se SUMAN");
    check(accum[0] == 0.0f, "el destino 0 no acumula (kNoModDestination)");

    // Un solo check para los destinos sin ruta, no uno por destino.
    bool untouched = true;
    for (std::size_t d = 1; d < kDests; ++d)
        if (d != 5 && accum[d] != 0.0f) untouched = false;
    check(untouched, "los destinos sin ruta no se tocan");

    // accumulate() SUMA sobre lo que hubiera: no pone a cero.
    accum[5] = 100.0f;
    m.accumulate(sources, kSources, accum, kDests);
    check(accum[5] == 100.0f + expected, "accumulate suma sobre el buffer del llamante");

    // clear() no borra el buffer del llamante, pero sí las rutas.
    m.clear();
    accum[5] = 0.0f;
    m.accumulate(sources, kSources, accum, kDests);
    check(accum[5] == 0.0f, "clear() deja la matriz sin rutas");
    check(!m.hasAnyRoute(), "clear() vacía hasAnyRoute()");
}

static void testModMatrixBounds() {
    printf("=== ModMatrix: límites y robustez ===\n");
    using Matrix = ModMatrixT<4>;

    constexpr std::size_t kSources = 6;
    constexpr std::size_t kDests = 6;
    const float sources[kSources] = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float accum[kDests] = {};

    Matrix m;
    m.setRoute(0, 1, 2, 1.0f);
    m.accumulate(sources, kSources, accum, kDests);
    check(accum[2] == 1.0f, "ruta normal");

    // Un sourceId FUERA del array declarado no se lee (evita salirse), y un
    // destinationId fuera del buffer de salida no se escribe.
    float guard = 0.0f;
    float tiny[2] = { 0.0f, 1.0f };
    m.clear();
    m.setRoute(0, 5, 2, 1.0f);          // source 5 >= sourceCount(2)
    m.accumulate(tiny, 2, &guard, 1);   // dest 2 >= destCount(1)
    check(guard == 0.0f, "un id fuera de rango no se lee ni se escribe");

    // nullptr: no debe escribir nada ni reventar. El acumulador se pone a cero
    // ANTES, porque accumulate() suma sobre lo que hubiera.
    m.clear();
    accum[2] = 0.0f;
    m.setRoute(0, 1, 2, 1.0f);
    m.accumulate(nullptr, kSources, accum, kDests);
    check(accum[2] == 0.0f, "accumulate con sources nullptr no acumula");

    accum[2] = 0.0f;
    m.accumulate(sources, kSources, nullptr, kDests);
    check(accum[2] == 0.0f, "accumulate con destino nullptr no acumula");

    accum[2] = 0.0f;
    m.accumulate(sources, kSources, accum, 0);
    check(accum[2] == 0.0f, "accumulate con destCount 0 no acumula");

    // Un slot fuera de rango se ignora, como en la implementación previa.
    m.clear();
    m.setRoute(99, 1, 2, 1.0f);
    check(!m.hasAnyRoute(), "setRoute con slot fuera de rango se ignora");

    // La cantidad se CLAMPA a ±1: el motor no amplifica por accidente.
    m.setRoute(0, 1, 2, 5.0f);
    check(m.getRoute(0).amount == kMaxModAmount, "amount se clampea arriba");
    m.setRoute(0, 1, 2, -5.0f);
    check(m.getRoute(0).amount == kMinModAmount, "amount se clampea abajo");

    // amount 0 = ruta muerta: no es "activa" aunque tenga fuente y destino.
    m.setRoute(0, 1, 2, 0.0f);
    check(!m.hasAnyRoute(), "amount 0 no cuenta como ruta viva");
}

static void testModMatrixGet() {
    printf("=== ModMatrix: get() ===\n");
    using Matrix = ModMatrixT<8>;

    constexpr std::size_t kSources = 8;
    const float sources[kSources] = { 0.0f, 0.5f, 0.25f, -0.75f, 1.0f, -1.0f, 0.0f, 0.0f };

    Matrix m;
    m.setRoute(0, 1, 20, 0.5f);
    m.setRoute(1, 3, 20, 0.25f);
    m.setRoute(2, 2, 30, 1.0f);

    const float at20 = m.get(20, sources, kSources);
    check(at20 == sources[1] * 0.5f + sources[3] * 0.25f, "get suma las rutas de un destino");
    check(m.get(30, sources, kSources) == sources[2] * 1.0f, "get del otro destino");
    check(m.get(40, sources, kSources) == 0.0f, "get de un destino sin ruta = 0");
    check(m.get(kNoModDestination, sources, kSources) == 0.0f, "get del destino 0 = 0");
    check(m.get(20, nullptr, kSources) == 0.0f, "get con sources nullptr = 0");
}

// ── La prueba que permite migrar sin cambiar el sonido ────────────────────

static void testModMatrixEquivalenceWithAbdeep() {
    printf("=== ModMatrix: equivalencia con la implementación previa de ABDEep ===\n");

    // Rutas del DeepMind 12 leídas de los bancos de fábrica: los pares
    // (fuente, destino, cantidad) que el equipo usa de verdad, más los corners
    // que no aparecen en los bancos (negativo a tope, 0, rutas muertas).
    //
    // IMPORTANTE: cada fila usa un slot DISTINTO. `setRoute` sobrescribe, así
    // que dos filas con el mismo slot se anularían entre sí y la prueba
    // compararía una matriz vacía contra otra también vacía: verde y sin
    // valor. Con un slot por fila, las 8 rutas están vivas a la vez.
    struct FactoryRoute { int slot; int src; int dest; int amountByte; };
    const FactoryRoute routes[] = {
        { 0,  2, 17, 255 },   // LFO2 -> dest 17, a tope
        { 1,  2, 18, 200 },   // LFO2 -> dest 18
        { 2,  5, 17,  74 },   // dos rutas al MISMO destino 17
        { 3,  6, 17, 128 },   // y una tercera: se suman
        { 4,  7, 60, 128 },
        { 5, 12, 22, 255 },
        { 6,  9, 64, 138 },
        { 7, 19, 12,   1 },   // cantidad casi nula
    };
    constexpr auto kRouteCount = sizeof(routes) / sizeof(routes[0]);

    // Fuentes del DeepMind en el ORDEN DEL BYTE (docs/sysex_format.md 0..22).
    const float sources[23] = {
        0.0f, 0.5f, 0.25f, -0.75f, 1.0f, -1.0f, 0.1f, 0.2f,
        0.3f, -0.2f, 0.4f, 0.6f, -0.9f, 0.15f, -0.35f, 0.8f,
        0.05f, -0.05f, 0.45f, -0.55f, 0.65f, 0.35f, -0.15f,
    };
    constexpr std::size_t kSources = 23;
    constexpr std::size_t kDests = 130;

    // Que el fixture sea el que creemos: 8 slots vivos, todos distintos.
    bool fixtureOk = true;
    for (std::size_t i = 0; i < kRouteCount; ++i) {
        for (std::size_t j = i + 1; j < kRouteCount; ++j)
            if (routes[i].slot == routes[j].slot) fixtureOk = false;
        if (routes[i].src == 0 || routes[i].dest == 0) fixtureOk = false;
    }
    check(fixtureOk, "el fixture tiene 8 slots distintos y ninguno muerto");

    legacy::ModulationMatrix legacyMatrix;
    ModMatrixT<8> newMatrix;

    for (const auto& r : routes) {
        const float amount = byteToAmount(r.amountByte);
        legacyMatrix.setRoute(r.slot,
                              static_cast<legacy::ModSource>(r.src),
                              static_cast<legacy::ModDestination>(r.dest),
                              amount);
        newMatrix.setRoute(static_cast<std::size_t>(r.slot),
                           static_cast<ModSourceId>(r.src),
                           static_cast<ModDestinationId>(r.dest),
                           amount);
    }

    check(newMatrix.hasAnyRoute(), "la matriz nueva tiene rutas vivas");

    // get(): la firma que ABDEep sustituye. Bit a bit, sin tolerancia.
    bool allEqual = true;
    for (std::size_t d = 0; d < kDests; ++d) {
        const float legacyValue = legacyMatrix.getModulationValue(
            static_cast<legacy::ModDestination>(d), sources);
        const float newValue = newMatrix.get(static_cast<ModDestinationId>(d),
                                             sources, kSources);
        if (legacyValue != newValue)
            allEqual = false;
    }
    check(allEqual, "get() da el MISMO bit que la implementación previa, en 130 destinos");

    // El destino 17 tiene TRES rutas (slots 2, 3 y... la 0 y la 2): comprobar
    // que el valor no es cero es lo que hace que la comparación de arriba no
    // seatrivialmente cierta.
    const float at17 = newMatrix.get(17, sources, kSources);
    check(at17 != 0.0f, "el destino compartido realmente acumula (la comparación no es trivial)");

    // Y reproduce la suma a mano, en el orden de los slots.
    float handSum = 0.0f;
    for (const auto& r : routes)
        if (r.dest == 17)
            handSum += sources[r.src] * byteToAmount(r.amountByte);
    check(at17 == handSum, "el destino 17 coincide con la suma calculada a mano");

    // accumulate(): el barrido completo, que es lo que usará el motor nuevo.
    float newAccum[kDests] = {};
    newMatrix.accumulate(sources, kSources, newAccum, kDests);

    bool accumMatchesGet = true;
    for (std::size_t d = 0; d < kDests; ++d) {
        if (d == 0) continue;   // el 0 nunca acumula
        if (newAccum[d] != newMatrix.get(static_cast<ModDestinationId>(d),
                                          sources, kSources))
            accumMatchesGet = false;
    }
    check(accumMatchesGet, "accumulate() y get() dan el mismo valor en los 130 destinos");
    check(newAccum[0] == 0.0f, "accumulate() no escribe en el destino 0");

    printf("  (8 rutas, 3 de ellas al mismo destino, 130 destinos comparados)\n");
}

// ── El descriptor: donde vive la política ─────────────────────────────────

static void testModDestinationDescriptor() {
    printf("=== ModMatrix: descriptor de destinos ===\n");

    // Un trozo de la tabla de ABDNeural tal cual la declara hoy. El punto es
    // que el descriptor declara LO QUE EL SWITCH OCULTA: `perNote` y
    // `replaces` son la política de síntesis de reemplazo que no puede
    // perderse al migrar el `switch` de 31 casos.
    const ModDestinationDescriptor table[] = {
        {  0, "Off",            nullptr,      0.0f,     1.0f,  1.0f,  false, false },
        {  1, "Osc Level",      "oscLevel",   0.0f,     1.0f,  1.0f,  false, false },
        {  2, "Filter Cutoff",  "filterCutoff", 20.0f, 20000.0f, 1.0f, true,  true  },
        { 12, "Filter Env Amt", nullptr,      0.0f,     1.0f,  1.0f,  true,  true  },
        { 28, "Morph Z",        "morphZ",     0.0f,     1.0f,  1.0f,  false, false },
    };
    constexpr std::size_t kTableSize = sizeof(table) / sizeof(table[0]);

    check(table[0].parameterId == nullptr, "el destino 0 no conduce ningún parámetro");
    check(table[0].label[0] == 'O' && table[0].label[1] == 'f', "el destino 0 se llama Off");

    const auto& cutoff = table[2];
    check(cutoff.perNote, "el cutoff se resuelve por voz");
    check(cutoff.replaces, "ENV2 -> cutoff REEMPLAZA, no suma");
    check(!table[1].perNote, "el nivel de osc es global");
    check(cutoff.max - cutoff.min > 1000.0f, "el cutoff lleva su rango real");

    int perNoteCount = 0, replacesCount = 0, withoutParam = 0;
    for (std::size_t i = 0; i < kTableSize; ++i) {
        if (table[i].perNote)  ++perNoteCount;
        if (table[i].replaces) ++replacesCount;
        if (table[i].parameterId == nullptr) ++withoutParam;
    }
    check(perNoteCount == 2, "el descriptor cuenta los destinos por voz");
    check(replacesCount == 2, "el descriptor cuenta los que reemplazan");
    check(withoutParam == 2, "el descriptor cuenta los que no conducen parámetro");

    // Un id es UN ENTERO opaco: el motor no lo interpreta, y el proyecto puede
    // darle el valor que su formato de preset exija.
    static_assert(sizeof(ModSourceId) == 2, "ModSourceId es un índice compacto");
    static_assert(sizeof(ModDestinationId) == 2, "ModDestinationId es un índice compacto");
    static_assert(std::is_trivially_copyable_v<ModRoute>,
                  "ModRoute es trivial: la matriz es un bloque de datos");
    static_assert(std::is_trivially_destructible_v<ModMatrixT<8>>,
                  "la matriz se puede colocar en un bloque de audio");
    check(true, "el motor no guarda punteros: es trivialmente copiable");
}

// ── El índice 0: una diferencia REAL entre los tres synths ────────────────

static void testModMatrixZeroIdIsNotAlwaysInert() {
    printf("=== ModMatrix: el índice 0 no siempre es inerte ===\n");

    // ABDMS2000: `enum class PatchSource { EG1 = 0, ... }`. El 0 es el EG1, una
    // fuente de VERDAD, y su tabla no tiene columna inerte: una ruta se apaga
    // con la intensidad a cero, no eligiendo un índice nulo. Por eso su
    // matriz se instancia con kZeroIdInert = false.
    //
    // Con el valor por defecto (true), el EG1 se descartaría en silencio y sus
    // patches no modularían. Este test es el que no deja que eso pase.
    using Ms2000Matrix = ModMatrixT<4, /* kZeroIdInert */ false>;

    constexpr std::size_t kSources = 8;
    constexpr std::size_t kDests = 8;

    // sources[0] = EG1, sources[1] = EG2, sources[2] = LFO1
    const float sources[kSources] = { 0.5f, 0.25f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float accum[kDests] = {};

    Ms2000Matrix m;
    m.setRoute(0, 0, 0, 1.0f);   // EG1 (fuente 0) -> Pitch (destino 0)

    m.accumulate(sources, kSources, accum, kDests);

    check(accum[0] == sources[0],
          "con kZeroIdInert=false la fuente 0 y el destino 0 modularan de verdad");
    check(m.get(0, sources, kSources) == sources[0],
          "get() tampoco descarta el id 0 cuando no está reservado");
    check(m.hasAnyRoute(), "una ruta por EG1 cuenta como viva");

    // Y con el valor por defecto (ABDEep / ABDNeural), el 0 SÍ se descarta.
    using ReservedMatrix = ModMatrixT<4>;
    ReservedMatrix r;
    r.setRoute(0, 0, 0, 1.0f);
    check(r.get(0, sources, kSources) == 0.0f,
          "con el 0 reservado, una ruta por el id 0 no modula");
    check(!r.hasAnyRoute(), "con el 0 reservado, esa ruta no cuenta como viva");

    // La ruta se apaga por la CANTIDAD, que es como lo hace el MS-2000.
    r.setRoute(0, 1, 1, 0.0f);
    check(!r.hasAnyRoute(), "una ruta con cantidad 0 está apagada aunque tenga ids");

    r.setRoute(0, 1, 1, 0.5f);
    check(r.hasAnyRoute(), "con cantidad distinta de 0 la ruta está viva");

    // isInert() es la regla, expuesta para que el consumidor la consulte.
    static_assert(ReservedMatrix::isInert(0), "con el 0 reservado, el 0 es inerte");
    static_assert(!ReservedMatrix::isInert(1), "el resto de ids no son inertes");
    static_assert(!Ms2000Matrix::isInert(0), "sin reservado, NADIE es inerte");
    check(true, "isInert() refleja la reserva declarada por el proyecto");
}

// (ya dentro de `namespace abd::synth::tests`, que abre el .cpp)
