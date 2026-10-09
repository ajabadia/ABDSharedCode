# 🧭 Hoja de Ruta de Migración de Efectos (Rack FX)
## `ABDEep/Source/DSP/FX` ➔ `ABDSharedCode/DspEffects` (`abd::dsp`)

> **Propósito del Documento:**  
> Plan maestro de migración, desacoplamiento y elevación de calidad de los **56 algoritmos de efectos** del sintetizador DeepMind 12 en `ABDEep` hacia la librería compartida `ABDSharedCode/DspEffects`.  
> Incluye el **mapeo de emulación hardware real** (TC Electronic, Klark Teknik, Midas, Roland, Lexicon, Fairchild, Eventide, etc.), el estado actual de fidelidad y las áreas de investigación pendientes para alcanzar emulación de precisión.

---

## 🏛️ Principios de Arquitectura para la Migración

1. **Separación de Responsabilidades (La regla del módulo):**
   * **`ABDSharedCode/DspEffects` (`abd::dsp`):** Motor de cálculo puro en C++ estándar (sin dependencias de JUCE). Cero asignaciones dinámicas en hilos de audio (`RT-Safe`). Recibe muestras, perfiles de máquina (`Profile`) y etapas de carácter inyectadas (`CharacterStage`).
   * **`ABDEep/Source/DSP/FX` (`ABD::FXBase`):** Envoltorio ligero de integración (*policy wrapper*). Maneja el ciclo de vida de JUCE (`prepare`, `process`), mapeo de mandos 0..1 del hardware DeepMind al motor (`setParameter`), y serialización/modulación en la matriz de 132 destinos.
2. **Documentación Explícita de Hardware:**
   * Cada fichero C++ debe documentar con precisión **a qué máquina o procesador analógico/digital emula**, cómo están modeladas sus curvas, y qué limitaciones tiene la implementación actual frente al hardware real para guiar futuras mediciones.
3. **Flujo de Calidad Ininterrumpido:**
   * En cada paso de cada fase se debe mantener compilación limpia y paso del 100% de las suites de prueba en C++ (`ABDEep_UnitTests`, `ABDShared_DspEffects_Tests`) y WebUI (`npm run test`).

---

## 📋 Catálogo Completo de Efectos (56 Tipos): Emulación Real y Diagnóstico

### A. Reverbs Algorítmicas (Klark Teknik / Lexicon / TC Electronic)
| Tipo | Nombre Interno | Emulación Hardware Real | Estado Actual | Fidelidad y Tareas de Investigación / Mejora |
| :---: | :--- | :--- | :---: | :--- |
| **1** | `HallRev` | **Klark Teknik DN780 / Lexicon 480L Hall** | ✅ Migrado (`DspSchroederReverb`) | Schroeder-Moorer con perfil. *Investigar:* Modulación de retardo en lazo para mayor difusión estéreo. |
| **2** | `PlateRev` | **EMT 140 / Klark Teknik Plate** | ✅ Migrado (`DspSchroederReverb`) | Densidad y dispersión metálica modelada por perfil. |
| **3** | `RichPltRev` | **Lexicon 480L Rich Plate** | ✅ Migrado (`DspSchroederReverb`) | Red Schroeder de alta densidad. |
| **4** | `AmbVerb` | **Klark Teknik DN780 Ambience** | ✅ Migrado (`DspSchroederReverb`) | Reflexiones tempranas dominantes sin cola densa. |
| **5** | `GatedRev` | **AMS RMX16 / Klark Teknik Gated Reverb** | ✅ Migrado (`DspSchroederReverb`) | Corte no lineal de cola con pendiente abrupta. |
| **6** | `Reverse` | **AMS RMX16 NonLin / Reverse Reverb** | ✅ Migrado (`DspSchroederReverb`) | Crecimiento de energía invertido antes del corte. |
| **22** | `TC-DeepVRB` | **TC Electronic M3000 / System 6000 VSS3** | ✅ Migrado (`DspSchroederReverb`) | Reverb algorítmica estéreo suave y espaciosa. |
| **26** | `ChamberRev` | **Klark Teknik DN780 Chamber** | ✅ Migrado (`DspSchroederReverb`) | Modelo de sala de absorción media. |
| **27** | `RoomRev` | **Klark Teknik DN780 Acoustic Room** | ✅ Migrado (`DspSchroederReverb`) | Respuesta acústica de sala seca. |
| **28** | `VintageRev` | **EMT 250 / Lexicon 224 Vintage Verb** | ✅ Migrado (`DspSchroederReverb`) | Ancho de banda limitado (10-12 kHz) y ruido digital suave. |
| **52** | `FDNReverb` | **8×8 Householder Feedback Delay Network** | 🔒 Local en ABDEep | Matriz unitaria 8x8 sin coloreo espectral. *Investigar:* Modulación de absorción por bandas. |
| **53** | `ZitaReverb` | **Fons Adriaensen Zita-Rev1** | 🔒 Local en ABDEep | Red de 8 peines cruzados con EQ de absorción de aire. Muy alta calidad. |

---

### B. Efectos Híbridos y Procesamiento de Modulación Reverb
| Tipo | Nombre Interno | Emulación Hardware Real | Estado Actual | Fidelidad y Tareas de Investigación / Mejora |
| :---: | :--- | :--- | :---: | :--- |
| **12** | `ModDlyRev` | **Lexicon PCM70 Concert Wave / Delay-Reverb** | 🔒 Local en ABDEep | Delay con realimentación inyectada a la cola de reverb. |
| **23** | `FlangVerb` | **TC Electronic M-One Flanger + Reverb** | ✅ Migrado (Híbrido) | Flanger en serie con reverb compartida. |
| **24** | `ChorusVerb` | **TC Electronic M-One Chorus + Reverb** | ✅ Migrado (Híbrido) | Chorus suave modulando las reflexiones de la reverb. |
| **25** | `DelayVerb` | **TC Electronic M-One Delay + Reverb** | ✅ Migrado (Híbrido) | Ping-pong delay acoplado al lazo de entrada de reverb. |

---

### C. Dinámica, Ecualización y Conformación de Tono
| Tipo | Nombre Interno | Emulación Hardware Real | Estado Actual | Fidelidad y Tareas de Investigación / Mejora |
| :---: | :--- | :--- | :---: | :--- |
| **18** | `Enhancer` | **SPL Vitalizer / BBE Sonic Maximizer** | 🔒 Local en ABDEep | Desfase espectral + generación armónica de graves/agudos. *Mejora:* Medir curvas de ecualización psicométrica. |
| **19** | `EdisonEX1` | **Behringer Edison EX1 Stereo Imager (1990s)** | 🔒 Local en ABDEep | Procesador Mid/Side con ensanchamiento de fase psicoacústica. |
| **20** | `AutoPan` | **Tremolo / Auto-Pan Analógico estéreo** | 🔒 Local en ABDEep | Modulación sinusoidal cruzada L/R con control de fase. |
| **21** | `NoiseGate` | **Drawmer DS201 Dual Noise Gate** | 🔒 Local en ABDEep | Detector de pico con umbral, ataque, caída y retención. *Mejora:* Añadir histéresis analógica. |
| **30** | `MidasEQ` | **Midas Heritage / Klark Teknik DN370 4-Band EQ** | 🔒 Local en ABDEep | 4 bandas paramétricas (Low, Lo-Mid, Hi-Mid, High) con curvas analógicas de campana constante. |
| **31** | `FairComp` | **Fairchild 670 Tube Limiter / Compressor** | 🔒 Local en ABDEep | Compresor de válvulas vari-mu con pendiente suave y constante de tiempo programada dependiente del nivel. |
| **33** | `SimpleComp` | **VCA Studio Compressor (DBX 160 style)** | 🔒 Local en ABDEep | Compresor VCA clásico con rodilla dura/blanda. |

---

### D. Distorsión, Saturación y Modelado de Amplificador
| Tipo | Nombre Interno | Emulación Hardware Real | Estado Actual | Fidelidad y Tareas de Investigación / Mejora |
| :---: | :--- | :--- | :---: | :--- |
| **7** | `RackAmp` | **Tech 21 SansAmp / Mesa TriAxis Rack Preamp** | 🔒 Local en ABDEep | Emulación de previos de guitarra con curva de saturación triodo y filtro de altavoz. |
| **32** | `MultiBandDist` | **TC Electronic Triple-C Multi-band Saturation** | 🔒 Local en ABDEep | 3 bandas con crossover Linkwitz-Riley y distorsión asimétrica independiente. |
| **50** | `OversamplingDist` | **Klon Centaur / TS-9 con 4x Oversampling** | 🔒 Local en ABDEep | Saturador con sobremuestreo polifásico para evitar aliasing. |
| **51** | `WaveShaper` | **Buchla 259 Timbre Wavefolder** | 🔒 Local en ABDEep | Plegado de onda sinusoidal/tangente para creación de armónicos pares e impares. |

---

### E. Modulación y Filtros Creativos
| Tipo | Nombre Interno | Emulación Hardware Real | Estado Actual | Fidelidad y Tareas de Investigación / Mejora |
| :---: | :--- | :--- | :---: | :--- |
| **8** | `MoodFilter` | **Minimoog Ladder / Moog MF-101 Low Pass Filter** | 🔒 Local en ABDEep | Filtro 4 polos con saturación resonante y envolvente/LFO acoplado. |
| **9** | `Phaser` | **Electro-Harmonix Small Stone / MXR Phase 90** | 🎯 Candidato Inmediato (`Phaser4`) | En ABDEep corre por muestra; en `DspEffects/Phaser4.h` ya existe el port optimizado a 27 ns/muestra. |
| **10** | `Chorus` | **Boss CE-1 / TC Electronic SCF Chorus** | 🔒 Local en ABDEep | LFO dual con retardo modulado e interpolación lineal. |
| **11** | `Flanger` | **Electro-Harmonix Electric Mistress / MXR 117** | 🔒 Local en ABDEep | Delay BBD corto (0.5–10 ms) con realimentación resonante en fase y contrafase. |
| **16** | `RotarySpeaker` | **Leslie 122 Rotary Cabinet** | 🔒 Local en ABDEep | Simulación de trompeta de agudos (horn) y tambor de graves (rotor) con efecto Doppler y filtrado direccional. |
| **17** | `ChorusD` | **Roland SDD-320 Dimension D (4 Botones)** | 🔒 Local en ABDEep | Modulación multi-fase fija sin barrido cíclico evidente. |
| **36** | `RolandBBDChorus` | **Roland Juno-60 / Juno-106 BBD Chorus** | ✅ Migrado (`JunoBBD`) | MN3009 BBD físico con filtrado de reconstrucción, ruido de reloj y fuga estéreo modelada. |
| **37** | `SolinaEnsemble` | **ARP / Eminent Solina String Ensemble** | 🔒 Local en ABDEep | Coro de 3 líneas de retardo desfasadas 120° a 0.6 Hz y 6 Hz (efecto ensemble icónico). |
| **38** | `RingModulator` | **IRCAM Diode Bridge / Maestro Ring Modulator** | 🎯 Listo en `DspEffects` (`RingMod`) | Oscilador tri-onda modulado por LFO y recorte de puente de diodos. |
| **46** | `FrequencyShifter` | **Bode 1630 Frequency Shifter** | 🔒 Local en ABDEep | Red de Hilbert y cuadratura para desplazamiento armónico lineal (inarmónico). |

---

### F. Delays y Ecos Analógicos / Digitales
| Tipo | Nombre Interno | Emulación Hardware Real | Estado Actual | Fidelidad y Tareas de Investigación / Mejora |
| :---: | :--- | :--- | :---: | :--- |
| **13** | `Delay` | **TC Electronic 2290 Dynamic Delay** | 🔒 Local en ABDEep | Delay estéreo limpio con feedback y filtros hi/lo cut. |
| **14** | `3TapDelay` | **TC 2290 3-Tap Delay** | 🔒 Local en ABDEep | 3 lecturas temporales con paneo y ganancia independientes. |
| **15** | `4TapDelay` | **TC 2290 4-Tap Delay** | 🔒 Local en ABDEep | 4 lecturas temporales en patrón rítmico. |
| **21** | `TapeDelay` / `T-RayDelay` | **Tel-Ray Adineko Oil Can Delay / Binson Echorec** | 🔒 Local en ABDEep | Sonido oscuro de tambor/lata con modulación de wow/flutter. |
| **34** | `DecimDelay` | **12-bit Vintage Digital Delay (Korg SDD-3000)** | 🔒 Local en ABDEep | Reducción de tasa de muestreo y cuantización de bits en la realimentación. |
| **39** | `SpaceEchoRE201` | **Roland RE-201 Space Echo (Cinta + Muelle)** | 🎯 Listo en `DspEffects` (`MultiHeadEcho`) | 3 cabezales sobre línea de cinta, saturación magnética y reverb de muelles integrada. |
| **40** | `AnalogTapeDelay` | **Maestro Echoplex EP-3 Tape Echo** | 🔒 Local en ABDEep | Cinta cálida con compresión de saturación asimétrica en la grabación. |
| **41** | `ShimmerDelay` | **Brian Eno / Eventide Pitch Feedback Delay** | 🔒 Local en ABDEep | Realimentación que atraviesa un cambiador de octava superior continua. |
| **44** | `DuckingDelay` | **TC Electronic 2290 Ducking Delay** | 🔒 Local en ABDEep | Atenuación del nivel del delay mientras el instrumento está tocando. |

---

### G. Pitch, Resonadores y Procesadores Vocales / Espectrales
| Tipo | Nombre Interno | Emulación Hardware Real | Estado Actual | Fidelidad y Tareas de Investigación / Mejora |
| :---: | :--- | :--- | :---: | :--- |
| **29** | `DualPitch` | **Eventide H3000 Pitch Shifter** | 🔒 Local en ABDEep | Transposición por grano con ventana cruzada y modulación de desafinación estéreo. |
| **35** | `VintagePitch` | **Eventide H910 Harmonizer (1975)** | 🔒 Local en ABDEep | Glitches característicos, respuesta no adaptativa y cuantización lo-fi temprana. |
| **42** | `GranularDelay` | **Granular Texture Cloud Delay** | 🔒 Local en ABDEep | División en micro-granos dispersos en tiempo y panorama. |
| **43** | `PatternFreeze` | **Electro-Harmonix Freeze Sound Retainer** | 🔒 Local en ABDEep | Captura de búfer circular congelado con crossfade suave. |
| **45** | `SpectralDelay` | **Spectral / FFT Multiband Delay** | 🔒 Local en ABDEep | Retardo individual por bandas de frecuencia FFT. |
| **47** | `Resonator` | **Polymoog 3-Band Resonator Section** | 🔒 Local en ABDEep | Banco de 3 resonadores sintonizados en frecuencias fijas/modulables. |
| **48** | `Combulator` | **Dual Comb Filter Resonator** | 🔒 Local en ABDEep | Filtros de peine cruzados con retroalimentación para modelado físico de cuerdas. |
| **49** | `Vocoder` | **EMS Vocoder 2000 / Roland SVC-350 / VP-330** | 🔒 Local en ABDEep | Banco de 16-32 filtros pasabanda biquad y seguidores de envolvente. *Candidato a unificar con ABDMS2000.* |
| **54** | `Nimbus` | **Mutable Instruments Clouds (Granular Processor)** | 🔒 Local en ABDEep | 16 granos simultáneos con dispersión de posición, tamaño, pitch y textura. |
| **55** | `Bonsai` | **Lo-Fi Cassette Tape & Wow/Flutter Degradation** | 🔒 Local en ABDEep | Simulación de pletina de cassette barata, envejecimiento de cinta, saturación y desvío de velocidad. |
| **56** | `Treemonster` | **Pitch-tracking Ring Mod / Synthesizer** | 🔒 Local en ABDEep | Seguidor de cruces por cero para oscilador esclavo de modulación. |

---

## 🚀 Fases de Ejecución

### 🟢 Fase 1: Los Motores Listos en `DspEffects` (Foco Inmediato)
* **Objetivo:** Conectar los motores ya probados en `ABDSharedCode/DspEffects` eliminando duplicidad en ABDEep y retirándolos de la lista de pendientes de consumo.
* **Componentes:**
  1. `FXRingModulator` (Tipo 38) ➔ Delegar en `abd::dsp::RingMod` + `abd::dsp::DiodeBridge` (`RingModProfile`).
  2. `FXSpaceEchoRE201` (Tipo 39) ➔ Delegar en `abd::dsp::MultiHeadEcho` + `abd::dsp::Re201Profile` + `abd::dsp::TapeColour`.

### 🟡 Fase 2: Unificación con `ABDMS2000` y Filtros de Modulación
* **Objetivo:** Compartir bloques matemáticos idénticos entre ABDEep y ABDMS2000.
* **Componentes:**
  3. `FXPhaser` (Tipo 9) ➔ Delegar en `abd::dsp::Phaser4`.
  4. `FXVocoder` (Tipo 49) ➔ Extraer `abd::dsp::DspVocoderBank` y `abd::dsp::DspEnvelopeFollower` compartiéndolo con `Vocoder16Band` de MS2000.
  5. `FXSolinaEnsemble` (Tipo 37) ➔ Extraer a perfil BBD tri-fásico en `DspEffects`.
  6. `FXMoodFilter` (Tipo 8) ➔ Conectar al Moog Ladder ya promovido en `DspCore/DspMoogLadder.h`.

### 🟠 Fase 3: Bloques de Dinámica y Saturación / Distorsión
* **Objetivo:** Promover motores analógicos compartidos de compresión y recorte.
* **Componentes:**
  7. `FXSimpleComp` (21), `FXFairComp` (31), `FXNoiseGate` (33) ➔ `DspEffects/DspDynamics.h`.
  8. `FXWaveShaper` (51), `FXOversamplingDistortion` (50), `FXMultiBandDist` (32), `FXRackAmp` (7) ➔ `DspEffects/DspSaturation.h` / `DspWaveShaper.h`.

### 🔵 Fase 4: Ecualización y Modulación Estándar
* **Objetivo:** Promover ecualizadores de consola y coros/flangers clásicos.
* **Componentes:**
  9. `FXMidasEQ` (Tipo 30) ➔ `DspEffects/CascadeShelfEq.h` y filtros paramétricos compartidos.
  10. `FXChorus` (10), `FXChorusD` (17), `FXFlanger` (11), `FXAutoPan` (20), `FXRotarySpeaker` (16) ➔ `DspEffects/DspChorus.h` / `DspFlanger.h`.

### 🟣 Fase 5: Delays Especializados y de Cinta
* **Objetivo:** Unificar líneas de retardo y degradación de cinta.
* **Componentes:**
  11. `FXDelay` (13), `FXMultiTapDelay` (14/15), `FXTapeDelay` (21), `FXAnalogTapeDelay` (40) ➔ `DspEffects/DspDelay.h` + `TapeColour`.
  12. `FXDuckingDelay` (44), `FXDecimDelay` (34), `FXModDelayRev` (12), `FXShimmerDelay` (41).

### ⚪ Fase 6: Reverbs Avanzadas, Pitch y Procesadores Espectrales / Granulares
* **Objetivo:** Promover algoritmos de alta complejidad matemática.
* **Componentes:**
  13. `FXFDNReverb` (52), `FXZitaReverb` (53).
  14. `FXPitchShifter` (29/35), `FXFrequencyShifter` (46).
  15. Granulares y experimentales: `FXGranularDelay` (42), `FXNimbus` (54), `FXBonsai` (55), `FXTreemonster` (56), `FXPatternFreeze` (43), `FXSpectralDelay` (45), `FXResonator` (47), `FXCombulator` (48), `FXEdison` (19), `FXEnhancer` (18).
