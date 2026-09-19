# Homonimias de cabeceras en el ecosistema ABDSynths

> **Fecha:** 2026-09-19
> **Alcance:** solo cabeceras `*.h` de autoría propia (C++). **No** cubre JS/CSS,
> presets ni contratos generados por contenido — eso es otro barrido.
> **Objetivo:** saber qué nombres de fichero se repiten entre proyectos, y de esos,
> cuáles son deuda real (dos entidades distintas con el mismo nombre) y cuáles son
> benignos por construcción o por ser shims.

---

## 1. Método

Se listan los basenames de todas las cabeceras bajo `*/Source` y `*/wasm` de cada
proyecto del workspace, y se agrupan por nombre:

```bash
cd D:/desarrollos/ABDSynths
for d in ABDAudioLab ABDBankManager ABDCZ101 ABDEep ABDJUNiO601 ABDMS2000 \
         ABDNeural ABDOmegaUnified ABDPro008 ABDScope; do
  find "$d/Source" "$d/wasm" -name '*.h' -printf '%f\t%s\n' 2>/dev/null \
    | awk -v p="$d" -F'\t' '{print $1"\t"p}'
done > headers.tsv
```

Agrupar y quedarse con los nombres presentes en más de un proyecto dio **209
nombres**. La gran mayoría **no son deuda**: son las copias vendorizadas de JUCE y
zlib (`juce_*.h`, `zlib.h`, `inflate.h`, …) presentes en las carpetas de los
proyectos que aún llevan JUCE dentro del árbol. Filtrando ese vendor y los
generados por JUCE/el generador de contratos quedan **25 nombres de autoría
propia**, que es lo que analiza este documento.

### Regla de lectura

| Si los dos ficheros son… | Es… | Acción |
|---|---|---|
| la misma entidad y uno re-exporta al otro | **shim** | ninguna: es el mecanismo de migración |
| la frontera obligatoria del plugin (`PluginProcessor`, `BuildVersion`, …) | **benigno por construcción** | ninguna |
| entidades distintas con el mismo nombre | **homonimia real** | renombrar uno, o converger en un módulo compartido |

El riesgo de una homonimia real no es estético: si algún día una carpeta compartida
entra como *include dir* de un proyecto, un `#include "LFO.h"` se resuelve por
**orden de include dirs** → bug silencioso, dependiente del orden del CMake, sin
error de compilación. Es la misma clase de fallo que ya sufrió ABDMS2000 con su
lista de fuentes WASM.

---

## 2. Las 25, con veredicto

### A. Benignos por construcción (6) — no hacer nada

| Nombre | Proyectos | Por qué es benigno |
|---|---|---|
| `BuildVersion.h` | 6 | Generado por proyecto (número de build propio de cada uno). |
| `ParameterRegistry.gen.h` | 4 | Generado desde el contrato de parámetros de cada synth; un fichero por sintetizador por definición. |
| `PluginEditor.h` | 4 | Punto de entrada del plugin; el nombre y la clase son obligatorios por convención de JUCE. Cada uno declara su propia clase en su namespace. |
| `PluginProcessor.h` | 3 | Ídem. |
| `PluginEditor_ResourceProvider.h` | 2 (ABDCZ101 5L, ABDMS2000 9L) | El proveedor de recursos de WebView de cada plugin. Mismo rol, implementación propia. El compartido (`ABDSharedCode/WebView2Bridge/WebView2ResourceProvider.*`) existe precisamente para sustituirlo — ver prioridad P5. |
| `WasmBridge.h` | 3 (ABDCZ101 54L, ABDJUNiO601 94L, ABDMS2000 69L) | La frontera C++↔JS de cada synth: cada uno tiene su superficie de API. El **contrato** sí está compartido (`BridgeProtocolContractTest`), la implementación no. Candidato a convergencia a largo plazo. |

### B. Shims (1 nombre + 3 cruzados) — el mecanismo de migración, no deuda

| Nombre | Relación | Nota |
|---|---|---|
| `ADSREnvelope.h` | ABDMS2000 (16L) → `SynthCore/ADSREnvelope.h` | Re-exporta `abd::synth::ADSREnvelope` a `ABDMS2000`. |
| `AudioThreadSnapshot.h` | ABDMS2000 (15L) → `SynthCore/AudioThreadSnapshot.h` | Ídem. |
| `LFO.h` | ABDMS2000 (17L) → `SynthCore/LFO.h` | Ídem (y el motivo de que `LFO.h` salga en 4 proyectos). |
| `VoiceAllocator.h` | `LutDSP/VoiceAllocator.h` (19L) → `SynthCore/VoiceAllocator.h` | Curioso: **dentro de ABDSharedCode hay una homonimia resuelta como shim**, `abd::lutdsp` re-exporta `abd::synth`. El canónico es SynthCore (403L). |

Los shims son identificables en dos segundos por su cabecera: llevan la palabra
`Compatibility shim` y un `NOTE: sync artifact, not hand-maintained`.

### C. Homonimias reales (18 de los 25 nombres) — aquí está la deuda

| Nombre | Implementaciones | Naturaleza de la duplicación |
|---|---|---|
| `ADSREnvelope.h` | **ABDCZ101** (72L, `namespace CZ101`) vs SynthCore → MS2000 | Dos ADSR distintos: CZ101 no consume SynthCore. |
| `Arpeggiator.h` | **ABDCZ101** (336L) vs **ABDMS2000** (81L) | Dos arpeggiadores, uno por producto. |
| `Chorus.h` | **ABDCZ101** (56L) vs **ABDNeural** (96L, envoltorio) | **Ya existe el motor compartido** (`DspEffects/DspChorus.h`). |
| `Reverb.h` | **ABDCZ101** (28L) vs **ABDNeural** (101L, envoltorio) | Ídem (`DspEffects/DspReverb.h`). |
| `DSPHelpers.h` | **ABDCZ101** (49L) vs **ABDEep** (212L) | Utilidades DSP homónimas de contenido distinto. |
| `Envelope.h` | **ABDEep** (97L) vs **ABDNeural** (95L) | Dos envolventes de producto. |
| `Oscillator.h` | **ABDEep** (55L) vs **ABDNeural** (90L) | Dos osciladores de producto. |
| `FactoryPresets.h` | **ABDCZ101** (38L) vs **ABDJUNiO601** (287L) | Bancos de fábrica, uno por producto. |
| `HardwareConstants.h` | **ABDCZ101** (47L) vs **ABDMS2000** (37L) | Constantes del hardware real de cada synth: **convergir no tiene sentido**. Solo renombrar. |
| `JunoHPF.h` | **ABDJUNiO601** (272L) vs **ABDEep** (221L) | Dos implementaciones del HPF del Juno. |
| `LFO.h` | **ABDNeural** (93L, `NEURONiK::DSP::Core`) vs **SynthCore** (`abd::synth`) vs **ABDCZ101** (55L) | **Tres LFOs distintos** con APIs que no son superconjunto la una de la otra. |
| `PresetBrowser.h` | **ABDJUNiO601** (120L) vs **ABDNeural** (92L) | Dos navegadores de presets de UI. |
| `PresetManager.h` | **ABDCZ101** (157L + mock de 68L) vs **ABDJUNiO601** (141L) vs **ABDNeural** (58L) | Tres gestores de presets. El de ABDNeural es solo serialización. |
| `SynthEngine.h` | **ABDEep** vs **ABDMS2000** (176L) | El motor de cada synth: convergencia vía `SynthCore`/`DspCore`, no renombrado. |
| `SysExManager.h` | **ABDCZ101** (123L) vs **ABDMS2000** (191L) | Dos gestores SysEx; los **codecs** sí están compartidos (`HardwareDrivers`). |
| `Voice.h` | **ABDCZ101** (341L) vs **ABDMS2000** (208L) | Dos implementaciones de voz. |
| `VoiceManager.h` | **ABDCZ101** (192L) vs **ABDMS2000** (110L) | Dos asignadores de voz, y existe `SynthCore::VoiceAllocator` (403L) que generaliza la política de robo de MS2000. |
| `BridgeActions.h` | **ABDEep** (91L), **ABDJUNiO601** (126L), **ABDMS2000** (86L) | Tres superficies de acciones JS→C++ por producto (frontera, como `WasmBridge.h`). |

---

## 3. Matriz de adopción real (solo `target_link_libraries`, sin comentarios)

Verificado con `grep -v '^\s*#' CMakeLists.txt` — las primeras pasadas dan falsos
positivos porque varios `CMakeLists.txt` **mencionan** `ABDShared::X` en
comentarios explicativos.

| Proyecto | Módulos compartidos que enlaza | Estado del refactor |
|---|---|---|
| **ABDNeural** | `DspCore`, `DspEffects` | Fase 1 de-JUCE completa + efectos migrados a `DspEffects`. |
| **ABDMS2000** | `SynthCore`, `HardwareDrivers`, `AutoUpdater` | Fase 2 DRY hecha sobre `SynthCore` (con shims). |
| **ABDEep** | `SynthCore` | Consume `SynthCore` target a target (tiene `.cpp`). |
| **ABDJUNiO601** | `HardwareDrivers`, `LutDSP` | Adopción parcial. |
| **ABDAudioLab** | `AutoUpdater`, `HardwareMidiDetect`, `StudioTopologyAssets` | Adopción de infraestructura, no de DSP. |
| **ABDCZ101** | *(ninguno)* | **Sin adoptar**: mantiene sus propios ADSR, Arpeggiator, LFO, Reverb, Chorus, PresetManager… |
| **ABDScope**, **ABDPro008**, **ABDOmegaUnified**, **ABDBankManager** | *(ninguno)* | Sin código DSP compartido que adoptar por ahora. |

**La lectura clave:** CZ101 es el proyecto con más homonimias reales (aparece en 12
de las 18 filas) y el único synth de la familia que **no enlaza nada** de
`ABDSharedCode`. Es
también donde la convergencia es más barata, porque en varios casos el destino ya
existe y está probado.

---

## 4. Prioridades sugeridas (por valor / riesgo)

| # | Acción | Por qué ahora | Coste |
|---|---|---|---|
| **P1** | `ABDCZ101/Source/DSP/Effects/{Reverb,Chorus}.h` → consumir `ABDShared::DspEffects` | El motor compartido **ya existe, está portado y verificado bit a bit** (tests de paridad en ABDNeural). Es el caso de DRY con más retorno y menos diseño pendiente. | Bajo |
| **P2** | `ABDCZ101/Source/DSP/Envelopes/ADSREnvelope.h` → `SynthCore` | El canónico ya lo consumen MS2000 y ABDEep; es un shim de 16 líneas lo que falta, no un port. | Bajo |
| **P3** | `LFO.h`: decidir convergencia vs renombrado | **Es el único nombre con 3 implementaciones reales** y bloquea 4 proyectos. No se resuelve renombrando sin más: hay que decidir si el LFO común es parametrizable (Free/Sync + ondas MS2000 + `syncNoteIdx`) o si cada familia tiene el suyo con nombre distinto. | Medio (decisión) |
| **P4** | `Voice.h` / `VoiceManager.h`: converger sobre `SynthCore::VoiceAllocator` | La política de robo de MS2000 ya está generalizada ahí (con `StealHint` por slot). CZ101 es el otro asignador. | Medio |
| **P5** | Fronteras de plugin (`WasmBridge`, `BridgeActions`, `PluginEditor_ResourceProvider`) | Convergen vía `WebView2Bridge` + los contratos de bridge ya compartidos. Es el bloque más grande y el de mayor riesgo; va al final. | Alto |

### Nota de P5 (2026-09-19): NEURONiK empieza a poner su mitad

ABDNeural arranca 8.1 (el editor del plugin hospeda la página web) y lo hace **sin crear
nombres nuevos**, que es exactamente lo que P5 pide:

- `ABDNeural/Source/WebUI/BridgeAdapters.h` — **no** es homónimo: es el único
  `BridgeAdapters.h` del workspace (comprobado). Contiene los tres adaptadores que enchufan el
  procesador real al `ParameterBridge` (`PresetManagerAdapter`, `MidiInjectionAdapter`,
  `EngineModelsAdapter`) en `namespace NEURONiK::WebUI`, compartidos por el editor y por la
  bancada del piloto en vez de una copia por superficie. Es la mitad "backend" del bridge; el
  contrato ya estaba compartido (`BridgeProtocolContractTest`).
- Su proveedor de recursos (`Source/WebPilotHost.cpp`, ~150 líneas: normalizar URL, MIME,
  catálogo embebido y fallback) es **la misma función** que
  `abd::webview2::webView2ResourceProvider`. Decisión tomada: el *plugin* adopta el compartido;
  la bancada de desarrollo conserva el suyo **solo** por el *disco primero* con hot-reload, que
  el compartido no hace. Así no nace un tercer `PluginEditor_ResourceProvider`.

Y una regla para el resto del refactor: **si el contenido podría acabar en un módulo
compartido, nombrarlo ya con la convención de destino** (`Dsp*`, `abd::dsp`), y
reservar el renombrado para cuando se sepa que el fichero es de producto.

---

## 5. Cómo repetir el inventario

```bash
# 1. Basenames por proyecto (zona de fuentes, no build)
find <proyecto>/Source <proyecto>/wasm -name '*.h' -printf '%f\t<proyecto>\n'

# 2. Nombres en más de un proyecto
awk -F'\t' '{p[$1]=p[$1]" "$2} END{for (f in p){n=split(p[f],a," "); if(n>1) print f"\t"p[f]}}'

# 3. Quitar el vendor (JUCE/zlib) para quedarse con la autoría propia
grep -vE 'juce_|^zlib|^zconf|^deflate|^inflate|^inffast|^inffixed|^inftrees|^gzguts|^trees\.h|^zutil|^crc32|^wasm_compat|^if_dl'
```

Nota: el vendor aparece porque ABDCZ101, ABDEep y ABDJUNiO601 llevan copias de JUCE
dentro del árbol. Si algún día convergen a una JUCE única compartida, esa parte del
inventario desaparece sola.
