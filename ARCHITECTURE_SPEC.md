# ABDScope — Especificación Técnica y Arquitectura de Módulo Reutilizable

**Versión:** 2.0.0  
**Fecha:** 2 de Septiembre de 2026  
**Autor:** Antigravity / DeepMind Pair Programming  
**Repositorio Principal:** `D:\desarrollos\ABDSynths\ABDScope`  
**Proyectos Consumidores:**
- Sintetizadores VST3/AU/Standalone: `ABDMS2000`, `ABDCZ101`, `ABDJUNiO601`, `ABDEep`, `ABDPro008`, etc.
- Herramientas Científicas / Diagnóstico: `ABDAudioLab`.

---

## 1. Visión y Objetivos del Proyecto

`ABDScope` es un **módulo universal y reutilizable de visualización y análisis de audio** diseñado bajo la filosofía **DRY (Don't Repeat Yourself)** y **Zero-Copy**:

1. **Objetivo Principal**: Proveer visualización gráfica de alta fidelidad (Osciloscopio, Analizador de Espectro FFT, Lissajous/Goniómetro, Medidores VU/Fase) a todos los sintetizadores e instrumentos de la suite ABDSynths, reemplazando los scopes decorativos/fake actuales con visualización de datos DSP reales.
2. **Objetivo Secundario**: Reemplazar la visualización de `ABDAudioLab` (`LiveSpectrumAnalyzer`, `SoundIdCurvePlotter`) consumiendo este módulo centralizado, asegurando que cualquier mejora, optimización o nuevo modo se propague inmediatamente a toda la suite.
3. **Cero Reprogramación (Flags declarativos)**: Cada proyecto consumidor puede encender o apagar modos con flags sin modificar el código del componente.
4. **Seguridad y Cero Sobrecarga de Audio**: Operación 100% *lock-free*, *zero-allocation* en el audio thread y arquitectura **Multi-Tap On-Demand** (solo se procesan y transmiten los puntos de medición que el usuario está visualizando activamente).

---

## 2. Principios de Ingeniería y Estándares de Código

> **Lecciones aprendidas:** En proyectos anteriores de la suite (`ABDMIDIKeyb`, `ABDMS2000`), la acumulación de funciones en ficheros monolíticos (ej. `keyboard.js` con 949 líneas y 171 funciones, `app.js` con 1753 líneas) ha provocado regresiones difíciles de detectar y corregir. ABDScope nace con estas reglas para no repetir esos errores.

### A. Responsabilidad Única por Fichero (SRP Estricto)

| Regla | Límite |
|---|---|
| **Máximo de líneas por fichero JS/TS** | **200 líneas** (excluidos comentarios y líneas en blanco). Si supera este umbral, debe dividirse. |
| **Máximo de funciones exportadas por fichero** | **5–8 funciones** relacionadas. Si son heterogéneas, mover a ficheros separados. |
| **Máximo de líneas por fichero C++ (.h/.cpp)** | **300 líneas** por fichero. Headers ligeros (declaraciones), implementación separada en `.cpp`. |
| **Un fichero = una responsabilidad** | Cada fichero tiene un propósito único y claro. Si necesitas escribir "y" para describir lo que hace, es candidato a separarse. |

### B. Granularidad de Módulos (Anti-Monolito)

```text
❌ MAL (patrón ABDMIDIKeyb):
    keyboard.js  →  949 líneas = DOM + eventos + MIDI + dibujo + config + QWERTY

✅ BIEN (patrón ABDScope):
    scope.js           →  Factory y orquestación (~100 líneas)
    trigger.js         →  Solo algoritmo de trigger (~120 líneas)
    frame.js           →  Solo transformación de datos (~80 líneas)
    AnalyserInput.js   →  Solo adaptador Web Audio (~60 líneas)
    PushInput.js       →  Solo adaptador push/bridge (~40 líneas)
    EmbeddedMount.js   →  Solo montaje embebido (~80 líneas)
    FloatingMount.js   →  Solo montaje flotante (~100 líneas)
    BaseRenderer.js    →  Solo interfaz base (~40 líneas)
    OscilloscopeRenderer.js → Solo renderizado de osciloscopio (~150 líneas)
```

### C. Reglas de Seguridad en Audio Thread (de la Skill JUCE + Lecciones ABDAudioLab)

Estas reglas se aplican a **toda la capa C++ de ABDScope** (`Source/Core/`):

1. **Zero Memory Allocation**: NUNCA llamar `new`, `malloc`, `std::vector::push_back`, `std::string` en el audio thread. Pre-alocar todo en `prepareToPlay`.
2. **Lock-Free Communication**: NUNCA usar `std::mutex`, `CriticalSection` ni ningún bloqueo en el audio thread. Solo `std::atomic` y buffers SPSC lock-free.
3. **No System/I/O Calls**: Nada de `DBG`, `printf`, `file I/O`, ni llamadas a UI en `processBlock`.
4. **Denormal Prevention**: `juce::ScopedNoDenormals` al inicio de todo procesado.
5. **Memory Ordering correcto** (lección ABDAudioLab §2): No usar `memory_order_relaxed` al leer datos publicados por otro thread. Usar `release` al publicar y `acquire` al leer:
   ```cpp
   // Writer (message thread): publicar frame listo
   frameReady.store(true, std::memory_order_release);
   // Reader (UI thread): leer frame publicado
   if (frameReady.load(std::memory_order_acquire)) { /* datos visibles */ }
   ```
6. **TOCTOU — Snapshot de puntero** (lección ABDAudioLab §3): Nunca verificar validez de un puntero/recurso y luego usarlo en dos pasos si otro thread puede invalidarlo. Capturar en una variable local:
   ```cpp
   // MAL:
   if (analyserNode) analyserNode->getData(); // puede ser null entre las dos líneas
   // BIEN:
   auto* node = analyserNode.load(); if (node) node->getData();
   ```
7. **`unique_ptr` no es thread-safe** (lección ABDAudioLab §4): Si un `unique_ptr` se comparte entre threads, protegerlo con `std::mutex` o reemplazar por un patrón lock-free (puntero atómico raw + destrucción diferida).

### D. Reglas de Calidad WebUI y Frontend (de ABDSKILLS & Skill JUCE)

1. **Vanilla CSS & Tokens Obligatorios**: Sin frameworks CSS pesados (Tailwind/Bootstrap). Toda propiedad visual debe usar CSS Custom Properties (`--scope-bg`, `--scope-grid`, `--color-accent`) heredables del host. Prohibido hardcodear colores HEX/RGB en el código de componentes.
2. **Iconografía Monocromática Vectorial (Cero Emojis)**: Prohibido terminantemente el uso de emojis Unicode (📷, ⚙️, 🔴, ❌) en botones, controles o badges de telemetría. Usar iconos vectoriales SVG monocromáticos configurados con `currentColor` / variables CSS o formas vectoriales CSS puras, centralizados en `ABDSharedAssets/icons/`.
3. **100% English en Código Técnico**: Todos los identificadores técnicos, nombres de variables, propiedades, eventos, clases y schemas (`ScopeDataFrame`) deben estar en inglés técnico estricto.
4. **Cero `console.log` en Producción**: Toda traza de depuración debe encapsularse en un logger con flags de depuración o eliminarse para builds de producción.
5. **Throttling & High-Frequency Safety**: Limitar eventos de alta frecuencia con `requestAnimationFrame`. No enviar más de un mensaje por frame al bridge IPC.
6. **Zero-Copy Heap & Garbage Collector Avoidance**: Reutilizar instancias de `Float32Array` y `Uint8Array`. Prohibido instanciar nuevos TypedArrays en cada ciclo de render o audio callback.
7. **Ciclo de Vida y Limpieza Explícita (`destroy`)**: Todo componente o renderer debe implementar `destroy()` que desconecte `AnalyserNode`, cancele `requestAnimationFrame`, desconecte `ResizeObserver`, remueva listeners del DOM y anule referencias a TypedArrays.

### E. Reglas de Testing y Verificación

1. **Test antes de pintar**: Los algoritmos core (trigger, frame, métricas) deben tener tests unitarios que pasen **antes** de que exista ningún renderer.
2. **Tests en cada fichero nuevo**: Todo fichero con lógica (no solo UI/DOM) debe tener su fichero `.test.js` correspondiente.
3. **100% tests pass & Cobertura >= 80%**: El CI (o `npm test`) nunca puede tener tests rotos en `main`. La lógica matemática y de transformación debe mantener al menos un 80% de cobertura.
4. **Test roto = arreglar o borrar** (lección ABDAudioLab §8): Un test que falla y se ignora es peor que no tener test — crea falsa sensación de cobertura y oculta regresiones reales. Si un test falla, se arregla o se borra en el mismo commit.
5. **Nada de stubs en producción** (lección ABDAudioLab §9): Si una función de análisis devuelve un valor constante hardcodeado (`return 0.02f`), es un stub. Los stubs van en código de test, nunca en producción.

### F. Higiene de Código y Cuatro Documentos Obligatorios (ABDSKILLS §0)

1. **Cero código muerto**: Si un archivo, clase o función no se usa, se borra. No se deja "por si acaso" — es deuda técnica encubierta que consume tiempo de compilación y confunde auditorías.
2. **Cero `#include` / `import` muertos**: Si no usas nada de un módulo, quita el import. Las dependencias fantasma crean acoplamiento invisible que rompe al refactorizar.
3. **DRY sin excepciones**: Si una lógica aparece en más de un archivo, extraerla a un módulo compartido.
4. **Los 4 Documentos Obligatorios desde el Día 1**:
   - `README.md`: Visión general, features, arquitectura y guía rápida de uso.
   - `CHANGELOG.md`: Historial de versiones semánticas (SemVer).
   - `HANDOFF.md`: Estado actual, decisiones clave y contexto para relevo entre desarrolladores/agentes.
   - `ROADMAP.md`: Fases con Definición de Hecho y checkboxes actualizados en cada avance.

### G. Regla de Revisión Pre-Commit

Antes de dar por terminado un fichero, verificar:
- [ ] ¿Tiene menos de 200 líneas (JS) o 300 líneas (C++)?
- [ ] ¿Cada función tiene una sola responsabilidad?
- [ ] ¿Hay funciones que podrían vivir en un fichero `utils` o auxiliar dedicado?
- [ ] ¿El fichero tiene tests unitarios con cobertura adecuada?
- [ ] ¿El audio thread es 100% lock-free y zero-allocation?
- [ ] ¿Se usa `acquire`/`release` en los atómicos donde corresponde?
- [ ] ¿No hay imports/includes muertos ni `console.log` residuales?
- [ ] ¿No hay estilos hardcodeados (se usan CSS Custom Properties)?
- [ ] ¿No hay valores hardcodeados que deberían calcularse?

---

## 3. Estrategia de Enlace e Integración (Zero-Copy & DRY)

Siguiendo las directrices de `ABDAudioLab/docs/SHARED_ASSETS_GUIDE.md`:

### A. Capa WebUI (Plugins Híbridos VST3/AU con WebView2)
- Se vincula directamente a la carpeta `WebUI` de cada sintetizador mediante **Directory Junction NTFS**:
  ```cmd
  mklink /J "WebUI\src\components\scope" "..\..\ABDScope\WebUI\src"
  ```
- Cualquier ajuste de estilo CSS o mejora del motor Canvas se refleja en tiempo real en todos los sintetizadores dependientes.

### B. Capa C++ (JUCE Engine & ABDAudioLab)
- Integración en `CMakeLists.txt` del host:
  ```cmake
  # En CMakeLists.txt de ABDMS2000, ABDAudioLab, etc.
  add_subdirectory(../ABDScope ${CMAKE_CURRENT_BINARY_DIR}/ABDScope)
  target_link_libraries(${PROJECT_NAME} PRIVATE ABDScope::ABDScopeCore)
  ```
- **Garantía de ABI:** Ambos proyectos se compilan juntos con las mismas banderas de compilador (`/MD`, `/std:c++20`, `/permissive-`).

---

## 4. Arquitectura del Sistema (Strategy & Pipeline Pattern)

Para garantizar una **base técnica sólida e indestructible**, el sistema se divide estrictamente en 3 capas desacopladas:

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ 1. CAPA DE ENTRADA DE DATOS (Dual: Web Audio Directo + Bridge Push)          │
│                                                                              │
│  ┌─────────────────────────────┐     ┌────────────────────────────────────┐  │
│  │ Modo A: Web Audio Directo   │     │ Modo B: Bridge / WASM / C++ Push  │  │
│  │ scope.connectAnalyser(node) │     │ scope.pushFrame(dataFrame)        │  │
│  │ (Demo Standalone / WASM)    │     │ (Plugin VST3 vía JUCE IPC)       │  │
│  └─────────────┬───────────────┘     └──────────────────┬─────────────────┘  │
│                └───────────────┬─────────────────────────┘                   │
└────────────────────────────────┼─────────────────────────────────────────────┘
                                 ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│ 2. CAPA DSP / CAPTURA DE AUDIO (Lock-Free Multi-Tap On-Demand)               │
│                                                                              │
│    ┌──────────────┐         ┌───────────────┐        ┌────────────────────┐  │
│    │ Tap: Master  │         │ Tap: PreFilter│        │ Tap: LFO / Envelop │  │
│    └──────┬───────┘         └───────┬───────┘        └─────────┬──────────┘  │
│           │ [isActive==true]        │ [isActive==false]        │ [isActive==false]
│           ▼                         ▼                          ▼             │
│    ┌──────────────┐            (CERO CPU)                 (CERO CPU)         │
│    │ SPSC RingBuf │                                                          │
│    └──────┬───────┘                                                          │
└───────────┼──────────────────────────────────────────────────────────────────┘
            │ Lectura Lock-free (30 / 60 FPS)
            ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│ 3. MOTOR DE PROCESAMIENTO & FRAME DATA                                        │
│    - Trigger Engine (Zero-Crossing + Histéresis + Auto-Pitch / Period)       │
│    - FFT Pipeline (Ventana Hann + Cálculo de magnitudes logarítmicas dBfs)   │
│    - Medidores (RMS, Peak, Coeficiente de correlación de fase estéreo)       │
│    - HiDPI Scaling (ResizeObserver + devicePixelRatio)                       │
└───────────────────────────────────┬──────────────────────────────────────────┘
                                    │ ScopeDataFrame normalizado
                                    ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│ 4. CONTRATO DE RENDERERS (Plugin View Modes)                                 │
│    Interface IScopeRenderer: init(), render(), resize(), destroy()          │
│                                                                              │
│   ┌────────────┬────────────┬────────────┬───────────┬──────────────────┐    │
│   ▼            ▼            ▼            ▼           ▼                  ▼    │
│ ┌──────────┐┌──────────┐┌──────────┐┌─────────┐┌──────────┐┌──────────────┐ │
│ │Oscillosc.││ Spectrum ││Lissajous ││ Phase/  ││ VU Meter ││ Spectrogram  │ │
│ │ Renderer ││ Renderer ││ Renderer ││ Correl. ││ Renderer ││ (Futuro)     │ │
│ └──────────┘└──────────┘└──────────┘└─────────┘└──────────┘└──────────────┘ │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. Estructura del Frame de Datos (`ScopeDataFrame`)

Tanto en C++ como en JavaScript, los datos transferidos hacia los renderers siguen una estructura fija y normalizada:

```typescript
interface ScopeDataFrame {
  // Tipo de señal (determina comportamiento del renderer)
  signalType: 'audio' | 'control';
  // 'audio'  → PCM bipolar ±1.0, trigger activo, FFT activo, escala en ms
  // 'control' → CV/LFO 0..1 o ±1.0, sin trigger, sin FFT, escala en segundos

  // Dominio temporal (PCM normalizado -1.0 a +1.0 para audio, 0..1 o ±1.0 para CV)
  timeDataL: Float32Array;        // Canal izquierdo / Mono / Señal CV
  timeDataR: Float32Array | null; // Canal derecho (null si mono o señal de control)
  numSamples: number;             // Longitud útil (ej. 512, 1024 o 2048)
  sampleRate: number;             // Frecuencia de muestreo (ej. 44100, 48000, 96000)

  // Sincronización de onda (solo relevante si signalType === 'audio')
  triggerIndex: number;           // Índice donde se detectó el cruce por cero óptimo
  estimatedFrequencyHz: number;   // Frecuencia fundamental detectada (para auto-timebase)
  detectedNoteName: string;       // Nota MIDI equivalente (ej. 'A4') — informativo

  // Dominio frecuencial (solo relevante si signalType === 'audio')
  spectrumDb: Float32Array | null;  // Magnitudes en dB (512 o 1024 bins). null si no calculado
  spectrumBins: number;

  // Medición y métricas de fase
  rmsL: number;
  rmsR: number;
  peakL: number;
  peakR: number;
  phaseCorrelation: number;       // Coeficiente [-1.0 .. +1.0] (+1=Mono, 0=Estéreo, -1=Anti-fase)
}
```

---

## 6. Entrada Dual de Datos

ABDScope admite **dos modos de alimentación de datos**, seleccionados automáticamente según el contexto:

### A. Modo Web Audio Directo (`connectAnalyser`)
Para modo standalone (web sin JUCE), WASM y entornos donde hay un `AudioContext` accesible:
```javascript
const scope = createScope({ containerId: 'scope-view', enabledModes: ['oscilloscope', 'spectrum'] });

// Conectar un AnalyserNode del Web Audio API
const analyser = audioCtx.createAnalyser();
analyser.fftSize = 2048;
masterGain.connect(analyser);
scope.connectAnalyser(analyser);
// ABDScope gestiona internamente el requestAnimationFrame y la extracción de datos
```

### B. Modo Bridge / Push (`pushFrame`)
Para plugins VST3/AU donde el C++ envía datos vía IPC/WebView bridge:
```javascript
const scope = createScope({ containerId: 'scope-view', enabledModes: ['oscilloscope'] });

// Desde el bridge, al recibir un snapshot del C++:
bridge.on('scopeFrame', (frame) => {
  scope.pushFrame(frame);
});
```

> **Regla:** Ambos modos son mutuamente excluyentes por instancia. Si se llama a `connectAnalyser()`, el scope arranca su propio render loop interno. Si se usa `pushFrame()`, el consumidor controla la cadencia de actualización.

---

## 7. Wire Protocol (Contrato de Serialización C++ → JavaScript)

Para plugins VST3 que usan JUCE WebView2 bridge, el C++ envía frames serializados como eventos JSON ligeros:

```json
{
  "type": "scopeFrame",
  "signalType": "audio",
  "timeDataL": [0.0, 0.12, 0.23, ...],
  "timeDataR": [0.0, 0.11, 0.22, ...],
  "numSamples": 512,
  "sampleRate": 44100,
  "rmsL": 0.34,
  "rmsR": 0.31,
  "peakL": 0.78,
  "peakR": 0.72,
  "phaseCorrelation": 0.92
}
```

> **Frecuencia de envío:** 30 Hz (cada ~33 ms). El C++ submuestrea y empaqueta en el message thread, nunca en el audio thread.

> **Optimización futura:** Si el JSON resulta demasiado pesado, migrar a transferencia binaria vía `ArrayBuffer` compartido.

---

## 8. Algoritmos Clave de Calidad de Estudio

### A. Triggering con Histéresis y Auto-Timebase (Inspiración Signalizer / Surge)
Para que la forma de onda permanezca estática en pantalla sin jitter visual:
1. **Histéresis:** Se define un umbral de rearme $T_{low} = -\epsilon$ y un umbral de disparo $T_{high} = +\epsilon$. La señal debe descender por debajo de $T_{low}$ antes de que un cruce ascendente por $T_{high}$ dispare el frame.
2. **Auto-Period Matching:** Se calcula el periodo fundamental aproximado mediante autocorrelación rápida o espaciado de cruces por cero. El tiempo de barrido visual se ajusta automáticamente para mostrar exactamente 1, 2 o 4 ciclos completos de la nota que se esté tocando.
3. **Indicador de nota:** Cuando la frecuencia es estable, se muestra la nota MIDI equivalente (ej. "A4 — 440 Hz") usando `midiToName()` de `@abdsynths/midi-keyb`.

### B. Analizador FFT Logarítmico con Peak-Hold y Decay (Inspiración ABDAudioLab)
1. Ventana de análisis: **Hann** o **Blackman-Harris** de 2048 puntos.
2. Escala frecuencial logarítmica de 20 Hz a 20 kHz:
   $$\text{normX} = \frac{\log_{10}(f) - \log_{10}(20)}{\log_{10}(20000) - \log_{10}(20)}$$
3. Decaimiento balístico: Ataque instantáneo, caída exponencial a $\approx 30\text{ dB/s}$ con indicador de retención de picos (*peak-hold* de 1.5 segundos).

### C. Goniómetro / Lissajous X-Y con Persistencia de Fósforo (Inspiración ljv)
1. Mapeo: $X = \text{Left}(t)$, $Y = \text{Right}(t)$ rotado $45^\circ$ para orientación de vectorescope tradicional ($M/S$).
2. Persistencia analógica mediante refresco de Canvas con opacidad alfa controlada, creando una estela fluida idéntica a un osciloscopio analógico de rayos catódicos.

### D. HiDPI / Retina Rendering
1. Escucha de `ResizeObserver` sobre el contenedor.
2. Multiplicación de `canvas.width` y `canvas.height` por `window.devicePixelRatio`.
3. Escalado del contexto 2D con `ctx.scale(dpr, dpr)` para renderizado nítido en pantallas 4K.

---

## 9. Sistema de Flags, Modos de Montaje y Contrato de Uso

```javascript
import { createScope } from './components/scope/scope.js';

// ─── Caso 1: Panel embebido con Multi-Lane Split View (Grid Responsive 2 Columnas) ───
const scope = createScope({
  containerId: 'scope-view',
  mountMode: 'embedded',                // Panel integrado en la UI del sinte
  maxLanes: 4,                          // Soporta de 1 a 4 carriles diagnósticos
  layout: '2',                          // Comienza en modo Dual Split
  enabledModes: ['oscilloscope', 'spectrum', 'lissajous', 'phase', 'spectrogram'],
  defaultMode: 'oscilloscope',

  // Sondas multi-tap expuestas por el sintetizador
  availableTaps: [
    { id: 'master', name: 'Master Out' },
    { id: 'osc1',   name: 'Oscillator 1' },
    { id: 'osc2',   name: 'Oscillator 2' },
    { id: 'filter', name: 'Ladder Filter' }
  ],
  defaultTap: 'master',

  // VU Meters compañeros
  showVuMeters: true,
  showFreeze: true,
  showSnapshot: true,

  // Callback de cambio de tap por carril
  onTapChange: (tapId, laneIdx) => {
    bridge.setActiveScopeTap(tapId, laneIdx);
  }
});

// ─── Caso 2: Modal flotante arrastrable con Multi-Lane ───
const floatingScope = createScope({
  containerId: 'floating-scope',
  mountMode: 'floating',                // Widget flotante con drag, open/close, botón ×
  maxLanes: 4,
  layout: '1',
  enabledModes: ['oscilloscope', 'spectrum', 'lissajous', 'phase', 'spectrogram'],
  showVuMeters: false
});
floatingScope.open();   // Muestra con animación
floatingScope.close();  // Oculta
floatingScope.toggle(); // Alterna

// ─── Caso 3: Solo 1 carril / Solo 1 modo → máxima ligereza y área útil ───
const miniScope = createScope({
  containerId: 'mini-scope',
  maxLanes: 1,
  enabledModes: ['oscilloscope'],
  showVuMeters: false
});
```

---

## 10. Ciclo de Vida Completo (`IScopeRenderer`)

Cada renderer implementa un contrato estricto para evitar memory leaks:

```javascript
class IScopeRenderer {
  /** Inicializa el canvas, listeners y recursos GPU */
  init(canvas, options) {}

  /** Renderiza un frame de datos en el canvas */
  render(dataFrame, renderOptions) {}

  /** Responde a cambios de tamaño del contenedor (incluye HiDPI) */
  resize(width, height, devicePixelRatio) {}

  /** Libera todos los recursos: cancela rAF, desconecta AnalyserNode,
   *  elimina DOM generado, anula referencias a TypedArrays */
  destroy() {}
}
```

La instancia creada por `createScope()` también expone `destroy()`:
```javascript
const scope = createScope({ ... });
// ... uso ...
scope.destroy(); // Limpia TODO: renderers, DOM, observers, animaciones
```

---

## 11. Mecanismo de Captura "Multi-Tap On-Demand" (Seguridad DSP)

```cpp
// En prepareToPlay del sintetizador / anfitrión:
// registerTap(name, type, capacity, [wireId]) — wireId (slug) debe coincidir con availableTaps[].id del WebUI.
scopeCollector.registerTap("Master Output",       ScopeTapType::StereoAudio, 4096, "master");
scopeCollector.registerTap("Pre-Filter (Mixer)", ScopeTapType::MonoAudio, 4096, "pre_filter");
scopeCollector.registerTap("LFO 1 / Env 1 (CV)", ScopeTapType::ControlSignal, 4096, "lfo1");

// En processBlock (Audio Thread):
void SynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Solo se copia si el tap está activo en la UI:
    scopeCollector.pushIfActive(0, buffer.getReadPointer(0), buffer.getReadPointer(1), buffer.getNumSamples());

    // Render de osciladores...
    scopeCollector.pushIfActive(1, oscMixBuffer.getReadPointer(0), oscMixBuffer.getNumSamples());
}
```

Tipos de tap y su efecto sobre el renderer:

| `ScopeTapType` | Rango de señal | Escala temporal | Trigger | FFT |
|---|---|---|---|---|
| `StereoAudio` | ±1.0 bipolar | ms (rápido) | ✅ Activo | ✅ Activo |
| `MonoAudio` | ±1.0 bipolar | ms (rápido) | ✅ Activo | ✅ Activo |
| `ControlSignal` | 0–1 unipolar o ±1.0 | s (lento, 0.1–20 Hz) | ❌ Off | ❌ Off |

---

## 12. Estructura del Repositorio `ABDScope`

```text
ABDScope/
├── Source/                       # Capa C++ (JUCE y DSP Puro)
│   ├── Core/
│   │   ├── ScopeTap.h                  # Tap lock-free con selector atómico
│   │   ├── ScopeDataCollector.h        # Gestor multi-tap y buffers circulares SPSC
│   │   ├── TapId.h                     # Slugs deterministas (makeSlug) del wire id
│   │   ├── ScopeFrameSerializer.h      # Serialización de frame a JSON para bridge
│   │   └── TriggerDetector.h           # Algoritmo de cruce por cero y estabilización
│   ├── JUCE/
│   │   ├── JuceScopeComponent.h        # Componente nativo JUCE para ABDAudioLab
│   │   ├── JuceWebScopeComponent.h     # Componente WebView2 plug-and-play multi-tap
│   │   └── ScopeResourceProvider.(h|cpp)  # Recursos Web embebidos (MIME estricto)
│   └── CMakeLists.txt                  # Target ABDScope::ABDScopeCore
├── WebUI/                        # Capa Web (Canvas 2D / WebGL)
│   ├── src/
│   │   ├── scope.js                    # Entry point factory (createScope)
│   │   ├── frame.js                    # Procesador y extractor de ScopeDataFrame
│   │   ├── trigger.js                  # Algoritmo de histéresis y pitch tracking
│   │   ├── input/
│   │   │   ├── AnalyserInput.js        # Adaptador para Web Audio AnalyserNode
│   │   │   └── PushInput.js            # Adaptador para datos bridge/push
│   │   ├── renderers/
│   │   │   ├── BaseRenderer.js         # Interfaz base IScopeRenderer (con destroy)
│   │   │   ├── OscilloscopeRenderer.js
│   │   │   ├── SpectrumRenderer.js
│   │   │   ├── LissajousRenderer.js
│   │   │   ├── PhaseMeterRenderer.js
│   │   │   └── VuMeterRenderer.js      # Barras VU compañeras
│   │   ├── mount/
│   │   │   ├── MountBase.js            # Orquestación multi-lane compartida
│   │   │   ├── EmbeddedMount.js        # Montaje como panel integrado
│   │   │   ├── FloatingMount.js        # Montaje como modal flotante arrastrable
│   │   │   ├── LaneController.js       # Lógica de cada carril (renderers, freeze)
│   │   │   ├── LaneView.js             # DOM de cada carril
│   │   │   └── mountLayout/mountDom.js # Helpers puros de grid y DOM
│   │   └── scope.css                   # Theming dinámico mediante CSS Custom Properties
│   ├── icons.js                       # Módulo generado desde ABDSharedAssets/icons (con test de paridad)
│   ├── tests/
│   │   ├── trigger.test.js             # Tests unitarios del algoritmo de disparo
│   │   ├── frame.test.js               # Tests de decodificación y métricas
│   │   ├── scope.test.js               # Tests de inicialización, flags y lifecycle
│   │   └── input.test.js              # Tests de adaptadores de entrada
│   └── package.json                    # @abdsynths/scope
├── docs/
│   ├── INTEGRATION_GUIDE.md            # Guía paso a paso para sintes VST y ABDAudioLab
│   └── DATA_CONTRACT.md                # Wire Protocol y ScopeDataFrame
├── ARCHITECTURE_SPEC.md                # Este documento
├── ROADMAP.md                          # Hoja de ruta de fases y tareas
├── README.md
└── .gitignore
```
