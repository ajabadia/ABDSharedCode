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

## 3. Evolución Arquitectónica: Pantalla SysEx y su Integración con BankManager

### 🔍 Diagnóstico Histórico
Durante las fases de ingeniería inversa y desarrollo inicial de cada sintetizador, se implementó una pantalla de visualización de bytes SysEx para monitorizar el comportamiento de los parámetros:
* **`ABDJUNiO601`:** Implementó `JunoSysExDisplay` como un componente nativo de JUCE en C++ con fuentes monoespaciadas y resaltado de bytes en rojo.
* **`ABDEep`:** Implementó `sysex_monitor.js` en el WebUI con contadores de tráfico NRPN y selección de bytes decodificados con `BYTE_MAP`.
* **`ABDMS2000`:** Implementó pantallas de diagnóstico en sus drawers y modales.

**Problema actual:**
1. Cada sintetizador lo hizo de una forma técnica distinta (fragmentación de código).
2. Ocupa un protagonismo visual excesivo en el frontal del plugin, añadiendo ruido estético a un instrumento que en el hardware real no tenía una pantalla de bytes hexadecimales.

### 💡 Nueva Estrategia: Reducción de Protagonismo y Convergencia en BankManager
1. **Menos Protagonismo en la UI de Síntesis Principal:**
   - La pantalla SysEx se retira del frontal del sintetizador.
   - En el sintetizador pasa a ser un **cajón plegable secundario** (Drawer de Diagnóstico/MIDI) o una opción dentro del menú de herramientas técnicas, orientado únicamente a desarrolladores o usuarios avanzados.
2. **Integración Natural dentro de `BankManager`:**
   - El lugar natural donde el volcado SysEx tiene el 100% del sentido es **`BankManager`**:
     - Al seleccionar un parche o volcado de banco, el usuario técnico puede abrir el **SysEx Inspector**.
     - Permite inspeccionar la estructura interna del patch, ver qué bytes cambian al comparar dos parches (Diff visual), verificar checksums y exportar volcados limpios.

---

## 4. Nuevos Componentes Transversales Planificados

### Componente 1: `SysExInspector` / `HexDumpViewer`
* **Tipo:** Web Component / Módulo JS reutilizable en `@abdsynths/shared/components`.
* **Responsabilidad:**
  - Visualizador de cuadrícula hexadecimal formateada (16 columnas con direcciones offset `0000: 00 1F ...`).
  - Comparador *Diff* entre dos buffers de bytes (resaltado en color de valores modificados).
  - Hover/Tooltip contextual: al pasar el ratón por encima de un byte, muestra el nombre del parámetro según el `byteMap` del contrato de hardware.
  - Botones rápidos: *Copy Hex*, *Export .syx*, *Validate Checksum*.
* **Puntos de Integración:**
  - Panel técnico dentro de `BankManager`.
  - Drawer de diagnóstico en `ABDEep`, `ABDMS2000`, `ABDCZ101`, `ABDJUNiO601`.

### Componente 2: `PatchNavigationBar` (Cabecera Universal de Presets)
* **Tipo:** Componente Web transversal en `@abdsynths/shared`.
* **Responsabilidad:**
  - Barra superior compacta con:
    - Botones `< Prev` y `Next >` para navegar por el banco activo.
    - Display editable con el nombre del preset actual.
    - Selector rápido de categorías (Bass, Lead, Pad, Strings, Brass, FX, User).
    - Botón `Save / Store` con diálogo de confirmación rápido.
    - Acceso directo para invocar el modal completo de `BankManager`.

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

| Sintetizador | `Scope` (ABDScope) | `AutoUpdater` | `BankManager` | `SysExInspector` | `PatchNavBar` | `LcdDisplay` | `ModMatrixGrid` |
|---|---|---|---|---|---|---|---|
| **`ABDMS2000`** | 🟡 Migrar a ABDScope | 📋 Planificado | ✅ Integrado | 📋 Mover a BankMgr | 📋 Estandarizar | 🟡 En LcdMenu | ✅ 4/8 Slots |
| **`ABDEep`** | 🟡 Migrar a ABDScope | 📋 Planificado | 🟡 En unificación | 📋 Mover a BankMgr | 📋 Estandarizar | 🟡 En Programmer | ✅ 8 Slots |
| **`ABDCZ101`** | 📋 Fase 4 | 📋 Planificado | 🟡 En unificación | 📋 Integrar en BankMgr | 📋 Estandarizar | 📋 Emular 2x16 | 📋 Modo Moderno |
| **`ABDJUNiO601`** | 📋 Planificado | 📋 Planificado | 🟡 En unificación | 📋 Mover a BankMgr | 📋 Estandarizar | ❌ No aplica | ❌ No aplica |
| **`ABDNeural`** | 📋 Planificado | 📋 Planificado | 📋 Planificado | 📋 Integrar en BankMgr | 📋 Estandarizar | 📋 Display OLED | 📋 Matriz Completa |
