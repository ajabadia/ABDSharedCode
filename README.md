# ABDSharedCode

Librería de código compartido entre proyectos ABDSynths.

Repositorio: https://github.com/ajabadia/ABDSharedCode.git

## Módulos disponibles

| Módulo | Contenido | Tipo | Target CMake | Consumidores |
|---|---|---|---|---|
| **SynthCore** | Primitivas DSP de los sintetizadores (`abd::synth`): PolyBLEP, ADSR, EnvelopeCurves, PortamentoGlide, LFO, AudioThreadSnapshot, VoiceAllocator, DSPUtils | STATIC | `ABDShared::SynthCore` | ABDMS2000, ABDEep |
| **DspCore** | Sustrato portado de `juce_core`/`juce_audio_basics` (`abd::dsp`): Maths, Range, SmoothedValue, HeapBlock, AudioBuffer, FloatVectorOperations, MidiMessage/Buffer, Debug, LeakedObjectDetector | INTERFACE (header-only, **sin JUCE**) | `ABDShared::DspCore` | ABDNeural (+ DspEffects) |
| **DspEffects** | Efectos sobre el sustrato DspCore (`abd::dsp`): Reverb (Freeverb, port literal de `juce::Reverb`), Chorus, Delay, Saturation | INTERFACE (header-only, **sin JUCE**), linka DspCore | `ABDShared::DspEffects` | ABDNeural |
| **LutDSP** | Evaluación de LUTs y modelado analógico (`abd::lutdsp`): LutEvaluatorSimd (SSE), AnalogLutFilterModule, VoiceDispersionModel, JunoBBD | INTERFACE (header-only) | `ABDShared::LutDSP` | ABDJUNiO601 |
| **HardwareDrivers** | Codecs de protocolo de hardware (`abd::hw`): SysExCodec, NRPNParser | STATIC | `ABDShared::HardwareDrivers` | ABDMS2000, ABDJUNiO601 |
| **HardwareMidiDetect** | Detección contract-driven de hardware MIDI (C++ puro + picker WebView2 estilo ABDScope) | INTERFACE | `ABDShared::HardwareMidiDetect` (+ `ABDShared::HardwareMidiPickerAssets`) | ABDAudioLab |
| **AutoUpdater** | Auto-actualización via GitHub Releases | STATIC | `ABDShared::AutoUpdater` | ABDMS2000, ABDAudioLab |
| **MidiKeyboard** | Teclado y ruedas compartidos (`@abdsynths/midi-keyb`) | paquete de workspace pnpm, no CMake | — | ABDMS2000 |
| **Segmented** | Selector segmentado universal (`abd::ui::Segmented`): radio group de botones planos, valor por índice, vetados con nota (gating por motor) | INTERFACE (header-only), sonda opt-in `ABDShared_SegmentedProbe` | `ABDShared::Segmented` | (gemelo JS: `@abdsynths/shared/components/segmented.js`; consumidor NEURONiK pendiente de adoptarlo en el panel nativo) |
| **LcdDisplay** | Pantalla de caracteres universal + máquina de menú (`abd::ui`): LcdDisplay + LcdMenuManager, arbol como dato y hooks | INTERFACE (header-only), gate WASM | `ABDShared::LcdDisplay` | (gemelo JS: lcdMachine/lcdScreen/lcdPanel en `@abdsynths/shared`) |

> `StudioTopology/resources` se expone como `ABDShared::StudioTopologyAssets` cuando el asset existe (consumido por ABDAudioLab). Los directorios `Certification`, `WebView2Bridge`, `AudioComparator` y `visualizers` **no están expuestos como target CMake** todavía: son fuentes para consumir por ruta o candidatas a módulo formal.

> **HardwareMidiDetect** consume los contratos single-source de `ABDSharedAssets/contracts`. Ninguna consulta SysEx ni mapeo fabricante/modelo está hardcodeado: todo se deriva de `midiIdentification` y `autoDetectSysEx` de cada contrato.

## Estado del refactor DRY transversal (2026-09-19)

Etapa en curso: extraer a este repo el DSP que hoy vive duplicado en los sintets, con
**cero cambio de comportamiento** en los consumidores.

**Cerrado en esta etapa**

- **`DspCore`** — sustrato portado de `juce_core`/`juce_audio_basics`. Es un port
  literal, no una reinterpretación: mantiene incluso los detalles de JUCE que
  parecen inocentes y no lo son (p. ej. que el array de punteros a canal de
  `AudioBuffer` viva **dentro** del objeto hasta 31 canales y en el heap desde 32).
  Tiene tests standalone propios (`ABDShared_DspCore_Tests`, sin JUCE).
- **`DspEffects`** — Reverb, Chorus, Delay y Saturation sobre ese sustrato.
  **Regla de diseño:** el motor expone la **muestra** (`processSample`, `advance`)
  y la **política de producto** —suavizado de parámetros, mapeo de controles
  (segundos→muestras, amount→drive), recorte de feedback, mezcla, denormals— se
  queda en el consumidor. Es lo que permite mover el algoritmo sin cambiar ni una
  muestra: los smoothers se leían dentro del bucle de muestras, así que una API
  por bloque habría cambiado el resultado.
- Los consumidores prueban la equivalencia donde sí se puede comprobar contra
  JUCE real: `ABDNeural/Tests/{DspEffectsParityTest,AudioBufferParityTest}.cpp`,
  con **referencia congelada** (copia literal del efecto pre-migración) y **0 ulps**.

**Contrato de estos dos módulos:** son **JUCE-free**, también en sus tests. Las
comparaciones bit-exactas contra los originales de JUCE no viven aquí a propósito:
van en el consumidor, que es quien tiene JUCE y quien asume el riesgo del port.

**Pendiente:** la lista priorizada de homonimias y convergencias que quedan está en
[docs/homonimias-cabeceras.md](docs/homonimias-cabeceras.md) — incluye la matriz de
qué proyecto ha adoptado qué módulo (hoy ABDCZ101 no enlaza ninguno).

## Integración rápida

```cmake
# En CMakeLists.txt del proyecto
set(ABDSHARED_CODE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ABDSharedCode")
if(EXISTS "${ABDSHARED_CODE_DIR}/CMakeLists.txt")
    add_subdirectory("${ABDSHARED_CODE_DIR}" "${CMAKE_BINARY_DIR}/ABDSharedCode")
else()
    include(FetchContent)
    FetchContent_Declare(
      ABDSharedCode
      GIT_REPOSITORY https://github.com/ajabadia/ABDSharedCode.git
      GIT_TAG        master
    )
    FetchContent_MakeAvailable(ABDSharedCode)
endif()

target_link_libraries(TuPlugin PRIVATE ABDShared::AutoUpdater)
target_link_libraries(TuPlugin PRIVATE ABDShared::HardwareMidiDetect)  # opcional
```

> **Nota:** El `GIT_TAG` puede ser una rama (`master`), un tag de versión (`v1.0.0`) o un hash. En producción es recomendable fijarlo a un tag de versión concreto (`vX.Y.Z`), no a `master`, para evitar cambios inesperados.

## Publicación y versionado

Este repo se publica en GitHub. Para releases estables:

1. Nueva funcionalidad → `git add`, `git commit`, `git push`
2. Crear tag de versión:
   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```
3. Los consumidores fijan el tag en el `GIT_TAG` de `FetchContent`

## Documentación

Ver [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md) para guía completa.
