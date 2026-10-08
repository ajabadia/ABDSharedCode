# Checklist Canónico de Promoción a `SynthCore`

> **Documento:** Protocolo Estándar de Extracción y Promoción DSP  
> **Destino compartido:** `ABDSharedCode/SynthCore` (`ABDShared::SynthCore`)  
> **Espacio de nombres canónico:** `abd::synth`  
> **Estándar:** C++20 puro (sin dependencias de JUCE, GUI ni SO)  
> **Propósito:** Guía de ejecución paso a paso para promover módulos DSP desde cualquier sintetizador donante (ABDEep, ABDMS2000, ABDCZ101, etc.) hacia el núcleo compartido sin regresiones sonoras ni duplicidad de código.

---

## 📋 Resumen del Flujo de Trabajo

```
[1. Auditoría & Extracción] ──▶ [2. Registro CMake] ──▶ [3. Shim en Donante]
             │                                                  │
             ▼                                                  ▼
[4. Matriz de Adopción]     ──▶ [5. Validación Suites]──▶ [6. Documentación]
```

---

## Fase 1: Auditoría de Superficie y Extracción Canónica

- [ ] **1.1. Inspección del archivo donante:**
  - Identificar la clase DSP original (ej. `Source/DSP/MiModulo.h` y `.cpp`).
  - Separar la **lógica algorítmica matemática** de la **política del sintetizador** (parámetros de panel, presets, tipos de framework).
- [ ] **1.2. Crear cabecera e implementación en `ABDSharedCode/SynthCore/`:**
  - Ruta de cabecera: `ABDSharedCode/SynthCore/MiModulo.h`.
  - Ruta de implementación: `ABDSharedCode/SynthCore/MiModulo.cpp` (o solo `.h` si es estrictamente *header-only*).
  - Envolver todo en `namespace abd::synth`.
- [ ] **1.3. Purga estricta de dependencias (100% C++20 agnóstico):**
  - [ ] Eliminar cualquier `#include <JuceHeader.h>` o cabeceras `juce_*`.
  - [ ] Reemplazar `juce::AudioBuffer<float>` por punteros crudos (`float*`, `const float*`) o referencias a arrays de tamaño fijo.
  - [ ] Reemplazar `juce::MidiBuffer` por callbacks de orden superior con plantillas:
    ```cpp
    template <typename EventConsumer>
    void generate(int numSamples, EventConsumer&& consume);
    ```
  - [ ] Reemplazar constantes de JUCE por `<cmath>`, `<numbers>` y `<algorithm>` (`std::clamp`, `std::numbers::pi_v<float>`).
- [ ] **1.4. Garantías de Tiempo Real (RT-Safety & Determinismo):**
  - [ ] **Zero-alloc en el lazo de audio:** Prohibido `new`, `malloc`, `std::vector`, `std::string` en `render()`, `nextSample()` o `generate()`. Usar arrays estáticos o tamaños de plantilla fijos.
  - [ ] **Determinismo:** Si el módulo requiere pseudoaleatoriedad, prohibido `std::rand()` o `std::random_device`. Usar `abd::synth::Arpeggiator::FastRng` (Xorshift32) o equivalente interno determinista.
  - [ ] **Sin excepciones ni bloqueos:** Prohibido `std::mutex` o lanzar excepciones en el hilo de audio.

---

## Fase 2: Registro en el Build System (CMake)

- [ ] **2.1. Registrar fuentes en `ABDSharedCode/CMakeLists.txt`:**
  - Si el módulo tiene archivo `.cpp`, añadirlo a la lista de fuentes del target `ABDShared_SynthCore`:
    ```cmake
    # En ABDSharedCode/CMakeLists.txt:
    add_library(ABDShared_SynthCore STATIC
        ...
        SynthCore/MiModulo.cpp
        ...
    )
    ```
  - Si es *header-only* (como `ModMatrix.h`), verificar que la carpeta `SynthCore/` esté en los `target_include_directories(ABDShared_SynthCore PUBLIC ...)`.
- [ ] **2.2. Verificar enlace downstream:**
  - Asegurar que el sintetizador consumidor enlace `ABDShared::SynthCore` en modo `PRIVATE`:
    ```cmake
    target_link_libraries(TuSintetizador PRIVATE ABDShared::SynthCore)
    ```

---

## Fase 3: Shim / Adaptador en el Sintetizador Donante

- [ ] **3.1. Convertir la cabecera original en un Shim ligero:**
  - Localización: `Source/DSP/MiModulo.h` en el proyecto donante (ej. ABDEep).
  - Incluir la cabecera canónica de `SynthCore`:
    ```cpp
    #pragma once
    #include "SynthCore/MiModulo.h"

    namespace MiSynth
    {
        class MiModulo : public abd::synth::MiModulo
        {
        public:
            // Reenvío de constructores canónicos:
            using abd::synth::MiModulo::MiModulo;

            // CRÍTICO: Si se añaden sobrecargas de funciones existentes (ej. generate con juce::MidiBuffer),
            // usar 'using' explícito para evitar ocultamiento de funciones miembro en C++ (name hiding):
            using abd::synth::MiModulo::generate;

            // Sobrecargas de conveniencia para el framework del plugin (JUCE):
            void generate(juce::MidiBuffer& out, int numSamples);
        };
    }
    ```
- [ ] **3.2. Reducir la unidad de traducción `.cpp` en el donante:**
  - **Regla:** No duplicar algoritmos ni definiciones de funciones.
  - Si el proyecto donante usa listas de fuentes estáticas (ej. `DspSources.cmake` en ABDEep), mantener el archivo `.cpp` conteniendo solo la inclusión de la cabecera local y las sobrecargas específicas del framework:
    ```cpp
    #include "MiModulo.h"

    namespace MiSynth
    {
        // Solo sobrecargas dependientes de JUCE o tipos del plugin.
        // La implementación algorítmica canónica reside en abd::synth::MiModulo.
    }
    ```
- [ ] **3.3. Eliminar PIMPL en el Heap (Alojamiento por valor):**
  - Si el componente se instanciaba dinámicamente mediante `std::unique_ptr<MiModulo>` solo por ocultamiento de compilación, sustituirlo por instanciación directa por valor de la clase en el motor de voz (`SynthVoice` o `SynthEngine`).
  - Esto elimina indirecciones de punteros y asignaciones dinámicas en tiempo de ejecución.

---

## Fase 4: Matriz de Adopción por Sintetizador de la Suite

Antes de dar por cerrado el módulo, documentar cómo consumirá cada miembro del ecosistema la nueva pieza:

| Sintetizador | Entorno / Framework | Perfil de Consumo | Mapeo Específico de Parámetros |
|---|---|---|---|
| **ABDEep** | JUCE / Desktop C++ | Shim en `Source/DSP/` | Mapeo a enums DeepMind 12 (`ModSource`, `ModDestination`). Escala unificada `/128.0f`. |
| **ABDMS2000** | JUCE / Desktop C++ | Shim o consumo directo | Mapeo a SysEx / Virtual Patch (6 slots fijos, 8 fuentes, 8 destinos Korg). |
| **ABDCZ101** | C++ puro / JUCE | Consumo directo `abd::synth` | Modulación de etapas de síntesis PD (DCA, DCW, DCO). |
| **ABDNeural** | C++ propio / JUCE | Consumo directo `abd::synth` | Uso de `ModDestinationDescriptor` (política de reemplazo `replaces`). |
| **WASM / WebAudio** | Emscripten / AudioWorklet | Consumo directo sin shims | Cero dependencias de JUCE. Compilación directa en Float32Array loop. |

---

## Fase 5: Doble Validación y Batería de Pruebas

- [ ] **5.1. Compilación estricta sin advertencias:**
  - Compilar con advertencias elevadas (`/W4` en MSVC, `-Wall -Wextra` en Clang/GCC).
  - Validar compatibilidad estricta con estándar C++20.
- [ ] **5.2. Suite nativa de pruebas compartida:**
  - Ejecutar la batería de tests de `SynthCore`:
    ```bash
    ctest --test-dir build --output-on-failure -R ABDShared_SynthCore_Tests
    ```
- [ ] **5.3. Suite completa del sintetizador donante:**
  - Ejecutar la suite nativa del plugin consumidor (ej. en ABDEep):
    - Tests de osciladores, voces y motor de síntesis.
    - Tests de equivalencia bit a bit (cero ULP de desviación sonora).
    - Tests de exportación y contratos.

---

## Fase 6: Documentación y Registro Canónico

- [ ] **6.1. Actualizar `ABDSharedCode/INTEGRATION_GUIDE.md`:**
  - Añadir el nuevo archivo al árbol de fuentes de `SynthCore/`.
  - Crear la sección `## Módulo: SynthCore — <Nombre>` con snippet de enlace CMake y consumo mínimo.
- [ ] **6.2. Actualizar `ABDSharedCode/README.md`:**
  - Incorporar el módulo y sus capacidades a la fila correspondiente de la tabla de módulos disponibles.
- [ ] **6.3. Guía técnica de integración (si aplica):**
  - Si el componente presenta complejidad algorítmica, tablas métricas o curvas no triviales, crear:  
    `ABDSharedCode/docs/<MODULO>_INTEGRATION_GUIDE.md` (ver como referencia `ARPEGGIATOR_INTEGRATION_GUIDE.md` o `CONTROL_SEQUENCER_INTEGRATION_GUIDE.md`).
- [ ] **6.4. Commit atómico:**
  - Registrar los cambios en `ABDSharedCode` y en el sintetizador donante con mensajes descriptivos bajo convención Conventional Commits:
    - `feat(SynthCore): promote <Componente> to shared C++20 engine`
    - `refactor(DSP): convert <Componente> to shim over abd::synth::<Componente>`
    - `docs(SynthCore): add integration guide for <Componente>`
