# SSOT de Integración: Tríada de SynthCore (Arpeggiator, ModMatrix, ControlSequencer)

> **Documento:** Single Source of Truth (SSOT) de Arquitectura e Integración  
> **Módulo:** `SynthCore` (`ABDSharedCode`)  
> **Espacio de nombres canónico:** `abd::synth`  
> **Target CMake compartido:** `ABDShared::SynthCore`  
> **Estándar:** C++20 puro (sin JUCE, sin dependencias de GUI, sin asignaciones en tiempo real)  
> **Ámbito de aplicación:** ABDEep, ABDMS2000, ABDCZ101, ABDNeural, sintetizadores WASM / WebAudio

---

## 1. Propósito y Contexto Arquitectónico

Durante la evolución del ecosistema **ABDSynths**, múltiples sintetizadores implementaron independientemente componentes para la generación rítmica de notas, la modulación por pasos y el enrutamiento de señales. Esta dispersión provocaba duplicidad de algoritmos, divergencia en el comportamiento del glide/swing y asignaciones dinámicas en el lazo de audio.

Este documento consolida la arquitectura canónica de la **Tríada de Control y Modulación**:
1. **`Arpeggiator`**: Motor determinista de arpegios sample-accurate de 11 modos.
2. **`ModMatrixT`**: Núcleo estático de enrutamiento y acumulación de modulación sin heap.
3. **`ControlSequencer`**: Secuenciador analógico de control de 32 pasos con swing y slew exponencial.

El principio rector inmutable de esta tríada es:
> **«El motor compartido calcula la muestra y el suceso; la política de producto y el framework residen en el sintetizador consumidor.»**

Para la metodología y flujo de trabajo para promover nuevos módulos DSP a `SynthCore`, consulta el [`Checklist Canónico de Promoción`](PROMOTION_WORKFLOW_CHECKLIST.md).

---

## 2. Inventario Canónico: Qué se promovió y Dónde vive

| Componente | Ubicación en `ABDSharedCode` | Tipo / Target | Responsabilidad Técnica |
|---|---|---|---|
| **`abd::synth::Arpeggiator`** | [`SynthCore/Arpeggiator.h`](file:///d:/desarrollos/ABDSynths/ABDSharedCode/SynthCore/Arpeggiator.h)<br>[`SynthCore/Arpeggiator.cpp`](file:///d:/desarrollos/ABDSynths/ABDSharedCode/SynthCore/Arpeggiator.cpp) | `STATIC`<br>`ABDShared::SynthCore` | Generación de eventos Note-On/Note-Off a nivel de muestra (*sample-accurate*), 11 modos algorítmicos, orden cronológico de teclas (`playOrder`), retrigger limpio sin notas colgadas y PRNG `FastRng` (Xorshift32). Zero-alloc en `generate()`. |
| **`abd::synth::ModMatrixT<N, Clamp>`** | [`SynthCore/ModMatrix.h`](file:///d:/desarrollos/ABDSynths/ABDSharedCode/SynthCore/ModMatrix.h) | `INTERFACE`<br>(Header-only en `SynthCore`) | Matriz de $N$ slots con evaluación plana. Desacoplada de enums específicos mediante identificadores opacos (`ModSourceId`, `ModDestinationId`). Sin asignaciones dinámicas, operable por valor en la pila o como miembro directo de clase. |
| **`abd::synth::ControlSequencer`** | [`SynthCore/ControlSequencer.h`](file:///d:/desarrollos/ABDSynths/ABDSharedCode/SynthCore/ControlSequencer.h)<br>[`SynthCore/ControlSequencer.cpp`](file:///d:/desarrollos/ABDSynths/ABDSharedCode/SynthCore/ControlSequencer.cpp) | `STATIC`<br>`ABDShared::SynthCore` | Secuenciador de control bipolar $[-1.0f, +1.0f]$ de 1 a 32 pasos, tabla de 16 divisores de reloj NRPN, swing estricto ($r = 0.5 + 0.25 \times \text{swing}$), filtro de slew analógico exponencial de 1 polo ($\tau = s \times 1.0\text{s}$) y cálculo continuo por muestra en `nextSample()`. |

---

## 3. El Triángulo Operativo: Qué Consume Qué

Los tres componentes no se acoplan directamente entre sí dentro de `ABDSharedCode`. Su interacción se orquesta en el **lazo de proceso del sintetizador**, formando un triángulo dinámico de control y modulación:

```
                          ┌──────────────────────────┐
                          │   Host Transport / BPM   │
                          └─────────────┬────────────┘
                                        │ tempo maestro
                     ┌──────────────────┴──────────────────┐
                     ▼                                     ▼
          ┌─────────────────────┐               ┌─────────────────────┐
          │     Arpeggiator     │               │  ControlSequencer   │
          │  (Disparo de notas  │               │ (Señal continua LFO │
          │   y duración gate)  │               │   por pasos / slew) │
          └──────────┬──────────┘               └──────────┬──────────┘
                     │                                     │
                     │                                     │ nextSample() -> [-1, +1]
                     │                                     ▼
                     │                         [Fuente #18: kControlSequencer]
                     │                                     │
                     │                                     ▼
                     │                          ┌─────────────────────┐
                     │                          │     ModMatrixT      │
                     │                          │  (Enrutamiento por  │
                     │                          │   slots por voz)    │
                     │                          └──────────┬──────────┘
                     │                                     │
                     │ Destino #71: kArpGate               │ Destino #72: kSeqSlew
                     │ (Modula longitud de puerta)         │ (Modula tasa de glide)
                     ▼                                     ▼
          [Arpeggiator::setGate]                [ControlSequencer::setSlewModulation]
```

### 3.1. Flujo de Control y Modulación

1. **Sincronización Común a Reloj:**  
   Tanto `Arpeggiator` como `ControlSequencer` reciben el BPM maestro del DAW/host. Esto asegura que los pasos rítmicos del arpegio y los pasos de modulación del secuenciador mantengan coherencia métrica y de fase.
2. **`ControlSequencer` como Fuente de Modulación (`ModSource`):**  
   En cada ciclo de audio (o sub-bloque de control), `controlSequencer.nextSample()` entrega un flotante bipolar $[-1.0f, +1.0f]$. Este valor se carga en el array de fuentes del sintetizador:
   ```cpp
   modSources[(int)ModSource::kControlSequencer] = controlSequencer.nextSample();
   ```
3. **`ControlSequencer` como Destino de Modulación (`ModDestination`):**  
   La matriz evalúa los destinos acumulados. El destino de slew (`kSeqSlew`) se extrae de la matriz y se inyecta en el secuenciador antes de procesar:
   ```cpp
   const float slewMod = modMatrix.get(ModDestination::kSeqSlew);
   controlSequencer.setSlewModulation(slewMod);
   ```
   *Efecto sonoro:* El glide del secuenciador puede acelerarse o ralentizarse en tiempo real con una envolvente, una rueda de modulación o la velocidad de pulsación.
4. **`Arpeggiator` como Destino de Modulación (`ModDestination`):**  
   La compuerta (*gate length*) del arpegiador (`kArpGate`) se modula dinámicamente:
   ```cpp
   const float gateMod = modMatrix.get(ModDestination::kArpGate);
   arpeggiator.setGate(std::clamp(baseGate + gateMod, 0.0f, 1.0f));
   ```
   *Efecto sonoro:* El arpegio puede abrirse gradualmente de un staccato percusivo a un legato completo según la dinámica de interpretación.

---

## 4. Perfiles de los Shims de Compatibilidad (Patrón ABDEep)

Para integrar estos componentes en proyectos basados en JUCE o con arquitecturas preexistentes, se utiliza el **Patrón Shim de Compatibilidad**. El objetivo es mantener el código fuente del plugin limpio y sin fricción de refactorización masiva.

### 4.1. Shim de `Arpeggiator` (`Source/DSP/Arpeggiator.h`)

Hereda de `abd::synth::Arpeggiator` y añade soporte para `juce::MidiBuffer`:

```cpp
#pragma once
#include "SynthCore/Arpeggiator.h"

namespace juce { class MidiBuffer; }

namespace MiSynth
{
    class Arpeggiator : public abd::synth::Arpeggiator
    {
    public:
        using abd::synth::Arpeggiator::Arpeggiator;

        // CRÍTICO: Expone la plantilla genérica base junto con la sobrecarga JUCE
        using abd::synth::Arpeggiator::generate;

        void generate(juce::MidiBuffer& out, int numSamples);
    };
}
```

*Implementación (`Source/DSP/Arpeggiator.cpp`):*
```cpp
#include "Arpeggiator.h"
#include <JuceHeader.h>

namespace MiSynth
{
    void Arpeggiator::generate(juce::MidiBuffer& out, int numSamples)
    {
        abd::synth::Arpeggiator::generate(numSamples, [&out](const NoteEvent& ev) {
            if (ev.isNoteOn)
                out.addEvent(juce::MidiMessage::noteOn(1, ev.note, ev.velocity), ev.sampleOffset);
            else
                out.addEvent(juce::MidiMessage::noteOff(1, ev.note, 0.0f), ev.sampleOffset);
        });
    }
}
```

### 4.2. Shim de `ControlSequencer` (`Source/DSP/ControlSequencer.h`)

El shim de `ControlSequencer` no requiere sobrecargas adicionales porque toda la interfaz es numérica nativa. Se implementa como una subclase con herencia de constructores:

```cpp
#pragma once
#include "SynthCore/ControlSequencer.h"

namespace MiSynth
{
    /**
     * @brief Shim de compatibilidad para ControlSequencer.
     * Hereda el motor canónico C++20 de abd::synth::ControlSequencer.
     */
    class ControlSequencer : public abd::synth::ControlSequencer
    {
    public:
        using abd::synth::ControlSequencer::ControlSequencer;
    };
}
```

*Implementación (`Source/DSP/ControlSequencer.cpp`):*
```cpp
#include "ControlSequencer.h"

namespace MiSynth
{
    // La implementación algorítmica canónica reside en abd::synth::ControlSequencer.
    // Este archivo se preserva como TU para listas de fuentes CMake preexistentes.
}
```

### 4.3. Adaptador de `ModulationMatrix` (Alojamiento Directo por Valor)

En lugar de utilizar punteros opacos o `std::unique_ptr` (PIMPL) en el heap, la matriz se aloja directamente por valor en la clase del sintetizador:

```cpp
#pragma once
#include "SynthCore/ModMatrix.h"

namespace MiSynth
{
    class ModulationMatrix
    {
    public:
        static constexpr size_t kNumSlots = 8; // O los requeridos por el sintetizador

        void clear() noexcept { matrix.clear(); }

        void setRoute(int slot, ModSource src, ModDestination dst, float amount)
        {
            matrix.setRoute(slot,
                            static_cast<abd::synth::ModSourceId>(src),
                            static_cast<abd::synth::ModDestinationId>(dst),
                            amount * HW_BIPOLAR_SCALE);
        }

        float getModulationValue(ModDestination dst, const float* sources) const noexcept
        {
            return matrix.get(static_cast<abd::synth::ModDestinationId>(dst), sources);
        }

    private:
        // Alojamiento estático directo: cero punteros, cero fragmentación
        abd::synth::ModMatrixT<kNumSlots, true> matrix;
    };
}
```

---

## 5. Guía de Adopción para Nuevos Sintetizadores

Cualquier nuevo sintetizador del monorepo que requiera arpegiador, secuenciador o matriz de modulación debe seguir estas directrices:

### 5.1. ABDMS2000 (Emulación Korg MS-2000)
- **ModMatrix:** El hardware original dispone de un parche virtual (*Virtual Patch*) de 6 slots fijos, con 8 fuentes y 8 destinos.  
  - Instanciar `abd::synth::ModMatrixT<6, true> virtualPatch;`.
  - Mapear el `enum class PatchSource` (0..7) y `enum class PatchDestination` (0..7) a identificadores opacos.
- **Arpeggiator:** El MS-2000 implementa 6 tipos (Up, Down, Alt1, Alt2, Random, Trigger).
  - Los 6 modos mapean directamente a los modos canónicos de `abd::synth::Arpeggiator` (Up=0, Down=1, Up/Down=2, Up-Alt=6, Random=8, Chord/Trigger=10).

### 5.2. ABDCZ101 (Síntesis de Distorsión de Fase Casio CZ)
- **ControlSequencer:** Utilizar `abd::synth::ControlSequencer` como modulador de 16/32 pasos para modular los parámetros de DCA (amplitud) y DCW (onda/filtro espectral).
- No requiere shims complejos si se compila con el motor de voz puro C++20.

### 5.3. ABDNeural (Motor Propio / Síntesis Neural)
- **ModMatrix:** Dispone de 8 fuentes y 31 destinos con semántica de reemplazo (`replaces`).
  - Utilizar el array de descriptores `ModDestinationDescriptor` provisto en `SynthCore/ModMatrix.h`.
  - Evaluar destinos por voz usando `matrix.accumulate(...)` directamente en el buffer de modulación local.

### 5.4. Sintetizadores en WebAssembly / AudioWorklet (WASM)
- Compilación 100% directa mediante Clang/Emscripten sin necesidad de capas de compatibilidad JUCE:
  ```cpp
  #include "SynthCore/Arpeggiator.h"
  #include "SynthCore/ControlSequencer.h"
  #include "SynthCore/ModMatrix.h"
  ```
- En el lazo de `process(inputs, outputs, parameters)`:
  - Consumir `arpeggiator.generate(numSamples, lambda)` para disparar notas sintetizadas en JS/WASM.
  - Llamar a `controlSequencer.nextSample()` en un bucle simple sobre `Float32Array`.

---

## 6. Antipatrones Terminantemente Prohibidos

Para preservar la pureza, estabilidad y velocidad del motor compartido:

1. ❌ **Reintroducir Enums Cerrados en `ABDSharedCode`:**  
   Prohibido añadir `enum class ModSource` o `enum class ModDestination` en `SynthCore`. Los identificadores deben permanecer opacos (`uint16_t`).
2. ❌ **Asignaciones en Tiempo Real:**  
   Prohibido usar `std::vector`, `std::string`, `new` o `malloc` en `generate()`, `nextSample()` o `get()`.
3. ❌ **Dependencias de JUCE en `SynthCore`:**  
   Prohibido incluir cabeceras de JUCE (`<JuceHeader.h>`, `juce_audio_basics`, etc.) en `SynthCore/Arpeggiator.h` o `SynthCore/ControlSequencer.h`. JUCE pertenece exclusivamente al sintetizador consumidor o a su shim.
4. ❌ **Duplicación de Código Algorítmico en Shims:**  
   Los archivos del sintetizador no deben reimplementar tablas de reloj, curvas de swing ni filtros de slew. Si se requiere un cambio en el algoritmo, se realiza en `SynthCore` y se valida con las suites de prueba compartidas.
5. ❌ **Ocultamiento de Nombres (*Name Hiding*):**  
   Al crear shims que agregan sobrecargas con tipos de JUCE (como `MidiBuffer`), siempre incluir `using Base::method;` para evitar que el compilador oculte la plantilla genérica.

---

## 7. Verificación y Batería de Pruebas

Antes de dar por finalizada la integración en cualquier sintetizador consumidor:

1. **Compilación Limpia:** Compilar sin advertencias en modo Debug y Release.
2. **Suite de Pruebas Compartida:**
   ```bash
   ctest --test-dir build --output-on-failure -R ABDShared_SynthCore_Tests
   ```
3. **Suite de Pruebas del Sintetizador Consumidor:**
   Verificar que todas las suites nativas de tests pasen al 100% (ej. en ABDEep: 160 suites, más de 1.1M aserciones superadas con cero fallos).
