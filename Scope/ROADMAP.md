# ABDScope — Hoja de Ruta y Fases de Desarrollo

**Versión:** 2.0.0  
**Fecha:** 2 de Septiembre de 2026  
**Estado:** Planificación Auditada y Corregida

---

## 🎯 Visión General de Fases

```text
┌──────────────────────────────────┐
│ FASE 1: Base Robusta (Core)      │ ──► Trigger, Frame, Inputs, HiDPI, Wire Protocol, Demo
└───────────┬──────────────────────┘
            ▼
┌──────────────────────────────────┐
│ FASE 2: Renderers Fundamentales  │ ──► Osciloscopio + Espectro FFT + VU Meters + Nota detectada
└───────────┬──────────────────────┘
            ▼
┌──────────────────────────────────┐
│ FASE 3: Modos Avanzados         │ ──► Lissajous + Phase Meter + Theming + Spectrogram + Export
└───────────┬──────────────────────┘
            ▼
┌──────────────────────────────────┐
│ FASE 4: Capa C++ & JUCE         │ ──► ScopeDataCollector Lock-Free + Componentes JUCE nativos
└───────────┬──────────────────────┘
            ▼
┌──────────────────────────────────┐
│ FASE 5: Integración en Suite    │ ──► Reemplazo en ABDMS2000, ABDCZ101, ABDAudioLab (Zero-Copy)
└──────────────────────────────────┘
```

---

## 📋 Detalle de Fases y Tareas

### FASE 1: La Base Robusta (Core Engine, Dual Input, Wire Protocol & Test Harness)
> **Objetivo:** Construir la base matemática, el contrato de datos y ambos modos de entrada antes de pintar píxeles finales. Incluir el wire protocol C++ → JS desde el día uno para no tener que rehacer la base cuando integremos con plugins.

- [x] **1.1 Inicialización del Repositorio:**
  - Configuración de `package.json` (`@abdsynths/scope`), `.gitignore`, configuración de `vitest`.
  - Estructura de carpetas: `WebUI/src/`, `WebUI/demo/`, `WebUI/tests/`, `Source/Core/`, `docs/`.

- [x] **1.2 Algoritmo de Triggering con Histéresis (`src/trigger.js`):**
  - Implementación de cruce por cero (*zero-crossing*) con ventana de histéresis regulable.
  - Estimador de frecuencia fundamental / pitch para auto-timebase.
  - Detección de nota MIDI equivalente (`frequencyToNoteName()`).
  - Desactivación automática del trigger cuando `signalType === 'control'`.
  - Batería de tests unitarios con Vitest (`tests/trigger.test.js`) contra senos puros, sierras, cuadradas, FM y señales con ruido.

- [x] **1.3 Normalizador de Frame de Datos (`src/frame.js`):**
  - Transformación de buffers PCM o datos de `AnalyserNode` a `ScopeDataFrame` normalizado.
  - Campo `signalType: 'audio' | 'control'` para adaptar rango de ejes y comportamiento.
  - Cálculo de RMS, Peak y correlación de fase estéreo.
  - Tests unitarios (`tests/frame.test.js`).

- [x] **1.4 Adaptadores de Entrada Dual (`src/input/`):**
  - **`AnalyserInput.js`:** Encapsula un `AnalyserNode` de Web Audio. Arranca un `requestAnimationFrame` loop interno que extrae `getFloatTimeDomainData` / `getFloatFrequencyData` y genera `ScopeDataFrame` automáticamente.
  - **`PushInput.js`:** Recibe frames externos vía `pushFrame()`. No tiene render loop propio — el consumidor controla la cadencia.
  - Tests unitarios (`tests/input.test.js`).

- [x] **1.5 Arquitectura Base de Renderers (`src/renderers/BaseRenderer.js`):**
  - Definición de la interfaz `IScopeRenderer` con ciclo de vida completo:
    - `init(canvas, options)`
    - `render(dataFrame, options)`
    - `resize(width, height, devicePixelRatio)`
    - **`destroy()`** — limpieza obligatoria: cancela `rAF`, desconecta nodos, anula referencias.

- [x] **1.6 HiDPI / Retina desde el día uno:**
  - `ResizeObserver` sobre el contenedor.
  - Multiplicación del canvas por `devicePixelRatio`.
  - Escalado del contexto 2D con `ctx.scale(dpr, dpr)`.

- [x] **1.7 Modos de Montaje (`src/mount/`):**
  - **`EmbeddedMount.js`:** Panel integrado en la UI del sintetizador.
  - **`FloatingMount.js`:** Modal flotante arrastrable con botón de cierre, animación fade-in y métodos `open()` / `close()` / `toggle()`.

- [x] **1.8 Factory Principal (`src/scope.js`):**
  - Implementación de `createScope({ containerId, mountMode, enabledModes, showVuMeters, ... })`.
  - Auto-ocultación de pestañas si `enabledModes.length === 1`.
  - Método `destroy()` que propaga a todos los renderers, inputs, observers y DOM.
  - Métodos de entrada: `connectAnalyser(node)` y `pushFrame(dataFrame)`.

- [x] **1.9 Wire Protocol (`docs/DATA_CONTRACT.md`):**
  - Especificación del formato JSON de frames enviados desde C++ vía JUCE WebView bridge.
  - Frecuencia: 30 Hz (~33 ms).
  - Campos, tipos y rangos documentados.
  - Ruta de optimización futura: transferencia binaria vía `ArrayBuffer`.

- [x] **1.10 Banco de Pruebas Interactivo (`demo/`):**
  - `demo/index.html` con generador Web Audio de tonos (Seno, Sierra, Cuadrada, Ruido, FM, entrada de Micrófono).
  - Selector de modos, controles de zoom/timebase, selector de canal, toggle montaje embebido/flotante.
  - Simulador de datos push (para probar el modo Bridge sin necesitar un plugin real).

---

### FASE 2: Modos Fundamentales (Osciloscopio, Espectro FFT, VU Meters)
> **Objetivo:** Implementar con máxima fidelidad visual y rendimiento de 60 FPS los modos más utilizados en sintetizadores.

- [x] **2.1 `OscilloscopeRenderer.js`:**
  - Renderizado de traza temporal sobre Canvas 2D acelerado.
  - Soporte de modos estéreo (Traza L en cian, Traza R en magenta) y mono superpuesto.
  - Control de escala temporal (*Timebase* / zoom de ms a µs) y ganancia (*Volts/Div*).
  - Rejilla de osciloscopio estilo retícula analógica con divisiones horizontales/verticales.
  - Modo *Freeze* (congelación de traza para análisis).
  - Indicador de nota / frecuencia detectada en la cabecera (ej. "A4 — 440 Hz").
  - Adaptación automática para `signalType === 'control'`: eje Y adaptado, escala temporal en segundos, sin trigger.
  - Persistencia analógica de fósforo CRT.

- [x] **2.2 `SpectrumRenderer.js`:**
  - Renderizado espectral logarítmico de 20 Hz a 20 kHz.
  - Rejilla de dB (-96 dB a 0 dB) y etiquetas de frecuencia (50, 100, 200, 500, 1k, 2k, 5k, 10k, 20k).
  - Curva de respuesta con gradiente translúcido de relleno.
  - Barras / marcadores de *Peak-Hold* con decaimiento suave (~30 dB/s).

- [x] **2.3 `VuMeterRenderer.js`:**
  - Barras verticales L/R con gradiente de color (verde → amarillo → rojo).
  - Indicador de peak con retención y caída lenta.
  - Se muestra al lado del renderer activo cuando `showVuMeters: true`.

---

### FASE 3: Modos Avanzados, Theming & Extras
> **Objetivo:** Incorporar los modos visuales de alto nivel, personalización de colores/temas y exportación de imágenes.

- [x] **3.1 `LissajousRenderer.js` (Goniómetro / Vectorescopio X-Y):**
  - Mapeo de Lissajous estéreo con rotación de $45^\circ$ ($X = \frac{L-R}{\sqrt{2}}$, $Y = \frac{L+R}{\sqrt{2}}$) para orientación de vectorescope tradicional ($M/S$).
  - Estela de persistencia de fósforo CRT.
  - Retícula circular con ejes $M/S$ y $L/R$.

- [x] **3.2 `PhaseMeterRenderer.js`:**
  - Medidor de correlación de fase estéreo de -1.0 a +1.0 con amortiguamiento balístico y zonas de color (Rojo anti-fase, Amarillo estéreo ancho, Verde mono).
  - Indicador numérico dinámico.

- [x] **3.3 Theming y Variables CSS (`src/scope.css`):**
  - Paletas preconfiguradas: `ms2000` (cian), `cz101` (rojo), `deepmind` (ámbar), `audiolab` (verde nórdico).
  - Mapeo automático de variables CSS del host (`--scope-bg`, `--scope-trace`, etc.).
  - Fallback a tema oscuro por defecto si no hay variables del host.

- [x] **3.4 Modo Spectrogram / Waterfall (Cascada):**
  - Cascada temporal-frecuencial 2D acumulativa con scroll vertical continuo y paleta de calor plasma/inferno.

- [x] **3.5 Exportación de captura a imagen/clipboard (`src/utils/exportImage.js`):**
  - Métodos `captureFrame()` (copia PNG al portapapeles) y `downloadFrame()` (descarga directa de PNG).

---

### FASE 4: Capa C++ Core & JUCE (Lock-Free Multi-Tap)
> **Objetivo:** Proveer el backend DSP en C++ para plugins VST y el componente nativo para `ABDAudioLab`.

- [x] **4.1 `ScopeTap.h` & `ScopeDataCollector.h` (C++20 Puro):**
  - Buffer circular SPSC *lock-free* y *zero-allocation*.
  - Bandera atómica de activación on-demand (`isTapActive`) con coste < 1 ns en audio thread.
  - Soporte de tipos: `StereoAudio`, `MonoAudio`, `ControlSignal`.

- [x] **4.2 `ScopeFrameSerializer.h`:**
  - Serialización de `ScopeDataFrame` a JSON para envío por JUCE WebView bridge.
  - Submuestreo temporal a ~512 muestras por frame a 30 Hz.

- [x] **4.3 `TriggerDetector.h` (C++):**
  - Implementación del cruce por cero con histéresis (adaptativa por defecto), estimador de nota MIDI con octava (p. ej. "A4") y slug determinista del tap (`TapId.h`) en C++.

- [x] **4.4 `JuceScopeComponent.h`:**
  - Componente gráfico nativo `juce::Component` para aplicaciones puramente C++ (reemplazo en ABDAudioLab).

- [x] **4.5 `CMakeLists.txt`:**
  - Configuración del target `ABDScope::ABDScopeCore` con exportación de include directories.

---

### FASE 5: Integración en la Suite y Validación (Zero-Copy)
> **Objetivo:** Conectar `ABDScope` a los proyectos de la suite reemplazando los scopes fake/placeholder.

- [ ] **5.1 Integración en `ABDMS2000`:**
  - Creación de Junction NTFS (`mklink /J`) para WebUI.
  - Reemplazo de `OscilloscopeModal.js` (scope fake) y `panelScope.js` (scope placeholder) por `createScope()`.
  - Eliminación del anti-patrón `window.updateAudioSnapshot`.
  - Conexión del `ScopeDataCollector` en `processBlock` del C++ del MS2000.

- [ ] **5.2 Integración en `ABDAudioLab`:**
  - Conexión vía CMake `add_subdirectory`.
  - Reemplazo de `LiveSpectrumAnalyzer` y pestaña Spectrum de `SoundIdCurvePlotter`.

- [ ] **5.3 Integración en otros sintetizadores (`ABDCZ101`, `ABDEep`, etc.):**
  - Patrón de copia rápida: junction + import + `createScope()` en 5 líneas.

- [x] **5.4 Documentación de Integración y Guía de Uso:**
  - `docs/INTEGRATION_GUIDE.md`: Guía paso a paso para conectar sintetizadores vía NTFS Junction y CMake en < 5 minutos.
  - `docs/USAGE_GUIDE.md`: Manual de referencia completo de API, opciones de inicialización, temas, flags y control de modos.

---

### FASE 6: Mejoras Futuras & Evolución (Post-V1)
> **Objetivo:** Funcionalidades avanzadas y extensiones para análisis multi-canal.

- [x] **6.1 Modo Multi-Lane / Multi-Canal Apilado:**
  - Posibilidad de apilar varias líneas de visualización (carriles independientes con modo y sonda independientes) en modo embebido y flotante con botones `[ 1 ]`, `[ 2 ]`, `[ 3 ]` para comparar simultáneamente múltiples señales (ej. Osc 1 en Osciloscopio + Master en Espectro).
- [ ] **6.2 Selector de Ventanas FFT (Hann, Blackman-Harris, Flat-Top):**
  - Descartado / Posponer (Hann fijo optimizado para sintetizadores y rendimiento).
- [ ] **6.3 Modos de Visualización 3D / Isometric Waterfall:**
  - Descartado (Spectrogram 2D continuo con mapa de calor plasma/inferno ofrece máxima claridad métrica y rendimiento).
- [x] **6.4 Refinamiento de Trigger en Sub-graves (< 140 Hz):**
  - Histéresis adaptativa escalada a amplitud de pico (por defecto en C++ y JS) y ventana de búsqueda extendida (4096 muestras) para estabilización perfecta en notas de bajo profundo (20 Hz a 60 Hz).

---

## 🚦 Criterios de Aceptación (Definición de Hecho)

1. **100% Tests Pass:** Toda la batería de pruebas de Vitest (`npm test`) pasa sin errores.
2. **Estabilidad Visual:** Las formas de onda sinusoidales, de sierra y cuadradas permanecen estables en pantalla sin vibración de fase.
3. **Cero Impacto de CPU:** En C++, los taps inactivos no realizan copias de memoria ni llamadas en el audio thread.
4. **Cero Reprogramación:** Un proyecto puede inicializar el componente con cualquier combinación de flags (`enabledModes`) sin modificar el código fuente del módulo.
5. **HiDPI Nítido:** El renderizado se ve perfectamente nítido en pantallas 1x, 2x (Retina) y 3x (4K).
6. **Entrada Dual Validada:** El demo standalone funciona tanto con Web Audio Directo (`connectAnalyser`) como con simulación de Push (`pushFrame`).
7. **Sin Memory Leaks:** Al destruir (`destroy()`), el heap de la aplicación no crece respecto al estado previo a la creación del scope.
