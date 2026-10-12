# 📋 Inventario de Migraciones por Proyecto — ABDSharedCode

> **Propósito:** Documento de referencia y estado de convergencia de módulos compartidos (`DspCore`, `DspEffects`, `SynthCore`, `HardwareDrivers`, `MidiKeyboard`, `Scope`) a lo largo de los sintetizadores del ecosistema **ABDSynths**.

---

## 📊 Matriz Resumen de Estado

| Proyecto | Estado en ABDSharedCode | Módulos Clave Integrados | Pendiente / Próximos Pasos |
|---|---|---|---|
| **`ABDMS2000`** | 🟢 **100% Completado** | `SynthCore`, `HardwareDrivers`, `Conversion.gen.h` (436 tests C++, 203 Vitest en verde) | SSOT en `parameters-spec.schema.v1.json` (200+ params). |
| **`ABDEep`** | 🟢 **100% Completado** | `DspJunoHPF`, `DspJunoVCF`, `VoiceAllocator`, `WebView2Bridge`, `MidiKeyboard` | Fase 5 cerrada (127 suites, 5,131 tests WebUI y 367 tests MidiKeyboard en verde). |
| **`ABDCZ101`** | 🟡 **DSP OK / Estandarización Suite en Curso** | `DspCz1`, `parameterLookup.js`, `@abdsynths/midi-keyb` | Convergencia a CMake modular, SSOT registry `--check` y `WebView2Bridge` (ver `ESTANDARIZACION_SUITE_BLUEPRINT.md`). |
| **`ABDJUNiO601`** | 🟢 **Completado** | `DspJunoHPF`, `ArpeggiatorJuno`, `JunoBBD`, `DspCore` | Pruebas nativas cerradas en verde. |
| **`ABDAudioLab`** | 🟢 **Completado** | `AudioComparator`, `HardwareDrivers`, `ScopeCore`, `LutDSP` | Consolidación continua de contratos. |
| **`ABDNeural`** | 🟡 **Parcial** | `DspCore`, `DspEffects` (C++20, 56 FX), `MidiKeyboard` | Verificar catalogo de efectos propios vs compartido. |
| **`ABDScope`** | 🟢 **En Absorción** | Contenido absorbido canónicamente en `ABDSharedCode/Scope` | Consolidar subárbol único. |
| **`ABDBankManager`** | 🟡 **Planificado (Opción B)** | C++ en `ABDSharedCode/BankManager/`, contratos en camino a `ABDSharedAssets` | Fusión de contratos TS y codecs SysEx en `ABDSharedAssets/contracts/`. |
| **`ABDOmegaUnified`** | ⚪ **Por confirmar** | No detecta consumo directo de `ABDSharedCode` | Evaluar convergencia de DSP y contratos. |

---

## 1. `ABDMS2000` (Korg MS2000 / microKORG)

### 1.1 Estado Actual
* **Abstracción DSP Completa (Fases 1 a 7 consolidadas en `main`):**
  - Todo el flujo de parámetros de síntesis (Osc1, Osc2, Mixer, Filter, Envelopes EG1/EG2, Modulaciones bipolares, LFO1/2, Amp, Pan, Portamento, Virtual Patch 1-4, EQ, ModFX, Delay y Arpegiador) migrado canónicamente a `conversion::normalizedToDomain()`.
  - **436 / 436 tests C++ en verde** y **203 / 203 tests Vitest en verde**. Cero desincronizaciones con `registry_generator.js --check`.
  - Paridad sónica 100% con hardware original preservada.

### 1.2 Aclaración Crítica de Contratos (SSOT de Parámetros)
* **`ABDMS2000/schemas/parameters-spec.schema.v1.json`:** Es la **Única Fuente de Verdad (SSOT)** con más de 200 parámetros mapeados con sus CCs, rangos, tipos y curvas.
* **`ABDSharedAssets/contracts/korg_ms2000.json`:** Es únicamente un contrato de prueba de hardware para `HardwareMidiDetect` y `AudioABComparator` (define solo 4 controles usados para barridos de test de audio: Cutoff CC 74, Res CC 71, Drive CC 79, Type CC 80). **No debe usarse como diccionario completo del sintetizador**.

---

## 2. `ABDEep` (Behringer DeepMind 12 / Juno Hybrid)

### 2.1 Estado Actual
* **Núcleo de Filtrado y Asignación:**
  - Consume de forma canónica los filtros ZDF de `ABDSharedCode/DspCore/` (`DspJunoHPF.h`, `DspJunoVCF.h` con calibración `DspVcfVoicing.h`).
  - `VoiceAllocator` híbrido implementado y protegido.
  - Generación WASM y 5.084 tests WebUI pasando en verde.

### 2.2 Preservación de la Identidad Analógica
* A diferencia de sintetizadores con curvas estáticas, el motor DSP de ABDEep preserva estrictamente su **`CalibrationSpec`** (XML dinámico gobernado por el Calibration Lab y ajustado mediante comparación FFT A/B con grabaciones reales de hardware DeepMind 12).
* **Hoja de Ruta:** Fase 5 añadida al roadmap para registrar el diccionario de control JSON en `ABDSharedAssets/contracts` y `MidiKeyboard` sin alterar el motor de calibración interno.

---

## 3. `ABDCZ101` (Casio CZ-101 / Phase Distortion)

### 3.1 Módulos DSP Migrados (de *melodee-main* a `ABDSharedCode/DspCore/`)
* **`DspCz1.h` / `.cpp`:** Síntesis de distorsión de fase, 96 parciales de amplitud, envolvente de 8 etapas.
* **Módulos adicionales portados:** `DspProphetVCF`, `DspProphetLFO`, `DspProphetEnvelope`, `DspProphetOscillator`, `DspSid`, `DspPhys`, `DspAnalog`, `DspCore`.
* **Parámetros extendidos en CZ-101:**
  - `DcoFineTune`: Ajuste fino en cents (-12 a +12) controlado por RPN.
  - `PitchBend`: Bendido de tono en semitonos (-12 a +12) controlado por RPN.
  - `Velocity`: Velocidad de note-on (0-127).
* **Integración UI:** `PhaseDistOscIntegration.cpp` con `mapDcwToM()` y `windowFactor()`.

### 3.2 Tareas de Verificación (Completadas ✅)
* **Tests de Vitest en `ABDSharedCode/MidiKeyboard`:** **367 / 367 tests pasando en verde** (incluyendo `parameterConversion.test.js` y `parameterLookup.test.js`).
* **Compilación C++20 MSVC:** Compilación limpia en Visual Studio 2026 sin errores.
* **Validación de round-trip en C++ (`Cz101ParamsTests.cpp`):** **8 / 8 suites pasando en verde** con ratio 1.0 exacto en Cutoff, Resonance, LFO1 Rate, LFO1 Amount, FineTune, PitchBend, Velocity y UI Integration.

### 3.3 Fase Actual: Estandarización de la Suite (Blueprint Universal)
* **Documento Maestro:** [ESTANDARIZACION_SUITE_BLUEPRINT.md](file:///d:/desarrollos/ABDSynths/ABDCZ101/ESTANDARIZACION_SUITE_BLUEPRINT.md).
* **Objetivos de Convergencia:** CMake modular (`add_subdirectory`), generador de registros SSOT con verificación `--check` desde `casio_cz101_parameter_spec.json`, desacoplamiento de `WebView2Bridge` y preparación del modelo de transferencia hacia `ABDNeural`.

---

## 4. `ABDSharedCode/MidiKeyboard`

### 4.1 Diccionarios de Parámetros y `parameterLookup.js`
* Soporte para 15 sintetizadores mediante esquemas JSON de `ABDSharedAssets/contracts/`.
* Conversión raw ↔ física con curvas `Linear`, `Exp`, `Pow` y `None` mediante `lookupRawValue()` y `domainToRawValue()`.
* Integración de Aftertouch y ModWheel en `keyboard.js`.

### 4.2 Tareas Pendientes
* Sincronizar los imports de esquemas en `parameterLookup.js` con los contratos reales de `ABDSharedAssets/contracts/` para evitar módulos faltantes.
* Asegurar que el mapa del MS2000 se extraiga del schema completo (`parameters-spec.schema.v1.json`) y no del contrato de sweep (`korg_ms2000.json`).

---

## 5. `ABDJUNiO601`

* **Consumo verificado:** `DspJunoHPF`, `ArpeggiatorJuno`, `JunoBBD` sobre `DspCore`.
* Batería completa de pruebas unitarias C++ pasando al 100% en verde con cero fallos.

---

## 6. `ABDAudioLab`

* Módulos de hardware y análisis consolidados: `HardwareDrivers`, `AudioABComparator`, `MidiEndpointSafetyPolicy`, `WebView2Bridge`.
* Absorción del subárbol de Scope en `ABDSharedCode/Scope`.

---

## 7. `ABDBankManager` y Convergencia de Contratos (Opción B — Roadmap)

### 7.1 Consolidación de Contratos y Codecs en `ABDSharedAssets`
* **Decisión de Arquitectura (Opción B):** Consolidar la Single Source of Truth (SSOT) de contratos de presets y protocolos SysEx directamente en `ABDSharedAssets/contracts/`, unificando la especificación declarativa (JSON) y la lógica ejecutable (TypeScript).
* **Migración desde `ABDSharedCode/BankManager/Contracts/`:**
  - Trasladar los contratos TypeScript (`ModelContract.ts`, `ContractRegistry.ts`, `PatchData.ts`, `HardwareLinkContract.ts`), los codecs de bajo nivel (`SysEx/codec.ts`: packing 7→8, 8→7, nibbles, checksums) y los adaptadores por fabricante (`Models/*.ts`, `Adapters/*.ts`) hacia `ABDSharedAssets/contracts/`.
  - Convertir `ABDSharedAssets` en el SDK central de WebUI para todos los plugins (`ABDMS2000`, `ABDCZ101`, `ABDEep`, `ABDJUNiO601`) y la app `ABDBankManager`.
* **Depuración de Shims Legacy:**
  - Una vez redirigidos los imports en los consumidores, eliminar la carpeta de shims obsoleta `ABDBankManager/Source/Contracts`.
* **Frontera C++ Limpia:**
  - `ABDSharedCode/BankManager/` mantendrá de forma estricta y limpia exclusivamente el motor nativo C++ (`ABDBankManagerCore.h/.cpp`, `BankManagerWebViewAdapter.h/.cpp`, `HardwareMidiPipe.h`, `Pro800Midi.h/.cpp`).
