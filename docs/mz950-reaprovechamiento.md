# Mz950 → ABDSynths: qué se aprovecha

Inventario de lo que el emulador **Akai S950** (`_RESOURCES/Mz950-main`) tiene que ya
resuelto y que ABDSynths repite por su cuenta. Para cada elemento: **qué es**, **a dónde
va**, **en qué estado** y **qué prioridad tiene**.

No es una lista de pendientes para hacer: es el mapa de lo que ya está profitando, de lo
que está-profitando, y de lo que se decidió **no** tocar. Sirve para no volver a
inventarlo y para no empezar un twin que ya existe en el paquete compartido.

## LA REGLA (esto va antes que todo)

`Mz950-main` es **AGPLv3**. ABDSynths no lo es. Por eso **nada de este inventario es una
copia**: cada fila dice qué idea se reescribe limpia en el destino, con el nombre, el
contrato y las pruebas del destino. Donde el destino ya tenía su propia versión, la idea
del S950 la **enriquece** (normalmente con una prueba que la obliga a), no la sustituye.

Corolario: las tablas de abajo son un catálogo de **ideas e interacciones**, no de código.
Si algún día alguien va a implementarlas, el primer paso es escribir el test del destino.

## Tabla A — Interfaz: destino `ABDSharedAssets` (paquete `@abdsynths/shared`)

El plugin JUCE y la WebUI de NEURONiK son dos clientes del mismo paquete. Lo que vive aquí
se paga una vez y se reusa en las dos superficies.

| # | Idea del S950 | Origen | Destino | Estado | Prioridad |
|---|---|---|---|---|---|
| A1 | **Editor de ADSR con esquinas arrastrables**: 3 asas, la central mueve decay *y* sustain a la vez, la curva es la verdad | `AkaiS950Studio/EnvelopeEditor.cs`, `Plugin/Source/PluginEditor.h` | `components/envelopeCurve.js` + `envelopeGestures.js` + `envelopePad.js` | **Hecho** (Fase 1) | P0 |
| A1b | **El knob nunca ha pintado su arco de valor**: dos bugs de la skin `vector` (el `transformOrigin` en CSS anulaba el atributo `transform`, y el `dasharray` fijo no podia acortar el trazo). Salia de 0° con el valor a 0.25 y de 180° con el valor a 1 | — (encontrado al medir el bipolar) | `components/skins/index.js` | **Hecho** (2026-09-29) | P0 |
| A2 | **Eje X por etapas, no por segundos**: los tiempos del S950 son exponenciales, un 80 guardado es ~2 s donde un 40 son 30 ms; dibujarlos a escala deja las envolventes cortas invisibles | `PluginEditor.h` (cabecera) | `envelopeCurve.js` (`SUSTAIN_SHARE`, compresión √) | **Hecho** — la geometría compartida ya comprime | P1 |
| A3 | **Sustain sin asa propia**: con decay 0 el corner cae encima del de attack y el sustain queda inalcanzable; se resolvió con **anchura mínima de tramo**, no con un cuarto asa | `PluginEditor.h` (`enum Corner`) | `envelopeCurve.js` (`MIN_SEGMENT_SHARE = 0.05`) | **Hecho** | P0 |
| A4 | **Asa central arrastre 2D**: una x (decay) y una y (sustain) del mismo asa | `EnvelopeEditor.cs` (`case 1`) | `envelopeGestures.js` (`ENVELOPE_HANDLES`, `dragSegment` + `dragSustain`) | **Hecho** | P0 |
| A5 | **Velocidad dibujada, no `TrackBar`**: 20 px de ancho, máximo arriba como un fader, el número debajo, `Selectable = false` para que no robe las flechas del teclado | `AkaiS950Studio/VelocitySlider.cs` | `components/` — control nuevo `abd-velocity` | Pendiente | P2 |
| A6 | **Vista de forma de onda con selección y cabezal** | `AkaiS950Studio/WaveformView.cs` | `components/waveforms.js` ya existe como catálogo de formas: falta la *vista de muestra* | Pendiente | P3 |
| A7 | **Teclado C0–G8 con teclas de los keygroups resaltadas y punto en la nota sostenida por MIDI** | `Plugin/Source/ProgramPage.h` (`KeygroupStrip`) | `styles/components/keyboard.css` ya existe; falta el widget con estado | Pendiente | P2 |
| A8 | **Knob bipolar con marca en el cero**: el arco se llena desde el centro y un punto marca el reposo, para que el 0 sea un sitio y no una suposición | `Plugin/Source/Look.cpp:180` | `components/knob.js` (`bipolar: true`), pintado por la skin `vector` | **Hecho** (2026-09-29) | P1 |
| A9 | **El color significa algo**: ámbar = lo que está en el disco, cian = un offset mientras tocas, violeta = un extra que el S950 no tuvo | `Look.h` (paleta) | `components/fxTheme.js` / `styles/components/fx.css` | Parcial (el tema de fx ya existe) | P3 |
| A10 | **Panel con título**: los controles viven en `content()`, no se reparentan nunca | `Look.h` (`Panel::content`) | `styles/components/panels.css` | Parcial | P3 |
| A11 | **Editar arrastrando sobre la propia lectura**: el control dice qué suena debajo del offset (`"-> 52"`) | `PluginEditor.h`, `ProgramPage.h` | Familia de `modMatrix.js` / `xypad.js` | Pendiente | P2 |

## Tabla B — Formatos nativos y E/S: destino `ABDSharedCode`, **solo estudio**

Decisión del usuario: **estudio, no migración**. No se portan gêmeis JUCE/C#.

| # | Pieza | Origen | Qué se sacaría | Prioridad |
|---|---|---|---|---|
| B1 | Lectura/escritura de **imágenes de disco 800K/1600K** | `Plugin/Source/S950/Disk.{h,cpp}` | El formato es el mismo que AkaiDisk: sector, cabecera, nombres a 8 bytes. Referencia para el importador nativo | P3 (nadie lo pide aún) |
| B2 | **HFE** (AKAI flattop) | `Plugin/Source/S950/Hfe.{h,cpp}`, `AkaiS950List/HfeWrite.cs` | Leer y escribir HFE, incluidos loops y map points | P3 |
| B3 | **Catálogo de patches** (el `c:` de la máquina) | `S950/Disk.cpp` (la tabla de bytes), `S950/PluginProcessor.cpp` (los trims) | `SynthCore/S950PatchFields.h` (los datos) + `SynthCore/S950Disk.h` (los bytes) + `ABDSharedAssets/contracts/s950_patch_fields.json` (el mismo catálogo en JS) | **Hecho** (2026-09-29): 38 campos de keygroup + 18 trims de Perform, un lector/escritor que los resuelve, y el contrato GENERADO para que un panel pinte los mismos nombres. Ver abajo |
| B4 | **Calibración medida** (`S950/Cal.h`) | `Cal.h` | `SynthCore/S950Calibration.h` (el contrato) + `S950CalibrationHarness.h` (el arnés) | **Hecho, SIN NINGÚN NÚMERO** (2026-09-29): la forma de las seis curvas, y la maquinaria para medirlas. Ver abajo |
| B5 | **Round-trip de samples** | `AkaiS950Studio/WavFile.cs`, `AudioImport.cs` | Reglas de importación (stretch, slice, loop) | P3 |

## Tabla C — DSP: destino `DspCore` / `DspEffects` / `SynthCore`

Ya está **cableado** en el plan aprobado: el motor de NEURONiK usa estas librerías, así que
todo lo que se escriba aquí es reutilizado de verdad.

| # | Idea del S950 | Origen | Destino | Nota |
|---|---|---|---|---|
| C1 | **Butterworth de 6º orden con resonancia que se autoseudoscila**: 3 secciones en cascada con Q 0.52 / 0.71 / 1.93 (las que hacen Butterworth en vez de tres resonantes iguales), la tercera como **SVF TPT** de Zavalishin en vez de biquad, con damping `k = k0 + beta*power` | `Plugin/Source/S950/Filter.h` | `DspCore/DspResonantFilter.h` (`abd::dsp::ResonantFilterStage`) | **Hecho, pero solo la ETAPA** (2026-09-29): la sección reusable con TPT y AGC por potencia de banda. La cascada de 3 secciones del S950 es de ESA maquina, no del sustrato, asi que sigue sin hacer |
| C2 | **El filtro corre PRIMERO en la cascada**, con el nivel de la etapa resonante al alza: lo que añade sobre el cutoff recibe 24 dB/octava de las dos secciones siguientes | `Filter.h` (comentario) | `DspCore` | Detalle de orden con motivo, no arbitrario |
| C3 | **Trims atómicos**: los parámetros de "Perform" son *offsets* sobre los del programa, y se aplican a todos los keygroups a la vez, en el hilo de audio, sin bloqueo | `S950/Engine.h` (`AtomicTrims`), `S950/Voice.h` (`Trims`) | `SynthCore/AudioThreadSnapshot.h` ya tiene el patrón | La idea ya está: **el offset se suma al valor del keygroup, no lo sustituye** |
| C4 | **Ring de notas sin bloqueo** entre hilo de mensaje y hilo de audio | `S950/Engine.h`, `Engine.cpp:120` | `DspCore/DspMidiBuffer.h` | Ya cubierto |
| C5 | **La velocidad decide el trim en el ataque y en ningún otro sitio** | `S950/Voice.cpp:45` | `SynthCore` | Regla de arquitectura de voz, no un efecto |
| C6 | **Espectro de síntesis**: FM, ring y los bends analizados a armónicos, horneados al renderizar | `S950/Synth.h` (`Spectrum`), `Synth.cpp:206` | `SynthCore` | P2, es motor de síntesis completo |
| C7 | **Trims 0..99 con unidades reales de la máquina** (pitch 1.527 cents/unidad) | `S950/Voice.h:112` | `SynthCore` | Tabla de unidades: sirve para que el panel y el motor digan lo mismo |
| C8 | **Nombres y colours por material** (percusión, cuerdas, piano, beds de ambiente) como *datos*, no como código | `S950/Patch.h:57` | Contrato de patches | P3 |

## Lo que ya está hecho (Fase 1, la de la misión)

El editor de envolvente de NEURONiK **dejó de ser una copia local**: `WebUI/src/ui/envelopeCurve.js`
se borró y la vista la da el paquete compartido.

| Fichero (ABDSharedAssets) | Qué aporta |
|---|---|
| `components/envelopeCurve.js` | Geometría pura: `envelopePoints`, `envelopeLinePath`, `envelopeAreaPath`, `envelopeNeedlePath`, y la fábrica `createEnvelopeCurve` |
| `components/envelopeGestures.js` | El gesto: 3 asas, arrastre absoluto, teclado (flechas, `Shift`×10, `PageUp/Down`×10, `Home/End`) |
| `components/envelopePad.js` | El control `EnvelopePad` (editable o vista) con el contrato de familia: `setValue`/`getValue`/`destroy`/`onChange` |
| `styles/components/envelope.css` | La hoja de la familia, importada también por la WebUI |

La API está descrita en `ABDSharedAssets/COMPONENTS.md` (§ EnvelopePad) y en
`ABDSharedAssets/docs/STYLES_GUIDE.md` (§4.6). Del lado del consumidor, en
`ABDNeural/WebUI/README.md` está la sección "La envolvente vive en el paquete compartido".

Dos decisiones que costaron medir y conviene no volver a tomar:

- **El caption se monta SIEMPRE, aunque esté vacío.** Un `span` sin texto mide 0, pero el
  `gap: 2px` del flex-column no; quitarlo cambiaba la altura del SVG en 2 px dentro del cajón
  y rompía la referencia visual.
- **NEURONiK pasa `toReal: realFromNormalized` en las dos llamadas** (lienzo y cajón). Sin
  él la vista compartida pinta en unidades normalizadas en vez de segundos y la forma de la
  curva cambia.

La regresión visual de la ficha y del cajón de envolventes quedó **pixel-perfect**, y el
e2e de las cuatro agujas (`needle-probe`) pasa con su umbral de 0.

## C1 — la etapa resonante (`DspCore/DspResonantFilter.h`)

`abd::dsp::ResonantFilterStage`: un paso bajo de dos estados cuyo amortiguamiento se puede
volver **negativo**, de modo que los polos cruzan y la etapa **se auto-oscila** a la
frecuencia de corte. Lo que no hace un filtro de los de siempre es **asentar** esa oscilación
en un nivel en vez de dejarla crecer. Lo hace con una AGC cuya entrada es la potencia de la
salida de banda:

```
k[n]        = k0 + beta * potencia[n]
potencia[n] += follow * (banda[n]^2 - potencia[n])
```

El equilibrio da el nivel, y por tanto la predicción que comprueban los tests:
`potencia = -k0 / beta`, `amplitud de banda = sqrt (-2 * k0 / beta)`.

Reescrito limpio desde la idea del estudio, **nada AGPL copiado**. Lo que se descartó al
medirlo, y que no debe volver a intentarse:

- **La cascada de 3 secciones de Q 0.52/0.71/1.93 no se trae.** Es la respuesta de 36 dB/octava
  de una máquina concreta; el sustrato compartido tiene la ETAPA, y encadenarla es del host.
- **La compensación de pasabanda no hace falta y estorba.** Se inventó, se midió, y subió la
  resonancia de 0 a 0.99 con el corte en 1 kHz deja la ganancia a 100 Hz en 1.01 y a medio
  corte la sube de 0.95 a 1.33: este esquema **realza** la pasabanda. Una compensación aquí no
  devolvería nada, lo multiplicaría por 5.4. No hay ninguna, y la cabecera del módulo explica
  por qué.
- **No es igual a un biquad de forma directa.** Se separan hasta 1.9e-3: comparten los polos
  pero no el numerador. El test que iba a compararlos barrido a barrido se reescribió como lo
  que sí es cierto: paso bajo de segundo orden, plano, monotónico, −3 dB en el corte y
  **12 dB/octava** (no 24: cada polo aporta 6 y este tiene dos).

Nueve tests en `DspCore/DspCoreTests.cpp`. Los que importan:

- **Se asienta en el nivel predicho, y el nivel NO crece.** Con resonancia 0.94/0.95/0.97 el
  pico de la última medio segundo coincide con `sqrt (-2 * k0 / beta)` con 0.03–0.4 % de
  error, y el RMS del segundo 2 y del 3 son el mismo número (< 0.5 % de diferencia).
- **El control negativo**, que es lo que hace que lo de arriba sea la AGC y no saturación
  dura: con `setLevelControl(0)` el nivel asienta en `infinito` y el RMS del primer segundo ya
  es `inf` — o sea, **sin AGC crece**, y el test lo dice.
- **El tono asentado es un seno**: tercer armónico a −84 dB (el test exige 40). Es lo que
  compra promediar la potencia a ~2 ciclos del corte en vez de usar `banda^2` muestra a
  muestra: lo segundo dobla la oscilación y la ensucia.
- **El umbral coincide con el analítico**: 0.93333, y a 0.93 abajo no hay oscilación propia
  mientras que a 0.94 arriba canta.
- Retune sin click, recortes de valores de panel hostiles, `reset()` que no toca los ajustes,
  y cero exacto en silencio (nada de denormales).

**Suite del módulo: 2281 comprobaciones verdes.**

## B3 — el catálogo de patches (`SynthCore/S950PatchFields.h`)

**38 campos de keygroup** y **18 trims de Perform**, en datos. Un panel y un motor que
no dicen lo mismo del mismo byte es la forma más barata de perder tiempo: el motor
aplica un rango, el panel enseña otro, y nadie se entera hasta que un patch importado
suena raro. Esta tabla es la respuesta — una sola, de la que leen los dos.

Reescrito limpio desde la idea del estudio, **nada AGPL copiado**: la tabla equivalente
del estudio vive partida entre `S950/Disk.cpp` (byte, rango y codificación) y
`S950/PluginProcessor.cpp` (los trims). Aquí están juntas, con otros nombres, otra
agrupación y otro contrato. Lo que sí son **datos de la máquina** —el byte 42 es el
fine de la zona suave y va de 0 a 255— son hechos, no código, y un importador los
necesita igual.

Lo que la tabla dice de cada campo, y por qué cada cosa hace falta:

| Columna | Qué es | Por qué está |
|---|---|---|
| `code` | Identificador corto y estable | Es la clave con la que un preset o un motor la referencian. Renombrarla rompe lo que ya existe |
| `name` | Lo que ve la persona, en inglés como el panel | Un `name` vacío no rompe nada al compilar: rompe el panel |
| `byteOffset` | Posición dentro del registro de **70 bytes** del keygroup | Y no dentro del fichero: un keygroup puede partirse entre el final de un bloque y el principio de otro que no tiene nada que ver, así que un offset absoluto sería un número que miente |
| `lo`/`hi` | Rango de **panel**, inclusivo | Y no es siempre 0..99. Ver abajo |
| `encoding` | `Unsigned` / `Signed` / `Port` / `Bit` | Tres de los cuatro no son "un byte": el signo, el puerto y los bits comparten byte con otras cosas, y quien los lea como `uint8` obtiene un número que existe y no significa nada |
| `bitMask` | Qué bit, solo para los flags | Los cuatro flags comparten el byte 18. Es lo que hace posible cambiar uno sin perder los otros |
| `group` | En qué pestaña del panel sale | Es como se lee la tabla |
| `trimId` | El trim de Perform que lo mueve | Un campo con trim vale `valor + trim`, y por eso están en la misma tabla y no en dos |
| `unit` | Lo que cuenta la unidad | Para quien tenga que explicar el número |

**El 0..99 es lo que imprime el panel, no lo que se guarda.** De los 38 campos, 35 van
0..99 y los otros tres son la razón de que esto sea una tabla y no un `for`:

- **Los fines de zona (`softFine`, `loudFine`) van 0..255**, porque no son una altura:
  son el byte **bajo** de un offset de 16 bits con signo, y lo alto es el transpose. Los
  dos juntos son un offset en dieciseiseavos de semitono. Recortarlos a 0..99 es un
  cuarto de tono de error que no da ningún fallo, solo afinación mala.
- **El switch de velocidad va 1..128**, y el 128 significa "no hay segunda zona". Una
  velocidad MIDI vale 0..127, así que nunca llega a 128, y por eso ese valor puede ser
  el que dice "nada que repartir".
- **El `velocitySwitch` empieza en 1** porque el 0 no es un valor: es el hueco.

**El byte 18 tiene cuatro bits conocidos y uno que no.** Los conocidos cubren `0x1D`; el
`0x02` no lo descifra el estudio, así que aquí se declara explícitamente en vez de
dejarse como un misterio. Un byte del que nadie sabe que un bit está vivo es un byte que
se pierde en la primera escritura.

**El offset de un trim no tiene que alcanzar los dos extremos del campo — y el VCF amount
es el caso que lo demuestra.** Lleva ±50 sobre un campo que recorre 100, así que no
llega a los dos extremos desde cualquier sitio. Se evaluó ponerlo a ±100 y se descartó
con un dato de la biblioteca: el VCF amount es el mismo valor en todos los keygroups de
todos los programas, así que no hay reparto que un offset tenga que preservar. Las
**envolventes sí llevan ±99**, porque ahí sí hay keygroups que se apartan y el mando
tiene que poder llevarlos a donde estaban. Un test fija la excepción, para que
"arreglarlo" no pase el test general sin que nadie lo note.

### Lo que los tests miran, y por qué un catálogo sin tests se pudre

Un `.inc` propio, **1516 comprobaciones verdes** en la suite del módulo (g++ y MSVC):

- **Todo byte dentro del registro de 70.** Un offset de 74 no es "casi vale": lee la
  cabecera del keygroup siguiente, y como puede empezar en cualquier punto de un programa
  encadenado, la lectura sigue siendo válida y devuelve basura con toda naturalidad.
- **Dos campos no se pisan en el mismo byte**, salvo los flags, que deben poder hacerlo.
- **Los códigos son únicos**, y `findField` devuelve *siempre* la fila que se le pide: dos
  filas con el mismo código se esconderían detrás de una que "funciona".
- **Los cuatro flags tienen bits distintos y rango 0..1**, y su suma es exactamente
  `0x1D`. Si se añade un flag, la suma tiene que cambiar con él.
- **Los `Signed` van de −50 a +50**, para que uno nuevo no se cuele con 0..99.
- **Las envolventes del filtro están en 34..37, en orden.** Es un test tonto que se
  rompe solo si alguien inserta una fila entre medias, que es justo cuando conviene que
  se note.
- **Los trims apuntan a campos que existen**, y un trim sin campo es una de las tres
  decisiones explícitas (resonancia, LFO al filtro, forma del LFO: controles que la
  máquina no tenía), no un olvido.

### B3b — el mismo catálogo en un panel (`ABDSharedAssets/contracts/s950_patch_fields.json`)

El catálogo era C++ y se quedaba en C++. Un panel en JavaScript tenía dos
opciones: copiar la tabla a mano —y entonces hay dos copias, y las copias se
separan sin ruido— o no tenerla. Se eligió la tercera: **el C++ manda y el JSON
se genera**.

```
ABDSharedCode/SynthCore/S950PatchFields.h     ← la fuente
        │  pnpm generate:s950-contract
        ▼
ABDSharedAssets/contracts/s950_patch_fields.json  ← generado, NUNCA editado a mano
        │  buildS950Catalogue()
        ▼
components/s950PatchFields.js  →  la WebUI de NEURONiK
```

`pnpm check:s950-contract` sale con 1 si el contrato commiteado se ha quedado
viejo, que es lo que un test de JavaScript no puede ver. Es el mismo patrón que
`generate_modulation_contracts.py --check`, que ya hacía falta para las tablas de
modulación.

**El generador no falla en silencio.** Dos cortes, y los dos existen porque el
generador de las tablas de modulación los necesitaba:

- Si la tabla sale **vacía** —el parser ha dejado de entender el código— no se
  escribe nada y el contrato commiteado se queda como estaba. Un parser roto que
  escribe una lista vacía encima parece un cambio de datos.
- Si el **número de filas** no es el que dicen los tests de C++, avisa en vez de
  generar un contrato con 39 campos. Los dos sitios —el contador del generador y
  el del test de C++— se actualizan juntos, o el contrato y el motor dejan de
  hablar el mismo idioma.

**Un bug real que cazó el contrato, y que los tests de C++ no veían.** La tabla
tenía `warpDepth` y `velToRelease` con un `trimId` de su propio nombre, y ese
trim **no existía** en la lista de Perform. Una referencia colgante. Ningún
motor se quejaba: un panel que lo buscase no encontraría nada, y solo se sabría
al mover el mando. Lo cazó el test que mira la relación **al revés** —del campo
al trim, y no solo del trim al campo— y se añadió también en C++, que es donde
vive el dato.

**Los nombres se guardan crudos.** El JSON dice `ALL` y `MONO1`; la tipografía
la pone `formatS950Name()`. Un contrato que maqueta se queda viejo el día que el
panel cambie su estilo, y entonces el desfase parece del panel cuando es del
dato.

Y `bipolar` **no es una columna**: se deriva del rango en el componente. El caso
que lo justifica es `softFine`, que llega a 255 pero no baja de 0: si `bipolar`
fuera un campo del JSON, alguien lo pondría a `true` por costumbre y el mando se
centraría sobre un valor que nunca es negativo.

### B3c — el importador (`SynthCore/S950Disk.h`)

Un catálogo sin un lector es una tabla. Esto lee y escribe los bytes **reales**, y
existe porque el formato tiene una trampa que un array de estructuras no puede tener:

**Como 70 no divide a 1024, un keygroup SIEMPRE se parte entre dos bloques.** No es un
caso raro, es lo que pasa siempre que el programa pasa de un bloque. Y esos dos bloques
**no tienen por qué ser contiguos**: la tabla de asignación es una lista encadenada. Un
`memcpy` de 70 bytes leería basura del bloque de al lado, y lo escribiría de vuelta al
sitio equivocado sin quejarse. Por eso la resolución es byte a byte cruzando la cadena,
y por eso un byte que cae fuera de la cadena devuelve **fallo**: un número inventado ahí
se escribiría después en el sitio que toque.

El test monta la imagen **byte a byte y por su cuenta**, sin pasar por el módulo — si
usara `addProgram` para construirla y `readField` para leerla, los dos compartirían el
mismo error y pasarían los dos. Y la monta a propósito con la **cadena rota**: bloques
4 → 9 → 20, ninguno contiguo, y el keygroup 14 partido justo entre el 4 y el 9.

**Las tres trampas del formato, y dónde están resueltas:**

| Trampa | Qué pasa si se ignora | Dónde |
|---|---|---|
| El byte va uno a uno | Basura al partir un keygroup entre bloques | `keygroupByteAt` cruza la cadena byte a byte |
| El `0xFF` del puerto es el `0` del panel | Un programa en stereo se lee como salida 255, que no existe | `readKeygroupField` / `writeKeygroupField` |
| Los cuatro flags comparten el byte 18 | Escribir un flag tira los otros tres y el bit reservado | Lectura-modificación-escritura del byte |

**Y una cuarta que no es de formato sino de lectura**: la envolvente del filtro en blanco.
Un S900 no tenía envolvente de filtro, y lo que escribía en sus cuatro bytes no era una
envolvente: eran **espacios**. Leídos como `uint8` son cuatro 32 —una envolvente lenta y
falsa que además no se puede arreglar desde el panel, porque el panel no sabe que hay un
espacio ahí—. Leer devuelve la envolvente **plana** (sustain 99, resto 0), y escribir un
solo campo la aplana antes, o el keygroup se queda con tres espacios y un número: medio
convertido, que es peor que no convertir.

**Verde: 1828 comprobaciones** (g++ y MSVC), incluidas las que importan más:

- **Escribir un campo cambia exactamente un byte de la imagen**, y es el byte que dice la
  cadena. Se compara la imagen antes y después byte a byte. Un byte de más es otro campo,
  el bit reservado o el nombre de una zona.
- **El bit reservado `0x02` sobrevive** a la escritura de un flag. Es la razón de que un
  flag se escriba con lectura-modificación-escritura y no a pelo.
- **Los 38 campos, escritos y releídos** en un keygroup nuevo: cada uno con su codificación,
  su recorte y su cadena. Y el otro keygroup sin enterarse de nada.
- **Una cadena que se apunta a sí misma se corta**, y leer un byte más allá **falla en vez
  de inventar**. Un disco raro no puede colgar el proceso.

### B4 — las curvas de unidades medidas (`SynthCore/S950Calibration.h`)

El catálogo de B3 dice **qué byte es cuál**. Este dice **cuánto vale**: que "attack
80" son 2.8 s, que "LFO rate 45" son 4 Hz. Sin esto el importador funciona igual
—guarda y devuelve enteros de panel— pero un motor que quiera *suenar* como la
máquina no tiene por dónde empezar.

**Y la tabla está vacía a propósito.** No es un TODO: es la decisión que se
tomó, y hay que entender por qué antes de llenarla.

Una curva de calibración son **resultados experimentales**. El estudio tiene una
campaña de medición de ~22 runs, con correcciones de sesgo, residuales, errores
estándar y juicios del autor ("6.25 is a CHOICE among values the measurement
allows", "STORED 90 IS THE ODD ONE… it is the one point a second take should be
asked about first"). Eso no es un hecho de formato —que se redescubre con un
editor hex, y por eso los offsets de B3 sí están aquí— sino una medición, que
pertenece a quien la hizo y cuya licencia es AGPLv3.

El coste de no tener los números es **cero** para lo que el repo hace hoy, y el
coste de inventarlos sería enorme:

| | ¿Lo necesita? |
|---|---|
| El importador (`S950Disk.h`) | **No.** Lee y escribe el valor *guardado*, que es un entero. Ni lo mira. |
| Un renderer | **Sí.** Y lo tendrá, de una medición propia hecha con el arnés de aquí. |

### La regla que sostiene el fichero: desconocido ≠ cero

El tentación sería devolver 0. Sería un desastre silencioso: un motor que pide el
attack y recibe 0 lo lee como "instantáneo", y una envolvente instantánea es un
**click**. Peor: 0 *es* un número, así que el motor no tiene nada de qué
suspicar. Un motor que hace un click sospecha del motor, no de los datos.

Por eso `read()` devuelve `std::optional<double>`, y `std::nullopt` no se
confunde con un valor porque **es otro tipo**. Eso es lo que hace que el
compilador ayude en vez de estorbar, y es el test central del bloque.

### Las seis curvas declaradas, y lo que sí se sabe de cada una

Solo la **forma** (código, unidad, eje, rango de panel), que sale del dominio del
panel de B3 y es dato de formato. Los puntos, ninguno.

| Curva | Unidad | Rango de panel | Sube con el byte |
|---|---|---|---|
| Tiempo de envolvente | s | 0..99 | **No** (más byte, más lento) |
| Velocidad del LFO | Hz | 0..99 | Sí, y **lineal** |
| Tiempo de WARP | s | 0..99 | Sí |
| Corte del filtro | Hz | 0..99 | Sí |
| Octavas de la envolvente de filtro | octavas | −50..+50 | No |
| Nivel del sustain | dB | 0..99 | No (más byte, más fuerte) |

Tres detalles que salen de la forma, no de la medición, y que ya están
comprobados:

- **El eje logarítmico y la obligatoriedad de positivo son cosas distintas.** Los
  dB tienen eje logarítmico y admiten el 0 —0 dB es el nivel de referencia, no un
  instante—, así que un sustain apagado es un valor que hay que poder escribir.
  La primera versión deducía "si la curva es de tiempo, el valor no puede ser
  cero" del hecho de que su eje fuera logarítmico, y rechazaba los 0 dB. Ahora
  son dos banderas: `logarithmic` y `positiveOnly`.
- **La interpolación es lineal en log, no una ley de potencia.** Este tipo de
  panel **salta**, no se desliza: un paso de 5 unidades dentro de una década no
  es la mitad de un paso de 10, y los pasos alternan de tamaño. Una ley de
  potencia encaja muy bien y es lo primero que se escribe, y no puede reproducir
  un escalón. Además, extrapolando en log, **nunca sale un tiempo negativo** —que
  es un cambio de signo, y un cambio de signo en una envolvente es un click.
- **Fuera del rango medido no se extrapola** salvo que se pida explícitamente. El
  extremo que nadie ha medido es el que se rellena con una conjetura: el rápido
  se termina antes de que la sonda lo vea, y el lento dura más que la nota.

### El arnés: por qué "vacío" no es "para siempre"

Sin una salida, "no medido" es una excusa. El arnés
(`S950CalibrationHarness.h`) es la salida: observaciones crudas más un modelo
de referencia con verdad conocida, corrige el sesgo de la propia ventana de
análisis y avisa si la sesión no ha convergido.

El sesgo es lo que hace falta corregir: medir "cuánto dura un attack" no es mirar
cuándo se acaba, es mirar una barrida pasar por una ventana de análisis — y una
ventana promedia, así que engaña, y engaña **según la velocidad de la barrida**.
No se corrige con una constante. Se corrige pasando un render de un modelo
propio por el mismo análisis: ahí el sesgo es `observado − verdadero`, y se sabe
de cuánto es.

**No mide.** No tiene sonda ni instrumentación: toma lo que alguien ya midió. Un
arnés que midiera sería un banco de pruebas, y es otra pieza.

### Tres bugs reales que los tests encontraron

1. **Un punto medido exacto en el borde devolvía `nullopt`.** El primer y el
   último punto de cada curva son, a la vez, "el extremo del rango" y "un valor
   medido"; si se mira primero el límite, el motor decía "no lo sé" sobre los dos
   únicos datos que sí estaban medidos. Son 13 comprobaciones en rojo que decían
   "el módulo está roto" cuando el módulo tenía un `if` mal colocado.
2. **La extrapolación devolvía el extremo sin moverse de él**, porque se le
   pasaba el `stored` del *anclaje* en vez del que se preguntaba. Dos
   parámetros, mismo nombre en la cabeza, y la función hacía una cosa distinta de
   la que se le pedía.
3. **`logarithmic` arrastraba a `positiveOnly`**, como se cuenta arriba.

Y dos expectativas mías que resultaron ser aritmética: el punto medio de un eje de
panel 0..99 es **49.5**, no 50. Leyendo 50 y esperando el valor de 49, la
interpolación salía "mal" un 1.9%.

**Verde: 1954 comprobaciones en SynthCore** (g++ y MSVC), sin tocar las 2281 de
DspCore.

### B4b — la frontera con el panel, y por qué no se puede "comparar los rangos"

Pedido literal: *aplicar el catálogo al importador de patches de ABDNeural y
comprobar que los rangos del panel coinciden con los del motor*. La primera mitad
**no tiene a qué aplicarse**: en ABDNeural no existe ningún importador de patches
del S950. `PresetManager` solo maneja presets propios de NEURONiK —APVTS—, y los
únicos ficheros S950 del repo son los que ha generado este propio trabajo.

La segunda mitad sí se ha hecho, y **la conclusión es que los rangos no pueden
coincidir** —y no por un error de alguien, sino porque no están en el mismo
dominio—:

| Concepto | S950 (panel) | NEURONiK (motor) |
|---|---|---|
| `vcaAttack` | 0..99 | `envAttack` 0.001..5 s |
| `vcaSustain` | 0..99 | `envSustain` 0..1 |
| `softFilter` | 0..99 | `filterCutoff` 20..20000 Hz |
| `vcfAmount` | −50..+50 | `filterRes` 0..1 |
| `lfoRate` | 0..99 | `lfo1RateHz` 0.01..20 Hz |

Un test que comparara esos rangos no miría nada: o bien pasaría siempre, o bien
fallaría siempre. Así que en vez de escribir ese test, `ABDNeural/WebUI/src/contracts/s950Bridge.js`
deja escrito **por qué no se puede escribir**, con ocho filas que emparejan concepto
con concepto y con la conversión declarada `null`.

Y la conversión no es un `null` decorativo: `s950ToNeuronik()` devuelve un
**símbolo** `UNKNOWN`, nunca un número y nunca un 0, para cualquier valor de panel
—incluidos 0, los negativos y un `NaN`—. Es el mismo argumento que sostiene
`std::nullopt` en C++, y está en la misma situación: la tabla de B4 está vacía.

Lo que sí es comparable, y se compara, son **las dos mitades por separado**:

- el `panelUnit` de cada fila tiene que ser **carácter a carácter** el que dice el
  catálogo —la fila reescribe a mano lo que el catálogo ya sabe, y es la única
  forma de que las dos cosas se separen en silencio—;
- el `realUnit` de cada fila tiene que coincidir con el rango que declara el APVTS
  real, leído de `parameters.generated.js` -que lo emite el exportador C++ del
  motor, así que un cambio de rango en el plugin rompe este test—.

Y hay un test deliberadamente tonto que no miraba nada antes y ahora sí: **ninguno
de los ocho parámetros del motor tiene un rango 0..99**. Si alguno lo tuviera,
"los rangos coinciden" dejaría de ser imposible, y ese test lo avisa.

El hueco que queda es el que B4 deja, y está escrito en el propio fichero:
volcar la calibración a JSON, poner el `panelValue` de cada fila, y los tests que
ahora comprueban que las conversiones siguen siendo `null` **se pondrán en rojo
nombrando las ocho filas** que hay que revisar a mano. Un hueco que se llena sin que
nadie mire es un hueco con números inventados dentro.

**Verde: 536 comprobaciones en la WebUI** (34 ficheros), 23 de ellas nuevas.

### B4c — la calibración a JSON: un panel puede dibujar cuatro ejes y no cinco

B4 dejó la tabla de curvas declarada y **vacía a propósito**, y esa es una
decisión que se entiende mucho mejor cuando se ve lo que un panel puede hacer
con ella. Este paso es el que convierte `SynthCore/S950Calibration.h` en
`ABDSharedAssets/contracts/s950_calibration.json`, generado por
`scripts/generate_s950_calibration_contract.py` con el mismo patrón que el
catálogo: **el C++ manda, el JSON se genera, y `--check` devuelve 1 si está
desfasado.**

Lo que sale son las seis curvas con su unidad, su rango de panel, su sentido y
su escala —y con `measured: false`, `pointCount: 0`, `points: []` y
`measuredRange: null` explícitos—. Un panel pinta los ejes hoy, sin un solo punto
medido, y marca lo que no sabe. Y esa es exactamente la respuesta que pedía el
encargo: dibujar los ejes **y** marcar qué curvas están sin medir.

**El resultado que no se esperaba, y que es el hallazgo de esta fase.** De las
seis curvas, solo **dos** tienen eje vertical dibujable. Cuatro son logarítmicas
—`envelopeTime`, `warpTime`, `filterCutoff`, `sustainDb`— y un eje logarítmico no
se puede dibujar con un mínimo inventado: necesita un mínimo **real**, y ese
mínimo es un valor medido, que es de los que no hay. Sin él, `Math.log(0)` es
`-Infinity` y la curva se va a menos infinito; y si alguien pone `0.001` porque
ha visto ese número en otro sitio, el log **no falla**: produce una curva con
toda la pinta de medida. El problema no es que el número sea feo.

Lo que se puede dibujar entero es el eje **horizontal** de las seis, porque sale
del dominio del panel —que ya está probado en B3— y es dato de formato. Y
`lfoRate` y `filterEnvOctaves`, que son lineales, sí admiten un mínimo nominal.

Así que `s950AxisFor()` devuelve `needsMeasuredMinimum`, y hay un test que cuenta
cuántas de las seis lo tienen. **Son cuatro, y ese número es la información más
útil que sale de aquí**: dice exactamente qué parte de un panel hay que rayar en
vez de dibujar, y dice cuál. También sale un `s950Coverage()` para el rótulo, que
responde "0 de 6 medidas" con una frase y no con un objeto vacío.

`valueAt()` devuelve `null` para todo, siempre. Es `std::nullopt` en JS, y el
motivo es el mismo de siempre y sigue siendo el bueno: **0 s de attack no es "no
medido", es un ataque instantáneo, que es un click**, y 0 *es* un número, así que
un `|| 0` en el panel lo volvería indistinguible de un dato. La rama donde la
tabla deje de estar vacía se deja escrita e inalcanzable, con un comentario que
dice que ese es el sitio donde va el puente.

**La exposición a un panel** es `ABDNeural/WebUI/src/contracts/s950Calibration.js`,
que **no reimplementa la regla**: delega `needsMeasuredMinimum` en el módulo
compartido. Duplicada, la lista de cuatro se pondría vieja en un panel y no en el
otro, y un panel con un mínimo inventado tiene un eje de ataque bonito y falso.

#### Tres cosas que los tests encontraron, y que no eran de la regla

1. **El validador de esquemas tenía código muerto, y el esquema lo tapaba.** El
   `measuredRange` del contrato es `["object","null"]`, y `null` es el hueco que
   el contrato entero existe para representar. Pero la guarda del validador
   descartaba `null` *antes* de llegar a la comparación de tipos, así que
   `measuredRange: null` salía **en rojo**. Lo grave no era el falso rojo: es que
   `unsupportedKeywords()` —que existe precisamente para cazar esquemas que
   parecen comprobar lo que no comprueban— daba el visto bueno, porque la palabra
   estaba soportada mientras el camino no. Es decir, un esquema que
   acepta y no ejecuta estaba dentro del sistema, y el sistema lo daba por bueno.
2. **Un booleano mal escrito reventaba el generador con un TypeError.** Con un
   `1` donde iba `true`, el parser entregaba un `int` a un regex, y salía un
   traceback de Python que no decía qué columna estaba mal. El trabajo de un
   generador de contrato es **decir la columna**, y un fallo que se lee como un
   fallo del script es un fallo que nadie arregla.
3. **El aviso de "sobra o falta una curva" se lo comía el de "las listas no
   coinciden".** Las dos cosas eran ciertas a la vez, y solo se informaba de la
   segunda, que no dice *si* sobra o *si* falta. Ahora el número sale primero.

Y una cuarta que era un test que probaba lo que no decía: el caso de "falta una
curva" quitaba **la mitad** de una fila, porque las filas ocupan dos líneas, así
que el generador —haciendo bien su trabajo— avisaba de columnas. Pasaba en verde
por el motivo equivocado. Y el caso de la columna booleana rompía el *tercer*
booleano de la fila y luego exigía el nombre del primero.

**Verde: 1433 comprobaciones en ABDSharedAssets** (32 ficheros), 44 de ellas
nuevas, y los dos generadores con `--check` al día. La WebUI queda en
**546 comprobaciones** (35 ficheros), 10 nuevas.

### B4d — el banco de pruebas: el sesgo de verdad, no el sesgo inventado

`S950CalibrationHarness.h` es el arnés, y en su propia cabecera dice con razón que
**no mide**: *«un arnés que midiese sería un banco de pruebas, y eso es otra
pieza»*. `SynthCore/S950EnvelopeBench.h` es esa otra pieza, y solo su mitad.

Lo que hace es lo que faltaba para que el arnés fuese honesto: renderiza un
pulso cuya respuesta se conoce **porque es nuestro**, lo pasa por el **mismo
análisis** que vería una sonda, y devuelve `observado - verdadero`. Ese número
es el sesgo, y el arnés lo resta. Sin él, los puntos que salen de una medición
son números con un error del que nadie sabe el origen, que es **peor** que no
tener tabla: la tabla existe y parece buena.

Y sigue sin producir ni un punto de la curva del S950. La verdad la pone este
repositorio. Ejecutar el banco deja `S950Calibration.h` igual de vacía, y hay
un test que lo comprueba (§10 del `.inc`).

#### El sesgo no es una constante, y ahora está medido

La idea de la medición suena a que se mira cuándo se acaba la envolvente. No es
así: se mira el paso de una barrida por una **ventana de análisis**, y una
ventana *promedia*. Eso mete un error que depende de la **velocidad**, así que no
se arregla con un factor fijo. Medido con la ventana de 2 ms, forma lineal:

| rise | ×ventana | sesgo |
|---|---|---|
| 6,0 ms | 3,0 | **−0,347 %** |
| 28,6 ms | 14,3 | +0,034 % |
| 73,8 ms | 36,9 | −0,017 % |
| 119,1 ms | 59,5 | +0,005 % |
| 300,0 ms | 150,0 | −0,007 % |

**±0,35 % y cambiando de signo.** Ese es el número que el banco produce, y es
veinte veces más pequeño que el `* 1.08` que inventaba el test de al lado. No
por casualidad: corregir con una constante *equivocada* es peor que no
corregir, porque deshace la corrección real. El test 9 lo pone lado a lado: con
el sesgo medido el arnés devuelve la maquina con menos de un 0,1 % de error, y
con un 8 % de mentira quedan casi ocho puntos de error. Un factor fijo no es
una corrección aproximada, es **un error nuevo**.

#### Dos fallos que aparecieron al medir, no al leer

**La verdad analítica de la exponencial estaba mal, y se equivocaba en un 58 %.** Con
`tau = rise / ln 2` la subida llega *justo* a 0.5 en `rise`, así que la verdad
`rise + hold` cuadraba… **a umbral 0.5 y solo a 0.5**. A umbral 0.25 y a 0.75
el conteo real del render se separaba de la fórmula un 58 % en ambos sentidos.
No era redondeo: con la constante fija la subida *nunca* pasa de la mitad, así
que un umbral por debajo la cuenta desde donde le da la gana y uno por encima
no la cuenta en absoluto. El render se **normaliza** ahora y la verdad trae la
fórmula entera. El test 1 lo comprueba contando muestras a mano, con ventana de
**una** muestra, en las seis combinaciones de forma y umbral: si las dos no
coinciden dentro de una muestra, el banco no vale.

**Una espiga de una muestra no es una medición.** Con la ventana muy ancha
frente a la envolvente, el promediado puede cruzar el umbral en *una* muestra
aunque el pulso sea larguísimo, y el análisis responde «ha durado 0,02 ms» para
un pulso de 5 ms. **Medido**: ventana de 20 ms, rise de 5 ms, y el sesgo que
salía era del **−99,6 %**. Un sesgo enorme y constante se nota; un −99,6 % con
toda la pinta de un número, no. Si fuera a `addReference()` restaría el 99 % de
un punto real. El criterio cabe en una línea —el tramo por encima del umbral
tiene que ser al menos tan largo como la ventana— y el banco lo aplica antes de
que un pulso así llegue al arnés.

#### Lo que el banco además dice

- **El suelo de visibilidad**, `visibilityFloor()`: el rise más corto que el
  análisis ve, y **crece como la cuarta parte de la ventana** (2 ms → 0,5 ms;
  20 ms → 5 ms; 40 ms → 10 ms). Es el número accionable: por debajo de él la
  sonda no mide, y repetir la medición no lo arregla porque el problema no es
  el ruido, es que la ventana es más larga que la envolvente. Y de ahí sale la
  contradicción que hay que nombrar: **la ventana que hace falta para ver un
  attack largo es una ventana que no ve el attack corto**, así que el barrido
  tiene que ir a velocidades que se puedan distinguir.
- **La ventana va en segundos**, no en muestras. Con la ventana en un número fijo
  de muestras, el mismo banco mediría cosas distintas a 44,1 que a 96, y el
  número sería del montaje y no del método. Lejos de la ventana el sesgo es el
  mismo a las dos tasas (verificado a 0,02 % de diferencia).
- **Cuatro "no medido" que son cuatro cosas**, y ninguna es un cero: `NeverRose`
  (la ventana es más larga que la envolvente), `NeverFell` (la grabación se corta
  antes de tiempo), `Empty` (no llegó nada), y *el no confiable* (una espiga que no
  cabe en la ventana). Si fueran el mismo estado, un informe no podría decir
  cuál de los cuatro pasó, que es justo lo que hay que rehacer.

Y el invariante: un mando que el análisis no ve **no se convierte en un sesgo de
cero**. Va a `skipped` con su motivo, y no entra en `reference`. Porque un sesgo
de cero es un sesgo **inventado**, y el arnés lo restaría con toda la confianza.
Un sesgo que falta es un hueco visible; un sesgo de mentira es un número.

**Verde: 1990 comprobaciones en SynthCore** (36 nuevas), g++ `-std=c++20`.

### B4e — el otro lado de "no medido": silencio, no click

Los tests de B4 ya dicen que la tabla vacía es un estado correcto. Faltaba la
**consecuencia**: qué hace un renderer que *consulta* esa tabla cuando está
vacía. Y la respuesta importa, porque hay dos degradaciones posibles y solo una
es aceptable.

Un click no es un sonido raro: es un error que **el motor no tiene motivo para
sospechar**. Si el motor hace un click, el oído oye un fallo del motor y nadie
mira la tabla. Si el motor no suena, el oído oye silencio y la primera pregunta
es «¿está cargado el instrumento?», que es la pregunta correcta. Lo que hay que
evitar no es el ruido: es el ruido **disfrazado de avería**.

**Y no había ningún renderer que consultara la tabla.** No se podía probar, así
que se ha escrito un doble mínimo: un attack-decay de una sola nota que le
pregunta a `S950Calibration` cuánto dura el ataque de un mando. Vive en el
`.inc` y no en un `.h` porque no es código de motor; lo que se prueba no es su
calidad de sonido, que es la de una línea recta, sino el **contrato**.

La forma de medir un click no es discutir si suena mal: es el **salto máximo
entre dos muestras consecutivas**. Una envolvente es continua —pasa por todos
los niveles—, así que el salto entre muestras lo fija la pendiente del ataque
más rápido posible, y en una tabla vacía con relleno a cero sale de la amplitud
entera. Eso se mide.

#### El resultado, que es una línea de código

Dos renderers, el mismo código y la misma tabla, y un `?:` de diferencia. Y el
conteo del desconcierto es **el mismo en los dos**: los dos saben igual de poco.
Lo único que cambia es qué se hace con esa falta.

| política | audio | salto máx | mandos sin resolver |
|---|---|---|---|
| `RequireKnown` | silencio exacto, 0 en todas las muestras | **0,0** | 1 |
| `ZeroFill` | 0 → amplitud entre las muestras 0 y 1 | **1,0** (la amplitud entera) | 1 |

Mil veces de diferencia en el salto, **misma falta de información**. Eso es lo
que significa que el problema no es un dato que falte, sino una decisión que se
toma sobre un dato que falta.

Con la tabla medida, el mismo renderer da un ataque de verdad (0,052 s en el
mando 50 con una curva 0,5 ms–5 s) y su salto sale **por debajo de 0,1**: una
pendiente, no un salto. Los tres casos salen del mismo sitio y con el mismo
criterio, que es lo que hace que compararlos signifique algo.

#### Y un fallo del propio test, que salió al ejecutarlo

El `ZeroFill` de la primera versión rellenaba el vector **entero** con la
amplitud. Su salto máximo era, por tanto, **cero**: un vector constante no tiene
ningún salto. El test que se suponía que demostraba el click estaba midiendo un
click que **no estaba en los datos** — y pasaba en verde por el motivo
equivocado, que es el peor modo de fallo posible en un banco de pruebas.

Lo que faltaba era el silencio *anterior* a la nota: un ataque de cero no es
«un ataque muy corto», es el paso de *estaba sonando a nada* a *está a fondo*. Sin
una muestra de silencio delante no hay salto que medir. Corregido, el click
aparece y el test lo mide.

Eso es lo mismo que pasó en B4d y va a seguir pasando mientras los tests
comprueben la forma de un número en vez de mirar los datos: **un test que
comprueba lo que crees que pasa, pasa**.

#### Lo demás que queda probado

- Las seis curvas degradan igual, y `read()` da desconocido en **todos** los
  mandos de todas ellas, sin una sola excepción.
- Un renderer devuelve **siempre** el número de samples que le han pedido, mida
  o no mida: uno que devuelve menos desplaza el audio y nadie lo oye, que es
  peor que un click.
- Sin `NaN` ni infinitos, que se propagarían a la cadena sin dejar rastro aquí.
- **Un solo punto no es una curva**, y el renderer correcto se calla igual: un
  punto de más no es mejor que ninguno, es la misma ausencia.
- `readOr(..., 0.0)` devuelve un click, que es exactamente por lo que esa función
  se declara peligrosa. Cuatro de las seis curvas exigen valor positivo
  (`positiveOnly`); los dB no, porque 0 dB es un nivel legítimo y no un
  instante — esa distinción se comprueba aquí y no se da por buena.

**Verde: 2018 comprobaciones en SynthCore** (28 nuevas), g++ `-std=c++20` y MSVC.
`S950Calibration.h` intacta, y el test 7 del `.inc` lo comprueba: probar cómo
degrada un renderer **no es** medir la máquina.

## Lo que NO se hace

- **No se portan los gêmeis.** Ni un `EnvelopeEditor` en C++, ni un `KeygroupStrip`, ni un
  `LookAndFeel` de JUCE al paquete. Cada idea entra reescrita, y solo si hay un consumidor.
- **No se arregla el trabajo ajeno.** A fecha de este documento la regresión visual de
  NEURONiK tiene cuatro fotos rojas (`ficha fx`, `cajon fx` y las dos de lienzo entero, que
  las contienen) que son de otra sesión, en curso sobre `FxCatalogue.h` y el catálogo
  generado. El diff está **acotado a la columna de fx**: la ficha de envolventes no tiene un
  solo pixel distinto.

- **Y hay un cambio de pintura propio pendiente de aceptar**: al arreglar el arco del knob
  (A1b) y añadir el bipolar (A8), ocho fotos más de la regresión visual de NEURONiK se
  han puesto rojas, y el diff de cada una es el arco que antes no se pintaba. Las
  referencias están sin regenerar a proposito, porque regenerarlas de golpe aceptaría
  también el trabajo de fx que está en curso.
