# ABDSharedCode

Librería de código compartido entre proyectos ABDSynths.

Repositorio: https://github.com/ajabadia/ABDSharedCode.git

## Módulos disponibles

| Módulo | Contenido | Tipo | Target CMake | Consumidores |
| **SynthCore** | Primitivas DSP de los sintetizadores (`abd::synth`): PolyBLEP, ADSR (MS2000), **EnvelopeAnalog** (envolvente analógica ADSR con curvatura continua por etapa, loop mode, one-shot), EnvelopeCurves, PortamentoGlide, LFO (MS2000), **LfoAnalog** (LFO analógico multionda con fade-in y slew), AudioThreadSnapshot, VoiceAllocator, **ModMatrix**, DSPUtils, **Arpeggiator** (motor determinista de 11 modos, sample-accurate, zero-alloc), **ControlSequencer** (secuenciador analógico de control de 32 pasos, swing, slew modulable). Y **la familia de osciladores**: `OscillatorFamily` (el contrato común, el idioma de sus parámetros en Hz y el rasgo `IsOscillator`), `OscVcoCa72` (el VCO de rampa, con su perfil medido como dato), `OscPolyBlep` (acumulador de fase) y `OscReference` (el prototipo estandarizado de nuevo miembro). Y **el convertidor exponencial independiente**: `CvToControl` (CA-72: teclado + VTUNE + trimpots → corriente de temporización, como puerta inyectable en cualquier VCO que reciba corriente) | STATIC | `ABDShared::SynthCore` | ABDMS2000, ABDEep, ABDMS2000/ABDEep/ABDNeural (ModMatrix, header-only) |
| **DspCore** | Sustrato portado de `juce_core`/`juce_audio_basics` (`abd::dsp`): Maths, Range, SmoothedValue, HeapBlock, AudioBuffer, FloatVectorOperations, MidiMessage/Buffer, Debug, LeakedObjectDetector, `DspMath` (trascendentes deterministas) y `ResonantFilterStage` (sección de paso bajo resonante que se auto-oscila y **se asienta** en un nivel). Y **la familia de filtros**: `DspFilterFamily` (el contrato común, el idioma de sus parámetros y el rasgo `IsFilterStage`), `FilterTpt` (los tres taps del mismo par de estados) y `FilterEquation` (escalera de cuatro polos con el lazo resuelto sin retardo) | INTERFACE (header-only, **sin JUCE**) | `ABDShared::DspCore` | ABDNeural (+ DspEffects) |
| **DspEffects** | Efectos sobre el sustrato DspCore (`abd::dsp`): Reverb (Freeverb, port literal de `juce::Reverb`), Chorus, Delay, Saturation, `SchroederReverb`; y los de maquina, por politica inyectada: `EffectPolicy` + motores (`MultiHeadEcho`, `RingMod`, `JunoBBD`), etapas de caracter (`TapeColour`, `DiodeBridge`, `BbdNoise`) y perfiles de dispositivo (`Re201Profile` con los doce modos del selector, `ReverbProfile` con las diez variantes del DeepMind 12, `JunoBbdProfile` con los dos clones de la Juno) | INTERFACE (header-only, **sin JUCE**), linka DspCore | `ABDShared::DspEffects` | ABDNeural, ABDEep, ABDAudioLab (BBD), ABDJUNiO601 (referencia congelada) |
| **LutDSP** | Evaluación de LUTs y modelado analógico (`abd::lutdsp`): LutEvaluatorSimd (SSE), AnalogLutFilterModule, VoiceDispersionModel, VoiceAllocator; y `LutFilter`, **el miembro LUT de la familia de filtros** (el corte y la Q medidos de una tabla, sobre el núcleo TPT del sustrato) | INTERFACE (header-only), linka DspCore | `ABDShared::LutDSP` | ABDJUNiO601 |
| **HardwareDrivers** | Codecs de protocolo de hardware (`abd::hw`): SysExCodec, NRPNParser. Enlaza `nlohmann_json` en `PUBLIC` porque `CasioCzVirtualController.h` es superficie pública del módulo e incluye `<nlohmann/json.hpp>` | STATIC | `ABDShared::HardwareDrivers` | ABDMS2000, ABDJUNiO601 |
| **HardwareMidiDetect** | Detección contract-driven de hardware MIDI (C++ puro + picker WebView2 estilo ABDScope) | INTERFACE | `ABDShared::HardwareMidiDetect` (+ `ABDShared::HardwareMidiPickerAssets`) | ABDAudioLab |
| **AutoUpdater** | Auto-actualización via GitHub Releases | STATIC | `ABDShared::AutoUpdater` | ABDMS2000, ABDAudioLab |
| **MidiKeyboard** | Teclado y ruedas compartidos (`@abdsynths/midi-keyb`) | paquete de workspace pnpm, no CMake | — | ABDMS2000 |
| **Segmented** | Selector segmentado universal (`abd::ui::Segmented`): radio group de botones planos, valor por índice, vetados con nota (gating por motor) | INTERFACE (header-only), sonda opt-in `ABDShared_SegmentedProbe` | `ABDShared::Segmented` | (gemelo JS: `@abdsynths/shared/components/segmented.js`; consumidor NEURONiK pendiente de adoptarlo en el panel nativo) |
| **LcdDisplay** | Pantalla de caracteres universal + máquina de menú (`abd::ui`): LcdDisplay + LcdMenuManager, arbol como dato y hooks | INTERFACE (header-only), gate WASM | `ABDShared::LcdDisplay` | (gemelo JS: lcdMachine/lcdScreen/lcdPanel en `@abdsynths/shared`) |
| **Scope** | Osciloscopio analítico multi-lane embebido en WebView2: taps nativos C++ que capturan a rate de bloque (master, pre-FX, osc-mix, post-filter...), snapshot lock-free a 60 FPS y WebUI embebida como binary data | INTERFACE + STATIC (core header-only + glue JUCE + WebUI), subtree de ABDScope desde v0.4.0 | `ABDShared::ScopeCore` (+ `ABDShared::ScopeCoreHeaders`, `ABDShared::ScopeWebAssets`) | ABDAudioLab, ABDMS2000 (consumidores del core; la WebUI se sirve por filesystem si no hay JUCE) |
| **AudioComparator** | Motor de comparación acústica A/B: alineación temporal por correlación cruzada FFT, medida de error temporal y espectral, veredicto formal (`pass`/`warn`/`fail`) contra matriz de tolerancias | STATIC (4 .cpp), linka juce_dsp | `ABDShared::AudioComparator` | (fuente para consumir por ruta o candidato a módulo formal) |
| **BankManager** | Módulo embebible del Bank Manager (corte desde ABDBankManager): core C++ (ValueTree v1, blobs Base64, IPC por callback), adaptador JSON <-> core sin dependencia de WebView2, loader de factory content y protocolo SysEx del Behringer Pro800. El lado contrato vive en TypeScript (`BankManager/Contracts/`): contrato de modelo (`ModelContract`), contrato de enlace MIDI (`HardwareLinkContract` / `BaseHardwareLink`), registro declarativo (`ContractRegistry`) y los adapters concretos, con los modelos por familia bajo `BankManager/Contracts/Models/` (Behringer DeepMind 12/DM6/DM12D/Pro800, Korg MS2000/microKORG/Prophecy, Roland Juno, Roland AIRA, Casio CZ, Yamaha DX7) y los adapters de import/export/hardware (`BankManager/Contracts/Adapters/`). Es la cara JS del mismo contrato que el corte C++ expone vía `ModelContract.h`, `ModelContractRegistry`, `ImportAdapter`, `ExportAdapter`, `HardwareLinkContract` y los adapters concretos. Ver `INTEGRATION_GUIDE.md` → «Contratos del Bank Manager» | INTERFACE, linka HardwareDrivers + JUCE | `ABDShared::BankManagerCore` | ABDBankManager (consumidor del módulo; OFF por defecto, lo activa quien consume; lado contrato: workspace pnpm, no CMake) |

> `StudioTopology/resources` se expone como `ABDShared::StudioTopologyAssets` cuando el asset existe (consumido por ABDAudioLab). Los directorios `Certification`, `WebView2Bridge`, `visualizers` **no están expuestos como target CMake** todavía: son fuentes para consumir por ruta o candidatas a módulo formal.

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
- **`DspCore/DspResonantFilter.h`** — `abd::dsp::ResonantFilterStage`: un paso bajo
  de dos estados (TPT) cuyo amortiguamiento puede volverse **negativo**, así que
  se auto-oscila a la frecuencia de corte, y cuya AGC por potencia de banda hace
  que esa oscilación **se asienta en un nivel en vez de crecer**
  (`amplitud = sqrt (-2 * k0 / beta)`). Reescrito limpio desde la idea del estudio
  Mz950, nada copiado. El nivel asienta a un 0.03–0.4 % de lo predicho y el tercer
  armónico queda a −84 dB. Los tests comprueban también el **control negativo**:
  con la AGC apagada el nivel es infinito y la señal crece. Detalle y medido en
  [`docs/mz950-reaprovechamiento.md`](docs/mz950-reaprovechamiento.md)  (fila C1).
- **`DspCore/DspFilterFamily.h`** — **LA FAMILIA DE FILTROS**: el contrato por
  convención que todo filtro del sustrato honra (`prepare`, `reset`, `setCutoff` en
  Hz, `setResonance` 0..1, `setMode`, `processSample`), el rasgo `IsFilterStage`
  que lo comprueba **al compilar** (sin clase base y sin vtable en el lazo), y el
  idioma de sus parámetros: la curva de corte (20 Hz..20 kHz, exponencial) y la de
  Q (1/√2..20) son **de la familia y no de cada miembro**, para que cambiar de
  filtro no cambie la afinación del panel. Tres miembros: `FilterTpt` (los tres
  taps del mismo par de estados; a resonancia 0 es el **mismo filtro** que
  `ResonantFilterStage`, comprobado a 1e-12 muestra a muestra), `FilterEquation`
  (una escalera de cuatro polos con el lazo resuelto **sin retardo** y `tanh` en la
  realimentación: a resonancia 1 el pico queda acotado en vez de crecer) y
  `LutDSP/LutFilter.h` (el corte y la Q de una **tabla medida**, sobre el mismo
  núcleo TPT). El `processBlock` (float y double) también es de la familia: no lo
  implementa cada miembro.
- **`SynthCore/OscillatorFamily.h`** — **LA FAMILIA DE OSCILADORES**, la hermana de
  la de filtros y con el mismo patrón: el contrato por convención que todo oscilador
  del módulo honra (`prepare`, `reset`, `setFrequency` en Hz, `setWaveform`,
  `setOversampling`, `setPulseWidth`, `processSample`) y el rasgo `IsOscillator` que
  lo comprueba **al compilar**. El idioma de sus parámetros es la mitad del asunto:
  la frecuencia en **Hz** (una unidad, no una posición de mando), las formas
  básicas de un oscilador de voz, el ancho de pulso 0..1 con el **mismo**
  significado en todos (0.5 es cuadrada), la salida bipolar y nominalmente ±1, y el
  sobremuestreo como factor 1/2/4/8. El `processBlock` (float y double) también es
  de la familia: no lo implementa cada miembro. Vive en `SynthCore` y no en el
  sustrato porque los primitivos de oscilador (`PolyBLEP`, `DSPUtils`) ya vivían
  aquí. Dos miembros:
  - **`abd::synth::OscVcoCa72`** (`OscKind::Circuit`) — el de **circuito**: un
    condensador que integra una corriente y un disparador de Schmitt que reinicia la
    rampa al cruzar su umbral —con el retardo del comparador y la espera del
    transistor, que son tiempos **absolutos** y por eso el núcleo tiene su propio
    tiempo dentro—, y tres conformadores de onda que salen del **mismo nodo**:
    diente de sierra (asimétrico: no sube lo que baja), triángulo por el **pliegue
    medido** y rectángulo con histéresis. El limitado de banda son escalones polyBLEP
    en los sucesos (reinicio y flancos) más decimación de media banda al
    sobremuestrear (`SynthCore/OscHalfbandDecimator.h`). **El hardware entra como
    DATO** (`SynthCore/OscVcoCa72Profile.h`): los números medidos del banco de
    pruebas y, inyectable entera con `setTriangleTable`, la transferencia medida del
    triángulo. **No tiene seno, y lo dice**: `supportsWaveform(Sine)` es `false` y
    `setWaveform` lo rechaza sin cambiar nada.
  - **`abd::synth::OscPolyBlep`** (`OscKind::Phase`) — el de **fase**: un acumulador
    con las cuatro formas básicas corregidas por el `PolyBLEP` que el módulo ya
    tenía (el triángulo sale de integrar su derivada ya banda-limitada). Declara que
    no sobresamplea, porque ya está limitado a 1x, y su propio tope: 0,45 de Nyquist.

  - **`abd::synth::OscReference`** (`OscKind::Phase`) — el **prototipo de
    referencia** de la familia: el esqueleto mínimo que un nuevo miembro tiene que
    poder compilar, declarar y probar aquí, con el mismo contrato (`IsOscillator`),
    el mismo idioma (Hz, formas básicas, ancho 0..1, salida bipolar ±1) y la misma
    disciplina de prueba. En esta versión del prototipo dice en voz alta qué tiene
    (sierra + seno, 1x) y qué falta (triángulo, rectángulo, ningún sobremuestreo),
    y no dibuja una aproximación cuando le falta una. No es una pieza de sonido, es
    la referencia estandarizada de cómo se escribe un miembro nuevo en
    `SynthCore`; el resto del archivo se lee como ejemplo antes de escribir el
    propio núcleo.

  Lo que se **midió**, para que esto no sea una promesa: el peor error de frecuencia
  entre 55 Hz y 4 kHz es **0,0071 %** y una octava es exactamente el doble de
  periodo; el ancho de pulso de la familia sale donde debe (0,20 → 0,200 y 0,50 →
  0,499 del ciclo) porque la pieza mide la fase en **tiempo** y no en tensión —la
  rampa es cóncava y cubre la mitad de su recorrido en el 49,26 % de su tiempo—; el
  ancho más estrecho que su comparador da es **0,051** y lo **declara**
  (`minPulseWidth()`) en vez de dejar el rectángulo pegado abajo; y el limitado de
  banda está medido, no afirmado: para un diente de sierra de 7 kHz la basura que 1x
  deja en 16,1 kHz vale 2,6e-2 y a 8x vale 8,0e-7, mientras el diezmador deja pasar
  entero (1,0000) un tono dentro de la banda que sobrevive y mata a 1e-6 uno por
  encima del nuevo Nyquist. La pieza declara además su propio retardo —1,0 muestras
  de salida a 1x, 23,75 a 8x— en vez de sonar medio ciclo antes sin decirlo. Verde
  en las 2097 comprobaciones de `ABDShared_SynthCore_Tests`.

  Y la provenance, que es lo primero que hay que saber al tocar esto: el algoritmo
  está escrito de nuevo desde la idea del VCO del aparato de estudio —cuya
  referencia está bajo GPL y de la que **no se copió ni una línea**—, y lo único que
  viaja son los **números medidos** del instrumento, como perfil inyectado.

- **`SynthCore/CvToControl`** — **EL CONVERTIDOR EXPONENCIAL INDEPENDIENTE DEL CA-72**:
  la puerta del aparato, separada del VCO: convierte voltios de teclado + VTUNE +
  trimpots de escala y centro → corriente de temporización, con el modelo de la
  pareja de transistores (Vbe = kT/q · ln Ic/I0), la beta del par y la temperatura
  del chip. Es **independiente**: no tiene muestreo, no tiene forma de onda, no tiene
  sobremuestreo; solo convierte V → A según el aparato, y puede inyectarse en un VCO
  que recibe corriente directa (`setTimingCurrent`), en un VCO que resuelve frecuencia,
  o en un motor de CV que no es un VCO. El perfil (`CvToControlProfile`) es datos del
  banco de pruebas (Vbe0, i0, beta, trimpots, temperatura de referencia), y el
  convertidor también hace las inversas (corriente → semitonos del teclado,
  corriente → VTUNE) para un panel o un afinador. Reescrito de nuevo desde la idea del
  convertidor exponencial de pareja de transistores; la referencia está bajo GPL y **no
  se ha copiado ni una línea**. Quedan fuera, dichos en voz alta: la tabla medida de la
  beta a temperatura, el ruido de la fuente de corriente fija y los
  microcompensados del banco de pruebas.
- **`SynthCore/S950PatchFields.h`** — el **catálogo de patches** del Akai S950: 38
  campos de keygroup y 18 trims de Perform, cada uno con su byte, su rango de panel,
  su codificación y su nombre de humano. Un panel y un motor que no dicen lo mismo del
  mismo byte es la forma más barata de perder tiempo, y esto es la tabla que hace que
  digan lo mismo. Ojo al detalle que la hace necesaria: el 0..99 es lo que **imprime**
  el panel, no lo que se guarda — los fines de zona van 0..255 porque son el byte bajo
  de un offset de altura, y el switch de velocidad va 1..128 con un 128 que significa
  "no hay segunda zona". Reescrito limpio desde la idea del estudio, nada copiado.
  Detalle y medido en [`docs/mz950-reaprovechamiento.md`](docs/mz950-reaprovechamiento.md)
  (fila B3).
  **El mismo catálogo llega a los paneles en JavaScript como un contrato
  GENERADO**, no como una copia: `ABDSharedAssets/contracts/s950_patch_fields.json`
  se produce desde esta cabecera y se verifica con `pnpm check:s950-contract` en
  el paquete compartido. Panel y motor no pueden discrepar sobre el mismo byte;
  si discrepan, es que el generador no se ha corrido.
- **`SynthCore/S950Disk.h`** — lee y escribe los bytes **reales** de un programa del
  S950, y resuelve cada campo por el catálogo de arriba en vez de por offsets escritos a
  mano. La primera mitad es la geometría del disco (directorio de 64 entradas, tabla de
  asignación, cadena de bloques) y la segunda son los campos. El detalle que obliga a
  que sea así: como 70 no divide a 1024, **un keygroup SIEMPRE se parte entre dos
  bloques**, y esos dos bloques no tienen por qué ser contiguos. Por eso se resuelve byte
  a byte y no con un `memcpy`, y por eso un byte que cae fuera de la cadena devuelve
  fallo en vez de un número inventado. Reescrito limpio desde la idea del estudio.
- **`SynthCore/S950Calibration.h`** — el contrato de las curvas de **unidades
  medidas** del S950 (envolvente a s, LFO a Hz, warp a s, cutoff a Hz, octavas de
  la envolvente de filtro, dB del sustain): qué curva existe, en qué unidad y
  sobre qué rango de panel. **Deliberadamente sin un solo número.** Una tabla de
  calibración son resultados experimentales, y los del estudio Mz950 son AGPL;
  en cambio un offset de byte es un hecho de formato que cualquiera redescubre.
  El coste de no tenerlos es cero hoy: el importador lee y escribe el valor
  *guardado* y ni lo mira. La regla que lo sostiene es que **`read()` devuelve
  `std::nullopt` —nunca 0— mientras no haya puntos medidos**, porque un 0 en un
  tiempo de envolvente es un click, y 0 *es* un número, así que el motor no
  tendría nada de qué sospechar.
  **`S950CalibrationHarness.h`** es la salida: convierte una sesión de medición
  en esa tabla corrigiendo el sesgo de la propia ventana de análisis, y avisa si
  no ha convergido. No mide — un arnés que midiera sería un banco de pruebas.
  La tabla **tambien llega a un panel**: `ABDSharedAssets` la vuelca a
  `contracts/s950_calibration.json` con `pnpm generate:s950-cal` y la indexa en
  `components/s950Calibration.js`, que con ella puede dibujar los ejes y marcar
  cuales no estan medidas. El C++ manda y el JSON se genera, como con el
  catalogo de patches. Y sale de aqui un numero que no era obvio: de las seis
  curvas solo **dos** tienen eje vertical dibujable, porque las otras cuatro son
  logaritmicas y un log sin un minimo real —un valor medido— se va a menos
  infinito. El eje horizontal se dibuja en las seis.
  Detalle y medido en
  [`docs/mz950-reaprovechamiento.md`](docs/mz950-reaprovechamiento.md) (filas B4 y B4c).
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

**Y lo contrario:** qué hay aquí que no usa ningún producto. Hoy son 19 de 170
fuentes (la medida del propio auditor), todas con motivo escrito en la lista blanca de
`tools/audit_unconsumed_sources.py` (`--check` devuelve 1 si aparece una nueva).
El inventario y el porqué están en
[docs/fuentes-sin-consumidor.md](docs/fuentes-sin-consumidor.md).

**Y lo que se estudia todavia no:** el emulador **Akai S950** (`_RESOURCES/Mz950-main`, AGPLv3)
resuelve cosas que aqui repetimos. El inventario de que se aprovecha, a donde va cada cosa
y con que prioridad esta en
[docs/mz950-reaprovechamiento.md](docs/mz950-reaprovechamiento.md) — y la regla que lo
gobierna es que **nada se copia**: cada idea se reescribe limpia en el destino, con sus
pruebas. La primera en aterrizar fue la curva ADSR con esquinas arrastrables, que hoy vive
en `ABDSharedAssets/components/envelopeCurve.js` y la consume NEURONiK sin copia local.

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

## Pre-commit Hooks (formateo automático de C++)

Este repositorio incluye un hook de `pre-commit` en `.githooks/` que formatea
automáticamente el código C++ con `clang-format` antes de cada commit. El estilo
está definido en [`.clang-format`](.clang-format) y es el punto único de verdad
para el formato de todos los archivos `.h`/`.cpp`.

### Instalación (un solo comando)

**Después de clonar el repositorio, ejecuta esto una sola vez:**

```bash
bash .githooks/install.sh
```

Este script ejecuta `git config core.hooksPath .githooks` para que git use los
hooks de este directorio en lugar de `.git/hooks/`. Solo hay que correrlo una
vez por clon. Para desinstalar:

```bash
git config --unset core.hooksPath
```

### Qué hace el hook

1. **Busca `clang-format` versión 18+.** Si no lo encuentra o la versión es
anterior, el hook falla y el commit no se completa.
2. **Revisa que `.clang-format` sea válido** (lo parsea con `--dump-config`).
3. **Detecta los archivos `.h`/`.cpp` staged.**
4. **Los formatea in-place** con `clang-format -i` según la configuración de
`.clang-format`.
5. **Re-stagea** los archivos formateados automáticamente, de modo que el
commit incluye el código ya formateado.

> Si necesitas forzar un commit ignorando el hook (por ejemplo, para un commit
> masivo o un archivo que aún no está formateado), usa:
> ```bash
> git commit --no-verify -m "..."
> ```
> No es lo habitual; el hook está ahí para que el estilo se mantenga sin que
> nadie tenga que pensar en ello.

### Estilo de código (`.clang-format`)

La configuración en [`.clang-format`](.clang-format) define el estilo compartido:

| Propiedad | Valor | Ejemplo |
|---|---|---|
| Indentación | 4 espacios, sin tabs | `    body();` |
| Braces | En nueva línea (namespace, clase, struct, función, control) | `class Foo
{` |
| Pointers/references | Pegados al tipo | `Type* p`, `Type& p` |
| Spaces before parens | Solo después de keywords (`if`, `for`, `while`) | `if (x)` / `foo(x)` |
| Espacio después de C-style cast | No | `(float)x` no `(float) x` |
| Límite de columna | Ilimitado (`0`) | las líneas largas se preservan |
| Standard | `c++20` | requiere clang-format 18+ |

### Instalar `clang-format` 18+

| Sistema | Comando |
|---|---|
| Ubuntu / Debian | `sudo apt install clang-format-18` |
| macOS (Homebrew) | `brew install xcode-clang` (o `brew install llvm@18`) |
| Windows (MSYS2) | `pacman -S mingw-w64-x86_64-clang-format` |
| Cualquier sistema (pip) | `pip install clang-format==18.*` |

### Verificación en CI

El workflow [`shared-code-ci.yml`](.github/workflows/shared-code-ci.yml) incluye
un job `clang-format` que ejecuta **el propio hook en modo `VERIFY_ONLY`** sobre todos
los archivos `.h`/`.cpp` en cada *push* a `master` y en cada *pull request`. El
modo `VERIFY_ONLY` hace que el hook use `git ls-files` para listar todos los
archivos trackeados y los compruebe con `clang-format --dry-run --Werror`, sin
modificar nada en disco.

Además incluye un **test negativo** que crea un archivo C++ deliberadamente mal
formateado, ejecuta el hook, y verifica que falla (exit ≠ 0). Si el hook no detectara
el archivo mal formateado, el CI falla diciendo que el hook ignora violaciones.

El hook local y el CI verifican lo mismo: el hook formatea antes de commitear, y
el CI comprueba que nada se haya escapado —y que el hook funciona realmente.

### Makefile (uso alternativo en local)

Además del hook, este repositorio incluye un [`Makefile`](Makefile) con targets de
conveniencia para verificar o aplicar el formato sin usar git hooks:

```bash
make check-format    # verifica estilo sin modificar (dry-run --Werror)
make format          # formatea todos los .h/.cpp in-place
make verify          # ejecuta el hook en modo VERIFY_ONLY (equivalente al CI)
```

| Target | Qué hace | Modifica archivos? |
|---|---|---|
| `make check-format` | `clang-format --dry-run --Werror` sobre todos los `.h`/`.cpp` | No |
| `make format` | `clang-format -i` sobre todos los `.h`/`.cpp` | Sí |
| `make verify` | Ejecuta `VERIFY_ONLY=1 bash .githooks/pre-commit` | No |

Usa `make help` para ver los targets disponibles.

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
