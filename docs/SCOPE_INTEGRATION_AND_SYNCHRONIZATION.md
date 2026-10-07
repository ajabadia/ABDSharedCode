# ESPECIFICACIÓN DE INTEGRACIÓN Y SINCRONIZACIÓN — MÓDULO SCOPE
## ABDSharedCode — Arquitectura Autónoma, Gobernanza y Contratos de Consumo

**Fecha de Actualización:** 7 de Octubre de 2026  
**Versión del Módulo Scope:** `0.4.0` (`ABD_SCOPE_VERSION`)  
**Target Público Canónico:** `ABDShared::ScopeCore`  
**Target Header-Only Canónico:** `ABDShared::ScopeCoreHeaders`  
**Ubicación en Árbol:** `ABDSharedCode/Scope/` (Subtree canónico)  
**Estado de Sincronización:** 🟢 **SINCRONIZADO Y CERTIFICADO**

---

## 1. Contexto y Objetivos de Sincronización

A partir de la versión **0.4.0**, el subsistema de telemetría y visualización osciloscópica multi-lane (`ABDScope`) se consolida definitivamente como un módulo compartido dentro de `ABDSharedCode/Scope/`.

El objetivo de este documento es establecer la fuente única de verdad para cualquier consumidor dentro del ecosistema ABDSynths (`ABDAudioLab`, `ABDMS2000`, `ABDCZ101`, etc.), eliminando configuraciones ad-hoc y garantizando que el módulo sea autónomo, testeable y desacoplado.

---

## 2. Matriz de Ownership y Gobernanza

Para evitar discrepancias y prevenir modificaciones en repositorios erróneos, se establece la siguiente matriz de propiedad:

| Entidad / Capa | Repositorio / Ruta | Responsabilidades |
|---|---|---|
| **Módulo Core & Glue** | `ABDSharedCode/Scope/` | API C++20, ring buffer lock-free (`SpscRingBuffer`), recolector (`ScopeDataCollector`), serializador wire protocol (`ScopeFrameSerializer`), detector pitch/trigger (`TriggerDetector`), componente base JUCE (`JuceWebScopeComponent`), resource provider (`ScopeResourceProvider`). |
| **Frontend WebUI & Assets** | `ABDSharedAssets` & `ABDSharedCode/Scope/WebUI/` | Estilos base (`scope.css`), layout multi-lane, renderers Canvas/WebGL, tokens de diseño y temas corporativos (`ABDSharedAssets/themes/`). |
| **Host de Laboratorio** | `ABDAudioLab` | Integración en motor (`LabAudioEngine`), ventana flotante (`ScopeWebFloatingWindow`), estrategia de pre-calentamiento (`preWarmScopeWindow()`), ciclo de vida UI. |
| **Host Sintetizador** | `ABDMS2000` (y futuros) | Taps específicos del motor de síntesis (Osc 1/2, Filter, VCA, LFO), temas retro (`ms2000`), controles embebidos. |
| **Repositorio Legado** | `ABDScope/` (raíz histórica) | **Archivado / Deprecado.** Se conserva solo como historial git inmutable. Ningún desarrollo nuevo se aplica sobre él. |

---

## 3. Integración en CMake y Targets Públicos

Los consumidores integran el módulo añadiendo `ABDSharedCode` a su build.

```cmake
# En el CMakeLists.txt del consumidor:
target_link_libraries(MiProyecto PRIVATE ABDShared::ScopeCore)
```

### 3.1 Targets Exportados y Frontera de Dependencias

Se delimita la arquitectura de targets en tres niveles ortogonales:

| Target CMake | Tipo | Alcance, Componentes y Propósito |
|---|---|---|
| **`ABDShared::ScopeCoreHeaders`** | `INTERFACE` | **Core C++20 header-only puro (cero dependencias externas).**<br>• Componentes: `<Core/SpscRingBuffer.h>`, `<Core/ScopeTap.h>`, `<Core/ScopeTapType.h>`, `<Core/ScopeDataCollector.h>`, `<Core/TapId.h>`, `<Core/TriggerDetector.h>`, `<Core/ScopeFrameSerializer.h>`.<br>• Cero JUCE, cero WebView2, cero assets web.<br>• Destinado a motores DSP en tiempo real, utilidades CLI, arneses de pruebas unitarias puras y compilaciones WebAssembly (Emscripten). |
| **`ABDShared::ScopeCore`** | `INTERFACE` | **Target principal para hosts de producto con GUI JUCE.**<br>• Incluye: `ScopeCoreHeaders` + pegamento JUCE (`JuceScopeComponent.h`, `JuceWebScopeComponent.h`, `ScopeResourceProvider.h/.cpp`).<br>• **Transitividad de Assets:** Enlaza transitivamente `ABDShared::ScopeWebAssets` (`target_link_libraries(ABDScopeCore INTERFACE ABDScopeWebAssets)` en `Scope/CMakeLists.txt`) cuando JUCE está en alcance.<br>• **Consumo Simplificado:** El producto host solo necesita enlazar `target_link_libraries(MiHost PRIVATE ABDShared::ScopeCore)`. **No se requiere enlazar `ABDShared::ScopeWebAssets` de forma manual**. |
| **`ABDShared::ScopeWebAssets`** | `INTERFACE` | **Binarios de recursos web compilados.**<br>• Generado condicionalmente mediante `juce_add_binary_data(ABDScopeWebAssets ...)` conteniendo los módulos empaquetados de `WebUI/src/*` e `index.html`.<br>• Si `juce_add_binary_data` no está disponible (e.g. toolchains WASM o consumidores sin JUCE), este target no se genera y el módulo opera en modo fallback de sistema de archivos. |

### 3.2 Regla de Exclusión de `add_subdirectory(../ABDScope)`
⛔ **Está terminantemente prohibido incluir `add_subdirectory(../ABDScope)` o un `FetchContent` paralelo del repositorio legado `ABDScope`.**
Dado que `ABDSharedCode` ya registra internamente el módulo en `Scope/`, un segundo registro duplicaría los identificadores de target (`ABDScopeCore`) y rompería la generación de CMake con:
`add_library cannot create target ABDScopeCore because another target with the same name already exists`.

---

## 4. Contrato de API Pública C++20 (RT-Safe)

### 4.1 Captura en Tiempo Real (`ScopeTap`)
* **`writeStereo(const float* l, const float* r, size_t numSamples) noexcept`**:
  * Lock-free, zero heap allocation, thread-safe (SPSC).
  * Si la sonda está inactiva (`isActive() == false`), el host elude el cómputo sin coste en CPU.
* **Activación Atómica**:
  * `isActive()` y `setActive(bool)` operan sobre `std::atomic<bool>` con semántica acquire/release.

### 4.2 Recolector Central (`ScopeDataCollector`)
* **Registro de Taps:** Fuera del hilo de audio (`registerTap()`).
* **Acceso y Activación:**
  * `[[nodiscard]] ScopeTap* getTap(size_t index) noexcept`: Firma tipada no-const que elimina la necesidad de `const_cast` en ventanas y controladores host.
  * `[[nodiscard]] const ScopeTap* getTap(size_t index) const noexcept`: Firma const para consultas de sólo lectura.
  * `findTapIndex(const std::string& query)`: Búsqueda flexible que soporta correspondencia exacta, slug en minúsculas y consultas insensibles a mayúsculas (`DIAG_TONE` -> `"diag_tone"`).
  * `deactivateAll()`: Desactiva atómicamente todas las sondas registradas (invocado al minimizar u ocultar ventanas host).

---

## 5. Contrato de Serialización y Protocolo Wire (JSON)

La comunicación entre el backend C++ y el frontend WebUI embebido se realiza mediante el `ScopeFrameSerializer`, transmitiendo tramas de datos a 30-60 Hz vía IPC (`postMessage`).

### 5.1 Esquema Normativo del Frame JSON

```json
{
  "signalType": "audio",
  "tapId": "hardware_in",
  "numSamples": 256,
  "sampleRate": 48000.0,
  "timeDataL": [0.012, 0.045, -0.021],
  "timeDataR": [0.010, 0.041, -0.019],
  "rmsL": 0.025,
  "rmsR": 0.024,
  "peakL": 0.089,
  "peakR": 0.085,
  "detectedFreq": 440.0,
  "detectedNoteName": "A4"
}
```

* **`signalType`**: `"audio"` (para `StereoAudio`) o `"control"` (para `ControlSignal`). Si es `"control"`, `timeDataR` se omite.
* **`tapId`**: Cadena determinista con el identificador slug de la sonda.
* **`timeDataL` / `timeDataR`**: Muestras normalizadas en el rango `[-1.0, 1.0]`.
* **`detectedFreq` / `detectedNoteName`**: Estimación producida por `TriggerDetector` con histéresis adaptativa al pico.

---

## 6. Frontend WebUI, Temas y Assets

* **Ubicación Canónica de Código Web:** `ABDSharedCode/Scope/WebUI/src/`.
* **Temas e Iconografía:**
  * El módulo Scope no acopla paletas privadas. La apariencia se rige por variables CSS (`scope.css`).
  * Los temas visuales (`audiolab`, `audiolab-light`, `ms2000`, `dark`, etc.) residen canónicamente en `ABDSharedAssets`.
* **Entrega de Recursos:**
  * En producción: `ScopeResourceProvider` sirve los archivos embebidos en el binario sin acceder al sistema de archivos ni abrir servidores HTTP locales.
  * En desarrollo: Fallback automático al árbol local si los binarios no estuvieran empaquetados.

---

## 7. Verificación Autónoma y Aislamiento (Criterio C-6)

Para certificar que el módulo `ABDSharedCode/Scope` es completamente autónomo y no depende de la compilación de `ABDAudioLab`:

1. **Test Standalone C++ (`ABDScope_CppSmoke`):**
   * Configurado en `Scope/CMakeLists.txt` enlazando exclusivamente `ABDScopeCoreHeaders`.
   * Verifica `SpscRingBuffer`, `ScopeDataCollector`, `TriggerDetector` (incluyendo tracking de 55 Hz A1) y serialización JSON.
   * Ejecutable directamente desde `Scope/build.bat` sin requerir JUCE ni dependencias externas.
2. **Suite WebUI Vitest (`WebUI/tests/`):**
   * **62 tests pasando \| 1 omitido (63 tests totales en 9 suites de pruebas)** bajo Node.js / happy-dom:
     - `frame.test.js` (10 tests): Normalización de trama `ScopeDataFrame`, cálculo de picos y RMS.
     - `icons.test.js` (3 tests): Catálogo SVG canónico y paridad con `ABDSharedAssets`.
     - `input.test.js` (5 tests): Proveedor de entrada `PushInput` vía IPC/bridge y `AnalyserInput` WebAudio.
     - `lane.test.js` (11 tests): Empaquetado inteligente de carriles, control de ancho 50%/100%, freeze y snapshots.
     - `renderers.test.js` (13 tests): Osciloscopio temporal, analizador FFT logarítmico, vectorescopio Lissajous M/S, correlación estéreo y espectrograma cascada.
     - `scope.test.js` (3 tests): Orquestador fábrica `createScope`, montaje en DOM y ciclo de vida.
     - `smoke.test.js` (2 tests): Inicialización DOM y arranque básico.
     - `theme.test.js` (4 tests): Inyección de temas, CSS custom properties y paridad con `ABDSharedAssets`.
     - `trigger.test.js` (12 tests): Estabilización por histéresis adaptativa y pitch lock en sub-graves.
   * *Nota:* La suite histórica de 56 tests de v0.3.x creció a 63 tests al incorporar la validación de empaquetado y control de ancho de carriles en `lane.test.js` y `renderers.test.js`.

---

## 8. Checklist de Validación para Nuevos Consumidores

Al integrar `ABDShared::ScopeCore` en un nuevo sintetizador o herramienta:

- [x] JUCE configurado con `JUCE_USE_WIN_WEBVIEW2=ON` (en plataformas Windows).
- [x] Inclusión de `ABDSharedCode` en CMake posterior a la declaración de JUCE.
- [x] Consumo exclusivo de `ABDShared::ScopeCore` o `ABDShared::ScopeCoreHeaders`.
- [x] Cero invocaciones a `const_cast` al activar o consultar sondas.
- [x] Invocación de `collector.deactivateAll()` al cerrar o destruir componentes de interfaz.
- [x] Cero asignaciones en el hilo de audio al invocar `ScopeTap::writeStereo()`.
