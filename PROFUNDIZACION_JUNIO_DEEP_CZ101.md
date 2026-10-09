# Profundización por repo — ABDJUNiO601 / ABDEep / ABDCZ101

> Qué parece estar ya en ABDSharedCode, qué parece sustituible por lo que hay allí,
> y qué parece candidato a ir (o unificarse) allí.
>
> Criterio: evidencia alcanzable en este árbol montado (CMakeLists, DspSources.cmake,
> scripts de sync/generación, includes, paquete.json, docs de estado). Donde no hay
> evidencia, figura como “por confirmar”.

---

## 0. Cómo leer esto

- **Ya en ABDSharedCode / consumible directo**: el proyecto ya apunta a un target/paquete/componente compartido y, en muchos casos, ya lo consume.
- **Sustituible por lo que hay allí**: parece duplicado o parece reemplazable por un módulo/componente/contrato ya existente.
- **Candidato a ir a ABDSharedCode**: parece utilizable por más de un proyecto y no está claro que pertenezca al repo actual.
- **Propio / por confirmar**: parece específico del producto, o no tengo evidencia suficiente para decir que pertenezca al compartido.

---

## 1. ABDJUNiO601

### 1.1 Evidencia que tengo

- CMake raíz integra `ABDSharedCode` con `add_subdirectory(../ABDSharedCode)`.
- Enlaza `ABDShared::LutDSP`, `ABDShared::DspCore`, `ABDShared::HardwareDrivers`.
- Usa `DspSources.cmake` con dos listas compartidas (nativo + WASM), y el comentario dice explícitamente que es para evitar drift entre dos builds.
- Tiene un conjunto Juno-propio muy marcado:
  - `Source/Synth/ChorusBBD`, `JunoTapeEcho`, `JunoVCF`, `JunoADSR`, `JunoLFO`, `JunoVoice`, `JunoDCO`,
  - `Source/Core/JunoSysExEngine`, `JunoSysEx`, `JunoTapeDecoder`, `JunoVoiceManager`, `PresetManager`, `ServiceModeManager`,
  - importers (`JunoTapeImporter`, `JunoSysexImporter`, `JunoFormatConverter`, `TalImporter`),
  - `Source/UI/WebUI/*` y `Source/UI/Sections/JunoSysExDisplay`.
- Enlaza `ABDShared::HardwareDrivers` en tests y plugin, no solo en uno de los dos.
- Tiene un `handoff.md` que lista ChorusBBD y Memory Tests como fallos pre-existentes.

### 1.2 Ya en ABDSharedCode / consumible directo

Esto ya está formalmente en el compartido y consumido activamente:
- `ABDShared::DspCore` (incluyendo `DspCore/DspJunoHPF.h` consumido vía shim `Source/Synth/JunoHPF.h`)
- `ABDShared::SynthCore` (incluyendo `SynthCore/ArpeggiatorJuno.h` consumido vía shim zero-alloc en `Source/Synth/JunoArpeggiator.h`)
- `ABDShared::LutDSP`
- `ABDShared::HardwareDrivers`
- `ABDShared::DspEffects` (`DspEffects/JunoBBD.h` como núcleo de coro BBD)
- Toda la suite nativa `JunoUnitTests` pasando al 100% en verde con cero fallos.

### 1.3 Sustituible por lo que hay allí / estado de convergencia

- **JunoHPF:** ✅ **Completado**: El filtro paso alto de 4 etapas está completamente unificado en `DspJunoHPF.h`.
- **Arpegiador Juno:** ✅ **Completado**: `JunoArpeggiator` migrado a `abd::synth::ArpeggiatorJuno` sin allocations en audio thread.
- **Chorus BBD:** Núcleo delegado en `JunoBBD.h`.
- **Tape echo y decodificación de cinta:** Parecen Juno-específicos, pero si hay lógica de FSK/modem/cinta más general, esa parte podría encajar en `HardwareDrivers`.
- **Voice manager/allocator:** Coexiste con la semántica Juno; evaluable si converge con `SynthCore/VoiceAllocator.h`.

### 1.4 Candidato a ir a ABDSharedCode

- Si `JunoVCF_ZDF` se generaliza, es candidato a un módulo transversal de filtros modelados (`ABDShared::Filters` o `DspCore`).
- Helpers de decodificación FSK / tape modem generalizables.

Pero esto es “candidato si se generaliza”, no “ya está listo para moverse”.

### 1.5 Propio / por confirmar

Parece propio o con alta carga Juno-specific:
- DCO, JunoVoice, PerformanceState, ServiceModeManager,
- UI del WebView2 de JUNiO601 (componentes, diálogos, import),
- el formato de preset/decodificación específico de Juno,
- y en general toda la UI/UX y pipeline de importación de cinta.

Esto parece que no sale del proyecto sin reescritura o without perder la marca Juno.

---

## 2. ABDEep

### 2.1 Evidencia que tengo

- CMake raíz integra `ABDSharedCode` con `add_subdirectory(../ABDSharedCode)`.
- Enlaza `ABDShared::SynthCore`, `ABDShared::DspCore`, `ABDShared::DspEffects` desde varios targets.
- Tiene su propio `DspSources.cmake` con dos listas para seguridad anti-drift (nativo + WASM).
- Tiene un generador propio de parámetros/registry muy avanzado:
  - `scripts/registry_generator.js`,
  - artefactos `.gen` en `WebUI/js/registry.gen.js` y `Source/Core/ParameterRegistry.gen.{h,cpp}`,
  - y un sistema de guards muy explícito.
- Tiene generador propio de contrato de efectos WebUI: `scripts/generate_fx_contract.mjs`.
- Parece consumir:
  - `@abdsynths/midi-keyb` (keybed compartido),
  - `@abdsynths/shared` (componentes como `fitStage`, etc.),
- Tiene docs de estado muy detallados sobre qué está promovido y qué queda en backlog.

### 2.2 Ya en ABDSharedCode / consumible directo

Esto está formalmente delegado al compartido y consumido activamente:
- `ABDShared::SynthCore` (`VoiceAllocator`, `DriftEngine`, `EnvelopeAnalog`, `LfoAnalog`, `Arpeggiator`, `ControlSequencer`, `PolyBLEP.h` y `DSPUtils.h` alineados con osciladores).
- `ABDShared::DspCore` (incluyendo `DspJunoHPF.h` unificado vía shim en `Source/DSP/JunoHPF.h`).
- `ABDShared::DspEffects` (con paridad y regression tests pasando).
- Compilación y enlace WebAssembly (Emscripten) enlazando directamente las unidades de `SynthCore` (`abdeep_dsp.js/.wasm`).
- `ABDSharedAssets/contracts/fx-effects.json` como fuente del contrato de efectos.
- `@abdsynths/midi-keyb` y `@abdsynths/shared` como paquetes JS consumidos.
- 100% de la suite pasando: 1.119.736 aserciones en C++ y 5.084 tests de WebUI Vitest.

### 2.3 Sustituible por lo que hay allí

- **JunoHPF:** ✅ **Completado**: Delegado a `DspJunoHPF.h`.
- **Osciladores PolyBLEP:** ✅ **Completado**: Delegados al algoritmo unificado en `SynthCore/PolyBLEP.h`.
- Contrato de efectos proyectado directamente desde el contrato compartido.

### 2.4 Promovido a ABDSharedCode
 
- **Filtros modelados (`JunoVCF_ZDF`, `VcfVoicing`, `MoogLadderVCF`, `KorgMS20VCF`, `VAOnePoleFilter`):**
  ✅ **Completado**: Promovidos a `ABDSharedCode/DspCore/` (`DspJunoVCF.h`, `DspVcfVoicing.h`, `DspMoogLadder.h`, `DspKorgMS20.h`, `DspVAOnePole.h`) en C++20 puro, zero-alloc y zero-JUCE. Consumidos en `ABDEep` mediante shims limpios y disponibles para `ABDJUNiO601` y `ABDMS2000`.

### 2.5 Propio / Arquitectura de producto (debe quedarse en ABDEep)

Parece propio o con alta carga de producto:
- DCOs específicos de voz (`OSC1`, `OSC2`), orquestación de polifonía de 12 voces (`SynthEngine`, `SynthVoice`),
- Calibration Lab y herramientas de audio A/B,
- serialización XML/JUCE y bridge nativo+Juce,
- frontend de la WebUI de DeepMind y lógica de bridge específica.

---

## 3. ABDCZ101

### 3.1 Evidencia que tengo

- CMake raíz incluye `../ABDSharedCode` en include directories y en varios targets de tests.
- Enlaza `WebUIAssets` (juce_add_binary_data de `WebUI/src/*`).
- Tiene `DspSources.cmake` con dos listas (nativo + WASM) documentadas.
- Consume `@abdsynths/midi-keyb` y `@abdsynths/shared` desde package.json.
- Usa componentes compartidos concretos:
  - `mountFitStage`/`computeFit` desde `@abdsynths/shared/components`,
  - `createKeyboard` desde `@abdsynths/midi-keyb`,
  - `createOverlayFocus` desde `@abdsynths/shared/components`.
- Tiene un script propio `sync_shared.js` para copiar `fitStage.js` desde `ABDSharedAssets/components/`.
- Tiene tests muy centrados en contratos de WebUI, fitStage, keyboard, webviewBridgeDirection, effects persistence, sysex, etc.

### 3.2 Ya en ABDSharedCode / consumible directo

Esto parece ya consumir explícitamente el compartido en la capa JS:
- `@abdsynths/midi-keyb`,
- `@abdsynths/shared`,
- componentes como `mountFitStage`, `createKeyboard`, `createOverlayFocus`.

También parece tocar el compartido en C++:
- incluye `../../../../ABDSharedCode/LutDSP/LutEvaluatorSimd.h` desde un oscilador/efecto,
- y referencias cruzadas de WebView2Bridge en su build y tests.

Esto me dice que ABDCZ101 parece más “consumidor JS del workspace” que “creador de módulos compartidos”.

### 3.3 Sustituible por lo que hay allí

Aquí la señal es clara en la capa JS:
- `fitStage` parece ya vivir en `ABDSharedAssets/components/fitStage.js`, y ABDCZ101 mantiene una copia sincronizada; si algo más del repo tiene su propia versión de fitStage, eso es candidato de sustitución.
- el keybed parece ser el componente compartido `@abdsynths/midi-keyb`, así que si el repo tuviera un keybed propio, sería candidato de sustitución.
- si hay algún componente overlay/focus parecido al de `@abdsynths/shared/components`, también sería candidato.

### 3.4 Candidato a ir a ABDSharedCode

En ABDCZ101, la pista más fuerte me parece esta:
- el repo parece tener su propio sistema de generación y sync de contratos (`registry_generator.js`, `sync_shared.js`, `build_webui.js`), y si alguno de esos es genéricamente útil, podría ser candidato a vivir como herramienta/generador más compartido; pero puede ser también muy ligado a CZ101.
- si hay efectos DSP genéricos (chorus, reverb, delay, drive), podrían ser candidatos a ir mejor a algún módulo de efectos compartido; pero CZ101 tiene temática Casio PD y puede que gran parte de eso sea propio.

### 3.5 Propio / por confirmar

Parece propio o con alta carga de producto:
- lógica de CZ (phase distortion, osciladores, wavetable, waveshaper),
- state/preset/bank/presets serialization,
- UI y lógica de plugin/editor específicos,
- y gran parte de su `Source/State/*`, `Source/DSP/*` y `Source/Wasm/*`, salvo lo que ya apunta al compartido.

---

## 4. Cruce rápido entre los tres

### 4.1 Lo que parece repetirse como “yo también quiero esto del compartido”

- keybed/teclado: JUNiO601 y ABDEep y CZ101 parecen tener o consumir keybed como tema recurrente; el paquete compartido `@abdsynths/midi-keyb` parece la pieza canónica para eso.
- fitStage/responsividad WebView2: CZ101 y ABDEep parecen usar `@abdsynths/shared/components`; si JUNiO601 o otros tienen sus propias versiones, esa es candidata de unificación.
- contrato de efectos: ABDEep parece consumir `fx-effects.json`; si otro proyecto mantiene su propia lista de efectos, esa es candidata de sustitución por el contrato compartido.

### 4.2 Lo que parece “cada uno tiene su propio mundo DSP”

- JUNiO601: Juno.
- ABDEep: DeepMind con su propio DCO/VCF/mod matrix/secuenciador.
- CZ101: Casio PD con su propio oscilador/wavetable/effects.

Esto sugiere que, más que mover “todo el synth”, lo más realista es mover capas transversales: alocación de voces, LFOs/Envelope/Arp si son genéricos, algunos filtros, algunos efectos, y contratos de efectos/parámetros.

### 4.3 Lo que parece más candidato de ir a ABDSharedCode desde estos tres

- VoiceAllocator si no está ya completamente resuelto como canónico.
- Algunos filtros modelados si se generalizan (y si no rompen las reglas del repo).
- Algunos efectos genéricos reutilizables más allá del proyecto.
- Herramientas de generación de contratos si son genéricas (registry generator, generator de contrato de efectos, sync de piezas compartidas); pero con cautela, porque pueden ser muy ligados al producto.

---

## 5. Qué NO puedo afirmar con hoy

- no tengo lectura completa de `Source/DSP/*` de los tres repos, así que no puedo decir “este archivo exacto es duplicado” con certeza total.
- no tengo confirmación de que todos los efectos/filtros de cada repo ya estén o no ya en ABDSharedCode.
- no tengo detalle de si CZ101 o JUNiO601 tienen su propio “resource provider/picker” o “scope/visual” que ya esté o no en el compartido.
- no he entrado en submódulos JUCE de los tres, ni en wasm/ submodules, por tiempo/herramientas; ahí puede haber más señal.

---

## 6. Si quieres que siga, lo más rentable diría que es

1. un barrido de “qué archivos de estos tres repos incluyen `../../../../ABDSharedCode/...` o `ABDShared::` o `@abdsynths/`”, por repo, con recuento; eso te da un mapa de dependencia real y no de intención.
2. un inventario de “qué tiene cada uno en `Source/DSP/Effects` o `Source/DSP/Filters` que no parezca Juno/DeepMind/CZ-exclusive”.
3. un repaso de si CZ101 o JUNiO601 tienen sus propias versiones de fitStage/keybed/overlay que ya existen en `@abdsynths/shared`.

Si quieres, puedo ir a cualquiera de esos tres, pero con límite de no abrir todo el árbol de golpe.
