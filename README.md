# ABDSharedCode

Librería de código compartido entre proyectos ABDSynths.

Repositorio: https://github.com/ajabadia/ABDSharedCode.git

## Módulos disponibles

| Módulo | Contenido | Tipo | Target CMake | Consumidores |
|---|---|---|---|---|
| **SynthCore** | Primitivas DSP de los sintetizadores (`abd::synth`): PolyBLEP, ADSR, EnvelopeCurves, PortamentoGlide, LFO, AudioThreadSnapshot, VoiceAllocator, **ModMatrix**, DSPUtils | STATIC | `ABDShared::SynthCore` | ABDMS2000, ABDEep, ABDMS2000/ABDEep/ABDNeural (ModMatrix, header-only) |
| **DspCore** | Sustrato portado de `juce_core`/`juce_audio_basics` (`abd::dsp`): Maths, Range, SmoothedValue, HeapBlock, AudioBuffer, FloatVectorOperations, MidiMessage/Buffer, Debug, LeakedObjectDetector | INTERFACE (header-only, **sin JUCE**) | `ABDShared::DspCore` | ABDNeural (+ DspEffects) |
| **DspEffects** | Efectos sobre el sustrato DspCore (`abd::dsp`): Reverb (Freeverb, port literal de `juce::Reverb`), Chorus, Delay, Saturation, `SchroederReverb`; y los de maquina, por politica inyectada: `EffectPolicy` + motores (`MultiHeadEcho`, `RingMod`), etapas de caracter (`TapeColour`, `DiodeBridge`) y perfiles de dispositivo (`Re201Profile` con los doce modos del selector, `ReverbProfile` con las diez variantes del DeepMind 12) | INTERFACE (header-only, **sin JUCE**), linka DspCore | `ABDShared::DspEffects` | ABDNeural, ABDEep, ABDJUNiO601 |
| **LutDSP** | Evaluación de LUTs y modelado analógico (`abd::lutdsp`): LutEvaluatorSimd (SSE), AnalogLutFilterModule, VoiceDispersionModel, JunoBBD | INTERFACE (header-only) | `ABDShared::LutDSP` | ABDJUNiO601 |
| **HardwareDrivers** | Codecs de protocolo de hardware (`abd::hw`): SysExCodec, NRPNParser | STATIC | `ABDShared::HardwareDrivers` | ABDMS2000, ABDJUNiO601 |
| **HardwareMidiDetect** | Detección contract-driven de hardware MIDI (C++ puro + picker WebView2 estilo ABDScope) | INTERFACE | `ABDShared::HardwareMidiDetect` (+ `ABDShared::HardwareMidiPickerAssets`) | ABDAudioLab |
| **AutoUpdater** | Auto-actualización via GitHub Releases | STATIC | `ABDShared::AutoUpdater` | ABDMS2000, ABDAudioLab |
| **MidiKeyboard** | Teclado y ruedas compartidos (`@abdsynths/midi-keyb`) | paquete de workspace pnpm, no CMake | — | ABDMS2000 |
| **Segmented** | Selector segmentado universal (`abd::ui::Segmented`): radio group de botones planos, valor por índice, vetados con nota (gating por motor) | INTERFACE (header-only), sonda opt-in `ABDShared_SegmentedProbe` | `ABDShared::Segmented` | (gemelo JS: `@abdsynths/shared/components/segmented.js`; consumidor NEURONiK pendiente de adoptarlo en el panel nativo) |
| **LcdDisplay** | Pantalla de caracteres universal + máquina de menú (`abd::ui`): LcdDisplay + LcdMenuManager, arbol como dato y hooks | INTERFACE (header-only), gate WASM | `ABDShared::LcdDisplay` | (gemelo JS: lcdMachine/lcdScreen/lcdPanel en `@abdsynths/shared`) |

> `StudioTopology/resources` se expone como `ABDShared::StudioTopologyAssets` cuando el asset existe (consumido por ABDAudioLab). Los directorios `Certification`, `WebView2Bridge`, `AudioComparator` y `visualizers` **no están expuestos como target CMake** todavía: son fuentes para consumir por ruta o candidatas a módulo formal.

> **HardwareMidiDetect** consume los contratos single-source de `ABDSharedAssets/contracts`. Ninguna consulta SysEx ni mapeo fabricante/modelo está hardcodeado: todo se deriva de `midiIdentification` y `autoDetectSysEx` de cada contrato.

## ModMatrix — la matriz de modulación compartida

`SynthCore/ModMatrix.h` (header-only, `abd::synth::ModMatrixT<N>`) es el motor de
rutas que comparten ABDEep, ABDMS2000 y ABDNeural. **No implementa todavía ninguno
de los tres**: es el núcleo sobre el que se migran.

**La regla de diseño es la de DspEffects: el motor expone la muestra, la política se
queda en el consumidor.** No hay ni un nombre de fuente ni de destino en el núcleo,
ni una escala, ni un clamp de producto. Solo suma, sin asignaciones y sin llamada
virtual en el lazo de audio.

**Por qué los identificadores son opacos.** Un `enum` de fuentes obligaría al motor a
conocer el vocabulario de un synth, y los tres no van a converger: ABDEep y
ABDMS2000 emulan hardware y sus índices son índices de byte con el orden de su
manual, mientras que ABDNeural es propio y su tabla es además el **formato de
preset** (los choices guardan índice, así que no se puede reordenar sin re-mapear
los presets guardados). Con identificadores opacos, el índice es lo que el proyecto
dice que es.

**Qué trae:**

| Pieza | Para qué |
|---|---|
| `ModMatrixT<N>::accumulate()` | API primitiva: un barrido de slots sumando en el buffer del llamante |
| `ModMatrixT<N>::get()` | azúcar con la MISMA firma y los MISMOS bits que `ModulationMatrix::getModulationValue` de ABDEep |
| `ModDestinationDescriptor` | la tabla de destinos como **dato** (label, `parameterId`, rango, `perNote`, `replaces`, `engineMask`) |

`perNote` y `replaces` son la política que ABDNeural tiene hoy enterrada en un
`switch` de 31 casos: que el cutoff se resuelve por voz, y que una ruta desde una
envolvente **reemplaza** el factor en vez de sumar encima (síntesis de reemplazo).
Eso no puede perderse al migrar el `switch` a la tabla.

**La garantía que permite migrar sin cambiar el sonido:** hay un test de
equivalencia **bit a bit** contra una copia literal de la implementación previa de
ABDEep (`SynthCore/tests/ModMatrixTests.inc`), sobre rutas leídas de los bancos de
fábrica del hardware. Si el núcleo cambia una sola ULP, ese test falla.

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
- **`DspEffects`, segunda tanda: la política inyectada** (`EffectPolicy.h`). La
  regla anterior separa *máquina* de *política de producto*. La misma costura
  aplicada a la **emulación de hardware** es lo que hace que los efectos "de
  máquina" se puedan compartir: el motor es una plantilla sobre un **perfil** (los
  números del dispositivo, un `struct` de datos) y una **etapa de carácter** (su no
  linealidad, con estado). El Space Echo RE-201 y un eco de cinta de estudio son
  el mismo `MultiHeadEcho` con dos perfiles distintos; añadir una máquina es un
  fichero más en `profiles/`, no otra clase con el 95% del código duplicado.
  Sin llamada virtual en el lazo de audio: la etapa es un parámetro de plantilla.

  **Un modo dice QUÉ cabezales suenan, no con qué ganancia.** El reparto
  (1/activos) es de la máquina y lo calcula el motor, porque un RE-201 no tiene
  una tabla de ganancias: tiene un selector. Por eso `Re201Profile` lleva los
  doce modos de ese selector, tomados de `JunoTapeEcho` (ABDJUNiO601), que es la
  emulación del RE-201 **sana** de la suite.

  Y una nota de provenance, que es lo primero que hay que saber al tocar esto:
  ABDEep tiene tambien un RE-201 (`FXSpaceEchoRE201`) y **no sirve de referencia**.
  Su tabla tiene cinco modos escritos a mano que no coinciden con el selector, y
  su línea de retardo está rota: usa `kMaxDelay = 70560` como máscara de bits sin
  que sea potencia de dos, y como sus cinco bits bajos son cero,
  `writePos = (writePos + 1) & delayMask` se queda en 0 para siempre. La línea no
  avanza nunca, el eco no existe, y el efecto suena a seca + tanque. La sonda que
  lo demuestra va en el changelog de la extracción, con su test de regresión.
- Los consumidores prueban la equivalencia donde sí se puede comprobar contra
  JUCE real: `ABDNeural/Tests/{DspEffectsParityTest,AudioBufferParityTest}.cpp`,
  con **referencia congelada** (copia literal del efecto pre-migración) y **0 ulps**.
- **La familia de reverbs del DeepMind 12**, en dos piezas. La máquina es
  `DspSchroederReverb` (4 conbs + 3 allpass, port de `FXSimpleReverb` de ABDEep)
  y los diez tipos son `ReverbProfile`, que es una tabla y nada más: un solo
  motor para diez variantes, donde antes había una clase con un `switch` de diez
  casos. El perfil no solo lleva los números de fábrica, sino también **qué mando
  del panel mueve qué control del motor**, porque el orden es el del hardware y
  cambia por variante (Deep Verb pone el pre-retardo en el 3, donde las demás lo
  tienen en el 0; Gated y Reverse no tienen mando de tamaño ni de amortiguación).

  **La API de `SchroederReverb` es por MARCO estéreo, no por canal, y no es
  accidental.** Su pre-retardo es mono —escribe una vez por muestra con la media de
  L y R— así que una API por canal lo escribiría dos veces y lo desplazaría medio
  bloque, que es un cambio de sonido. Un marco sigue siendo "la muestra", que es
  la unidad que la regla del módulo expone.

  Y tres comportamientos que se conservan **a propósito**, porque corregirlos
  cambiaría el sonido de efectos ya publicados. No son descuidos del port:
  1. Mover el tamaño o el pre-retardo **redimensiona los conbs y borra la cola**.
  2. El pre-retardo real es `segundos × sampleRate × **0.2**`, no × 1: pedir 100 ms
     retrasa 20 ms.
  3. "Reverse" (id 6) **niega solo el canal izquierdo**. No es una inversión de
     fase, es un efecto Haas.

  La paridad vive en `ABDEep/Source/DSP/FX/FXUnitTests_ReverbParity.cpp`, en dos
  capas: **paridad estructural** contra una copia literal del kernel anterior
  (muestra a muestra, las diez variantes, con y sin barrido de mandos, **0 ulps**)
  y **hashes congelados** por variante, calculados sobre la copia y no sobre el
  código nuevo — si el hash lo generase la implementación nueva, regenerarlo sería
  tautología. Esa paridad encontró un fallo real: `setSize` con el mismo número de
  muestras no toca el contenido viejo, y redimensionar a igual tamaño tenía que
  vaciar la cola igualmente.

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

## CI compartido (GitHub Actions)

[`.github/actions/pnpm-workspace-bootstrap`](.github/actions/pnpm-workspace-bootstrap/action.yml)
es una composite action que monta el workspace pnpm de la suite en un runner:
clona el repo llamante + este repositorio + ABDSharedAssets como hermanos,
escribe `pnpm-workspace.yaml`, instala y verifica que las dependencias
`workspace:*` quedaron realmente enlazadas.

```yaml
- name: Bootstrap pnpm workspace (multi-repo)
  uses: ajabadia/ABDSharedCode/.github/actions/pnpm-workspace-bootstrap@master
  with:
    project: ABDEep     # directorio del repo llamante dentro del workspace
```

Consumidores actuales: ABDEep (`webui-ci.yml`) y ABDMS2000 (`webui-visual-qa.yml`).
`@master` es una ref móvil: para congelar la versión, usa un tag o un SHA.

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

**Si vienes a por efectos DSP** (motor compartido, perfiles de dispositivo, cómo
comprobar la paridad y qué queda por extraer), ve a `INTEGRATION_GUIDE.md` →
sección **«Módulo: DspCore + DspEffects (C++/DSP, sin JUCE)»**. Ahí está el
catálogo de motores, las dos reglas que deciden dónde vive cada cosa, el
procedimiento de adopción paso a paso, y el inventario de qué está extraído y qué
no.
