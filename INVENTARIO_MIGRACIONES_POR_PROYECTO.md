# Inventario de migraciones por proyecto — desde/abajo de ABDSharedCode

> Qué parece migrable o sustituible en cada proyecto del árbol montado, separado por nivel de certeza.
> Origen de las afirmaciones:
> - barrido de CMake/includes/importJS/docs visibles en este monorepo,
> - y ficheros del propio ABDSharedCode que ya documentan parte de esto.
> Dónde no hay evidencia, figura como "por confirmar", no como lista definitiva.

---

## 1. `ABDAudioLab`

### 1.1 Consumo ya confirmado (no es hipótesis)
- Integra `ABDSharedCode` por CMake con local + FetchContent.
- Compila y enlaza una gran parte del repo compartido:
  - `HardwareDrivers/`: `CasioCzVirtualController`, `CasioNibbleCodec`, `FskAudioModem`, `JunoTapeModem`, `SysExCodec`, `NRPNParser`,
  - `AudioComparator/`: `AudioABComparator`, `AudioABComparator_Alignment`, `AudioABComparator_Spectral`, `AudioABVerdictEngine`,
  - `HardwareMidiDetect/`: `JuceMidiHardwareBackend`, `MidiEndpointSafetyPolicy`,
  - `WebView2Bridge/`: `WebView2ResourceProvider`,
  - `StudioTopology/`,
  - `MidiKeyboard/` (capa nativa),
  - `tests/` del repo compartido (baterías herméticas).
- Usa `ABDShared::ScopeCore` desde `ABDSharedCode/Scope`.

Evidencia en este árbol:
- `ABDAudioLab/CMakeLists.txt`,
- `ABDAudioLab/docs/audits/*` (actas de sprint e inventario de Scope y hardware),
- `ABDAudioLab/contracts/hardware/*.json` que apuntan a `sourceOfTruth: ABDSharedCode/SynthCore/...`,
- `ABDAudioLab/docs/ROADMAP.md` con hitos de extracción ya completados.

### 1.2 Qué parece ya migrado o ya decidido
Según sus propios docs y hitos:
- extracción de `ABDShared::HardwareDrivers`,
- `CasioNibbleCodec`, `SysexPresetGenerator`,
- `VoiceAllocator`, `VoiceDispersionModel`, `JunoBBD` (con corrección documentada hacia `DspEffects/JunoBBD.h`),
- modelado de LUTs oficiales a `ABDSharedCode/LutDSP/models`,
- absorción de Scope como subtree canónico en `ABDSharedCode/Scope`.

Esto indica que `ABDAudioLab` ya está bastante alineado con el compartido; lo más probable es que los candidatos aquí no sean "migrar desde cero", sino "limitar derivas / consolidar".

### 1.3 Posibles candidatos por revisar aquí
- confirmar si sigue habiendo forwarders/stubs locales redundantes con lo que ya está en el compartido,
- verificar si algunos componentes visuales nuevos (p.ej. algo tipo `Waterfall3DComponent`) ya están promovidos o siguen en `ABDAudioLab/src/gui` con forwarder,
- revisar si algún contrato(schema)/generado puede alinearse más con `sourceOfTruth` del compartido.

---

## 2. `ABDMS2000`

### 2.1 Consumo ya confirmado
- Integra `ABDSharedCode` con local + FetchContent.
- Enlaza `ABDShared::SynthCore`, `ABDShared::HardwareDrivers`, `ABDShared::ScopeCore/ScopeCoreHeaders`.
- Consume `SysExCodec` y `NRPNParser` desde `Source/MIDI/`.
- Consume `MidiKeyboard` como workspace pnpm.

Evidencia en este árbol:
- `ABDMS2000/CMakeLists.txt`,
- `ABDMS2000/wasm/CMakeLists.txt`,
- `ABDMS2000/Source/MIDI/*.h`,
- `ABDMS2000/package-lock.json`, `ABDMS2000/ROADMAP.md`,
- `ABDMS2000/build-test/ABDSharedCode/...` empaquetados.

### 2.2 Evidencia documentada de duplicado/sustitución
Desde `ABDMS2000/ANALISIS_DRY_COMPARTIDO.md` y `ABDMS2000/ANALISIS_DRY_BANKMANAGER_CZ101_ABDEEP.md`:
- duplicados locales vs compartido:
  - `Source/MIDI/SysExCodec.{h,cpp}` vs `ABDSharedCode/HardwareDrivers/SysExCodec`,
  - `Source/MIDI/NRPNParser.{h,cpp}` vs `ABDSharedCode/HardwareDrivers/NRPNParser`,
  - `WebUI/src/components/utils.js` vs `ABDSharedCode/MidiKeyboard/src/utils.js`.
- provider WebView2 propio (`Source/PluginEditor_ResourceProvider.*`) vs `ABDSharedCode/WebView2Bridge/WebView2ResourceProvider.*`.
- selección manual de puertos vs `HardwareMidiDetect` + contratos (`korg_ms2000.json` ya existe).
- AutoUpdater enlazado sin referencias en Source (seudo dependencia muerta).

Esto es candidato fuerte de sustitución/limpieza, con evidencia explícita en los docs del proyecto.

### 2.3 Posibles candidatos de extracción/unificación
- asignación de voz: converger `VoiceManager`/`VoiceAllocator`.
- posible `ABDShared::Filters` o extensión de `LutDSP` para filtros modelados/ZDF si se decide salir del modulo actual.

Recordatorio de regla:
- lo que lleve JUCE/libm en el lazo de audio no entra en capas que la guía dice libm-free/JUCE-free sin reescritura.

---

## 3. `ABDJUNiO601`

### 3.1 Consumo confirmado y migraciones cerradas
- Integra `ABDSharedCode` por CMake (`add_subdirectory`).
- Enlaza `ABDShared::LutDSP`, `ABDShared::DspCore`, `ABDShared::HardwareDrivers`.
- **`DspJunoHPF` migrado y verificado**: `Source/Synth/JunoHPF.h` actúa como shim sobre `ABDSharedCode/DspCore/DspJunoHPF.h`.
- **`ArpeggiatorJuno` migrado y verificado**: `Source/Synth/JunoArpeggiator.h` actúa como shim zero-alloc sobre `abd::synth::ArpeggiatorJuno`.
- BBD Chorus integrado desde `ABDSharedCode/DspEffects/JunoBBD.h`.
- Suite completa de tests unitarios nativos pasando al 100% en verde con cero fallos (`--- JUNO UNIT TESTS FINISHED ---`).

### 3.2 Candidatos restantes / por revisar
- Evaluación de generalización de decodificación de cinta / FSK hacia `HardwareDrivers`.
- Evaluación de `JunoVCF_ZDF` frente a un futuro módulo transversal de filtros.

---

## 4. `ABDEep`

### 4.1 Consumo confirmado y migraciones cerradas
- Integra `ABDSharedCode` por CMake (`add_subdirectory`).
- Enlaza `ABDShared::SynthCore`, `ABDShared::DspCore`, `ABDShared::DspEffects`.
- **Unidades C++20 promovidas a SynthCore**: `VoiceAllocator`, `DriftEngine`, `EnvelopeAnalog`, `LfoAnalog`, `Arpeggiator`, `ControlSequencer`.
- **Alineación de osciladores**: `DSPHelpers.h` consume el `PolyBLEP.h` canónico de `SynthCore` y utilidades de `DSPUtils.h`.
- **`DspJunoHPF` migrado y verificado**: `Source/DSP/JunoHPF.h` es shim sobre `DspJunoHPF.h`.
- **Paridad WASM completa**: `wasm/CMakeLists.txt` enlaza directamente las unidades de `SynthCore`, con generación exitosa de `abdeep_dsp.js/.wasm` y 5.084 tests de WebUI pasando al 100%.

### 4.2 Candidatos restantes / activos
- **Filtros modelados locales**: `MoogLadderVCF`, `KorgMS20VCF`, `VAOnePoleFilter` (candidatos activos de promoción hacia `DspCore` o `ABDShared::Filters`).

---

## 5. `ABDCZ101`

### 5.1 Lo que sí parece cierto
- consume `@abdsynths/midi-keyb` y `@abdsynths/shared` desde workspace,
- tiene schemas y scripts propios,
- hay menciones cruzadas en `ABDAudioLab/docs/ROADMAP.md` de reutilización hacia editor de parches/envelope de CZ101.

### 5.2 Qué no afirmo sin más revisión
No tengo inventario de su `Source/` C++ en esta pasada, así que no listo duplicados concretos. Queda "por confirmar".

---

## 6. `ABDNeural`

### 6.1 Consumo ya confirmado (con reserva sobre `DspEffects`)
- consume `ABDShared::DspCore`,
- hace referencia documentada a `ABDShared::DspEffects` (p.ej. en CMake de `ABDNeural`); **no se confirmó en esta sesión que `ABDSharedCode/DspEffects/` exista como carpeta/módulo disponible** en el árbol montado, por lo que este consumo debe tratarse como señal de consumo documentado más que como evidencia de que el módulo de efectos esté ya entregado y consumible tal cual,
- consume `MidiKeyboard`/workspace,
- consume `WebView2Bridge` desde testing/contracto,
- tiene refs cruzadas a `ABDSharedCode/SynthCore/S950PatchFields.h` y `S950Calibration.h` desde contratos generados.

Evidencia en este árbol:
- `ABDNeural/CMakeLists.txt`,
- `ABDNeural/Source/DSP/*`,
- `ABDNeural/WebUI/pnpm-workspace.yaml`,
- `ABDNeural/ROADMAP.md`, `ABDNeural/HANDOFF.md`.

### 6.2 Posibles candidatos por revisar
- si tiene efectos/catálogo propios duplicados del catálogo compartido,
- si hay Scope/picker/resource provider propio que pueda alinearse al patrón compartido.

---

## 7. `ABDScope`

### 7.1 Situación clave
- parece un repo hermano histórico de Scope,
- cuyo contenido canónico se está absorviendo en `ABDSharedCode/Scope` según sus propios HANDOFF/docs.

### 7.2 Implicación
Esto es estructural: no es solo "migrar algo", es decidir dónde vive Scope de forma canónica y unificar los dos lados.

Evidencia:
- `ABDScope/HANDOFF.md`,
- `ABDScope/docs/EVALUATION_ABDSHAREDCODE_ASSETS.md`,
- `ABDScope/docs/SESSION_CONTINUATION_PROMPT.md`.

---

## 8. `ABDOmegaUnified`

### 8.1 Lo que dice la evidencia ahora
- su `host/CMakeLists.txt` no referencia a `ABDSharedCode`.
- sus `package.json` (`host`, `web`, `host/ui`) no declaran dependencias `@abdsynths/*`.
- su stack parece: JUCE, YAML, Catch2, WAMR, CLAP extension, Next.js para el editor web.

### 8.2 Veredicto
**Por confirmar / sin consumo directo detectado en este árbol.** No afirmo que no tenga nada que ver con ABDSharedCode, pero en este monorepo montado no hay evidencia de consumo directo a nivel de CMake o package.json.

### 8.3 Qué haría falta para decidir
- revisar `ABDOmegaUnified/host/Source/` y submódulos para ver si ya tiene sus propios efectos/filtros/detección/scope,
- mirar si su editor web usa contratos de `ABDSharedAssets/contracts` o si es totalmente propio,
- ver si tiene bins/common DSP que pueda converger con el compartido.

---

## 9. Resumen por balde

### 9.1 Consumo real / ya migrado
- `ABDAudioLab`
- `ABDMS2000` (parcial, pero muy integrado)
- `ABDNeural` (DspCore, MidiKeyboard, WebView2Bridge references, contratos generados desde SynthCore; la referencia a `DspEffects` figura como consumo documentado pero **no fue confirmada en esta sesión como módulo disponible** en `ABDSharedCode`)
- `ABDScope` como repo absorbible en `ABDSharedCode/Scope`

### 9.2 Sustitución con evidencia documentada
Principalmente desde `ABDMS2000`:
- SysExCodec/NRPNParser locales,
- utils.js del WebUI,
- provider WebView2 propio,
- detección manual de puertos vs HardwareMidiDetect,
- AutoUpdater muerto enlazado.

Nota: en todos los puntos que mencionan sustitución por un módulo de efectos/filtros del compartido, hay que tener presente que, en este árbol, **no hay evidencia de que ese módulo de efectos esté ya publicado como target disponible**; donde los docs hablan de `DspEffects`/filtros compartidos, lo correcto es leerlo como “si existe / si se decide crearlo”, no como capa consumible confirmada.

### 9.3 Candidatos a extracción (nuevo módulo compartido)
- filtros modelados/ZDF (`ABDMS2000` sugiere `ABDShared::Filters` o extensión LutDSP),
- asiganador/unificación de voz,
- posible convergencia de patrón resource provider WebView2 y Scope canónico.

### 9.4 Por confirmar
- `ABDJUNiO601`
- `ABDEep`
- `ABDCZ101`
- `ABDOmegaUnified`
- cualquier otro repo cuyo `Source/` y CMake no se haya revisado en profundidad.

---

## 10. Regla general que limita las migraciones reales

Si algo usa JUCE o libm en el lazo de audio, la migración no es directa hacia módulos que la guía del repo dice JUCE-free / libm-free. Eso puede significar:
- reescritura,
- shim local,
- o no moverlo tampoco.

Así que "susceptible" no siempre significa "se mueve tal cual".

---

Si quieres, puedo:
- convertir esto en una tabla con una fila por proyecto y columnas: consumo real / duplicado documentado / candidato extracción / pendiente,
- o empezar a redactar la hoja de ruta mínima para `ABDMS2000` (porque es donde hay más evidencia documentada ahora mismo).
