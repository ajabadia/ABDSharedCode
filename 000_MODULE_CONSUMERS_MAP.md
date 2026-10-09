# Mapa de funcionalidades y archivos — ABDSharedCode

> Raíz: `D:\desarrollos\ABDSynths\ABDSharedCode`
> Propósito del directorio: código compartido (C++ y JS) usado por varios proyectos ABDSynths.
> Cómo leer este fichero:
> - Cada sección dice qué hace la carpeta/módulo, qué proyecto lo consume, y su estado.
> - Estado: `activo`, `parcial`, `legacy`, `huérfano`, `deprecado`.
> - Donde habla el documento, la evidencia es: CMake de los proyectos, pnpm workspace/lock, y docs de ABDSharedCode.

---

## 1. Raíz del directorio

**`ABDSharedCode/`** es el repositorio raíz del código compartido. No es un producto por sí mismo: vive aquí y se consume desde otros repos.

Qué hay en la raíz y a quién sirve:

| Elemento | Qué es | Quién lo usa | Estado |
|---|---|---|---|
| `CMakeLists.txt` | Orquestador de los targets C++ del repo (crear librerías/alias/tests) | Todos los proyectos que consumen ABDSharedCode por CMake | activo |
| `INTEGRATION_GUIDE.md` | Guía de integración del repo | Proyectos consumidores (C++ y JS) | activo |
| `package.json` (raíz) |penas raíz del workspace pnpm de este repo; sirve para que `MidiKeyboard` pueda resolver su dependencia `workspace:*` a `@abdsynths/shared`, que vive en otro repo | `ABDSharedCode/MidiKeyboard` y los proyectos que consumen ese paquete | activo (como soporte de workspace, no como producto) |
| `pnpm-workspace.yaml` | Miembros del workspace local de este repo | Instalación dentro de ABDSharedCode | activo |
| `pnpm-lock.yaml` | Lock del workspace local | Instalación dentro de ABDSharedCode | activo |
| `node_modules/` | Dependencias del workspace local | Mismo razonamiento que package.json | activo (artefacto, no funcionalidad) |
| `build/` | Build artifacts de MSVC/CMake | Resultado del build del repo | artefacto |
| `.github/` | CI del repo | `shared-code-ci.yml` y acciones reutilizadas | activo |
| `docs/` | Documentación de aspectos específicos del repo | Consumidores y mantenimiento del repo | activo |
| `tools/` | Scripts de auditoría/ingeniería del repo | Mantenimiento interno del repo | activo |
| `notes/`, `temp/`, `visualizers/`, `.freebuff/` | Documentación/artefactos/scratch | Varía; ver nota de "cosas sin consumidor claro" | mezclado |
| `Nuevo Documento de texto.txt`, `nul.exe` | Ficheros sueltos sin relación clara con el proyecto | Ninguno documentado | dudoso / posible basura |
| `_Deprecados/` | Código retirado (con `#error` en algunos casos) | Ningún consumidor activo | deprecado |

---

## 2. Módulos C++ que se consumen desde fuera

Estos son los que la guía de integración documenta como integrables desde otros proyectos.

### 2.1 `SynthCore/`

**Qué hace:** Motores y primitivas de síntesis en C++20 sin JUCE: arpegiador, secuenciador de control, matriz de modulación, envolventes, LFOs, drift, portamento, osciladores modelados (ca72 y polyBLEP), convertidor exponencial CA-72, etc.

**Proyectos que lo consumen:**
- `ABDEep`
- `ABDMS2000`
- `ABDJUNiO601`
- `ABDNeural`
- `ABDAudioLab` (vía integración de ABDSharedCode)

**Estado:** activo.

**Archivos notables:**
- `SynthCore/Arpeggiator.*`
- `SynthCore/ArpeggiatorJuno.h` (unificado zero-alloc C++20; consumido por ABDJUNiO601)
- `SynthCore/ControlSequencer.*`
- `SynthCore/LfoAnalog.*`
- `SynthCore/EnvelopeAnalog.*`
- `SynthCore/DriftEngine.*`
- `SynthCore/PortamentoGlide.*`
- `SynthCore/PolyBLEP.h` (utilizado por ABDEep y JUNiO)
- `SynthCore/DSPUtils.h`
- `SynthCore/OscillatorFamily.*`
- `SynthCore/OscVcoCa72.*`
- `SynthCore/CvToControl.*`
- `SynthCore/VoiceAllocator.*`

**Obs:** Según la guía y el CMake de los proyectos, `ABDShared::SynthCore` es el target que consume el núcleo. Motores como `ArpeggiatorJuno`, `LfoAnalog`, `EnvelopeAnalog`, `DriftEngine` y `PolyBLEP` ya están consumidos activamente por `ABDEep` (nativo + WASM) y `ABDJUNiO601`.

---

### 2.2 `DspCore/` y `DspEffects/`

**Qué hace:**
- `DspCore/` es el sustrato DSP sin JUCE ni libm: math, buffer utils, familia de filtros, HPF analógico modelado (`DspJunoHPF.h`), etc.
- `DspEffects/` está descrito en varios docs y CMake como la capa de efectos/comparadores sobre ese sustrato (chorus, delay, reverb, saturación, BBD, shelf, sistema de slots, etc.), pero **en el árbol revisado en esta sesión no apareció como carpeta presente**; por eso la descripción que sigue es lo que indican los documentos existentes, no un inventario verificado archivo a archivo.

**Proyectos que los consumen (según CMake y docs):**
- `ABDEep` (incluye `DspJunoHPF.h` unificado vía shim en `Source/DSP/JunoHPF.h`, y referencia `SynthCore`/`DspCore`/`DspEffects` en CMake)
- `ABDMS2000` (al menos algunos efectos migrados según sus docs)
- `ABDNeural` (referencia `DspCore`/`DspEffects` en CMake)
- `ABDJUNiO601` (`DspJunoHPF.h` unificado vía shim en `Source/Synth/JunoHPF.h`, coro BBD y efectos relacionados)

**Estado:** 
- `DspCore` parece activo y con archivos visibles en este árbol (`DspJunoHPF.h` incluido).
- `DspEffects` parece documentado como módulo, pero **no fue verificado como existente en este barrido**; cualquier afirmación de consumo debe leerse como “consumo documentado/centrado en CMake/docs”, no como “módulo de efectos disponible y comprobado archivo a archivo”.

**Archivos notables (parte verificada y parte documentada):**
- `DspCore/DspCore.h`, `DspCore/DspMath.h`, `DspCore/DspFilterFamily.h`, `DspCore/DspFilterTpt.h`, `DspCore/DspFilterEquation.h`, `DspCore/DspJunoHPF.h` — verificados/partialmente visibles en este árbol.
- `DspEffects/...` — descritos en docs como `FxEngine.h`, `FxSlot.h`, `FxRegistry.h`, `FxDefaultCatalogue.h`, `DspChorus.h`, `DspDelay.h`, `DspReverb.h`, `DspSchroederReverb.h`, `DspSaturation.h`, `JunoBBD.h`, `ShelfFilter.h`, `MultiHeadEcho.h`, `RingMod.h`, etc. **Estos nombres salen de los documentos existentes, no de un recuento verificado en este barrido**.

**Obs:** Este par es el ejemplo más explícito del contrato "JUCE-free y libm-free" del repo en los documentos, y de que la paridad se comprueba en el consumidor. `DspJunoHPF.h` está completamente integrado en ABDEep y ABDJUNiO601 con 100% de paridad y tests pasando.

---

### 2.3 `HardwareDrivers/`

**Qué hace:** Controladores/núcleos de protocolo para hardware físico: SysEx codec, NRPN parser, FSK modem, controladores AIRA, controladores Casio CZ/nibble, validador de routing, etc.

**Proyectos que lo consumen:**
- `ABDAudioLab` (compila la mayoría de sus fuentes)
- Otros proyectos que usan la capa de hardware del ecosistema

**Estado:** activo como capa compartida.

**Archivos notables:**
- `HardwareDrivers/SysExCodec.*`
- `HardwareDrivers/NRPNParser.*`
- `HardwareDrivers/FskAudioModem.*`
- `HardwareDrivers/AiraSysExController.*`
- `HardwareDrivers/RoutingValidator.*`
- `HardwareDrivers/MidiCcController.*`
- `HardwareDrivers/JunoTapeModem.*`
- `HardwareDrivers/CasioCzVirtualController.*`
- `HardwareDrivers/CasioNibbleCodec.*`

**Obs:** Según la guía, `CasioCzVirtualController.cpp` NO está en el target compartido a propósito, por evitar forzar `nlohmann/json` a todo consumidor.

---

### 2.4 `HardwareMidiDetect/`

**Qué hace:** Detección de hardware MIDI mediante contratos (sin SysEx hardcodeado), con capa C++ pura (`HardwareMidiDetector`) y capa WebView2 con WebUI (`JuceHardwareMidiPicker` y resource provider).

**Proyectos que lo consumen:**
- `ABDAudioLab` (compila sus fuentes y palos de tests)
- Proyectos con WebView2 que quieran picker de hardware

**Estado:** activo.

**Archivos notables:**
- `HardwareMidiDetect/HardwareMidiDetector.*`
- `HardwareMidiDetect/HardwareContractRegistry.*`
- `HardwareMidiDetect/HardwareContract.*`
- `HardwareMidiDetect/MidiHardwareBackend.*`
- `HardwareMidiDetect/JuceHardwareMidiPicker.*`
- `HardwareMidiDetect/HardwareMidiPickerResourceProvider.*`
- `HardwareMidiDetect/WebUI/`

---

### 2.5 `StudioTopology/`

**Qué hace:** Componente de topología/entorno de estudio: detector de interfaces audio/MIDI, controlador y WebUI embebida.

**Evidencia de uso:**
- `ABDAudioLab` referencia `StudioTopology/StudioTopologyController.*` y `StudioTopologyResourceProvider.*`, y empaqueta `StudioTopology/resources/*` como `StudioTopologyAssets`.
- `ABDNeural/build-reference` también tiene `StudioTopologyAssets.vcxproj` con los mismos recursos.

**Estado:** activo como componente integrado en, al menos, ABDAudioLab (y referencia cruzada en ABDNeural).

**Archivos notables:**
- `StudioTopology/StudioTopologyController.*`
- `StudioTopology/StudioTopologyResourceProvider.*`
- `StudioTopology/JuceStudioTopologyComponent.*`
- `StudioTopology/AudioMidiInterfaceDetector.*`
- `StudioTopology/StudioTopologyFloatingWindow.*`
- `StudioTopology/resources/`

---

### 2.6 `AudioComparator/`

**Qué hace:** Comparador A/B de alta precisión: alineamiento temporal (correlación cruzada FFT), métricas temporales y espectrales, y motor de veredicto configurable.

**Proyectos que lo consumen:**
- `ABDAudioLab` compila sus cuatro fuentes `.cpp` y los enlaza.

**Estado:** activo como módulo integrado en ABDAudioLab. La guía lo etiqueta como "fuente para consumir por ruta o candidato a módulo formal", así que hay que verificar si algún otro proyecto lo enlaza como target.

**Archivos notables:**
- `AudioComparator/AudioABComparator.*`
- `AudioComparator/AudioABComparator_Alignment.*`
- `AudioComparator/AudioABComparator_Spectral.*`
- `AudioComparator/AudioABVerdictEngine.*`

---

### 2.7 `MidiKeyboard/` (mitad nativa)

**Qué hace (mitad nativa):** Monta el teclado MIDI en WinPE/WebView2 desde un plugin: componente JUCE, ventana flotante y resource provider.

**Proyectos que lo consumen:**
- `ABDAudioLab` (usuario de `MidiKeyboardResourceProvider.*` y `JuceMidiKeyboardComponent.*`)

**Estado:** activo en su parte nativa.

**Archivos notables:**
- `MidiKeyboard/JuceMidiKeyboardComponent.h`
- `MidiKeyboard/MidiKeyboardFloatingWindow.h`
- `MidiKeyboard/MidiKeyboardResourceProvider.*`

**Obs:** Ver `2.8` para la mitad JS.

---

### 2.8 `MidiKeyboard/` (mitad JS → `@abdsynths/midi-keyb`)

**Qué hace (JS):** Teclado virtual MIDI para WebUI: keybed, ruedas, pedales, QWERTY/touch, animaciones, y feedback host-driven sin eco.

**Proyectos que lo consumen (JS workspace):**
- `ABDEep` — depende de `@abdsynths/midi-keyb` (workspace:*)
- `ABDMS2000` — depende de `@abdsynths/midi-keyb`
- `ABDCZ101` — depende de `@abdsynths/midi-keyb`
- `ABDNeural/WebUI` — depende de `@abdsynths/midi-keyb`
- (El propio paquete vive en `ABDSharedCode/MidiKeyboard`)

**Estado:** activo como paquete JS compartido.

**Archivos notables:**
- `MidiKeyboard/package.json` → `@abdsynths/midi-keyb`
- `MidiKeyboard/src/keyboard.js`
- `MidiKeyboard/src/keyboard.css`
- `MidiKeyboard/tests/`
- `MidiKeyboard/demo/` (usa puppeteer para grabar demo)

**Obs:** Este es el paquete del workspace raíz de ABDSynths, NO es código que se compile con CMake. La referencia cliente es `workspace:*` desde `ABDSharedCode/MidiKeyboard`.

---

### 2.9 `WebView2Bridge/`

**Qué hace:** Puente/patron base para incrustar WebView2 en plugins: componente JUCE y resource provider que sirve la WebUI embebida + catálogo + fallback de filesystem.

**Proyectos que lo consumen:**
- `ABDAudioLab` referencia `WebView2ResourceProvider.*` (y `JuceWebView2Component.*` en tests).
- `ABDScope` tiene tema duplicado/documentado relativo a este puente (ScopeResourceProvider vs `HardwareMidiPickerResourceProvider`).

**Estado:** activo, pero hay que revisar qué tan publicado está. La guía y el HANDOFF de Scope dicen que ABDAudioLab lo referencia por path en `target_sources`, y que el status de publish del target en CMake merece revisión.

**Archivos notables:**
- `WebView2Bridge/WebView2ResourceProvider.*`
- `WebView2Bridge/JuceWebView2Component.*`
- `WebView2Bridge/testing/`

---

### 2.10 `BankManager/` (corte como módulo compartido)

**Qué hace:** Módulo embebible del Bank Manager: core C++ v1 con ValueTree y blobs, adaptador JSON<->core, loader de factory content y protocolo SysEx del Pro800.

**Proyectos que lo consumen:**
- `ABDBankManager` es su consumidor principal (el corte viene de ahí).
- El contrato JS/TS del Bank Manager vive aquí también (`BankManager/Contracts/`), consumido desde `ABDBankManager` y 아마도 otros proyectos del ecosistema.

**Estado:** activo como módulo opcional. Por defecto está OFF (`ABDSHAREDCODE_BUILD_BANKMANAGER=OFF`) y lo activa quien lo consume.

**Archivos notables:**
- `BankManager/ABDBankManagerCore.*`
- `BankManager/BankManagerWebViewAdapter.*`
- `BankManager/FactoryContentLoader.*`
- `BankManager/Pro800Midi.*`
- `BankManager/HardwareMidiPipe.*`
- `BankManager/Contracts/` (ModelContract, HardwareLinkContract, etc., en TS)

**Obs:** Según la guía, esto viene del corte desde `ABDBankManager`, y sigue la misma arquitectura contract-driven del ecosistema.

---

### 2.11 `LcdDisplay/`

**Qué hace (históricamente):** Proveía componente/puente de display LCD para plugins.

**Proyectos que lo consumen:**
- Ninguno activo según el inventory del repo: varios documentos (guía, audit, HANDOFF de ABDNeural) lo marcan como migrado al WebUI y sin consumidor real.

**Estado:** huérfano / probable deprecación. Python de auditoría del repo lo lista como fuente huérfana justificada.

**Archivos notables:**
- `LcdDisplay/LcdDisplay.h`
- `LcdDisplay/LcdMenuManager.h`
- `LcdDisplay/LcdDisplayProbe.cpp`

**Obs:** Hay un gemelo/reescritura del lado de NEURONiK (`Source/UI/LcdDisplay.*` en ABDNeural) que se retiró y se migró al WebUI; lo compartido quedaría aquí como residuo.

---

### 2.12 `LutDSP/`

**Qué hace:** Evaluador SIMD de LUTs, módulo de filtrado polifónico analógico y miembro LUT de la familia de filtros.

**Proyectos que lo consumen:**
- `LutDSP/LutFilter.h` es el miembro LUT de la familia, usado en el routing de filtros.
- El módulo en sí tiene test standalone y target, pero según la guía hay extracciones/lands migratorias que movieron cosas fuera, por ejemplo `JunoBBD.h` que se movió a `_Deprecados`.

**Estado:** parcial / en transición.

**Archivos notables:**
- `LutDSP/LutEvaluatorSimd.*`
- `LutDSP/AnalogLutFilterModule.*`
- `LutDSP/LutFilter.h`
- `LutDSP/VoiceAllocator.h` (re-export de `SynthCore/VoiceAllocator.h` para `abd::lutdsp`)
- `LutDSP/models/`

**Obs:** En particular, `LutDSP-JunoBBD.h` fue movido a `_Deprecados` con `#error` según la guía.

---

### 2.13 `Scope/` (como subtree dentro de ABDSharedCode)

**Qué hace:** Osciloscopio/analyzers embebido en WebView2: recolector lock-free, taps por bloque, snapshot a 60 fps, serialización y WebUI embebida.

**Proyectos que lo consumen:**
- `ABDShared::ScopeCore` es el target que consumen los plugins que integran Scope.
- `ABDAudioLab` consume Scope vía `ABDSharedCode/Scope` (según CMake y la nota de integración de ABDAudioLab).
- `ABDMS2000` también documenta que Scope vive en `ABDSharedCode/Scope` desde v0.4.0.

**Estado:** activo como módulo dentro de ABDSharedCode.

**Archivos notables (canónicos dentro de ABDSharedCode):**
- `Scope/Source/Core/` → `ScopeTap.h`, `ScopeDataCollector.h`, `ScopeTapType.h`, `TapId.h`, `SpscRingBuffer.h`, `TriggerDetector.h`, `ScopeFrameSerializer.h`
- `Scope/Source/JUCE/` → `JuceWebScopeComponent.h`, `JuceScopeComponent.h`, `ScopeResourceProvider.*`
- `Scope/WebUI/` → paquete JS `@abdsynths/scope`
- `Scope/build/`, `Scope/docs/`, `Scope/tools/`
- `Scope/package.json` y `Scope/WebUI/package.json` (ambos `@abdsynths/scope`)

**Obs:** Hay gemelo histórico fuera: el repo `ABDScope/` también tiene su propio `Source/` y `WebUI/`. De hecho, el repo `ABDScope` tiene `package.json` propio con nombre `@abdsynths/scope`. Esto puede ser fuente de confusión; lo que probablemente ocurra es que el repo `ABDScope` es la vía antigua/standalone y `ABDSharedCode/Scope` es la nueva fuente canónica dentro del workspace compartido.

---

### 2.14 `Segmented/`

**Qué hace:** Puerta/visualización (?) de segmentación de forma de onda / datos (el nombre sugiere segmenter de imágenes o secuencias; hay probe nativo).

**Proyectos que lo consumen:**
- Ninguno activo documentado como enlazando el target; el probe (`SegmentedProbe`) aparece como compilado dentro del propio repo, pero sin consumidor de producto claro.
- La guía y el audit lo marcan como "trabajo terminado a medias": target existe, probe compila, pero falta un producto que lo enlace.

**Estado:** huérfano interino / trabajo a medias.

**Archivos notables:**
- `Segmented/Segmented.h`
- `Segmented/SegmentedProbe.cpp`

---

## 3. Módulos/funcionalidades sin consumidor claro, legacy o dudoso

### 3.1 `visualizers/` (en la raíz del repo)

**Qué parece:** Carpeta genérica de visualizaciones. No aparece como módulo integrado documentado en la guía de integración, y no hay evidencia de consumo desde los proyectos revisados.

**Estado:** sin consumidor claro.

**Obs:** Si tiene código real y útil, conviene moverlo a su módulo correspondiente o documentar quién lo consume.

---

### 3.2 `Certification/`

**Qué parece:** Carpeta de certificación/validación. Aparece en el árbol y en la guía como módulo sin sección de integración propia todavía.

**Estado:** sin consumo documentado en este inventario.

**Obs:** Si es reglas/actividades de certificación para proyectos del ecosistema, el sitio correcto sería la sección propia en la guía o un directorio de docs; si es artefacto temporal o legacy, deprecarlo.

---

### 3.3 `AutoUpdater/`

**Qué hace:** Actualización automática basada en GitHub Releases, con config por proyecto.

**Proyectos que lo consumen:**
- `ABDAudioLab` compila y enlaza `ABDShared::AutoUpdater` (según el .sln/tlog de ABDAudioLab).
- Los documentos de evaluación del repo mencionan `AutoUpdater/AutoUpdater.cpp` como modificado en ciertos análisis.
- Hay un config por proyecto (`AutoUpdaterConfig.h`) que cada consumidor tiene que aportar.

**Estado:** activo como módulo, pero con consumo real documentado al menos en ABDAudioLab.

**Archivos notables:**
- `AutoUpdater/AutoUpdater.h`
- `AutoUpdater/AutoUpdater.cpp`
- `AutoUpdater/AutoUpdaterConfig.h`

---

### 3.4 `_Deprecados/`

**Qué hace:** Residuo de código retirado, con `#error` en algunos ficheros para evitar inclusiones accidentales.

**Proyectos que lo consumen:** Ninguno activo.

**Estado:** deprecado.

**Archivos notables (ejemplo):**
- `_Deprecados/LutDSP-JunoBBD.h`

---

### 3.5 Ficheros sueltos en la raíz sin relación clara

- `Nuevo Documento de texto.txt`
- `nul.exe` (posible basura o fichero mal nombrado; sin contexto claro)

**Estado:** dudoso. Si no son de trabajo activo, convendría limpiarlos o al menos moverlos fuera de la raíz.

---

## 4. Proyectos del ecosistema que consumen ABDSharedCode

Esto son los que he podido confirmar con evidencia en este directorio. No es una lista de todos los repos de la suite, sino de los que el árbol actual muestra relaciones medibles hacia `ABDSharedCode`.

### 4.1 `ABDAudioLab`

**Qué parece ser:** Plugin de desarrollo/laboratorio de audio (nombre sugiere "audio lab").

**Cómo consume ABDSharedCode:**
- CMake: `add_subdirectory(ABDSharedCode)` desde local, con fallback FetchContent.
- Compila desde `ABDSharedCode/`: HardwareDrivers, AudioComparator, HardwareMidiDetect, WebView2Bridge, MidiKeyboard (parte nativa), StudioTopology, AutoUpdater, Scope (vía el subtree dentro de ABDSharedCode), y sus tests/targets asociados.
- Además referencia `Scope/Source/` y `HardwareMidiDetect/WebUI/*` embebido.

**Estado del consumo:** activo y amplio.

---

### 4.2 `ABDEep`

**Cómo consume ABDSharedCode:**
- CMake: integración local/FetchContent de `ABDSharedCode`.
- JS workspace: depende de `@abdsynths/midi-keyb` y `@abdsynths/shared`.
- Consumo activo de `SynthCore` (`VoiceAllocator`, `DriftEngine`, `EnvelopeAnalog`, `LfoAnalog`, `Arpeggiator`, `ControlSequencer`, `PolyBLEP`), `DspCore` (`DspJunoHPF`), y `DspEffects`.
- Compilación nativa y WebAssembly (Emscripten) enlazando directamente las unidades C++20 de `SynthCore` y shims unificados.

**Estado del consumo:** activo (100% verificado en tests nativos y WebUI Vitest).

---

### 4.3 `ABDMS2000`

**Cómo consume ABDSharedCode:**
- CMake: "ABDSharedCode Integration" en su CMakeLists.txt, con doble vía local/FetchContent. Además, incluye `ABDBankManager/cpp` como submódulo y hace notar que ABDBankManager ya incluye ABDSharedCode.
- JS workspace: depende de `@abdsynths/midi-keyb` y `@abdsynths/shared`.
- Migraciones documentadas: efectos como `TapeDelay`, `EnsembleFx`, `ShelfFilter` desde el propio `Source/DSP/Effects` del proyecto hacia el módulo compartido.

**Estado del consumo:** activo.

---

### 4.4 `ABDJUNiO601`

**Cómo consume ABDSharedCode:**
- CMake: integración local de `ABDSharedCode`.
- Enlaza `ABDShared::LutDSP`, `ABDShared::DspCore` y `ABDShared::HardwareDrivers`.
- Consumo activo y verificado de:
  - `ABDSharedCode/DspCore/DspJunoHPF.h` (vía shim `Source/Synth/JunoHPF.h`)
  - `ABDSharedCode/SynthCore/ArpeggiatorJuno.h` (vía shim zero-alloc en `Source/Synth/JunoArpeggiator.h`)
  - `ABDSharedCode/DspEffects/JunoBBD.h` (modelo BBD)
- Suite completa `JunoUnitTests` pasando al 100% con cero fallos.

**Estado del consumo:** activo.

---

### 4.5 `ABDNeural`

**Cómo consume ABDSharedCode:**
- WebUI: depende de `@abdsynths/midi-keyb` y `@abdsynths/shared`.
- CMake: integración de `ABDSharedCode` (con build-reference notable en el árbol).
- Consumo documentado de `DspCore`, `DspEffects`, `SynthCore`, `MidiKeyboard`, `LcdDisplay` (legacy/migrado), etc.
- Aparición de `StudioTopologyAssets` en su build-reference también apunta a consumo indirecto del paquete de assets de StudioTopology.

**Estado del consumo:** activo.

---

### 4.6 `ABDCZ101`

**Cómo consume ABDSharedCode:**
- JS workspace: depende de `@abdsynths/midi-keyb` y `@abdsynths/shared`.
- CMake: tiene integración propia de JUCE/wasm y build/_deps.

**Estado del consumo:** activo en la capa JS del workspace; para C++ marca revisar la integración concreta del proyecto.

---

### 4.7 `ABDScope` (repo hermano)

**Qué parece ser:** Repo propio de Scope (oscilloscope/analyzer), que ahora también tiene codificación dentro de `ABDSharedCode/Scope`.

**Cómo relacionarse con ABDSharedCode:**
- `ABDScope/CMakeLists.txt` además de su propio target, documenta que cuando se consume vía `add_subdirectory`, el Scope en cuestión es el de `ABDSharedCode/Scope`.
- `ABDScope/package.json` → `@abdsynths/scope`, igual que `ABDSharedCode/Scope/package.json`.

**Estado del consumo/relación:** hay superposición canónica. Lo más probable es que `ABDSharedCode/Scope` sea la vía canónica dentro del monorepo compartido y `ABDScope/` sea la vía standalone/legacy.

---

### 4.8 `ABDOmegaUnified`

**Qué parece ser:** Suite/unificador (host + web + módulos). En el árbol aparece con `host/` y `web/`.

**Consumo de ABDSharedCode:**
- No se detectó consumo directo en este inventario mediante los cruces realizados.

**Estado:** por confirmar. Posiblemente no consuma este repo tal cual, o lo haga de forma indirecta que aún no aparece en los ficheros revisados.

---

### 4.9 `ABDAudioLab` (ya mencionado) y `ABDBankManager` (relación de corte)

**ABDBankManager** no es consumidor externo en este inventario de la misma forma que los demás, sino el proyecto desde el que se cortó el módulo `BankManager/` dentro de ABDSharedCode. Es decir: ABDSharedCode tiene un módulo que viene del corte, y ABDBankManager es el repo que lo consume/mantiene.

---

## 5. Resumen de estado por carpeta

### Carpas de módulos activos y claramente consumidos
- `SynthCore`
- `DspCore`
- `DspEffects`
- `HardwareDrivers`
- `HardwareMidiDetect` (y `HardwareMidiDetect/WebUI`)
- `StudioTopology` (y `StudioTopology/resources`)
- `AudioComparator`
- `MidiKeyboard` (mitad nativa + `MidiKeyboard/src` + `MidiKeyboard/tests` + `MidiKeyboard/demo`)
- `WebView2Bridge` (y `WebView2Bridge/testing`)
- `BankManager` (y `BankManager/Contracts`)
- `Scope` (y `Scope/Source`, `Scope/Source/Core`, `Scope/Source/JUCE`, `Scope/WebUI`, `Scope/build`, `Scope/docs`, `Scope/tools`, `Scope/node_modules`)
- `AutoUpdater`

### Carpas parciales / en transición
- `LutDSP` (y `LutDSP/models`)
- `Segmented`
- `LcdDisplay`

### Carpas sin consumidor claro / revisar
- `visualizers`
- `Certification` (y subcarpetas que pueda tener)

### Carpas deprecadas o residuo
- `_Deprecados`
- raíz: ficheros sueltos sin contexto (`Nuevo Documento de texto.txt`, `nul.exe`)

### Carpas de infraestructura o artefactos (no funcionalidad de producto)
- `build/` y sus subcarpetas de targets (`*.dir`, `x64`, `Release`, etc.)
- `node_modules/` y subcarpetas internas del workspace local
- `tests/` (tests propios del repo)
- `tools/` (scripts internos del repo, incluyendo `tools/wasm_freestanding`)
- `docs/` (docs internos del repo)
- `notes/` (notas internas)
- `temp/` (scratch)
- `.github/` (CI del repo)
- `.git/` (control de versiones)
- `.freebuff/` (metadatos de la herramienta)
- **carpeta duplicada:** `ABDSharedCode/ABDSharedCode/` y su subcarpeta `tools/` — aparece en el árbol actual y no está documentada; hay que decidir si es layout real o copia residual

---

## 6. Dudas y pendiente

Cosas que este inventario no puede cerrar solo con los ficheros revisados:

1. **`visualizers/`** — si tiene código vivo, a qué proyecto sirve, o si es scratch.
2. **`Certification/`** — si es reglas/actividades de certificación del ecosistema y quién lo consume, o si es residuo.
3. **¿Scope canónico es `ABDSharedCode/Scope` o `ABDScope/`?** — conviene decidirlo explicitamente y alinear los dos lados, porque ahora hay dos `package.json` con el mismo nombre de paquete.
4. **¿Cuál es el estado exacto de consumo de `AudioComparator` y `LutDSP` como target formal vs por-ruta?** — la guía parece indicar que todavía no está del todo consolidado en todos los proyectos.
5. **¿ABDOmegaUnified consume o no ABDSharedCode?** — no se encontró evidencia en este directorio; conviene mirar sus CMake y sus package.json concretos.
6. **Ficheros raíz sueltos** — valorar si `nul.exe` y el txt son basura, artefacto de herramienta, o documentación pendiente.

Si quieres, puedo:
- añadir una columna de "evidencia exacta" por carpeta (ficheros/cmakelists/lock del consuming project),
- convertir el resumen en tablas de actor→módulo,
- o limpiar/condenar las carpetas sospechosas (con tus indicaciones) antes de publicar el documento.
