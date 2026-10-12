# Hoja de ruta mínima — ABDMS2000

> Qué hacer primero en ABDMS2000 para reducir duplicación, alinearse al compartido y, si se decide, preparar nuevas extracciones.
> Criterio: mínimo, ordenado por riesgo/beneficio, y anclado a evidencia visible en este árbol (CMake, includes, docs del propio proyecto, evidencia cruzada con ABDSharedCode).
> No es un backlog completo ni un plan de producto. Es un orden operativo razonable para el trabajo de “unificar / limpiar / decidir”.

---

## 1. Principio rector

Si algo usa JUCE o libm en el lazo de audio, la migración no es “mover tal cual” hacia capas que la guía del repo define como JUCE-free / libm-free. Eso puede requerir:
- shim local,
- reescritura,
- o no moverlo y dejarlo como código propio documentado.

Por eso, “susceptible” no siempre significa “se mueve sin más”.

---

## 2. Nivel 1 — Limpieza y desduplicación (pierde poco, gana claridad)

### 2.1 Eliminar duplicados locales documentados vs compartido — [COMPLETADO]

- [x] **`Source/MIDI/SysExCodec.{h,cpp}` y `Source/MIDI/NRPNParser.{h,cpp}`:** Eliminados de `ABDMS2000/Source/MIDI/`. Se consumen de forma unificada desde `ABDShared::HardwareDrivers` (namespace `abd::hw`).
- [x] **`WebUI/src/components/utils.js`:** Eliminado de los componentes locales. Se consume como parte del paquete `@abdsynths/midi-keyb` vía Vite workspace.

### 2.2 Revisar el provider WebView2 local

Según lo anotado en este árbol, ABDMS2000 tiene un provider local (`Source/PluginEditor_ResourceProvider.*`) y existe `ABDSharedCode/WebView2Bridge/WebView2ResourceProvider.*` con el mismo propósito.

Qué hacer:
1. comparar qué hace cada uno,
2. si el local hace lo mismo que el puente compartido, migrar al puente,
3. si el local tiene diferencias reales, ver si encajan como extensión del puente y no como copia paralela.

Por qué está en nivel 1 y no más arriba:
- es andamiaje, no DSP del lazo,
- hay evidencia de que el puente existe para este patrón exacto,
- pero si el local tiene comportamiento propio, la migración no es directa sin esa revisión.

### 2.3 Revisar detección manual de puertos vs HardwareMidiDetect

Según lo anotado en este árbol, ABDMS2000 tiene selección manual de puertos y existe `ABDShared::HardwareMidiDetect` + contratos en `ABDSharedAssets/contracts` (p.ej. `korg_ms2000.json`).

Qué hacer:
1. confirmar si ABDMS2000 necesita detectar hardware MS2000 físico o sólo manejarlo de forma manual,
2. si necesita detección, evaluar usar `HardwareMidiDetect` + contrato ya existente,
3. si no lo necesita, dejar la versión manual pero documentar que hay un módulo compartido disponible para ese caso.

Por qué no es obligatorio:
- no toda integración de hardware es obligatoria para todos los proyectos,
- lo razonable es adoptar si encaja, no forzar la integración.

### 2.4 Revisar AutoUpdater enlazado sin uso — [COMPLETADO]

- [x] **Verificación:** En `ABDMS2000/CMakeLists.txt`, `ABDShared::AutoUpdater` **no está enlazado** (solo se enlazan `ABDBankManagerCore`, `SynthCore`, `HardwareDrivers` y `ScopeCore`). No hay dependencia muerta en el target principal.

---

## 3. Nivel 2 — Unificaciones de andamiaje (con beneficio pero con más revisión)

### 3.1 Unificar asignación de voz

Según lo anotado en este árbol, hay señales de dos cosas parecidas:
- `VoiceAllocator` del lado compartido,
- `VoiceManager`/lógica de voz local en ABDMS2000.

Qué hacer:
1. revisar si son la misma responsabilidad o distintas,
2. si son la misma, decidir cuál es la versión canónica y converger,
3. si son distintas, documentar los límites para que no se copien de nuevo por error.

Riesgo:
- la asignación de voz puede tener semántica propia de producto,
- no todo lo que parece “asignador” es intercambiable.

### 3.2 Unificar patrón resource provider WebView2

Más allá del caso puntual de ABDMS2000, hay señales de que varios proyectos tienen su propio proveedor de assets WebView2.

Qué hacer:
1. definir un patrón base compartido si no está claro,
2. migrar los casos idénticos,
3. dejar claro qué es “base” y qué es “extensión por proyecto”.

Por qué está en nivel 2:
- no es sólo ABDMS2000, puede ser una decisión transversal,
- si se hace mal, se introduce un target/Proof de infra que después cuesta mover.

---

## 4. Nivel 3 — Extensiones nuevas (solo si se decide salir del módulo actual)

Estas no son limpieza; son creación de superficie compartida nueva.

### 4.1 Filtros analógicos modelados promovidos a `DspCore` — [EN PROGRESO / DISPONIBLES]

- [x] **`VAOnePoleFilter`:** Promovido a `ABDSharedCode/DspCore/DspVAOnePole.h` (`abd::dsp::VAOnePoleFilter`).
- [x] **`MoogLadderVCF`:** Promovido a `ABDSharedCode/DspCore/DspMoogLadder.h` (`abd::dsp::MoogLadderVCF`).
- [x] **`KorgMS20VCF`:** Promovido a `ABDSharedCode/DspCore/DspKorgMS20.h` (`abd::dsp::KorgMS20VCF`).
- [x] **Documentación y tests:** Registrado en `ABDSharedCode/docs/ANALOG_FILTERS_DESIGN_AND_INTEGRATION.md` y probado en `ABDShared_DspCore_Tests`. Listos para adopción en ABDMS2000 cuando se requieran filtros multi-modelo.

### 4.2 Posible convergencia de teclado / utils.js / contratos

Hay señales de que algunos artefactos web (p.ej. `utils.js`) o ciertos contratos pueden tener gemelos locales.

Qué hacer:
1. revisar si realmente son idénticos,
2. si lo son, consumir el compartido,
3. si divergen, documentar la divergencia y evitar que se sincronicen por copia manual.

---

## 5. Orden recomendado de ejecución

1. **Revisión AutoUpdater** (muy rápido, puede ser limpieza puntual o integración real).
2. **Desduplicación de SysExCodec / NRPNParser / utils.js** (beneficio claro, riesgo contenido).
3. **Comparar provider WebView2 local vs puente compartido** (decisión puntual, impacto andamiaje).
4. **Decidir detección de hardware** (sí/no/adoptar parcial), con contrato existente como referencia.
5. **Unificar asignación de voz** (si corresponde).
6. **Definir unificación de patrón resource provider** (si hay varios afectados).
7. **Si se decide nueva extensión**, empezar por `ABDShared::Filters` / LutDSP con reglas y tests, no con conexión directa a todos los consumidores.

---

## 6. Evidencia usada para esta hoja de ruta

Fuentes visibles en este árbol que sostienen lo anotado:
- `ABDMS2000/ANALISIS_DRY_COMPARTIDO.md`
- `ABDMS2000/ANALISIS_DRY_BANKMANAGER_CZ101_ABDEEP.md`
- `ABDMS2000/ROADMAP.md`
- `ABDMS2000/CMakeLists.txt`
- `ABDMS2000/wasm/CMakeLists.txt`
- `ABDMS2000/Source/MIDI/*.h`
- `ABDMS2000/package-lock.json`
- `ABDSharedCode/INTEGRATION_GUIDE.md`
- `ABDScope/HANDOFF.md` y su evaluación de WebView2Bridge

Esto es lo que permite decir “documentado aquí” en vez de “supongo”.

---

## 7. Qué NO afirmo con esta hoja de ruta

- No afirmo que todos los duplicados existentes ya estén listados con nombre exacto en este documento.
- No afirmo que todos los efectos/filtros locales de ABDMS2000 sean migrables.
- No afirmo que la adopción de HardwareMidiDetect sea obligatoria.
- No afirmo que `ABDShared::Filters` deba crearse; solo que aparece como candidato anotado en este árbol.

---

## 8. Primer paso operativo sugerido

Si hay que elegir un único primer movimiento, lo más barato y visible parece:
- revisar y limpiar duplicados locales documentados y la dependencia muerta de AutoUpdater,
- al mismo tiempo, abrir la comparación provider WebView2 local vs puente compartido,
- dejar para después cualquier decisión de módulo nuevo.

Eso da un nivel 1 completo sin tocar el lazo de audio ni crear nuevo módulo.

---

## 9. Portabilidad Cruzada y Validación Multiorigen

- [x] **Opción C: Consolidar en otro sintetizador (ABDEep):** Completada la auditoría y alineación de ABDEep:
  - Implementado el validador SSOT con gate `--check` (`npm run check:registry` con 43 tests pasando sin escritura espuria).
  - Verificado que ABDEep ya consume las primitivas canónicas compartidas (filtros `abd::dsp::JunoVCF_ZDF` / `JunoHPFContinuous`, `abd::synth::VoiceAllocator`, `Arpeggiator`, `LfoAnalog`, `EnvelopeAnalog`, `DriftEngine`).
  - Establecido el límite estricto de preservación de identidad de producto: no se altera la presentación visual de la WebUI ni se exponen filtros o controles ajenos a la arquitectura de 12 voces del DeepMind 12.
- [x] **Revisión Synth Profile & Paridad microKORG vs MS2000:**
  - [x] **Sincronización Bidireccional Tema <-> Perfil:** Vinculado el `ThemeSwitcher` del navbar con `synthMode` del store y el bridge de forma bidireccional (cambiar tema visual conmuta el perfil del motor y viceversa, verificado con `synthProfileSync.test.js`).
  - [x] **Impacto Real en DSP / WASM:** `WasmBridge.cpp` ahora soporta y despacha `synthMode` (4 voces vs 32 expandidas), `voiceMode`, `unisonDetune` y `unisonSpread` a `VoiceManager`.
  - [x] **Vocoder (8 canales microKORG vs 16 bandas MS2000):** Actualización dinámica de card `#card-vocoder` (`8-CHANNEL VOCODER` / `8 CHANNELS` / `ON (8 Channels Active)` vs `16-BAND VOCODER` / `16 BANDS` / `ON (16 Bands Active)`) y consola gráfica del drawer de vocoder adaptada dinámicamente según el perfil activo (muestra 8 faders duales `CH 1`..`CH 8` sincronizando pares de bandas [1,2]..[15,16] en microKORG, y 16 bandas discretas `B1`..`B16` en MS2000/Advanced, con refresco reactivo al cambiar de perfil).
  - [x] **Desactivación del Mod Sequencer en microKORG:** En perfil microKORG, el Mod Sequencer queda completamente inhibido en DSP nativo (`SynthEngine.cpp`) y WASM (`WasmBridge.cpp`). En WebUI, la tarjeta muestra badge `ARPEGGIATOR`, el botón `MOD SEQ` queda atenuado como no disponible, el resumen indica `N/A (microKORG)` y el drawer muestra panel informativo con conmutador directo a MS2000.
  - [x] **Matriz de Modulación Virtual:**
    - Modo Estándar (Hardware MS2000 / microKORG): **4 slots** físicos (`Patch 1`..`Patch 4`) confirmados en WebUI y hardware.
    - Modo Extendido (Advanced): Diseñado para **8 slots** (`kNumVirtualPatchSlotsAdvanced = 8` en `HardwareConstants.h`).
    - Suite de pruebas de ABDMS2000: **201/201 tests pasando** en 19 suites de Vitest.
  - **Matriz de Diferencias MS2000 vs microKORG:**
    1. *Mod Sequencer:* Exclusivo de MS2000 (3 pistas x 16 pasos); microKORG carece de él (inhibido en perfil microKORG).
    2. *Step Arpeggiator:* microKORG incorpora máscara rítmica de 8 pasos on/off; MS2000 opera por tipos/resolución estándar.
    3. *Estructura de Bancos/SysEx:* microKORG organiza 128 programas en matriz de géneros (A/B x 8) vs MS2000 (Bancos A-H x 16).
    4. *Control Físico:* microKORG usa matriz matricial de 5 encoders; MS2000 es knob-per-function.

---

Si quieres, puedo:
- convertir esto en una tabla con columna “primer paso concreto / dueño probable / riesgo”,
- o añadir un capítulo de “qué evidencia falta para firmar cada paso”.

