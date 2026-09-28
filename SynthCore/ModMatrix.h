#pragma once
#include <cstddef>
#include <cstdint>

namespace abd::synth {

/**
 * @brief Matriz de modulación genérica — el motor, sin política.
 * =============================================================================
 * LA REGLA DE DISEÑO (la misma que DspEffects): **el motor expone la muestra y
 * la política se queda en el consumidor**. Aquí no hay ni un nombre de fuente
 * ni un nombre de destino, ni una escala, ni un clamp de producto: solo suma.
 *
 * POR QUÉ LOS IDENTIFICADORES SON OPAKOS
 * --------------------------------------
 * Un `enum class ModSource { kLFO1, kEnv1, ... }` obligaría a que el motor
 * conociera el vocabulario de un synth. Los tres consumidores de la suite lo
 * tienen distinto y no van a converger:
 *
 *   - ABDEep es una EMULACIÓN: sus índices son índices de BYTE del DeepMind 12,
 *     y el orden es el del manual (0..22 fuentes, 0..129 destinos).
 *   - ABDMS2000 es otra emulación, con solo 8 fuentes y 8 destinos, en el orden
 *     de su SysEx.
 *   - ABDNeural es 100% propio: 8 fuentes, 31 destinos, y su tabla es el
 *     FORMATO DE PRESET (los choices guardan índice, así que no se puede
 *     reordenar sin re-mapear los presets guardados).
 *
 * Si el motor llevara el enum, cada uno de esos tres órdenes sería un
 * `static_cast` y el primero en llegar marcaría el orden canónico. Con
 * identificadores opacos, el índice es lo que el proyecto dice que es, y el
 * motor solo suma: el MISMO núcleo vale para los tres.
 *
 * QUÉ SE GARANTIZA Y QUÉ NO
 * -------------------------
 * Se garantiza: sin asignación, sin excepciones, sin llamada virtual en el
 * lazo de audio, suma en el mismo orden que un recorrido de slots, y que
 * `get()` da EXACTAMENTE lo mismo que la implementación previa de ABDEep
 * (hay un test de equivalencia bit a bit en SynthCoreTests.cpp: es lo que
 * permite migrarlo sin cambiar una sola muestra).
 *
 * NO se decide aquí: el nombre, el rango, la escala, si un destino es por voz o
 * global, ni qué hacer cuando dos rutas apuntan al mismo destino. Eso va en el
 * descriptor (ModDestinationDescriptor) o en el consumidor, según corresponda.
 */

// ── Identificadores opacos ─────────────────────────────────────────────────
// Índices en tablas que DA EL PROYECTO. El motor nunca los interpreta.

using ModSourceId = std::uint16_t;
using ModDestinationId = std::uint16_t;

/** Valor reservado: una fuente o destino que no hace nada ("None" / "Off"). */
inline constexpr ModSourceId kNoModSource = 0;
inline constexpr ModDestinationId kNoModDestination = 0;

/** Bipolar, como el hardware y como los tres synths: -1..+1. */
inline constexpr float kMinModAmount = -1.0f;
inline constexpr float kMaxModAmount = 1.0f;

/**
 * @brief Una ruta: una fuente hacia un destino, con cantidad.
 *
 * `amount` bipolar. NO se guarda normalizado: el hilo de audio lee cantidades
 * en unidades del destino (semitonos, Hz, %), que es lo que consume el motor de
 * voz. Quien venga de una tabla lo normaliza antes de escribir aquí.
 */
struct ModRoute {
    ModSourceId      source{ kNoModSource };
    ModDestinationId destination{ kNoModDestination };
    float            amount{ 0.0f };
};

/**
 * @brief Descriptor de un destino — la tabla como DATO, no como enum.
 *
 * Aquí vive la parte de la política que ABDNeural tiene hoy enterrada en un
 * `switch` de 31 casos (y que el plan de la matriz compartida saca a la luz):
 *
 *  - `perNote`: el destino se resuelve POR VOZ (una envolvente no es global).
 *  - `replaces`: cuando la fuente es una envolvente, la ruta REEMPLAZA el
 *    factor en vez de sumar encima. Es el caso "síntesis de reemplazo" que
 *    ABDNeural usa para ENV 1 -> VCA y ENV 2 -> cutoff, y que no puede
 *    desaparecer en la refactorización sin cambiar el sonido.
 *  - `engineMask`: qué motores del synth pueden consumir este destino. Es lo
 *    que ya usa el gating del desplegable de la WebUI de ABDNeural.
 *  - `min`/`max`/`scale`: el rango real del parámetro destino. La UI normaliza
 *    contra él (los anillos de modulación) y el motor escala con él.
 *
 * `label` no lo usa el motor: vive aquí para que la tabla sea la fuente única
 * y la UI no tenga que reescribir la lista de destinos a mano.
 */
struct ModDestinationDescriptor {
    ModDestinationId id{ kNoModDestination };
    const char*      label{ "" };
    const char*      parameterId{ nullptr };  ///< nullptr = "Off", no conduce nada
    float            min{ 0.0f };
    float            max{ 1.0f };
    float            scale{ 1.0f };           ///< unidades del amount por 1.0
    bool             perNote{ false };       ///< se resuelve por voz
    bool             replaces{ false };      ///< ENV: reemplaza, no suma
    std::uint32_t    engineMask{ 0xFFFFFFFFu };
};

/**
 * @brief El motor: N slots, suma en bucle de audio.
 *
 * Tamaño fijo en tiempo de compilación, sin asignaciones: el objeto entero
 * son `N` structs triviales. `N` lo elige el proyecto (8 buses el DeepMind
 * clásico, 4 el MS-2000, 4 hoy ABDNeural y hasta 32 en su futuro).
 *
 * @tparam kNumSlots        buses de la matriz.
 * @tparam kZeroIdInert     si el índice 0 de fuente y destino está RESERVADO
 *                          (o sea, es el "None"/"Off" y no modula). Es true en
 *                          ABDEep y en ABDNeural, y **false en ABDMS2000**,
 *                          cuyo `PatchSource::EG1 == 0` es una fuente de
 *                          verdad: su tabla no tiene columna inerte y una ruta
 *                          se apaga con la INTENSIDAD a cero, no eligiendo un
 *                          índice nulo.
 *
 *                          No se puede suponer lo de la derecha: con el valor
 *                          equivocado, el EG1 del MS-2000 se descartaría
 *                          silenciosamente y sus patches no modularían.
 */
template <std::size_t kNumSlots, bool kZeroIdInert = true>
class ModMatrixT {
public:
    static constexpr std::size_t kSlots = kNumSlots;

    ModMatrixT() = default;

    /** Borra todas las rutas (un `clear()` de la matriz de ABDEep). */
    void clear() noexcept
    {
        for (auto& route : routes_)
            route = ModRoute{};
    }

    /**
     * ¿Este id es el reservado (el "None"/"Off")? Con `kZeroIdInert` el
     * reservado es el 0; sin él, no hay ninguno y TODOS los ids valen.
     */
    static constexpr bool isInert(std::size_t id) noexcept
    {
        return kZeroIdInert && id == 0;
    }

    /**
     * Escribe una ruta. `slotIndex` fuera de rango se ignora en silencio: es la
     * misma política que la implementación previa de ABDEep, y así una tabla
     * con un slot de más no puede romper el audio.
     *
     * La cantidad se CLAMPA a ±1 (como entonces): el motor no debe amplificar
     * por accidente una ruta mal formada.
     */
    void setRoute(std::size_t slotIndex,
                  ModSourceId source,
                  ModDestinationId destination,
                  float amount) noexcept
    {
        if (slotIndex >= kNumSlots)
            return;

        routes_[slotIndex].source      = source;
        routes_[slotIndex].destination = destination;
        routes_[slotIndex].amount      = amount < kMinModAmount ? kMinModAmount
                                     : (amount > kMaxModAmount ? kMaxModAmount
                                                                : amount);
    }

    const ModRoute& getRoute(std::size_t slotIndex) const noexcept
    {
        return routes_[slotIndex < kNumSlots ? slotIndex : 0];
    }

    /** ¿Hay alguna ruta viva (fuente distinta de "None")? */
    bool hasAnyRoute() const noexcept
    {
        for (const auto& route : routes_) {
            if (route.amount == 0.0f)
                continue;
            if (!isInert(static_cast<std::size_t>(route.source))
                && !isInert(static_cast<std::size_t>(route.destination)))
                return true;
        }
        return false;
    }

    /**
     * @brief API primitiva: acumula TODAS las rutas en un buffer del llamante.
     *
     * Recorre los slots UNA vez. `sourceValues` son los valores de este
     * instante, indexados por ModSourceId; `destAccum` recibe la suma por
     * destino y lo que tuviera de antes (el llamante decide si lo pone a cero:
     * ABDNeural lo hace, ABDEep no necesita hacerlo porque pregunta por destino).
     *
     * `sourceCount`/`destCount` acotan el barrido a lo que el proyecto tiene
     * realmente. Un `sourceId` mayor que `sourceCount` NO se lee (evita salirse
     * del array) y un `destinationId` mayor que `destCount` se descarta (esa
     * ruta no tiene a dónde escribir; en un preset de hardware puede ocurrir).
     *
     * Sin asignaciones, sin llamada virtual, sin lanzar excepciones: apto para
     * el hilo de audio sin nada que reservar.
     */
    void accumulate(const float* sourceValues,
                    std::size_t sourceCount,
                    float*       destAccum,
                    std::size_t destCount) const noexcept
    {
        if (destAccum == nullptr || destCount == 0)
            return;

        for (std::size_t slot = 0; slot < kNumSlots; ++slot)
        {
            const auto& route = routes_[slot];

            const auto sourceIndex = static_cast<std::size_t>(route.source);
            const auto destIndex = static_cast<std::size_t>(route.destination);

            if (isInert(sourceIndex) || isInert(destIndex))
                continue;
            if (route.amount == 0.0f)
                continue;
            if (sourceIndex >= sourceCount || sourceValues == nullptr)
                continue;
            if (destIndex >= destCount)
                continue;

            destAccum[destIndex] += sourceValues[sourceIndex] * route.amount;
        }
    }

    /**
     * @brief Azúcar sobre `accumulate`: la suma de un destino concreto.
     *
     * Misma firma y MISMOS resultados que `ModulationMatrix::getModulationValue`
     * de ABDEep, para que migrarlo sea sustituir la llamada. Son un barrido por
     * slot por consulta: O(slots) por destino, que es lo que ya se pagaba allí.
     * Quien necesite el coste O(1) por consulta usa `accumulate` y cachea.
     */
    [[nodiscard]] float get(ModDestinationId destination,
                            const float* sourceValues,
                            std::size_t   sourceCount) const noexcept
    {
        if (isInert(static_cast<std::size_t>(destination)) || sourceValues == nullptr)
            return 0.0f;

        float total = 0.0f;

        for (std::size_t slot = 0; slot < kNumSlots; ++slot)
        {
            const auto& route = routes_[slot];

            if (route.destination != destination)
                continue;
            if (route.amount == 0.0f)
                continue;

            const auto sourceIndex = static_cast<std::size_t>(route.source);
            if (isInert(sourceIndex) || sourceIndex >= sourceCount)
                continue;

            total += sourceValues[sourceIndex] * route.amount;
        }

        return total;
    }

private:
    ModRoute routes_[kNumSlots] {};
};

} // namespace abd::synth
