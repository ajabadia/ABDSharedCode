# Catálogo de módulos compartidos: ABDSharedCode

Objetivo: tener un mapa de lo que ya hay, qué interfaz usa cada cosa, y qué sigue duplicado o fuera de shared, para poder añadir piezas nuevas (filtros, osciladores, sumadores, etc.) sin duplicar y con una forma compatible entre familias.

Este fichero es REFERENCIA DE INVENTARIO, no especulación. Donde diga "cliente observable" o "shim", se refiere a lo que existe en esta rama, no a lo que se podría asumir. Donde no haya evidencia de consumo real, el catálogo lo deja como "no confirmado" en lugar de inventarlo.

---

## 1. Módulos actuales de ABDSharedCode

### 1.1 DspCore — base de síntesis libre de JUCE

Ruta: `ABDSharedCode/DspCore`
Namespace: `abd::dsp`

**Archivos y qué aporta:**

- `DspCore.h` — barrera de inclusión canónica, `AudioBuffer<float>` propio libre de JUCE (port literal de `juce::AudioBuffer`, con getters/setters, `getWritePointer`, etc.), `FreeFrame` (interfaz de cuadro libre) y compatibilidad de namespace.
- `DspMath.h` — trascendentes deterministas sin libm: sin, cos, atan, tanh, log2, exp2, pow con polinomios fijos; el objetivo es eliminar la dependencia de libm de la plataforma para mantener paridad nativo <-> WASM.
- `DspDebug.h` — utilidades de depuración.
- `DspLeakedObjectDetector.h` — detector de fugas portable.
- `DspMidiBuffer.h`, `DspMidiMessage.h` — mensajes MIDI puros sin JUCE.
- `DspResonantFilter.h` — filtro resonante TPT de etapa, con AGC de auto-oscilación.

**Contrato clave (leyendo el header):**

- `ResonantFilterStage`:
  - `void prepare(double sampleRate) noexcept`
  - `void reset() noexcept`
  - `void setCutoff(double hz) noexcept`
  - `void setResonance(double amount) noexcept`
  - `void setCutoffAndResonance(double hz, double amount) noexcept`
  - `double processSample(double input) noexcept`
  - `double getBandPower() const noexcept`
  - `double getDamping() const noexcept`
  - `double getBaseDamping() const noexcept`
  - `double getResonance() const noexcept`
  - `double getCutoff() const noexcept`

**Lo que no incluye aquí:** un biquad clásico genérico por defecto. Ahí están etapas TPT, no un filtro multimodo genérico.

---

### 1.2 SynthCore — bloques de síntesis básicos

Ruta: `ABDSharedCode/SynthCore`
Namespace: `abd::synth`

**Archivos y qué aporta:**

- `ADSREnvelope.h/.cpp` — generador ADSR exponencial, por muestra.
- `EnvelopeCurves.h` — curvas de envolvente.
- `LFO.h/.cpp` — LFO 1 y 2 con sincronización de teclado y tempo.
- `PortamentoGlide.h/.cpp` — generador de deslizamiento de tono.
- `PolyBLEP.h` — antifilterado de fase para osciladores.
- `OscillatorFamily.h` — EL CONTRATO de la familia de osciladores (idioma + rasgo `IsOscillator` + `processBlock`).
- `OscVcoCa72.h/.cpp` + `OscVcoCa72Profile.h` — el miembro de CIRCUITO (rampa integrada con reinicio) y su perfil medido, como datos.
- `OscPolyBlep.h` — el miembro de FASE (acumulador de fase sobre el PolyBLEP de arriba).
- `OscHalfbandDecimator.h` — la decimación de media banda que usa el VCO al sobremuestrear (herramienta, no miembro).
- `VoiceAllocator.h` — asignación polifónica sin dependencia de UI.
- `ModMatrix.h` — matriz de modulación genérica sin políticas de producto.
- `AudioThreadSnapshot.h` — utilidad de muestreo del hilo de audio.
- `DSPUtils.h` — utilidades DSP.

**Contrato clave (leyendo los headers):**

#### ADSREnvelope

- `void prepare(double sampleRate) noexcept`
- `void reset() noexcept`
- `void setAttack(float attack0to1) noexcept`
- `void setDecay(float decay0to1) noexcept`
- `void setSustain(float sustain0to1) noexcept`
- `void setRelease(float release0to1) noexcept`
- `void noteOn(float velocity = 1.0f) noexcept`
- `void noteOff() noexcept`
- `float getNextSample() noexcept`
- `float getCurrentLevel() const noexcept`
- `bool isIdle() const noexcept`
- `EnvelopeStage getStage() const noexcept`

#### LFO

- `void prepare(double sampleRate) noexcept`
- `void reset(float initialPhase = 0.0f) noexcept`
- `void setFrequencyHz(float freqHz) noexcept`
- `void setTempoSync(bool enabled, int syncNoteIdx = 4) noexcept`
- `void setBpm(double bpm) noexcept`
- `void setWaveformLFO1(LFOWaveform wave) noexcept`
- `void setWaveformLFO2(LFOWaveformLFO2 wave) noexcept`
- `void setKeySyncMode(int mode) noexcept`
- `void triggerKeySync(bool isFirstTimbreNote) noexcept`
- `void resetPhase() noexcept`
- `float getNextSample(float frequencyModulationOctaves = 0.0f) noexcept`
- `float getCurrentValue() const noexcept`
- `static float syncNoteToMultiplier(int idx) noexcept`

#### PortamentoGlide

- `void prepare(double sampleRate) noexcept`
- `void reset(float initialMidiNote = 60.0f) noexcept`
- `void setTargetNote(float targetMidiNote, bool glideEnabled) noexcept`
- `void setGlideTime(float timeParam0to1) noexcept`
- `float getNextPitchSemitones() noexcept`
- `float getCurrentPitch() const noexcept`

#### PolyBLEP

- `static float getResidual(float t, float dt) noexcept`
- `static float getResidualIntegrated(float t, float dt) noexcept`

#### VoiceAllocator

Plantilla `VoiceAllocator<MaxVoices = 8>`:

- `void reset() noexcept`
- `void setPolyMode(PolyMode newMode) noexcept`
- `PolyMode getPolyMode() const noexcept`
- `void setStealPolicy(StealPolicy newPolicy) noexcept`
- `StealPolicy getStealPolicy() const noexcept`
- `std::vector<int> allocateNoteOn(int midiNote, float velocity)`
- `std::vector<int> allocateNoteOff(int midiNote)`
- `void allNotesOff() noexcept`
- `size_t getNumActiveVoices() const noexcept`
- `const VoiceState& getVoiceState(size_t index) const noexcept`
- `int findVoiceToSteal() const noexcept`
- `int findVoiceToSteal(const StealHint* hints, size_t hintCount, int incomingNote) const noexcept`
- `void commitAllocation(int slotIndex, int midiNote) noexcept`
- `void markSlotReleased(int slotIndex) noexcept`

Nota: `StealPolicy::Ms2000Ladder` ya existe como vocabulario en shared (política de robo de voz), lo que confirma que parte del vocabulario de tus synths ya vive aquí.

#### ModMatrix

Plantilla `ModMatrixT<kNumSlots, kZeroIdInert = true>`:

- `using ModSourceId = std::uint16_t`
- `using ModDestinationId = std::uint16_t`
- `struct ModRoute { source, destination, amount }`
- `struct ModDestinationDescriptor { id, label, parameterId, min, max, scale, perNote, replaces, engineMask }`
- `void clear() noexcept`
- `void setRoute(slotIndex, source, destination, amount) noexcept`
- `bool hasAnyRoute() const noexcept`
- `const ModRoute& getRoute(slotIndex) const noexcept`
- `void accumulate(sourceValues, sourceCount, destAccum, destCount) const noexcept`
- `float get(destination, sourceValues, sourceCount) const noexcept`

---

### 1.3 DspEffects — motor de efectos compartidos

Ruta: `ABDSharedCode/DspEffects`
Namespace: `abd::dsp`
Modelo: motor sin política + catálogo inyectado + etapas inyectadas.

**Archivos y qué aporta:**

- `FxRegistry.h` — contrato de efecto y catálogo.
- `FxSlot.h` — ranura de efecto con mezcla wet/dry propia.
- `FxEngine.h` — motor de 4 ranuras y 9 topologías.
- `EffectPolicy.h` — política inyectada en efectos (etapas por plantilla, sin vtables en el lazo).
- `BasicAdapters.h` — adaptador de parámetros (normalizado <-> físico).
- Perfiles por dispositivo:
  - `profiles/JunoBbdProfile.h`
  - `profiles/JunoBbdJ106Profile.h`, `profiles/JunoBbdJ60Profile.h`
  - `profiles/MS2000EqProfile.h`
  - `profiles/Re201Profile.h`
  - `profiles/ReverbProfile.h`
- Motores por efecto:
  - `DspChorus.h`, `JunoBBD.h`
  - `DspDelay.h`, `MultiHeadEcho.h`
  - `DspReverb.h`, `DspSchroederReverb.h`
  - `DspSaturation.h`
  - `Phaser4.h`
  - `RingMod.h`
  - `ShelfFilter.h`
  - `CascadeShelfEq.h`
- Etapas de carácter:
  - `characters/JunoBbd.h`
  - `characters/DiodeBridge.h`
  - `characters/TapeColour.h`
  - `characters/BbdNoise.h`

**Contrato clave (leyendo los headers):**

#### FxRegistry

- `struct FxParamSpec { name, minValue, maxValue, defaultValue, skew, steps }`
- `struct FxEffectInfo { name, displayName, numParams, params, create, process, setParam, setAllParams, reset, destroy }`
- `FxCreateFn`, `FxProcessFn`, `FxSetParamFn`, `FxSetAllParamsFn`, `FxDestroyFn`, `FxResetFn`
- `float fxNormalise(const FxParamSpec&, float physical) noexcept`
- `float fxDenormalise(const FxParamSpec&, float normalised) noexcept`
- `const FxEffectInfo* fxEffectAt(const FxEffectInfo*, int count, int index) noexcept`
- `const FxEffectInfo* fxFindEffect(const FxEffectInfo*, int count, const char* name) noexcept`
- `kFxMaxParams = 12`
- `kFxNumSlots = 4`

#### FxSlot

- `void prepare(sampleRate, numChannels, maxBlockSize, catalogue, size) noexcept`
- `void setType(int) noexcept`
- `int getType() const noexcept`
- `bool isActive() const noexcept`
- `void setParameter(index, normalised) noexcept`
- `float getParameter(index) const noexcept`
- `bool isParameterTouched(index) const noexcept`
- `void setGain(gain) noexcept`
- `float getGain() const noexcept`
- `void setMix(mix) noexcept`
- `float getMix() const noexcept`
- `void reset() noexcept`
- `void process(AudioBuffer<float>&, numSamples) noexcept`
- `const FxEffectInfo* getEffectInfo() const noexcept`

#### FxEngine

- `void prepare(sampleRate, numChannels, maxBlockSize) noexcept`
- `void setCatalogue(catalogue, size) noexcept`
- `FxSlot& getSlot(int) noexcept`
- `const FxSlot& getSlot(int) const noexcept`
- `void setRouting(FxRouting) noexcept`
- `FxRouting getRouting() const noexcept`
- `void setMode(FxMode) noexcept`
- `FxMode getMode() const noexcept`
- `void setSendLevel(float) noexcept`
- `float getSendLevel() const noexcept`
- `void setFeedbackGain(float) noexcept`
- `float getFeedbackGain() const noexcept`
- `void reset() noexcept`
- `void process(AudioBuffer<float>&, numSamples) noexcept`
- `void processStereoPath(AudioBuffer<float>&, available) noexcept`
- `void processMonoPath(AudioBuffer<float>&, available) noexcept`

Rutas: Series, ParallelFront, ParallelPairs, FullParallel, DualSeriesParallel, SeriesSplitMiddle, ParallelPairsSeries, ParallelFrontSeries, SeriesChainPlusOne, SeriesWithFeedback.
Modos: Insert, Send, Bypass.

#### EffectPolicy

- `CharacterStage { prepareSampleRate, getSampleRate, reset }`
- `NullStage : CharacterStage { processSample(x,=,=) noexcept }`
- Contrato de etapa POR CONVENCIÓN: `prepareSampleRate` + `getSampleRate` + `reset` + `processSample(float, ...) -> float`.
- Inyección por plantilla, SIN vtable en el lazo de audio.

---

### 1.4 LutDSP — motor de evaluación LUT / respuesta medida

Ruta: `ABDSharedCode/LutDSP`
Namespace: `abd::lutdsp`
Dependencia externa: tablas `AbdBatchedPoint` alineadas producidas por ABDAudioLab.

**Archivos y qué aporta:**

- `LutEvaluatorSimd.h` — evaluador SIMD 1D/2D con interpolación bilineal.
- `AnalogLutFilterModule.h` — módulo polifónico de evaluación LUT (8 voces SIMD) con suavizado exponencial balístico.
- `VoiceAllocator.h` — re-exportación compat de SynthCore/VoiceAllocator.
- `VoiceDispersionModel.h` — modelo de dispersión de voz.
- Modelos LUT (`models/`):
  - `lut_mock_va_synth_moog_ladder.h` — **filtro Moog 24 dB/oct (4-pole ladder), tabla LUT 32 puntos** (junto a `.json` de metadatos).
  - `lut_roland_juno106_ir3109_vcf.h` — filtro IR3109 Juno-106 (tabla LUT).
  - `lut_casio_cz101_phase_distortion_resonant.h` — resonante de distorsión de fase CZ-101 (tabla LUT).
  - `lut_roland_aira_bitrazer_filter.h` — filtro Aira Bitrazer (tabla LUT).
  - `lut_manual_eurorack_vcf_diode_ladder.h` — ladder de diodo EuroRack manual (tabla LUT).
  - `lut_behringer_pro800_cem3320_vcf.h` — CEM3320 Pro800 (tabla LUT).
  - `OfficialLutModels.h` — registro canónico de modelos oficiales.

**Contrato clave (leyendo los headers):**

#### LutEvaluatorSimd

- `struct alignas(16) AbdBatchedPoint { p1, p2, mu, sigma, sec_mu, sec_sigma, thd_percent, reserved }`
- `enum class LutMetric { PrimaryMean, PrimaryStdDev, SecondaryMean, SecondaryStdDev, ThdPercent }`
- `class LutEvaluator1DSimd`:
  - `void loadLutData(const std::vector<float>&)`
  - `int getLutSize() const noexcept`
  - `float getScaleFactor() const noexcept`
  - `bool isEmpty() const noexcept`
  - `__m128 evaluateSingleValueSimd(__m128) const noexcept`
  - `float evaluateSingle(float) noexcept`
  - `void processBlockSimd(float*, numSamples) const noexcept`
  - `void processStereoBlockSimd(float* left, float* right, numSamples) const noexcept`
- `LutEvaluatorSimd::evaluateBilinear4Voices(p1_4x, p2_4x, lut, gridSize, metric)` y `evaluateBilinear8Voices`.

#### AnalogLutFilterModule

- `void setSmoothingTime(timeMs, sampleRate) noexcept`
- `void setVoiceParameters(voiceIndex, p1, p2) noexcept`
- `void snapVoiceParameters(voiceIndex, p1, p2) noexcept`
- `float getVoiceParam1Current(voiceIndex) const noexcept`
- `float getVoiceParam2Current(voiceIndex) const noexcept`
- `void processPolyphonicBlock(voiceOutputs[], numSamples, lut, gridSize, metric = LutMetric::PrimaryMean) noexcept`

**Nota importante:** esto no es una interfaz unificada de "filtro". Es un motor de evaluación LUT. Para poder "plugar" un filtro LUT en un synth hace falta una interfaz por encima que exija cutoff/resonance/params unificados. Hoy no la tiene LutDSP en shared; eso es parte de lo que hay que decidir.

---

### 1.5 Otros módulos compartidos (transversales)

No son de síntesis pura, pero forman parte del catálogo compartido:

- **AudioComparator** — comparador A/B de audio (tiempo/espectral).
- **HardwareDrivers** — controladores hardware (SysEx, NRPN, etc.).
- **HardwareMidiDetect** — detección y picker MIDI.
- **BankManager** — gestión de banco (`ABDBankManagerCore`, `FactoryContentLoader`, `Pro800Midi`, `hardwareMidiPipe`, `BankManagerWebViewAdapter`).
- **Scope** — analizador de señal (core JUCE-free + componentes JUCE).
- **LcdDisplay**, **Segmented** — LCD/pantallas segmentadas.
- **StudioTopology** — topología de estudio.
- **WebView2Bridge** — puente WebView2.
- **AutoUpdater** — actualización automática.
- **ABDAudioLab** — generación de tablas LUT (fuera del espacio del synth, en la raíz del workspace).

---

### 1.6 Diseño del build

- Cada módulo es un target independiente en `ABDSharedCode/CMakeLists.txt`: DspCore, DspEffects, SynthCore, LutDSP, etc. Son headers, parte inline y parte `.cpp`.
- Los consumidores los consumen con `add_subdirectory(ABDSharedCode)` y/o `include_directories(../ABDSharedCode)`.
- Algunos módulos enlazan JUCE (p. ej. HardwareMidiDetect, algunas capas de HardwareDrivers). El core de síntesis busca ser tan JUCE-free como le basta; hay dependencia de `juce_audio_basics` en algunas capas (p. ej. `AudioBuffer` de DspCore y `AnalogLutFilterModule`).
- Compatibilidad: algunos synths tienen shims locales que referencian los headers compartidos con namespace o alias (p. ej. `using DSP = abd::synth` en los shims de ABXportDsp). Esto es compatibilidad, no duplicación de implementación, pero hay que mirar el árbol de cada producto antes de citar un shim concreto como "consumo real": un shim a veces citado, `ABXportDsp/Source/DSP/ADSREnvelope.h`, **NO existe en disco en esta rama**.

---

## 2. Qué se usa hoy en cada synth (referencia por consumo observable)

Esto es un resumen de OBSERVACIÓN, no un inventario completo de cada producto. Donde no haya evidencia, no se afirma consumo.

- **ABDCZ101 (emulador CZ101):**
  Usa su propio DSP local (`Source/DSP/...`) y, en algunos casos, un shim local que hace `using DSP = abd::synth`. No usa DspResonant ni LutDSP directamente según lo observable; su filtro es su propia clase `ResonantFilter` local (no la TPT compartida). Su matriz de mod es una estructura local de 8 slots estilo ABDEep.

- **ABDEep (DeepMind 12):**
  Usa su propio engine JUCE; puede aprovechar el motor compartido FxEngine donde encaje, pero tiene su propia FXEngine con parámetros JUCE y matrices de efectos de 50. Tiene su propio filtro JunoVCF ZDF local y sus propias envolventes/LFO locales. Existe también `MoogLadderVCF.*` local en ABDEep (duplicado frente al Moog LUT de shared). La existencia del Moog LUT en shared no implica que el producto lo consuma hoy.

- **ABDMS2000 (emulador MS-2000):**
  Usa SynthCore (ADSREnvelope, LFO, PolyBLEP, PortamentoGlide, VoiceAllocator) a través de shims, y tiene su propio `MultiModeFilter` local. No usa DspEffects para sus efectos (tiene DelayFX, Equalizer, ModFX locales). Tiene su propio asignador/espaciador de voz local. El filtro Moog no usa el compartido hoy (tiene su propia implementación).

- **ABDOmegaUnified (módulos Omega):**
  Arquitectura modular WASM; cada módulo es un `.cpp` con ABI `omega_*`, sin consumo directo de SynthCore observable. Sus módulos ejemplo (vco/vcf/vca) son ZDF/PolyBLEP generados, no los bloques compartidos. El filtro actual es un **ZDF TPT OTA** (IR3109/80017A) en `modules/vcf/vcf.cpp`, no un Moog de tabla ni de circuito compartido.

- **ABDJUNiO601 (Juno-106):**
  Tiene su propio ADSR/LFO/Envelope local, su propio JunoVCF y coro BBD local. No consume SynthCore/LutDSP hoy (aunque el coro BBD está en DspEffects como `JunoBBD` y perfiles `JunoBbd*`, el synth tiene su propia implementación).

- **ABDNeural (NEURONiK):**
  Usa SynthCore/ModMatrix y SynthCore/VoiceAllocator, pero tiene su propio DSP de envolventes/filtros/osciladores locales (`AdditiveVoice`, `NeurotikEngine`, `FilterBank`, etc.). No usa DspEffects para sus efectos; tiene su propio catálogo.

- **ABDAudioLab:**
  Fuera del motor, pero es de donde vienen las tablas LUT que usa LutDSP. Relevante si se quiere añadir un modelo LUT nuevo.

**Conclusión rápida:** la mayoría de los synths tienen sus propias copias locales de envolventes, LFOs, filtros y osciladores, con distintos niveles de cercanía a shared. SynthCore ya tiene los bloques básicos; lo que falta es confirmar cuánto se usan esos bloques realmente en cada synth y qué está duplicado en producto que podría venir de shared con un adapter mínimo.

---

## 3. Inventario de métodos firmados (para estandarización)

Esto es lo que sirve de contrato hoy en shared, con firma real. Sirve para saber qué es "compatible" si se quiere sustituir en un producto por shared: si la firma coincide, puede haber adapter sin cambiar el motor; si no, hay que decidir si estandarizar el producto o crear una nueva familia.

### 3.1 Envolventes (ADSR)

Interfaz compartida: `abd::synth::ADSREnvelope` — ver firma en §1.2.

### 3.2 LFO

Interfaz compartida: `abd::synth::LFO` — ver firma en §1.2.

Nota: el LFO compartido tiene dos familias de waveform (LFO1/LFO2) y soporte de sincronización de teclado/tempo. Los productos que esperan únicamente LFO con triángulo/cuadrada/etc. y sin sync pueden adaptarse sin problema.

### 3.3 Portamento / Glide

Interfaz compartida: `abd::synth::PortamentoGlide` — ver firma en §1.2.

### 3.4 Oscilador antifilterado

Interfaz compartida: `abd::synth::PolyBLEP` (estático, sin estado) — ver firma en §1.2.

Y la interfaz de la FAMILIA de osciladores (no el primitivo suelto):
`abd::synth::IsOscillator<T>` como rasgo de compilación sobre el contrato de
`OscillatorFamily.h`, con `OscVcoCa72` y `OscPolyBlep` como miembros — ver §6.4.

### 3.5 Asignación de voz

Interfaz compartida: `abd::synth::VoiceAllocator<MaxVoices>` — ver firma en §1.2.

### 3.6 Matriz de modulación

Interfaz compartida: `abd::synth::ModMatrixT<N>` — ver firma en §1.2.

### 3.7 Filtro resonante TPT (etapa)

Interfaz compartida: `abd::dsp::ResonantFilterStage` — ver firma en §1.1.

### 3.8 Motor de efectos

- `FxEngine`: prepare, setCatalogue, getSlot, setRouting/getRouting, setMode/getMode, setSendLevel/getSendLevel, setFeedbackGain/getFeedbackGain, reset, process, processStereoPath, processMonoPath.
- `FxSlot`: prepare, setType/getType, isActive, setParameter/getParameter, isParameterTouched, setGain/getGain, setMix/getMix, reset, process, getEffectInfo.
- `FxRegistry`: fxNormalise, fxDenormalise, fxEffectAt, fxFindEffect, kFxMaxParams, kFxNumSlots.

### 3.9 Evaluación LUT / filtros por respuesta medida

- `LutEvaluator1DSimd`: loadLutData, getLutSize, getScaleFactor, isEmpty, evaluateSingleValueSimd, evaluateSingle, processBlockSimd, processStereoBlockSimd.
- `AnalogLutFilterModule`: setSmoothingTime, setVoiceParameters, snapVoiceParameters, getVoiceParam1Current, getVoiceParam2Current, processPolyphonicBlock.

---

## 4. Duplicación y huecos hoy

### 4.1 Cosas que ya están en shared y pueden reutilizarse directamente o casi

- ADSR exponencial (SynthCore).
- LFO con sync (SynthCore).
- Glide/Portamento (SynthCore).
- PolyBLEP (SynthCore).
- Asignación de voz polifónica + políticas de robo (SynthCore).
- Matriz de modulación genérica sin policy (SynthCore).
- Etapa filtro resonante TPT con AGC (DspCore).
- Motor de efectos con ranuras, mezcla, topologías y registro (DspEffects).
- Etapas de carácter y perfiles inyectados (DspEffects).
- Motor LUT SIMD y módulo polifónico LUT (LutDSP).
- Filtros modelo LUT ya precompilados: Moog, Juno IR3109, CZ, Aira Bitrazer, EuroRack diode ladder, Pro800 CEM3320 (LutDSP).

### 4.2 Cosas que siguen duplicadas / locales / sin interfaz unificada

- Cada synth tiende a llevar su propia versión de ADSR/LFO/filtro/oscilador, a veces por compatibilidad hacia atrás, a veces porque su API es distinta.
- **Filtro Moog:** existe en shared como LUT (`lut_mock_va_synth_moog_ladder`), pero la mayoría de los synths no lo usan (usan sus propios filtros o ZDF/TPT; ABDEep tiene además su propio `MoogLadderVCF` local). El ca-72 trae un modelo de circuito muy diferente que no está traducido a shared.
- **Filtros en general:** no hay interfaz unificada que englobe etapa TPT, tabla LUT y ecuación en tiempo real; cada familia está en su propio sitio con sus propias firmas.
- **Osciladores:** no hay interfaz unificada de VCO/DCO/wavetable/phase-distortion en shared. **(ENTREGADO el 2026-10-06: la familia de osciladores y sus dos miembros, ver §6.4.)**
- **Sumadores/mixers:** no hay interfaz unificada de mezcla de señales; cada voz arma su propia mezcla de osc/ruido.
- **UI de controles:** no hay un componente compartido de "control de panel" reutilizable entre synths.

---

## 5. El ca-72 como caso de ejemplo a reescribir/traducir al nuevo sistema

### 5.1 Qué es

El ca-72 es un plugin en Rust (`D:/desarrollos/ABDSynths/_RESOURCES/ca-72-main`) que modela el circuito del Minimoog Model D (~1972-73) a partir de netlists SPICE y lo simula en tiempo real. Los bloques musicales del ca-72, en términos de familias futuras, son:

- **VCO** (`crates/ca72/src/vco.rs`, `expo.rs`): control exponencial de corriente, formas de onda múltiples (saw, triangle, shark tooth, square, rect, reverse saw), oversampling 1x-8x con decimadores halfband + blep, parámetros calibrados de ngspice (`CoreParams`, `ShaperParams`).
- **VCF** (`crates/ca72/src/vcf.rs`): ladder de Moog con control de corriente (nodo de control: cutoff, keyboard tracking 1/2, amount of contour, emphasis), tres modos de fidelidad (No Compromises / High Fidelity / Potato), prewarping, Newton por paso, integración trapezoidal/nystrom, tablas para el modo de plugin.
- **VCA**: modelo de circuito con control por contorno.
- **Contours**: ataque/decay/sustain para filtro y volumen, con modelo del circuito.
- **Noise**: blanco/pink/rojo, con modelo de circuito.
- **Keyboard circuit** (`keyboard.rs`): afinación, track-and-hold, glide, lowest-note priority, trigger.
- **Preamp** de entrada externa: acoplamiento, ganancia del lazo, lámpara.
- **Modulation mix**: mezcla del oscilador 3 con noise en la línea de modificación.
- **Panel/UI** (`crates/ca72-panel/`): SVG + renderer, knobs, selectors, rockers, wheels (`controls.rs`, `art.rs`).
- **Plugin params** (`crates/ca72-plugin/src/params.rs`): ids estables, unidades, skew, valores por defecto, conversión dial 0..1.

### 5.2 Lo que importa y lo que no para shared

Lo más portátil: el **MODELO DE AUDIO** (matemáticas/física/ecuaciones/transcripción de circuito).
Lo menos portable: la UI del plugin y el framework de host (nih-plug, baseview, resvg).

Las tablas, parámetros y curvas del ca-72 son útiles como **REFERENCIA** y como posible base de perfiles, pero hay que decidir si se traducen como implementación de circuito, como tablas LUT/lookup, o como referencia documental.

### 5.3 La duda central del Moog

Shared **YA TIENE** un Moog, pero es LUT (respuesta medida/modelada en tabla). NO es el modelo de circuito del ca-72. El ca-72 aportaría una versión distinta (circuito resuelto paso a paso).

Entonces el Moog del ca-72 no entra en shared "porque ya hay un Moog"; entra (o no) dependiendo de qué quieras que sea el Moog objetivo en shared:

- **Opción A:** como referencia de circuito + posible implementación C++ por circuito (fidelidad alta, más coste).
- **Opción B:** como tabla LUT (si la exactitud del circuito no es el objetivo).
- **Opción C:** no lo llevas, o lo dejas como referencia documental y usas lo que ya hay.

Esto afecta directamente a qué interfaz de filtro defines y qué implementación encaja.

---

## 6. Familias: situación ideal y propuesta de ejemplo nuevo

Criterio general: **estandarizar INTERFAZ POR FAMILIA, con el algoritmo/hardware como políticas/perfiles/tablas inyectadas.** Ese es el patrón que ya usa DspEffects (motor+perfil+etapa) y ModMatrix (genérico, sin vocabulario de producto).

### 6.1 Envolventes (ADSR/multietapa)

- **Ideal:** mismo ciclo de vida que SynthCore (prepare/reset/noteOn/noteOff/getNextSample), con unidades de tiempo/rates normalizadas y claridad de curva.
- **Ejemplo nuevo:** base de envolvente con dos modos (ADSR clásico y multietapa configurable), con perfil de curva por familia, sin imponer la política del producto en el motor.

### 6.2 LFO

- **Ideal:** waveform configurable, frecuencia en Hz (+ sync opcional teclado/tempo), salida por muestra. Distinguir LFO1/LFO2 si el producto los distingue.
- **Ejemplo nuevo:** LFO por instancia con waveform por enum y sync opcional; el motor no sabe quién lo consume; el producto pone la tabla de destinos/unidades/retrigger.

### 6.3 Glide/Portamento

- **Ideal:** target MIDI note + enabled, time normalizado, salida semitonos por muestra.
- **Ejemplo nuevo:** porta/glide genérico por voz, con unidades y modo de "target changed" claros.

### 6.4 Osciladores (VCO/DCO/Wavetable/Phase Distortion)

- **Ideal:** entrada de frecuencia (Hz o pitch escalado), forma de onda/tabla cambiable, (opcional) ancho/pulse-width/modulación, oversampling/anti-aliasing parametrizable, salida por muestra/bloque. Motor común: fase + forma/tabla + anti-aliasing parametrizable.
- **Ejemplo nuevo:** bloque oscilador genérico (fase + forma/tabla + anti-aliasing) + familias concretas que lo instancian con sus parámetros (VCO exponencial estilo ca-72, DCO, wavetable).

- **Estado: ENTREGADO (2026-10-06) — la familia y sus dos miembros.** El contrato
  vive en `ABDSharedCode/SynthCore/OscillatorFamily.h` (`abd::synth`) y fija el
  idioma: frecuencia en **Hz** (y cada pieza devuelve en `getFrequency()` la que
  de verdad da), formas con `OscWaveform { Sawtooth, Triangle, Rectangle, Sine }`,
  ancho de pulso 0..1 con el mismo significado en todos los miembros (0,5 =
  cuadrada), salida **bipolar y nominalmente ±1** (`oscNominalPeak`) y
  sobremuestreo pedido como factor 1/2/4/8. El contrato se comprueba **al
  compilar** con el rasgo `IsOscillator<T>` — igual que `IsFilterStage<T>` en los
  filtros: no hay clase base ni vtable en el lazo — y el `processBlock` (float y
  double) es de la familia, no de cada miembro. Los dos miembros:

  - **`OscVcoCa72`** (`SynthCore/OscVcoCa72.h` + `.cpp`): el miembro de
    **circuito**. Rampa integrada con reinicio por comparador (retardo que crece
    cuando la rampa se frena, techo que baja cuando corre, espera del transistor) y
    sus tres conformadores: diente de sierra, triángulo y rectángulo. **No tiene
    seno**, y lo dice (`supportsWaveform(Sine)` es `false` y `setWaveform` lo
    rechaza sin cambiar nada). Su hardware son **datos**:
    `SynthCore/OscVcoCa72Profile.h` trae los números medidos del aparato
    (capacidad de temporización, umbral del Schmitt, tabla del retardo del
    comparador, pendiente del efecto Early, conformadores), y la transferencia
    **medida** del triángulo se puede inyectar entera con `setTriangleTable` (el
    perfil trae tres puntos: el pliegue y los dos extremos del barrido). Su puerta
    de circuito (`setTimingCurrent`) sigue abierta para modular en corriente.
  - **`OscPolyBlep`** (`SynthCore/OscPolyBlep.h`): el miembro de **fase**, sobre el
    `PolyBLEP` que ya existía. Da las cuatro formas, declara su propio tope (0,45
    de Nyquist) y dice que no sobresamplea porque ya está banda-limitado a 1x.

  Y un tercer fichero que **no es miembro de la familia sino su herramienta**:
  `SynthCore/OscHalfbandDecimator.h`, la decimación de media banda (taps
  dispersos, etapas cortas delante y la larga al final) que usa el VCO al
  sobremuestrear; es ajeno a la familia a propósito (solo sabe de muestras).

  **Por qué la familia vive en SynthCore y no en DspCore.** Las dos familias son
  gemelas en el contrato, no en el módulo: los primitivos de oscilador de este
  repo (`PolyBLEP`, `DSPUtils`) ya viven en SynthCore, y un oscilador no necesita
  el sustrato entero que un filtro sí usa (por eso la familia de filtros sí está
  en DspCore). Arrastrar aquí DspCore sería cambiar las dependencias de SynthCore
  por una curva de nota.

  **Qué de este 6.4 queda cubierto:** frecuencia en Hz (con la pieza diciendo la
  que de verdad da, no la que le pidieron), forma/tabla cambiable con lo que cada
  miembro declara que sabe hacer, ancho de pulso, anti-aliasing parametrizable
  (1/2/4/8 en el VCO con su diezmador) y salida por muestra y por bloque. **Qué
  no:** no hay contenedor de tiempo de ejecución que guarde "uno de los dos"
  (vive en el mismo sitio que en los filtros: el consumidor escribe el `switch` o
  elige con un `using`); al VCO le falta el seno y lo declara; y del convertidor
  exponencial del ca-72 **no** se porta la cadena voltios->Hz ni sus derivadas de
  temperatura, porque el idioma de la familia es Hz y quien pide Hz ya ha
  recorrido ese camino: lo que sí se queda, porque es carácter y no calibración,
  es el efecto Early (la corriente cae al caer la rampa, y por eso la rampa es
  cóncava y el diente de sierra no es una recta).

  **Medido, no afirmado** (2097 comprobaciones de `ABDShared_SynthCore_Tests` en
  verde, salida 0): el error de frecuencia peor es **0,0071 %** de 55 Hz a 4 kHz y
  una octava es exactamente el doble de periodo; el idioma del ancho de pulso se
  cumple (0,20 -> 0,200 y 0,50 -> 0,499 del ciclo, y para conseguirlo hubo que
  encontrar que en una rampa cóncava la fase tiene que ser la fracción de
  **tiempo** y no de tensión: la mitad del recorrido en tensión cae en el 49,26 %
  del tiempo); el pliegue del triángulo cae donde la curva medida lo pone
  (≈49 % del ciclo) y se mueve donde diga una tabla inyectada; con una sierra de
  7 kHz el bin no-armónico de 16,1 kHz queda en 2,6e-2 a 1x y en 8,0e-7 a 8x; el
  diezmador deja pasar 1,0000 dentro de su banda y tapa a 1e-6 por encima del
  nuevo Nyquist; y la latencia que declara es 1,0 muestra a 1x y 23,75 a 8x.

  **Licencia:** la idea viene de un VCO de aparato **medido** cuya referencia está
  bajo **GPL**, y de ella no se copió ni una línea: el algoritmo está escrito de
  nuevo y lo único que viaja son los números medidos del instrumento, como perfil
  inyectado (un perfil son datos, igual que lo es la tabla de una respuesta
  medida).

### 6.5 Filtros (LP, multipaso, resonante, LUT, ecuación)

- **Ideal:** interfaz común de familia (`setCutoff`/`setResonance`/`prepare`/`reset`/`processSample` o `processBlock`), con implementaciones distintas dentro de la familia:
  - a) etapa TPT (como `DspResonantFilterStage`) para cascadas o etapa genérica;
  - b) tabla LUT (como LutDSP/`AnalogLutFilterModule`) para respuestas medidas;
  - c) modelo de circuito/ecuación (como el ca-72) para fidelidad de circuito.
- **Ejemplo nuevo:** interfaz de filtro por familia con motor común + implementaciones Moog/Juno/resonante/LUT/circuito, de modo que un synth elija sin duplicar ni elegir al azar entre dos filosofías.

### 6.6 VCA

- **Ideal:** entrada señal + control de amplitud (por voz o global), posible curva/no linealidad, salida por muestra. Motor simple; la mezcla/política queda en el consumidor.
- **Ejemplo nuevo:** VCA de familia con interfaz de control unificada.

### 6.7 Noise

- **Ideal:** fuente de ruido configurable (blanco/pink/rojo/etc.), nivel y salida por muestra. Bloque reutilizable para oscilador/mixto/modulación.
- **Ejemplo nuevo:** generador de ruido de familia conmutable (blanco/pink/rojo).

### 6.8 Sumadores/Mezcladores

- **Ideal:** entrada múltiple y salida combinada con pesos, estado mínimo. Bloque pequeño reutilizable para mezclar osciladores/ruido/filtro/VCA; los pesos los pone el producto.
- **Ejemplo nuevo:** sumador de familia (N entradas, pesos, salida por muestra/bloque).

### 6.9 Modulación/Matriz

- **Ideal:** continuar usando ModMatrix como sustrato compartido, con descriptores de destino reutilizables por familia y sin meter el vocabulario del producto en el motor.
- **Ejemplo nuevo:** set de descriptores de destino por familia (cutoff, reso, A/D/S/R, pan...) sobre `ModMatrixT`.

---

## 7. Plan para adaptar los existentes al sistema común (mejores prácticas de mercado)

1. **No estandarizar "todo junto".** Estandarizar por familia con interfaz de entrada común, y dejar las diferencias como perfiles/implementaciones. Eso es lo que ya hace DspEffects con motor/perfil/etapa y lo que hace ModMatrix con identificadores opacos y suma sin policy (patrón "policy injection"/"engine + profile", habitual en librerías DSP modernas).
2. **Primero definir el contrato POR FAMILIA** con ficha de métodos, unidad, sample-threading, y qué se permite cambiar entre implementaciones. Sin eso, la estandarización queda ambigua y cada producto vuelve a tener su propia variante.
3. **Reutilizar primero lo que ya está en shared:**
   - ADSR, LFO, glide, PolyBLEP, VoiceAllocator, ModMatrix como base de nuevas implementaciones.
   - El motor de efectos y sus perfiles/etapas como PLANTILLA de cómo hacer familia con motor común + políticas por dispositivo.
   - El Moog LUT existente como prueba de que una respuesta medida puede vivir en shared.
4. **Para cada familia, destinar un sitio claro en shared y probar primero con un synth nuevo (cliente real)**, no con reescritura masiva de todos los productos de golpe.
5. **Cuando un producto consuma shared, usar adapter mínimo** si la firma actual no coincide, y documentar la equivalencia de resultados o cómo medir la diferencia.
6. **Cuidar el tema del ca-72:** no copiar el plugin ni el framework de host; extraer el modelo de audio, decidir si se lleva como circuito/ecuación o como tablas/LUT, y convertirlo a C++ en el estilo del sistema común (interfaz de familia + implementación concreta).
7. **Para UI:** si no hay componente compartido de panel/knobs/switches, documentar qué parte del ca-72 es reutilizable como referencia visual (SVG, paths, medidas) y qué hay que reimplementar en JUCE o en tu propia capa de parámetros.

---

## 8. Decisiones pendientes (antes de tocar código)

- **A)** Qué synth nuevo quieres crear o migrar primero.
- **B)** Para filtros: qué Moog quieres en shared: circuito tipo ca-72, tabla LUT, o solo tener una opción Moog sin más.
- **C)** Qué familias quieres estandarizar primero y cuáles esperas.
- **D)** Nivel de dependencia aceptable en shared (JUCE total / parcial / libre de JUCE en capas puras de síntesis).
- **E)** Qué productos hoy debes mantener intactos y cuáles puedes migrar/adaptar.
- **F)** Cómo quieres tratar la UI: ¿incluir componentes de panel/knobs/switches en shared, o dejarlo como referencia externa?

---

## 9. Verificación y limitaciones de este catálogo

**Lo que está firmado por lectura directa de headers:**

- DspCore: `ResonantFilterStage` (firma completa).
- SynthCore: `ADSREnvelope`, `LFO`, `PortamentoGlide`, `PolyBLEP`, `VoiceAllocator`, `ModMatrix` (firmas completas).
- DspEffects: `FxRegistry`, `FxSlot`, `FxEngine`, `EffectPolicy`, perfiles, motores, etapas.
- LutDSP: `LutEvaluatorSimd`, `AnalogLutFilterModule`, tabla Moog LUT y registro oficial.

**Lo que queda como "no confirmado" en esta rama:**

- El consumo real de shared por producto, salvo donde se leyó un shim o se tuvo evidencia de include/alias.
- La existencia de un shim concreto (`ABXportDsp/Source/DSP/ADSREnvelope.h`) que **NO está en disco en esta rama**, por lo que el catálogo lo deja fuera en lugar de usarlo como evidencia.

**Lo que no está en shared y está fuera de este catálogo:**

- Interfaz unificada de filtro/oscilador/VCA/noise/mixer. **(De esa lista, el oscilador ya la tiene desde el 2026-10-06: ver §6.4.)**
- Componente compartido de panel/knobs/switches.
- El modelo de circuito del ca-72 (solo existe en el recurso externo).
