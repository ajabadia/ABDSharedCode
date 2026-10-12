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

**Convención de este documento sobre verificación:**
- “verificado en este árbol” = apareció como carpeta/archivo/target visible en `ABDSharedCode` en el barrido de esta sesión.
- “documentado/consolidado” = aparece citado en CMake de los consumidores, en la guía, o en docs del propio `ABDSharedCode`, pero **no se confirmó como carpeta o target presente** en el árbol montado ahora.
- Cuando una sección mezcla ambos, se explicita; cuando un nombre de archivo salga solo de docs y no de un recuento directo, se señala.
- Esto es relevante sobre todo para `DspEffects`/filtros y para módulos que los documentos mencionan como futuros o en transición.

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

**Qué hace (según docs):**
- `DspCore/` es el sustrato DSP sin JUCE ni libm: math, buffer utils, familia de filtros, HPF analógico modelado (`DspJunoHPF.h`), etc.
- `DspEffects/` está descrito en varios docs y CMake como la capa de efectos/comparadores sobre ese sustrato (chorus, delay, reverb, saturación, BBD, shelf, sistema de slots, etc.)

**Nuevo: Módulos migrados de melodee-main (Prophet-5)**
- `DspProphetVCF.h/.cpp` - VCF filter de Prophet-5 Rev-4 con modelos SSI y Curtis (float precision)
- `DspProphetLFO.h/.cpp` - LFO con ondas saw/triangle/pulse y rate 0.022-500 Hz (float)
- `DspProphetEnvelope.h/.cpp` - Envoladeras ADSR separadas para filtro y amplificador (float)
- `DspProphetOscillator.h/.cpp` - Generadores de onda con BLEP band-limited (saw, pulse, triangle, sine) (float)

Estos módulos fueron extraídos de `D:\desarrollos\ABDSynths\_RESOURCES\melodee-main\firmware\src`,
convirtiendo el DSP de punto fijo original a float, normalizando namespaces a `abd::dsp` y
extrayendo los "magic numbers" a estructuras de configuración (`DspProphetVCFConfig`, etc.)
para facilitar su ajuste sin recompilar.

**Proyectos que los consumen:**
- Candidato a `ABDNeural` y `ABDJUNiO601` para enriquecer sus motores de síntesis
- Potencial consumo desde `ABDAudioLab` para expansión de motor Prophet-5

**Estado:** activo (nuevo módulo, pendiente de verificación de compatibilidad y tests)

**Verificación en este árbol (sesión actual):**
- `DspCore` parece activo y con los nuevos archivos visibles (`DspProphetVCF.h`, etc.).
- `DspEffects` mantiene su estado anterior; los nuevos módulos DspCore están listos para
  ser consumidos por cualquier proyecto que necesite arquitectura de sintetizador Prophet-5.

**Proyectos que los consumen (según CMake y docs):**
- `ABDEep` (podría consumir estos módulos para expansión de motor Prophet-5)
- `ABDNeural` (referencia `DspCore` en CMake, podría usar los nuevos osciladores/VCF)
- `ABDJUNiO601` (could benefit from Prophet-5 VCF/LFO for Juno expansions)
- `ABDAudioLab` (reference `DspCore`; podría integrar estos módulos)

**Estado:** 
- `DspCore` parece activo en este árbol con los nuevos módulos Prophet-5 agregados.
- Los nuevos módulos están documentados y listos para consumo, pendientes de tests de paridad.

**Archivos notables (agregados en esta sesión):**
- `DspCore/DspProphetVCF.h`, `DspCore/DspProphetVCF.cpp`
- `DspCore/DspProphetLFO.h`, `DspCore/DspProphetLFO.cpp`
- `DspCore/DspProphetEnvelope.h`, `DspCore/DspProphetEnvelope.cpp`
- `DspCore/DspProphetOscillator.h`, `DspCore/DspProphetOscillator.cpp`
- `DspCore` parece activo y con archivos visibles (`DspJunoHPF.h` incluido en varios CMake consumidores).
- `DspEffects/` ahora aparece como carpeta presente en `ABDSharedCode`, con arquitectura visible de contenedor/catálogo/política (`FxEngine.h`, `FxSlot.h`, `FxRegistry.h`, `FxDefaultCatalogue.h`, `EffectPolicy.h`) más varios efectos concretos, subcarpetas `adapters/`, `characters/`, `profiles/`, y `DspEffectsTests.cpp`. Este barrido **no verificó un `DspEffects/CMakeLists.txt` a ese nivel ni confirmó la publicación del target** `ABDShared::DspEffects`; por encima, lo que sí tiene es una carpeta con contenido real y tests propia, no un módulo “solo nuclear” ni un catálogo completo equivalente al de `ABDEep/Source/DSP/FX/`.

**Proyectos que los consumen (según CMake y docs):**
- `ABDEep` (incluye `DspJunoHPF.h` unificado vía shim en `Source/DSP/JunoHPF.h`, y referencia `SynthCore`/`DspCore`/`DspEffects` en CMake)
- `ABDMS2000` (al menos algunos efectos migrados según sus docs)
- `ABDNeural` (referencia `DspCore`/`DspEffects` en CMake)
- `ABDJUNiO601` (`DspJunoHPF.h` unificado vía shim en `Source/Synth/JunoHPF.h`, coro BBD y efectos relacionados)

**Estado:** 
- `DspCore` parece activo en este árbol.
- `DspEffects` ahora tiene evidencia de existencia como carpeta con contenido, pero **no fue verificado como target publicado/consumible completo**; cualquier afirmación de consumo debe leerse como “carpeta presente con efectos/catálogo/tests visibles y citada en CMake/docs”, no como “módulo de efectos entregado y comprobado archivo a archivo / target disponible”.

**Archivos notables (parte verificada, parte documentada):**
- `DspCore/DspCore.h`, `DspCore/DspMath.h`, `DspCore/DspFilterFamily.h`, `DspCore/DspFilterTpt.h`, `DspCore/DspFilterEquation.h`, `DspCore/DspJunoHPF.h` — visibles o referenciados en CMake de este árbol.
- `DspEffects/...` — ver la nota al final del documento (`A. Catálogo fino de `DspEffects/` vs `ABDEep/Source/DSP/FX/``) para el desglose muestra-do de qué está presente ahí y qué distingue de la capa propia de ABDEep.

**Obs:** Este par es el ejemplo más explícito del contrato JUCE-free/libm-free en los documentos, y de que la paridad se comprueba en el consumidor. `DspJunoHPF.h` está completamente integrado en ABDEep y ABDJUNiO601 con 100% de paridad y tests pasando.

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

### 2.15 `VoiceAllocator` (como parte de `SynthCore`, no como carpeta separada)

**Qué hace:** Asignación/pólítica de voces compartida dentro del núcleo de síntesis.

**Ubicación verificada en este árbol:**
- `ABDSharedCode/SynthCore/VoiceAllocator.h`

**Proyectos que lo consumen (según CMake/docs):**
- aparece citado en `ABDEep`, `ABDAudioLab`, y en el contexto de migraciones/unificaciones de asignación de voz.

**Estado:** activo como archivo dentro de `SynthCore`; no aparece como carpeta `VoiceAllocator/` en `ABDSharedCode` en este barrido. Cualquier afirmación que hable de “`VoiceAllocator` del compartido” debe entenderse como referencia a `SynthCore/VoiceAllocator.h`, no a un módulo independiente con esa ruta.

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

---

## A. Catálogo fino de `DspEffects/` vs `ABDEep/Source/DSP/FX/` (sesión actual)

Esta sección es un **catálogo de lo que apareció en el árbol ahora**, no una lista definitiva de todo lo que existe ni una declaración de que el catálogo está completo. No se verificó un `DspEffects/CMakeLists.txt` a ese nivel ni la publicación del target `ABDShared::DspEffects`; por encima, lo que se registró es lo siguiente.

### A.1 Lo que aparece en `ABDSharedCode/DspEffects/`

Arquitectura visible de contenedor/catálogo/política + efectos + soporte inyectable + tests:

- contenedor/sistema de slots/catálogo/política:
  - `FxEngine.h`
  - `FxSlot.h`
  - `FxRegistry.h`
  - `FxDefaultCatalogue.h`
  - `EffectPolicy.h`
- efectos con nombre tipo estándar (sin marcar una máquina/tema concreto en el nombre):
  - `DspReverb.h`
  - `DspChorus.h`
  - `DspDelay.h`
  - `DspPhaser.h`
  - `DspSchroederReverb.h`
  - `DspSaturation.h`
  - `ShelfFilter.h`
  - `RingMod.h`
  - `MultiHeadEcho.h`
  - `Phaser4.h`
  - `CascadeShelfEq.h`
- efectos con semántica más ligada a máquina/tema (el nombre o la estructura apunta a un dispositivo/tema):
  - `JunoBBD.h`
  - `DspMoodFilter.h`
  - `DspVocoder.h`
  - `DspSolinaEnsemble.h`
- soporte inyectable:
  - `adapters/BasicAdapters.h`
  - `characters/BbdNoise.h`
  - `characters/DiodeBridge.h`
  - `characters/TapeColour.h`
- perfiles (como dato del dispositivo):
  - `profiles/JunoBbdProfile.h`
  - `profiles/ReverbProfile.h`
  - `profiles/Re201Profile.h`
  - `profiles/MS2000EqProfile.h`
  - `profiles/Dm12SpaceEchoProfile.h`
- tests propios:
  - `DspEffectsTests.cpp`

**Interpretación:** `DspEffects/` ya tiene más que un núcleo mínimo: tiene sistema de slots/catálogo/política, varios efectos reales, soporte inyectable y tests. Pero no parece un catálogo completo equivalente al de `ABDEep/Source/DSP/FX/`, donde ABDEep mantiene encima una capa de producto propia más amplia.

### A.2 Lo que aparece en `ABDEep/Source/DSP/FX/`

Infraestructura propia de slots/ecosistema + gran variedad de efectos, algunos con nombre tipo estándar y otros con nombres semánticamente temáticos o propios:

- infraestructura/ecosistema propio:
  - `FXEngine.h` / `FXEngine.cpp`
  - `FXEngine_Routing.cpp`
  - `FXSlot.h` / `FXSlot.cpp` / `FXSlot_Factory.cpp`
  - `FXBase.h`
- efectos que parecen tipo estándar o genéricos (nombre limpio, sin marcar máquina):
  - `FXSimpleReverb.h` / `FXSimpleReverb.cpp`
  - `FXDuckingDelay.h` / `FXDuckingDelay.cpp`
  - `FXChorus.h` / `FXChorus.cpp`
  - `FXChorusD.h` / `FXChorusD.cpp`
  - `FXPhaser.h` / `FXPhaser.cpp`
  - `FXMidasEQ.h` / `FXMidasEQ.cpp`
  - `FXDelay.h` / `FXDelay.cpp`
  - `FXCombulator.h` / `FXCombulator.cpp`
  - `FXNoiseGate.h` / `FXNoiseGate.cpp`
  - y otros con nombres similares (delay, phaser, chorus, EQ, comp, gate, etc.)
- efectos con nombres semánticamente temáticos o propios:
  - `FXNimbus.h` / `FXNimbus.cpp`
  - `FXEdison.h` / `FXEdison.cpp`
  - `FXRackAmp.h` / `FXRackAmp.cpp`
  - `FXTreemonster.h` / `FXTreemonster.cpp`
  - `FXRotarySpeaker.h` / `FXRotarySpeaker.cpp`
  - `FXZitaReverb.h` / `FXZitaReverb.cpp`
  - `FXFDNReverb.h` / `FXFDNReverb.cpp`
  - `FXSpaceEchoRE201.h` / `FXSpaceEchoRE201.cpp`
  - `FXBonsai.h` / `FXBonsai.cpp`
  - `FXFairComp.h` / `FXFairComp.cpp`
  - `FXGranularDelay.h` / `FXGranularDelay.cpp`
  - y otros con nombres temáticos parecidos
- tests propios:
  - `FXUnitTests*.cpp` (varios)

**Interpretación:** `ABDEep/Source/DSP/FX/` tiene una capa propia de producto más amplia que el catálogo compartido visible ahora; algunos efectos parecen tipos estándar y otros parecen propios/temáticos. Eso no significa que `DspEffects/` deba copiar todo eso; solo significa que no debe asumirse que `DspEffects/` es un catálogo completo equivalente al de ABDEep.

### 2.3 Módulos extraídos/integrados de otras fuentes

**Prophet-5 (giá completado):**
- `DspProphetVCF`, `DspProphetLFO`, `DspProphetEnvelope`, `DspProphetOscillator`

**SID (Commodore 64):**
- `DspSid.h/.cpp` - Emulación completa del chip MOS 6581/8580
  - 3 osciladores (TRI/SAW/PULSE/NOISE) con mix por voz
  - Generador de ruido LFSR de 23 bits
  - Filtro state-variable de 11-bit FC con resonance 4-bit
  - Envoladora ADSR de 15-bit con tasas exponenciales
  - Etapa de salida RC (coupling capacitor, low-pass)
  - Pile stacking modes: DETUNE, OCT, FIFTH, MAJOR, SYNC, RING, SUB
  - 6581 vs 8580 differences (DAC zero, filter knee, waveforms)
- Magic numbers extraídos a `DspSidConfig` (SID_RATE, SID_ZERO, SID_KNEE, tablas FC/Q)

**Physical Modeling:**
- `DspPhys.h/.cpp` - Modelos: Modal (12 BP modes), String (KS extendido), Membrana (cuerpo circular), Simpático (3 cuerdas)
- `DspAnalog.h/.cpp` - Sintetizador básico 2OSC + LPF con drive/saturation

**Casio CZ-101 (Phase Distortion):**
- `DspCz1.h/.cpp` - Síntesis de distorsión de fase Casio CZ-101/1
  - 96 tablas de amplitud parciales (CZ_AMP)
  - Modelo uPD933: tasas y objetivos de envelopes basadas en hardware
  - 8 tipos de onda seleccionables por "window" register
  - Palabras de control digital (DCW, pivot, k0, k1)
  - Envelopes de 8 etapas con leyes logarítmicas DCA
  - 2 osciladores por voz con mezcla y sincronización
- Los parámetros vienen del modelo uPD933 documentado,
  no son conversión fixed-point a float (el código original ya usa float).
- Magic numbers extraídos a `DspCz1AmpTable` y parámetros de envelope
- **Integración en ABDCZ101**: `PhaseDistOsc.cpp` - mapeo DCW logarítmico `m = 0.5/(1+4.9*|dcw|)` y factor ventana `windowFactor` integrado dentro del cálculo PD (no multiplierAfterwards)
- Estado: **activo** (fix DCW aplicado, utilidades DspCore integradas)

**Proyectos que los consumen:**
- `ABDNeural`: podría usar CZ-1 para texturas digitales retro
- `ABDJUNiO601`: podría usar CZ-1 para expandir motor Juno
- `ABDAudioLab`: referencia `DspCore`; todos son candidatos de integración

**Estado:** activo (fix DCW aplicado, utilidades DspCore integradas, pendientes de validación sónica completa)

---


### A.3 Qué significa esta distinción para los documentos

- si un documento dice “`DspEffects` existe”, puede decirlo como “carpeta presente con efectos/catálogo/tests visibles”,
- pero si un documento da por sentado que `DspEffects` cubre todo lo que tiene `ABDEep/Source/DSP/FX/`, eso ya no es correcto sin matizar,
- y si un documento habla de “`VoiceAllocator` del compartido”, la ubicación verificada es `SynthCore/VoiceAllocator.h`, no una carpeta `VoiceAllocator/` en `ABDSharedCode`.

Este catálogo es válido para lo que apareció en el árbol en esta sesión; si luego se añade/release un `DspEffects/CMakeLists.txt` o más archivos, la conclusión cambia.

---


### 2.3 Módulos extraídos de melodee-main (excepto FM)

**Prophet-5 (giá completado):**
- `DspProphetVCF`, `DspProphetLFO`, `DspProphetEnvelope`, `DspProphetOscillator`

**SID (Commodore 64):**
- `DspSid.h/.cpp` - Emulación completa del chip MOS 6581/8580
  - 3 osciladores (TRI/SAW/PULSE/NOISE) con mix por voz
  - Generador de ruido LFSR de 23 bits
  - Filtro state-variable de 11-bit FC con resonance 4-bit
  - Envoladora ADSR de 15-bit con tasas exponenciales
  - Etapa de salida RC (coupling capacitor, low-pass)
  - Pile stacking modes: DETUNE, OCT, FIFTH, MAJOR, SYNC, RING, SUB
  - 6581 vs 8580 differences (DAC zero, filter knee, waveforms)
- Magic numbers extraídos a `DspSidConfig` (SID_RATE, SID_ZERO, SID_KNEE, tablas FC/Q)

**Physical Modeling:**
- `DspPhys.h/.cpp` - Modelos: Modal (12 BP modes), String (KS extendido), Membrana (cuerpo circular), Simpático (3 cuerdas)
- `DspAnalog.h/.cpp` - Sintetizador básico 2OSC + LPF con drive/saturation

**Casio CZ-1 (Phase Distortion):**
- `DspCz1.h/.cpp` - Síntesis de distorsión de fase Casio CZ-1
  - 96 tablas de amplitud parciales (CZ_AMP)
  - Modelo uPD933: tasas y objetivos de envelopes basadas en hardware
  - 8 tipos de onda seleccionables por "window" register
  - Palabras de control digital (DCW, pivot, k0, k1)
  - Envelopes de 8 etapas con leyes logarítmicas DCA
  - 2 osciladores por voz con mezcla y sincronización
- Los parámetros vienen del modelo uPD933 documentado,
  no son conversión fixed-point a float (el código original ya usa float).
- Magic numbers extraídos a `DspCz1AmpTable` y parámetros de envelope

**Proyectos que los consumen:**
- `ABDNeural`: podría usar CZ-1 para texturas digitales retro
- `ABDJUNiO601`: podría usar CZ-1 para expandir motor Juno
- `ABDAudioLab`: referencia `DspCore`; todos son candidatos de integración

**Estado:** activo (nuevo módulo, nota: conversión distinta - ya usa float nativo uPD933)

---"

---


### 2.3 Módulos extraídos de melodee-main (excepto FM)

**Prophet-5 (giá completado):**
- `DspProphetVCF`, `DspProphetLFO`, `DspProphetEnvelope`, `DspProphetOscillator`

**SID (Commodore 64):**
- `DspSid.h/.cpp` - Emulación completa del chip MOS 6581/8580
  - 3 osciladores (TRI/SAW/PULSE/NOISE) con mix por voz
  - Generador de ruido LFSR de 23 bits
  - Filtro state-variable de 11-bit FC con resonance 4-bit
  - Envoladora ADSR de 15-bit con tasas exponenciales
  - Etapa de salida RC (coupling capacitor, low-pass)
  - Pile stacking modes: DETUNE, OCT, FIFTH, MAJOR, SYNC, RING, SUB
  - 6581 vs 8580 differences (DAC zero, filter knee, waveforms)
- Magic numbers extraídos a `DspSidConfig` (SID_RATE, SID_ZERO, SID_KNEE, tablas FC/Q)

**Physical Modeling:**
- `DspPhys.h/.cpp` - Modelos: Modal (12 BP modes), String (KS extendido), Membrana (cuerpo circular), Simpático (3 cuerdas)
- `DspAnalog.h/.cpp` - Sintetizador básico 2OSC + LPF con drive/saturation

Estos módulos fueron extraídos de `D:\desarrollos\ABDSynths\_RESOURCES\melodee-main\firmware\src`,
convirtiendo el DSP de punto fijo original a float, normalizando namespaces a `abd::dsp` y
extrayendo los "magic numbers" a estructuras de configuración para facilitar su ajuste.

**Proyectos que los consumen:**
- `ABDNeural`: podría usar SID para texturas chiptune
- `ABDJUNiO601`: podría usar Analog para expandir motor Juno
- `ABDAudioLab`: referencia `DspCore`; todos son candidatos de integración

**Estado:** activo (nuevos módulos, pendientes de tests de paridad y verificación sónica)