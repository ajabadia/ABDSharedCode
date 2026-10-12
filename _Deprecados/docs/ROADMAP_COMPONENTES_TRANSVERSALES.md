# 🗺️ Roadmap de Componentes Transversales — Ecosistema ABDSynths

> **Propósito:** Definir el estado, plan de adopción y especificación de los componentes compartidos reutilizables en **`ABDSharedCode`** y **`ABDSharedAssets`**, asegurando que los 15 sintetizadores de la suite compartan una base técnica homogénea y no reinventen la rueda.

---

## 📑 Índice de Contenidos

1. [Visión General de la Capa Transversal](#1-visión-general-de-la-capa-transversal)
2. [Auditoría de Componentes Existentes en ABDSharedCode](#2-auditoría-de-componentes-existentes-en-abdsharedcode)
   - [Osciloscopio y Análisis DSP: ABDScope](#21-osciloscopio-y-análisis-dsp-abdscope)
   - [Actualizaciones Automáticas: AutoUpdater](#22-actualizaciones-automáticas-autoupdater)
   - [Gestión de Bancos y Parches: BankManager](#23-gestión-de-bancos-y-parches-bankmanager)
   - [Teclado y Ruedas de Expresión: MidiKeyboard](#24-teclado-y-ruedas-de-expresión-midikeyboard)
   - [Escalado y Widgets UI: @abdsynths/shared](#25-escalado-y-widgets-ui-abdsynthsshared)
3. [Evolución Arquitectónica: Pantalla SysEx y su Integración con BankManager](#3-evolución-arquitectónica-pantalla-sysex-y-su-integración-con-bankmanager)
4. [Nuevos Componentes Transversales Planificados](#4-nuevos-componentes-transversales-planificados)
   - [Componente 1: SysExInspector / HexDumpViewer](#componente-1-sysexinspector--hexdumpviewer)
   - [Componente 2: PatchNavigationBar (Cabecera Universal de Presets)](#componente-2-patchnavigationbar-cabecera-universal-de-presets)
   - [Componente 3: LcdDisplay (Pantalla Virtual de Hardware)](#componente-3-lcddisplay-pantalla-virtual-de-hardware)
   - [Componente 4: ModMatrixGrid (Matriz de Modulación Visual)](#componente-4-modmatrixgrid-matriz-de-modulación-visual)
5. [Matriz de Adopción por Sintetizador](#5-matriz-de-adopción-por-sintetizador)

---

## 1. Visión General de la Capa Transversal

Los sintetizadores de la suite (`ABDEep`, `ABDMS2000`, `ABDCZ101`, `ABDJUNiO601`, `ABDNeural`, etc.) comparten una filosofía de diseño centrada en:
1. **Preservar el carácter sónico único:** Cada motor DSP modela con fidelidad absoluta los circuitos y peculiaridades de su máquina física.
2. **Unificar la infraestructura periférica:** El teclado, el escalado en DAWs, los controles interactivos, la gestión de presets, la inspección MIDI y las actualizaciones deben funcionar de manera idéntica en toda la suite.

---

## 2. Auditoría de Componentes Existentes en ABDSharedCode

### 2.1 Osciloscopio y Análisis DSP: `ABDScope`
* **Ubicación:** `ABDSharedCode/Scope/` (especificación en `ARCHITECTURE_SPEC.md`).
* **Estado Actual:**
  - Módulo universal de análisis y visualización de alta fidelidad.
  - Modos operativos: Osciloscopio temporal con trigger analógico, Analizador de Espectro FFT, Lissajous/Goniómetro y medidores de fase.
  - Implementación desacoplada: Operación *lock-free*, *zero-allocation* en el audio thread, con arquitectura *multi-tap on-demand* (solo procesa si la ventana está abierta).
  - Componente C++ para JUCE: `JuceWebScopeComponent.h` y `JuceScopeComponent.h`.
* **Plan de Adopción:**
  > ⚠️ **Directriz:** **No crear osciloscopios nuevos ni decorativos.** Los sintetizadores que actualmente tienen displays fake o implementaciones ad-hoc deben converger hacia `ABDShared::Scope`.

### 2.2 Actualizaciones Automáticas: `AutoUpdater`
* **Ubicación:** `ABDSharedCode/AutoUpdater/`.
* **Estado Actual:**
  - Implementado en C++20 (`AutoUpdater.h`, `AutoUpdater.cpp`, `AutoUpdaterConfig.h`).
  - Hilo en segundo plano de baja prioridad que consulta la API de GitHub Releases sin interferir en el audio thread.
  - Parsea versiones SemVer, release notes, assets descargables y checksums SHA-256.
* **Plan de Adopción:**
  - Conectar a la cabecera / menú "About" o modal de configuración de los sintetizadores.
  - Mostrar badge discreto en la WebUI cuando exista una nueva versión disponible.

### 2.3 Gestión de Bancos y Parches: `BankManager`
* **Ubicación:** `ABDSharedCode/BankManager/` y `ABDSharedAssets/contracts/`.
* **Estado Actual:**
  - Núcleo C++ (`ABDBankManagerCore.h/.cpp`) y adaptador para WebView (`BankManagerWebViewAdapter.h/.cpp`).
  - Manejo de volcados SysEx completos de bancos (128 parches), presets individuales y comunicación hardware bidireccional (`HardwareMidiPipe.h`).
* **Plan de Adopción:**
  - Consolidar la SSOT de contratos de modelos y codecs SysEx en `ABDSharedAssets/contracts/`.

### 2.4 Teclado y Ruedas de Expresión: `MidiKeyboard`
* **Ubicación:** `ABDSharedCode/MidiKeyboard/` (`@abdsynths/midi-keyb`).
* **Estado Actual:**
  - 100% consolidado con 367 tests unitarios pasando.
  - Textura vintage de marfil, ruedas filmstrip de pitch/modulación con físicas reales (`wheel.js`), Aftertouch, Velocity, selector de octavas y motor `parameterLookup.js` para 15 sintetizadores.

### 2.5 Escalado y Widgets UI: `@abdsynths/shared`
* **Ubicación:** `ABDSharedAssets/components/` y `ABDSharedAssets/styles/`.
* **Estado Actual:**
  - `fitStage.js`: Escalado matricial uniforme sin distorsión ni barras de desplazamiento para cualquier tamaño de ventana del DAW.
  - `createOverlayFocus`: Trampa de foco accesible para diálogos y modales.
  - Widgets Vanilla CSS: `Knob`, `Slider`, `Toggle`, `Segmented`, `Select`, `XYPad`.

---

## 3. Evolución Arquitectónica: Diferenciación entre BankManager y Live SysEx Monitor

### 🔍 Clarificación de Roles y Funcionalidades
Es fundamental no confundir dos herramientas con objetivos y momentos de uso radicalmente diferentes:

| Característica | 📦 **BankManager (Preset SysEx Viewer)** | ⚡ **Live SysEx Inspector (Edit Buffer Monitor)** |
|---|---|---|
| **Objeto de análisis** | Presets estáticos archivados en librerías o volcados de banco completos (.syx). | **Edit Buffer activo en tiempo real** (el sonido que suena en este milisegundo). |
| **Comportamiento** | Estático / Comparativo entre parches de catálogo. | **Dinámico y reactivo**: los bytes mutan al mover sliders, knobs o pulsadores. |
| **Propósito de uso** | Organización de bancos, backup a hardware, bibliotecas de sonidos. | Depuración sonora, entender cómo codifica el hardware, ingeniería inversa y diseño de parches. |
| **Acciones inmediatas** | Importar/Exportar banco completo, renombrar presets. | **Copiar código al portapapeles** (Hex/Raw), **Exportar .syx** del parche actual o **Guardar como nuevo preset**. |
| **Ubicación en UI** | Vista / Modal completa de gestión de librería (`BankManager`). | **Ventana flotante separable / modal desacoplable** (como `ABDScope`). |

---

## 4. Nuevos Componentes Transversales Planificados

### Componente 1: `LiveSysExInspector` (Monitor Flotante del Edit Buffer)
* **Tipo:** Utilidad Web transversal en `@abdsynths/shared/components` con arquitectura de montaje flotante (`FloatingMount`).
* **Concepto de UX & Ergonomía:**
  - **Cero intrusión en el frontal:** No ocupa espacio en la carátula vintage del sintetizador.
  - **Ventana Flotante Desacoplable (Floating Window):** Hereda el patrón de `ABDScope` (`FloatingMount.js`). La ventana se puede arrastrar (drag & drop con control de límites en viewport), redimensionar, minimizar y mover libremente, permitiendo al diseñador de sonido mover los knobs del sinte mientras ve en vivo cómo muta el flujo hexadecimal sin tapar los controles críticos.
  - **Invocación desde `nav_bar`:** Accesible como acción en el menú superior del sintetizador:
    - `View ➔ Live SysEx Inspector...` (o atajo de teclado `Ctrl+I`, y cierre rápido con tecla `Escape`).
    - Alternativamente accesible haciendo clic en el cuadro de visor SysEx del sinte.
* **Interface Pública Formal del Componente:**
  ```javascript
  const inspector = createLiveSysExInspector({
    title: 'CZ-101 Live SysEx Inspector',
    floating: true,
    bytesPerRow: 16,
    initialBuffer: currentSysExBytes,
    onOpen: () => {},
    onClose: () => {},
    onExport: (blob, filename) => {}
  });

  // Métodos de control:
  inspector.show();                     // Abre y enfoca
  inspector.hide();                     // Oculta sin destruir estado ni buffers
  inspector.toggle();                   // Alterna visibilidad
  inspector.isOpen();                   // Boolean indicando visibilidad
  inspector.updateBuffer(newBytes);     // Reactividad en vivo con pulso de mutación
  inspector.setBuffer(newBytes);        // Alias de updateBuffer
  inspector.getBuffer();                // Uint8Array del buffer actual
  inspector.destroy();                  // Desmontaje y limpieza completa de listeners
  ```
* **Funcionalidades Clave:**
  1. **Reactividad en Tiempo Real (Live Mutation Highlights):**
     - Al mover un slider de corte de filtro, resonancia o envolvente, la celda del byte correspondiente destella en verde neón (`sysex-pulse` a 800 ms) antes de volver a su tono normal.
     - Indicador del último parámetro modificado y estado de conexión en vivo con LED animado.
  2. **Portapapeles Instantáneo:**
     - Botón `📋 Copy Hex` (formato limpio separado por espacios `F0 42 30 ... F7`).
     - Botón `📋 Copy Array / C++` (para desarrolladores de DSP).
  3. **Generación y Guardado de Presets:**
     - Botón `💾 Export .syx`: Descarga directa o guardado en disco del parche activo en formato de volcado de hardware estándar.
  4. **Modo Diff Dinámico:**
     - Botón `Snap A` y `Diff A/B`: Congela una instantánea de referencia antes de editar y resalta en ámbar todos los bytes que difieren tras mover los parámetros con contador `Δ N B`.
* **Integración en la Suite:**
  - `ABDCZ101` (Casio CZ-101 265-byte Sound Data Dump) — ✅ Integrado y sincronizado.
  - `ABDEep` (DeepMind 12/6 Edit Buffer & NRPN translator).
  - `ABDMS2000` (MS2000 / microKORG 0x71 / 0x58 Edit Buffer).
  - `ABDJUNiO601` (Juno-106 18-byte Voice Buffer).

### Componente 2: `SynthNavBar` y `PatchNavigationBar` (Navegación Superior y Cabecera de Presets)
* **Tipo:** Componente Web transversal en `@abdsynths/shared/components/synthNavBar.js` y estilos en `navbar.css`.
* **Responsabilidad:**
  - Paridad 1:1 con la jerarquía de menús nativos JUCE (`File`, `Edit`, `Mode`, `View`, `Help`).
  - Soporte declarativo de submenús anidados (`▸`), checks reactivos (`✓`) y badges de atajos (`<kbd>`).
  - Navegación desktop/DAW con scrubbing de ratón al deslizar entre menús.
  - Ranuras modulares (`centerSlot` para preset actual/categoría y `rightSlot` para pánico MIDI, selector de modo y badges).
  - Cableado automático con `AboutDialog` para las acciones `Help ➔ About...` y `Help ➔ Check for Updates...`.

### Componente 2b: `AboutDialog` (Diálogo Universal "About..." con AutoUpdater)
* **Tipo:** Componente Web transversal en `@abdsynths/shared/components/aboutDialog.js` y estilos en `about-dialog.css`.
* **Responsabilidad:**
  - Identidad visual completa del sintetizador (nombre, subtítulo, descripción, build date/number).
  - Sección interactiva de comprobación y notificación de actualizaciones de firmware/motor DSP.
  - Comunicación bidireccional reactiva con `ABDShared::AutoUpdater` (eventos `checkForUpdates`, `getUpdateStatus`, `openBrowserUrl`, `updateCheckResult`).
  - Fallback automático para modo Web/WASM consultando la API pública de GitHub Releases.
  - Contrato de accesibilidad y foco completo (`overlayFocus.js`, Escape key, trap focus e `inert`).

### Componente 3: `LcdDisplay` (Pantalla Virtual de Hardware)
* **Tipo:** Módulo C++ (`ABDSharedCode/LcdDisplay/LcdDisplay.h`) y réplica visual Web en `@abdsynths/shared`.
* **Responsabilidad:**
  - Simulación gráfica fotorrealista de pantallas de sintetizadores vintage:
    - Modo LCD alfanumérico (2 líneas x 16 caracteres, iluminación de fondo verde/ámbar, matriz 5x7).
    - Modo VFD (Vacuum Fluorescent Display azul-verdoso brillante).
    - Modo OLED moderno de alto contraste.
  - **Comportamiento dinámico temporal:**
    - Al girar cualquier control (ej. Cutoff), la pantalla muestra temporalmente el nombre y valor del parámetro (ej. `"VCF CUTOFF: 1.25 kHz"`) durante un timeout de 1.5 s, y luego regresa suavemente al nombre del preset.

### Componente 4: `ModMatrixGrid` (Matriz de Modulación Visual)
* **Tipo:** Web Component interactivo en `@abdsynths/shared`.
* **Responsabilidad:**
  - Cuadrícula modular para sintetizadores con matriz de ruteo libre (ABDEep 8 slots, MS2000 Advanced 8 slots, CZ-101 Modern 8 slots, ABDNeural).
  - Selector desplegable de fuentes (LFO, Envelopes, ModWheel, Aftertouch, Velocity, KeyTrack).
  - Selector desplegable de destinos (Pitch, Cutoff, Resonance, Amp, Pan, FX).
  - Control de profundidad bipolar rotatorio o slider (-100% a +100%) con indicador visual de modulación en vivo.

---

## 5. Matriz de Adopción por Sintetizador

| Sintetizador | `Scope` (ABDScope) | `AutoUpdater` | `BankManager` | `LiveSysExInspector` | `SynthNavBar` | `AboutDialog` | `ModMatrixGrid` |
|---|---|---|---|---|---|---|---|
| **`ABDMS2000`** | 🟡 Migrar a ABDScope | 📋 Planificado | ✅ Integrado | 📋 Extraer a Floating Mount | 📋 Migrar a SynthNavBar | 📋 Integrar compartido | ✅ 4/8 Slots |
| **`ABDEep`** | 🟡 Migrar a ABDScope | 📋 Planificado | 🟡 En unificación | 🟡 Migrar a componente compartido | 📋 Migrar a SynthNavBar | 📋 Integrar compartido | ✅ 8 Slots |
| **`ABDCZ101`** | 📋 Fase 4 | ✅ Piloto Integrado | 🟡 En unificación | ✅ Integrado (FloatingMount & Edit Buffer) | ✅ Implementado (Paridad JUCE) | ✅ Integrado con AutoUpdater | 📋 Modo Moderno |
| **`ABDJUNiO601`** | 📋 Planificado | 📋 Planificado | 🟡 En unificación | 📋 Migrar C++ a Floating Web | 📋 Planificado | 📋 Planificado | ❌ No aplica |
| **`ABDNeural`** | 📋 Planificado | 📋 Planificado | 📋 Planificado | 📋 Integrar Floating Mount | 📋 Planificado | 📋 Planificado | 📋 Matriz Completa |

---

## 6. Siguientes Pasos de Implementación

1. **Prioridad 1 — Estandarización de `ABDCZ101` (Fase 0):** ✅ COMPLETADA (SSOT contract-driven con `--check`, CMake modular con `ABDSharedCode`, tests C++ y WebUI al 100%).
2. **Prioridad 2 — Módulo Transversal `LiveSysExInspector`:** ✅ COMPLETADO (Creado en `ABDSharedAssets/components/LiveSysExInspector.js` con `@keyframes sysex-pulse`, Diff A/B, exportación `.syx`, e integrado en `ABDCZ101` vía `View ➔ Live SysEx Inspector...` / `Ctrl+I`).
3. **Prioridad 3 — Piloto de `AutoUpdater` en `ABDCZ101`:** ✅ COMPLETADO (Integrado `ABDShared::AutoUpdater` en C++, menú `Help ➔ Check for Updates...`, bindings WebView2 y sección reactiva en diálogo `About...`).
4. **Prioridad 4 — Componentes Transversales `AboutDialog` y `SynthNavBar`:** ✅ COMPLETADO (Creados en `ABDSharedAssets/components/` con tests Vitest al 100%, estilos dedicados en `styles/components/` y exportación en el barrel principal).
5. **Prioridad 5 — Reemplazo Universal de Osciloscopios por `ABDScope`:** Sustituir los scopes locales de `ABDEep`, `ABDMS2000` y `ABDCZ101` por el componente oficial flotante.
6. **Prioridad 6 — Despliegue de `SynthNavBar`, `AboutDialog` y `LiveSysExInspector` en `ABDEep` y `ABDMS2000`:** Sustituir barras y modales locales por los nuevos componentes compartidos.
