# Source/ refs medidos y candidatos DSP genéricos — ABDJUNiO601 / ABDEep / ABDCZ101

> Dos cosas pedidas:
> 1. Recuento medido de archivos bajo `Source/` que incluyen referencias a
>    - `../../../../ABDSharedCode/...`
>    - `ABDShared::`
>    - `@abdsynths/*`
> 2. Candidatos concretos en `Source/DSP/Effects` y `Source/DSP/Filters` de esos tres repos que parecen genéricos o compartibles.
>
> Metodología:
> - Se recorrió cada repo con `grep -RIn` sobre `Source/, CMakeLists.txt, DspSources.cmake, scripts, WebUI/` y logs/docs relevantes.
> - Se excluyeron `node_modules, build, build-test, build-verify, wasm/build_wasm, webui/dist, wasm/build` donde corría riesgo de secar o liar.
> - La señal de “dependencia medida” es distinta según repo: unos referencian desde `Source/`, otros desde CMake/includes, otros desde JS.

---

## 1. Recuento medido por repo (hasta donde leyó este barrido)

### 1.1 ABDJUNiO601

**Hits en `Source/`:**
- `0` archivos con los patrones buscados en `Source/`.

**Dónde sí aparece la dependencia en este repo (fuera de `Source/`):**
- `ABDJUNiO601/CMakeLists.txt` enlaza:
  - `ABDShared::LutDSP`
  - `ABDShared::DspCore`
  - `ABDShared::HardwareDrivers`
- tanto en plugin como en tests (`JunoUnitTests`).

**Interpretación:**
- La dependencia de AJUNiO601 no parece pasar por includes puros dentro de `Source/` con path `../../../../ABDSharedCode/...`; parece expresarse casi por completo a nivel de CMake (target linkage).
- Esto no significa que no haya código compartible dentro de `Source/`; solo que, en este barrido, el grado de acoplamiento “nominal” con el compartido es menor que en ABDEep.

---

### 1.2 ABDEep

**Hits en `Source/`:**
- `2` archivos con esos patrones en `Source/`, y ambos son comentarios/doc del lado del WebUI/resource provider:
  - `Source/Plugin/PluginEditor_DevServer.cpp`
  - `Source/Plugin/PluginEditor_ResourceProvider.cpp`

**Dónde sí aparece la dependencia en este repo (muy por encima):**
- `ABDEep/CMakeLists.txt` enlaza en varios targets:
  - `ABDShared::SynthCore`
  - `ABDShared::DspCore`
  - `ABDShared::DspEffects` — en la sesión actual **sí aparece `ABDSharedCode/DspEffects/` como carpeta presente** en el árbol de `ABDSharedCode`, con archivos como `FxEngine.h`, `JunoBBD.h`, `DspReverb.h`, `DspChorus.h`, `DspDelay.h`, `DspPhaser.h`, `DspSaturation.h`, `ShelfFilter.h`, `FxSlot.h`, `FxRegistry.h`, `FxDefaultCatalogue.h`, `EffectPolicy.h`, etc.; sin embargo, este barrido no verificó un `DspEffects/CMakeLists.txt` a ese nivel ni confirmó la publicación del target, por lo que lo trato aquí como “carpeta visible + dependencia documentada”, no como evidencia de que el módulo de efectos esté entregado y consumible tal cual.
- `package.json` declara:
  - `@abdsynths/midi-keyb: workspace:*`
  - `@abdsynths/shared: workspace:*`
- Hay evidencia abundante de consumo JS de `@abdsynths/*` en:
  - `WebUI/js/keyboard.js`
  - `WebUI/js/fit-stage.js`
  - `WebUI/tests/*.test.js`
  - `scripts/build_webui.js`, `scripts/sync_shared.mjs`, `scripts/verify_embedded_bundle.js`

**Interpretación:**
- ABDEep tiene la dependencia más visible y multi-capas del grupo: C++ (targets) + JS (workspace + comp spores).
- En `Source/DSP/*` puro, los hits nominales al compartido fueron mínimos en este barrido; eso puede significar que la integración se hace a nivel de alias/wrapper y CMake, no por includes directos con path profundo.

---

### 1.3 ABDCZ101

**Hits en `Source/`:**
- `1` archivo con include directo al compartido:
  - `Source/DSP/Oscillators/PhaseDistOsc.h` → `#include "../../../../ABDSharedCode/LutDSP/LutEvaluatorSimd.h"`

**Dónde sí aparece la dependencia en este repo (fuera de `Source/`):**
- `package.json` declara:
  - `@abdsynths/midi-keyb: workspace:*`
  - `@abdsynths/shared: workspace:*`
- Evidencia abundante de consumo JS en:
  - `WebUI/src/app.js`
  - `WebUI/src/ui/blockDrawer.js`
  - `WebUI/src/styles/keyboard.css`
  - `WebUI/tests/*.test.js`
  - `WebUI/vite.build.config.js`, `vite.config.js`
- `scripts/sync_shared.js` hace copia gestionada desde `ABDSharedAssets/components/fitStage.js`.
- evidencia de CI/git de que el include `../../../../ABDSharedCode/LutDSP/LutEvaluatorSimd.h` es un eslabón real (y que el repo no compila sin `ABDSharedCode` a su lado).

**Interpretación:**
- CZ101 parece mix: consumo JS claro del workspace, y al menos un include directo al compartido **desde `Source/DSP/Oscillators/`**, no desde `Source/DSP/Effects/`.
- Esto lo pone como el repo más “mezclado” en este barrido: no todo por CMake ni todo por JS, sino una mezcla donde sí aparece un `#include` directo al compartido dentro de `Source/`, pero en un oscilador/PD, no en la carpeta de efectos.

---

## 2. Candidatos concretos en `Source/DSP/Effects` y `Source/DSP/Filters`

Aquí voy por evidencia de archivos reales, no por nombres genéricos.

### 2.1 ABDJUNiO601

**`Source/DSP/Effects`:**
- `ChorusBBDFrozen.h` existe y, según su propio encabezado, es **referencia congelada** para testear paridad contra el motor compartido `ABDSharedCode/DspEffects/JunoBBD.h`.
- Esto no es candidato de extraer tal cual: es testigo de regresión. Pero sí confirma que el motor BBD/Juno del compartido es la versión productiva y que JUNiO601 yaClaramente tiene eso en su historia.

**`Source/DSP/Filters`:**
- No existe este directorio en este repo (según `find` de este barrido).

**Candidato genérico posible:**
- Si JUNiO601 mantiene algo de chorus BBD/filtrado de cinta por fuera del frozen, la señal sería mirar `Source/Synth/ChorusBBD.*` / `JunoTapeEcho.*` / `JunoVCF.*` con lupa de “qué parte es Juno-only y qué parte es reusable”; pero no entraste en eso hoy, así que no lo afirmo.

---

### 2.2 ABDEep

**`Source/DSP/Effects`:**
- No existe este directorio en este barrido, pero ABDEep sí tiene `Source/DSP/FX/*` muy poblado según sus CMake y docs.
- Eso me dice: “no está bajo `Effects/`, está bajo `FX/`”. Por eso el recuento por ruta estricta puede under-reportar.
- Candidatos plausibles allí (sin abrir todos): chorus, delay, reverb, drive/distortion, flanger, phaser, tape delay, bucket-brigade, reverbs espaciales, etc. Pero, sin abrirlos, no puedo decir cuáles son genéricos hoy.

**`Source/DSP/Filters`:**
- No existe este directorio en este barrido.
- De nuevo, lo más probable es que los filtros vivan bajo otro arbol (p.ej. `Source/DSP/Filter.cpp`, `Source/DSP/JunoVCF_ZDF.*`, `Source/DSP/KorgMS20VCF.*`, `Source/DSP/MoogLadderVCF.*`, etc.), y que ya haya señales de “filtros modelados” como tema centripetal.

**Candidato genérico posible:**
- Si los filtros de ABDEep son versiones de filtro modelado adaptadas a un synth específico, ellos pueden ser candidatos a un módulo de filtros si se generalizan; pero sin abrirlos, no puedo separar “genérico” de “ligado al synth”.

---

### 2.3 ABDCZ101

**`Source/DSP/Effects`:** existe y tiene 12 archivos.
Archivos vistos en este barrido:
- `Chorus.h`
- `Chorus.cpp`
- `DriveEffect.h`
- `DriveEffect.cpp`
- `EffectsChain.h`
- `EffectsChain.cpp`
- `Reverb.h`
- `Reverb.cpp`
- `StereoChorus.h`
- `StereoChorus.cpp`
- `StereoDelay.h`
- `BBDChorus.h`

**`Source/DSP/Filters`:**
- Existe y tiene 2 archivos:
  - `ResonantFilter.h`
  - `ResonantFilter.cpp`

**Señales de “parece genérico o compartible”:**
- `EffectsChain` parece una cadena de efectos con smoothers y efectos tipo estándar:
  - Drive, Chorus, StereoDelay, Reverb.
- `DriveEffect`:
  - usa `juce::dsp::IIR::Filter`, `juce::dsp::ProcessorDuplicator`, buffer interno, y curva de saturación interna.
- `StereoDelay`:
  - implementa delay stereo, feedback, mix, ping-pong, interpolación lineal propia, buffer propio; parece delay genérico con lógica de feedback/saturación suave.
- `Reverb`:
  - encola `juce::Reverb` con `juce::Reverb::Parameters`; parece wrapper de reverb estándar más que algo Juno/CZ-only.
- `Chorus` y `StereoChorus`:
  - parecen chorus genéricos estilo LFO + delay lines, con interpolación y buffers propios.
- `ResonantFilter`:
  - parece filtro resonante 2-pole genérico (LP/HP/BP) con coeficientes propios; menos obvio que sea “genérico reusable” si está pegado a juce, pero la topología sí parece genérica.

**Candidatos concretos más obvios en CZ101:**
- `Reverb` parece candidato fuerte de sustitución por un reverb genérico **si existe** o se decide crear en el compartido.
- `StereoDelay` parece candidato fuerte de delay genérico (feedback, interpolación, ping-pong, mix).
- `Chorus` / `StereoChorus` parecen candidatos plausibles de chorus genérico si no dependen de detalles CZ-only.
- `EffectsChain` parece candidato de “cadena de efectos genérica” si se puede despojar de dependencias de JUCE/process context y de snapshot propio; pero eso puede requerir reescritura.
- `DriveEffect` es menos claro: usa `juce::dsp` y saturación interna; puede ser candidato de efecto de drive genérico, pero con dependencias de JUCE.

**Riesgo de afirmar candidato:**
- varios de estos archivos parecen depender de `juce::dsp`/`juce::AudioBuffer`, etc.; eso no los hace automáticamente “genéricos”, y si se mueven al compartido habría que confirmar que no rompen las reglas del repo para ese módulo.
- el include `../../../../ABDSharedCode/LutDSP/LutEvaluatorSimd.h` en `PhaseDistOsc.h` confirma que CZ101 ya apunta al compartido desde `Source/DSP/`, pero **no indica que el módulo de efectos al que teoricamente podría ir exista ya en el compartido**; hoy esa referencia apunta a `LutDSP`, no a un módulo de efectos genéricos.

---

## 3. Cruce rápido: qué parece más genérico por repo

### 3.1 ABDJUNiO601
- Probablemente Juno-only en gran parte del synth (DCO, JunoVoice, tape, SysEx, importers).
- Chorus BBD parece ya representado por el motor compartido + frozen de prueba.
- Menos obvio “cluster genérico” en `Source/DSP/Effects/` porque tiene muy poco allí en este barrido.

### 3.2 ABDEep
- Parece tener la superficie DSP más amplia (muchos FX), pero organizado bajo `Source/DSP/FX/`.
- Parece ya consumir mucho del compartido en C++ y JS, así que “candidato genérico” puede estar también en algo que ya haya promovido o que tenga shim propio.
- Sin entrar en `Source/DSP/FX/*`, no puedo afirmar cuáles son genéricos.

### 3.3 ABDCZ101
- Parece el que tiene efectos “tipo estándar” más visible bajo `Source/DSP/Effects/` en este barrido: chorus, delay, reverb, drive, effects chain.
- Parece el repo donde más fácilmente hay algo “genérico pero expresado con juce”.
- `Reverb` parece el más claro candidato de reemplazo por algo genérico; `StereoDelay` parece el siguiente; `Chorus/StereoChorus` parecen también candidatos si no llevan semántica CZ-only.

---

## 4. Qué no puedo afirmar con este barrido

- No puedo decir “este archivo concreto de ABDEep/Source/DSP/FX/*.cpp es genérico” sin abrirlo; solo tengo los nombres y la estructura de carpetas.
- No pude contar hits en submódulos de build (porque los excluí para no liar), y es posible que haya más acoplamiento ahí.
- No pude abrir todos los `.cpp/.h` de `Source/DSP/FX` de ABDEep ni todos de CZ101; solo leí lo más representativo para no saturar.
- No estoy diciendo que se muevan sin más; solo que “parecen candidatos de evaluar si son genéricos”.

---

## 5. Qué significan estos dos datos juntos

Si juntas el recuento medido y los candidatos DSP:

- ABDEep parece el repo con dependencia compartida más profunda y broad (C++ + JS), pero con una superficie FX grande que habría que revisar archivo a archivo.
- ABDCZ101 parece el repo con efectos “tipo estándar” más visibles en `Source/DSP/Effects` (reverb, delay, chorus, drive, effects chain), más un include directo al compartido desde un oscilador/PD en `Source/DSP/Oscillators/`, no desde `Effects/`.
- AJUNiO601 parece el repo donde la dependencia nominal al compartido es más visible en CMake que en `Source/`; el archivo `ChorusBBDFrozen.h` confirma la historia BBD pero no sugiere movimiento adicional inmediato.

**Advertencia transversal:** la discusión de “sustitución por `DspEffects`” o “mover a `DspEffects`” presupone que ese módulo existe con esa superficie en `ABDSharedCode`; en el árbol revisado en esta sesión **sí apareció `ABDSharedCode/DspEffects/` como carpeta presente**, con arquitectura visible de contenedor/catálogo/política (`FxEngine.h`, `FxSlot.h`, `FxRegistry.h`, `FxDefaultCatalogue.h`, `EffectPolicy.h`), varios efectos concretos (`JunoBBD.h`, `DspReverb.h`, `DspChorus.h`, `DspDelay.h`, `DspPhaser.h`, `DspSchroederReverb.h`, `DspMoodFilter.h`, `DspVocoder.h`, `DspSolinaEnsemble.h`, `ShelfFilter.h`, `RingMod.h`, `MultiHeadEcho.h`, `DspSaturation.h`, etc.), subcarpetas `adapters/`/`characters/`/`profiles/` y `DspEffectsTests.cpp`. Eso es más que un núcleo mínimo, pero no equivale a un catálogo completo idéntico al de `ABDEep/Source/DSP/FX/`, y este barrido no verificó un `DspEffects/CMakeLists.txt` a ese nivel ni la publicación del target, por lo que cualquier conclusión de esa índole debe leerse como “carpeta presente con efectos/catálogo/tests visibles y citada en CMake/docs, pero target no verificado”, no como “módulo de efectos entregado y listo para migración automática”.

---

## 6. Si quieres profundizar sin abrir todo, lo más rentable sería

1. en **ABDCZ101**, mirar `StereoDelay`, `Reverb`, `DriveEffect` y `EffectsChain` y separar:
   - qué es softmax/genérico,
   - qué depende de JUCE de forma exclusiva,
   - qué llevaría a un módulo compartido sin romper reglas.
2. en **ABDEep**, entrar a `Source/DSP/FX` y separar:
   - efectos que parecen modelo propio del DeepMind,
   - efectos que parecen “motor algorítmico genérico”,
   - efectos que ya tengan paridad/comparación con el compartido.
3. en **ABDJUNiO601**, entrar a `Source/Synth/ChorusBBD.*`, `JunoTapeEcho.*`, `JunoVCF.*`, `JunoVoice.*` y separar:
   - Juno-only,
   - reusable fonológico/filtrado,
   - lo que ya tiene gemelo en el compartido.

Si quieres, puedo empezar con el (1) de ABDCZ101 o con el (3) de AJUNiO601; son los dos más delimitados.
