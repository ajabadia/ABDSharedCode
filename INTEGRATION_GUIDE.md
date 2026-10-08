# Guía de Integración — ABDSharedCode

> **Propósito:** Librería de código compartido entre proyectos ABDSynths. Módulos reutilizables que se integran via CMake.

---

## Filosofía

- **Zero-copy:** Los proyectos nunca copian el código de ABDSharedCode
- **CMake nativo (módulos C++):** Se integra via `add_subdirectory` o `FetchContent`
- **pnpm workspace (módulos WebUI/JS):** Los módulos JS (MidiKeyboard) se consumen como
  dependencia de workspace pnpm, NO via CMake — ver la sección del módulo más abajo
- **Modular:** Cada módulo es una librería estática independiente (o paquete JS autónomo)
- **Configurable:** Cada proyecto define su propia configuración

---

## Estructura

> Árbol abreviado — los directorios reales incluyen también `AudioComparator`,
> `Certification`, `HardwareDrivers`, `LcdDisplay`, `LutDSP`, `Segmented`,
> `StudioTopology`, `SynthCore`, `WebView2Bridge`, `visualizers` y `docs`.
>
> Los que tienen sección propia más abajo son los que se integran desde fuera;
> `StudioTopology`, `visualizers` y `Certification` todavía no la tienen.

```
ABDSharedCode/
├── CMakeLists.txt              ← Orquestador (solo módulos C++)
├── INTEGRATION_GUIDE.md        ← Este archivo
├── DspCore/                    ← C++/DSP sin JUCE, INTERFACE (header-only)
│   ├── DspCore.h                   ← MathConstants, Range, FloatVectorOperations,
│   │                                  AudioBuffer, ScopedNoDenormals
│   ├── DspMath.h                   ← trascendentes deterministas (sin libm)
│   ├── DspDebug.h                  ← dspAssert
│   ├── DspLeakedObjectDetector.h   ← dspDeclareNonCopyableWithLeakDetector
│   ├── DspResonantFilter.h         ← etapa de paso bajo resonante (con AGC)
│   ├── DspFilterFamily.h           ← EL CONTRATO de la familia de filtros
│   ├── DspFilterTpt.h              ← miembro TPT (paso bajo / alto / banda)
│   ├── DspFilterEquation.h         ← miembro de ecuación (escalera de 4 polos)
│   └── DspMidiMessage.h / DspMidiBuffer.h
├── DspEffects/                 ← Efectos sobre DspCore, INTERFACE (header-only)
│   ├── EffectPolicy.h            ← contrato de política inyectada
│   ├── DspReverb.h               ← Freeverb, port literal de juce::Reverb
│   ├── DspChorus.h / DspDelay.h / DspSaturation.h
│   ├── DspSchroederReverb.h      ← 4 conbs + 3 allpass (API por MARCO)
│   ├── MultiHeadEcho.h            ← eco multi-cabezal, motor de máquina
│   ├── RingMod.h                  ← modulador de anillo
│   ├── characters/                ← TapeColour.h, DiodeBridge.h
│   └── profiles/                  ← Re201Profile.h, ReverbProfile.h
├── SynthCore/                  ← Primitivas DSP y motores C++20, STATIC (sin JUCE)
│   ├── Arpeggiator.h / .cpp    ← Motor de arpegiador determinista (11 modos, sample-accurate, zero-alloc)
│   ├── ControlSequencer.h/.cpp ← Secuenciador analógico de control (1-32 pasos, swing, slew modulable)
│   ├── ModMatrix.h             ← Matriz de modulación genérica desacoplada (header-only, N slots)
│   ├── OscillatorFamily.h      ← Contrato de la familia de osciladores
│   ├── PolyBLEP.h / .cpp       ← Corrección de discontinuidades banda-limitada
│   ├── ADSREnvelope.h / .cpp   ← Generador de envolvente ADSR
│   ├── LFO.h / .cpp            ← Oscilador de baja frecuencia MS2000 (LFO1/2, SquarePlus, tempo sync)
│   ├── LfoAnalog.h / .cpp      ← LFO analógico multionda (7 formas, fade-in delay, slew, audio-rate)
│   └── PortamentoGlide.h / .cpp← Suavizado de portamento y glide
├── AutoUpdater/
│   ├── AutoUpdaterConfig.h     ← Config por proyecto
│   ├── AutoUpdater.h           ← Interfaz pública
│   └── AutoUpdater.cpp         ← Implementación
├── HardwareMidiDetect/
│   ├── HardwareContract.h          ← Contrato de identidad base
│   ├── HardwareContractRegistry.*  ← Parser JSON de contratos (ABDSharedAssets)
│   ├── HardwareMidiDetector.*      ← Detector C++ contract-driven (sin GUI)
│   ├── MidiHardwareBackend.h       ← Interfaz de transporte inyectado por el host
│   ├── JuceHardwareMidiPicker.h    ← Componente WebView2 (pick UX) estilo ABDScope
│   ├── HardwareMidiPickerResourceProvider.* ← Sirve el WebUI embebido + assets
│   └── WebUI/index.html            ← WebUI de detección (queries por contrato)
└── MidiKeyboard/               ← Dos mitades: WebUI/JS (pnpm) y nativa (CMake)
    ├── package.json                ← @abdsynths/midi-keyb (workspace pnpm, NO CMake)
    ├── src/keyboard.js             ← createKeyboard: teclado + ruedas + pedals
    ├── src/keyboard.css            ← Temas/tokens (importa @abdsynths/shared)
    ├── tests/                      ← vitest + jsdom (323 tests)
    └── JuceMidiKeyboardComponent.h ← Capa nativa: monta el WebUI en un plugin
                                     ← target ABDShared::MidiKeyboardCpp
```

---

## Cómo funciona la integración

### Opción 1: Local (desarrollo)

Si tenés `ABDSharedCode` en tu máquina (mismo monorepo), CMake lo usa directo:

```cmake
set(ABDSHARED_CODE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ABDSharedCode")
if(EXISTS "${ABDSHARED_CODE_DIR}/CMakeLists.txt")
    add_subdirectory("${ABDSHARED_CODE_DIR}" "${CMAKE_BINARY_DIR}/ABDSharedCode")
endif()
```

**Ventaja:** Cambios en ABDSharedCode se reflejan al recompilar sin tocar nada.

### Opción 2: FetchContent (CI/CD, otros desarrolladores)

Si `ABDSharedCode` no existe localmente, **FetchContent lo descarga automáticamente de GitHub** durante la configuración de CMake:

```cmake
include(FetchContent)
FetchContent_Declare(
  ABDSharedCode
  GIT_REPOSITORY https://github.com/ajabadia/ABDSharedCode.git
  GIT_TAG        master  # o una versión específica: v1.0.0
)
FetchContent_MakeAvailable(ABDSharedCode)
```

**Qué hace FetchContent:**
1. Clona el repo de GitHub en `build/_deps/ABDSharedCode-src/`
2. Ejecuta `CMakeLists.txt` automáticamente
3. Los targets (`ABDShared::AutoUpdater`) quedan disponibles

**Cuándo usarlo:**
- CI/CD (GitHub Actions, Azure Pipelines)
- Otro desarrollador que no tiene el monorepo completo
- Builds en máquinas limpias

### Opción 3: Git Submodule (avanzado)

Si preferís control manual de la versión:

```bash
git submodule add https://github.com/ajabadia/ABDSharedCode.git
```

Luego en CMakeLists.txt:
```cmake
add_subdirectory(ABDSharedCode)
```

---

## Módulo: AutoUpdater

Consulta la API de GitHub Releases para detectar actualizaciones disponibles.

### Pasos para integrar

#### 1. CMakeLists.txt del proyecto

Antes de `juce_add_plugin()`:

```cmake
# --- ABDSharedCode Integration ---
set(ABDSHARED_CODE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ABDSharedCode")
if(EXISTS "${ABDSHARED_CODE_DIR}/CMakeLists.txt")
    message(STATUS "ABDSharedCode: Using local at ${ABDSHARED_CODE_DIR}")
    add_subdirectory("${ABDSHARED_CODE_DIR}" "${CMAKE_BINARY_DIR}/ABDSharedCode")
else()
    # Fallback: FetchContent para CI/CD
    include(FetchContent)
    FetchContent_Declare(
      ABDSharedCode
      GIT_REPOSITORY https://github.com/ajabadia/ABDSharedCode.git
      GIT_TAG        master
    )
    FetchContent_MakeAvailable(ABDSharedCode)
endif()
```

En `target_link_libraries()`:

```cmake
target_link_libraries(TuPlugin PRIVATE ABDShared::AutoUpdater)
```

#### 2. Archivo de configuración por proyecto

Crear `Source/Config/AutoUpdaterConfig.h` en el proyecto:

```cpp
#pragma once
#include <AutoUpdater/AutoUpdaterConfig.h>

namespace TuProyecto
{

inline ABDShared::AutoUpdaterConfig getAutoUpdaterConfig()
{
    ABDShared::AutoUpdaterConfig cfg;
    cfg.currentVersion = "1.0.0";  // Leer de BuildVersion.h
    cfg.repoOwner = "ajabadia";
    cfg.repoName = "TuRepo";
    cfg.appName = "TuProyecto";
    cfg.userAgent = "TuProyecto-AutoUpdater/1.0";
    cfg.assetNames.windows = "TuProyecto_Setup_x64.exe";
    cfg.assetNames.macos = "TuProyecto_macos_universal.dmg";
    cfg.assetNames.linux = "TuProyecto_linux_x86_64.AppImage";
    cfg.logCallback = [](const juce::String& msg) {
        juce::Logger::writeToLog(msg);
    };
    return cfg;
}

} // namespace TuProyecto
```

#### 3. Uso en PluginProcessor

```cpp
#include "Config/AutoUpdaterConfig.h"

// En miembro de la clase:
std::unique_ptr<ABDShared::AutoUpdater> autoUpdater;

// En constructor:
autoUpdater = std::make_unique<ABDShared::AutoUpdater>(
    TuProyecto::getAutoUpdaterConfig());

autoUpdater->setUpdateCallback(
    [](const ABDShared::AutoUpdater::UpdateInfo& info, bool manual) {
        // Mostrar diálogo al usuario
    });
```

---

## Agregar nuevos módulos

1. Crear carpeta en `ABDSharedCode/NombreModulo/`
2. Agregar `add_library()` en `CMakeLists.txt` raíz
3. Crear alias `ABDShared::NombreModulo`
4. Documentar en esta guía

---

## Módulo: DspCore + DspEffects (C++/DSP, sin JUCE)

> **Este módulo se integra de otra manera que los demás, y conviene leerlo entero
> antes de tocarlo.** No lleva configuración por proyecto ni binarios: son
> cabeceras. Y tiene una regla que los otros módulos no tienen: **la paridad bit a
> bit con un efecto original no se puede comprobar aquí**, porque el módulo no
> tiene el original a mano, así que **la comprobación vive en el consumidor**.

### Qué hay dentro

**`DspCore`** — el sustrato. Portado de `juce_core` / `juce_audio_basics`, sin
JUCE: `DspCore.h` (2566 líneas: `MathConstants`, `Range`, `FloatVectorOperations`,
`AudioBuffer`, `ScopedNoDenormals`, …), `DspMath.h`, `DspDebug.h`,
`DspLeakedObjectDetector.h`, `DspMidiMessage.h`, `DspMidiBuffer.h`, y los tres
ficheros de **la familia de filtros** (`DspFilterFamily.h` con el contrato,
`DspFilterTpt.h` y `DspFilterEquation.h` con sus dos miembros del sustrato; el
tercero, el de la tabla medida, vive en `LutDSP/`). Ver *La familia de filtros*
más abajo. Su familia hermana —la de **osciladores**— no vive aquí sino en
`SynthCore/`: los primitivos de oscilador (`PolyBLEP`, `DSPUtils`) ya estaban en
esa carpeta y un oscilador no necesita el sustrato que un filtro sí usa. Las dos
son gemelas en contrato, no en módulo. Ver *La familia de osciladores*.

**`DspEffects`** — cuatro clases de cosa, y distinguirlas es medio de usar bien el
módulo:

| | Qué es | Ficheros |
|---|---|---|
| **Motor genérico** | El efecto en sí, sin saber de qué dispositivo viene | `DspReverb.h` (Freeverb, port literal de `juce::Reverb`), `DspChorus.h`, `DspDelay.h`, `DspSaturation.h`, `DspSchroederReverb.h` |
| **Motor de máquina** | Emulación de hardware, parametrizada por un perfil inyectado | `MultiHeadEcho.h`, `RingMod.h` (+ `EffectPolicy.h`, el contrato) |
| **Etapa de carácter** | La no linealidad con estado, inyectada | `characters/TapeColour.h`, `characters/DiodeBridge.h` |
| **Perfil** | Los números de un dispositivo, como dato | `profiles/Re201Profile.h` (12 modos del RE-201), `profiles/ReverbProfile.h` (10 reverbs del DeepMind 12) |

### Las dos reglas que deciden dónde vive cada cosa

**1. El módulo es JUCE-free y libm-free.** Cualquier cosa que necesite `juce::`
o las funciones de la plataforma se queda en el consumidor. Lo de la libm es
menos obvio y más importante: las trascendentes van por `DspCore/DspMath.h`
(`sin`, `cos`, `atan`, `atanh`, `log2`, `exp2`, `pow`, `pow2i`, `tanh`,
`floorToInt`, `wrapPhase`), que solo usan
`+ - * /`. Es lo que sostiene que el mismo código dé el mismo audio en nativo y
en WASM; si un motor llama a `std::sin` de la plataforma, la paridad se rompe
entre plataformas sin que nadie lo vea.

**`wrapPhase` es el envuelto de fase de los LFOs, y hay que usarlo.** Los
acumuladores de fase de `DspChorus` y `RingMod` no pueden usar el
`if (fase >= 2*pi) fase -= 2*pi` de toda la vida, por dos razones que están
medidas:

- **Un `while` que reste de una en una tampoco vale.** Encuadra la fase, pero su
  coste es proporcional al número de vueltas: con un rate de 1e9 Hz son 22 676
  iteraciones POR MUESTRA, y con rate infinito el bucle no termina nunca, en el
  hilo de audio. `wrapPhase` resta las vueltas enteras de una vez, así que el
  coste no depende de la entrada.
- **Con `rate >= sampleRate` el `if` no envuelve nada.** Una sola resta solo
  envuelve mientras la incremento sea menor que 2*pi. Pasado eso, la fase se va
  sin limite y `sin` recibe argumentos donde la reducción ya no vale.

Y un detalle que se midió: `wrapPhase` es **bit a bit el `if` de una sola resta en
todo el rango de audio** (rate 0..40 Hz a 44.1/48/96/192 kHz, y el guion exacto
de la paridad congelada del chorus), que es lo que permite arreglar el `if` sin
que se mueva ni una muestra de lo que ya sonaba. Solo difiere donde el `if` ya
era incorrecto.

**NaN e infinito tienen que SALIR como NaN, en las doce funciones.** No es
cosmetico: un parámetro en NaN (un host que lo mande, un 0/0 en un mapeo) es lo
que te avisa de que algo se ha roto, y en un lazo de realimentación no se va
nunca. Antes `sin` y `cos` se lo tragaban (`sin(NaN)` daba 0 y `cos(NaN)` daba 1,
leyendo los bits de la mantiza del NaN) y el motor seguía sonando normal con la
modulación muerta, mientras `atan`, `tan`, `tanh` y `log2` sí propagaban. Si
añades una función a esta familia, que propague NaN como las demás.

**2. El motor expone LA MUESTRA; la política se queda en el consumidor.** En el
motor no hay suavizado de parámetros, ni mapeo de controles, ni mezcla wet/dry,
ni denormales a nivel de bloque. Eso no es descuido: es lo que permite que el
mismo motor sirva a dos productos con políticas distintas.

**La excepción que hay que conocer: por MARCO, no por canal.** `SchroederReverb`
tiene API por marco estéreo (`processFrame(inL, inR, outL, outR)`) en vez de
por muestra, y no es inconstencia sino que su pre-retardo es MONO: escribe una
vez por muestra con la media de L y R. Una API por canal lo escribiría dos veces
y lo desplazaría medio bloque, que es un cambio de sonido. Un marco sigue siendo
"la muestra", así que la regla se respeta; pero si un motor tiene estado
*compartido entre canales*, su API tiene que ser por marco.

**3. Lo que se encontró midiendo, y por qué estaba mal de verdad.** Los filtros
extraídos de ABDEep y ABDJUNiO nunca se habían pasado por un medidor. Se
midieron, y tres cosas estaban rotas de verdad. Las tres están arregladas y con
test de regresión; el resto de lo que se encontró se documentó como lo que es,
porque el módulo no puede cambiar el sonido de unos efectos ya publicados.

**Un motor de máquina se llama por CANAL, y su estado por muestra va en
`advance()`.** En `MultiHeadEcho` el pasabajos de tono y la escritura al tanque
estaban dentro de `processSample`, o sea por canal: en estéreo se ejecutaban dos
veces por muestra, el filtro corría **al doble** de la frecuencia para la que se
había calculado su coeficiente, y la segunda llamada pisaba la escritura de la
primera. Medido: la diferencia entre mono y estéreo-L era `5.8e-03`; con el
arreglo (acumular en `processSample`, aplicar en `advance`) es **exactamente
cero**. En mono la media es el propio valor, así que no cambia ni un bit.

**El reparto por cabezal va ENCIMA del desvío L/R, no en su lugar.** `headR`
sumaba `lectura * headRightScale[h]` y se comía el `gainPerHead` (1/activos).
Con un cabezal salía 0.95 por casualidad (1/1 = 1); con dos o tres, el canal
derecho salía **1.8 a 2.7 veces** el izquierdo. Medido antes: 1.80, 1.80, 2.70,
2.70, 2.70, 2.70. Medido después: 0.90–0.95 en los seis casos, que es lo que
dice el perfil.

**Un techo "defensivo" que duplica un filtro es peor que no tenerlo.** En
`SchroederReverb`, `maxCombSeconds` se aplicaba DESPUÉS del desplazamiento
anti-periodicidad `i * 7`, así que podía dejar dos conbs en la misma longitud.
Con un techo de 0.040 s a 48 kHz los cuatro acababan en 1920 muestras: el pico
de la respuesta al impulso subía de 0.1625 a **0.4875** (3×, porque disparaban
tres a la vez) con un eco de aleteo encima. Con 0.010 s colapsaban los cuatro a
480 y metían un zumbido de 100 Hz. Ahora el techo va antes, con lo que las
cuatro longitudes son siempre distintas: los conb ya van en orden y sumar `i * 7`
las deja estrictamente crecientes. **Es un no-op bit a bit para todo consumidor
actual**, porque con el techo por defecto de 0.25 s el recorte no llega a
dispararse ni de lejos (el conb más largo posible con `roomSize = 1.0` son
0.0621 s); solo se nota con techo ≥ 0.07 s. Aun así está arreglado, porque el
único llamador pasa un solo argumento y el defecto estaba a un `prepare` de
distancia de aparecer.

**Y lo que se midió y NO era un fallo, para que no se "arregle" luego.** El
pre-retardo entrega exactamente lo pedido con el factor 0.2 del original
(480/960/1440/1920 muestras a 48 kHz, sin desvío). `invertLeft` niega
**exactamente** el canal izquierdo, con y sin pre-retardo, y R no se mueve ni un
bit. Los dos canales están limpios: entrada solo por L deja R en 0.0 exacto. El
pre-retardo es mono a propósito, así que **entrada solo por L sale por R a
0.473** (unos −6.5 dB de cruce) — consequence directa de sumar `(L+R)/2` y
repartirlo a los dos conbs, no un descuido. La cola congelada al apagar el
pre-retardo suena limpia: cuando vale 0 la línea no se escribe nunca, así que no
puede guardar audio viejo.

**La lección de las dos mediciones que salieron falsas.** Comparar una reverb (o un
eco) **ya calentada** contra una recién construida no mide lo que parece: mide
que la primera tenga cola. Dio un `invertLeft` "tocando R" con 0.223 de error
que era puro estado residual, y en el eco dio "picos tempranos falsos" a 0.143 s
que el máximo global de la cola demostró que no existían. **Para medir audio,
usa dos instancias frías, o descarta los primeros segundos.**

**4. Lo que hay en el módulo y NADIE usa, medido y con lista blanca.** Una
extracción a medias es casi invisible: el fichero está escrito, está
documentado, tiene sus propios tests, compila — y no lo enlaza ningún producto.
No sale en ningún build porque no rompe nada. Hoy son **17 de 115 fuentes**, y
todas con motivo escrito:

- **Los 6 ficheros del "motor de máquina" de `DspEffects`** (`MultiHeadEcho`,
  `RingMod`, `EffectPolicy`, las dos etapas de carácter y el perfil RE-201): los
  usa solo su propio test. Es la partida abierta más grande del módulo, y la
  razón por la que en `MultiHeadEcho` se pudieron corregir tres defectos de
  verdad: **sin consumidor no hay paridad que romper.**
- **`SynthCore/ModMatrix.h`**: el resto de `SynthCore` lo consume ABDMS2000
  mediante shims; este no tiene shim y el `ModMatrix` que usan ABDEep, ABDCZ101
  y ABDNeural es el suyo.
- **`LcdDisplay` (3 ficheros): basura, no trabajo pendiente.** La migración al
  WebUI terminó, pero los ficheros y su target se quedaron. Ni siquiera
  `LcdDisplayProbe.cpp` está en un target de compilación, así que no puede
  romperse. Propuesta: borrarlo.
- **`Segmented` (2 ficheros): trabajo terminado a medias.** Tiene target, el
  probe se compila y se documenta como puerto nativo; solo falta que un producto
  lo enlace.
- **5 arranques y tests propios**, fuera de producto por diseño.

```bash
python tools/audit_unconsumed_sources.py --check   # 0 = todo huérfano está justificado
```

Devuelve 1 en cuanto aparece un huérfano que no esté en la lista blanca del
script, o una entrada que ya no hace falta. La herramienta **resuelve** cada
`#include` y cada ruta de CMake a un path real en vez de buscar nombres, porque
buscar nombres miente: un `#include` relativo en el consumidor gana al include
path del módulo y compila una copia privada mientras el módulo compartido parece
enlazado. Los detalles, y las tres trampas, en
[`docs/fuentes-sin-consumidor.md`](docs/fuentes-sin-consumidor.md).

##### La línea base: cómo el audit es puerta sin mentir

En local el comando anterior sale **0**: los consumidores existen en el disco. En
CI sale **1** con 55 fuentes, y no es un problema de este repo: en la foto de la
suite fijada por SHA, el trabajo que las consume está escrito pero **sin
commitear** en los repos hermanos. Un rojo así no se arregla arreglando este
repo, y un CI rojo el primer día es un CI que nadie mira.

La salida es un **trinquete** en `tools/audit-baseline.json`, con las 55 fuentes
con nombre:

| Qué pasa | Código |
|---|---|
| Fuente huérfana que **no** está en la línea base → hay que consumirla o documentarla en `ALLOWLIST` | **1** |
| Entrada de la línea base que **ya no** es huérfana → hay que podarla | **1** |
| Lista blanca obsoleta, o test declarado que el CMake ya no compila | **1** |
| Solo hay lo que ya estaba en la línea base | **0** |
| No hay repos hermanos clonados al lado → no se ha podido comprobar | **3** |

La segunda regla es la que lo hace un trinquete y no una lista de excepciones:
sin ella, la línea base sería un escudo permanente al que se le pueden añadir
todos los hallazgos que salgan. Con ella, cuando el trabajo se commitee en los
hermanos, subir los SHA del workflow hace **caer** el número de entradas, y lo
único que queda por hacer es quitar las que sobren:

```bash
# CI lo hace así, con el flag:
python tools/audit_unconsumed_sources.py --check --baseline tools/audit-baseline.json

# Para regenerar la línea base, cuando los SHA ya están subidos:
python tools/audit_unconsumed_sources.py --write-baseline
```

**`--baseline` es opt-in a propósito.** En local los repos hermanos tienen ese
trabajo sin commitear, así que la línea base describiría una foto que aquí no es
la realidad, y el script saldría en rojo pidiendo podar 55 entradas que en local
sí hacen falta. Sin el flag el script aplica el modo estricto: sale 1 ante
cualquier huérfana que no esté en `ALLOWLIST`.

El JSON lleva además una clave `foto` con el SHA exacto de cada hermano en el
disco donde se midió, y `--write-baseline` la escribe sola. Sin ella, subir un
SHA en el workflow no obligaría a nada: el trinquete compara nombres, y los
nombres no cambian al mover el suelo. `tools/verificar_foto.py` es el cuarto
trabajo del CI que vigila eso; ver *El trabajo `foto`* más abajo.

### Cómo integrarlo

**1. CMake.** Igual que cualquier módulo (ver *Cómo funciona la integración*,
arriba), con un apunte: los tests standalone de los módulos se desactivan en el
build del plugin, porque los efectos se prueban desde el propio consumidor. Hay uno
por módulo, y cada uno tiene su interruptor:

```cmake
set(ABDSHAREDCODE_BUILD_DSPCORE_TESTS     OFF)
set(ABDSHAREDCODE_BUILD_DSPEFFECTS_TESTS  OFF)
set(ABDSHAREDCODE_BUILD_SYNTHCORE_TESTS   OFF)   # si se usa SynthCore
set(ABDSHAREDCODE_BUILD_LUTDSP_TESTS      OFF)   # si se usa LutDSP

# ... tras add_subdirectory / FetchContent_MakeAvailable ...

target_link_libraries(MiProyecto PRIVATE
    ABDShared::DspEffects     # y ABDShared::DspCore si se usa el sustrato suelto
)
```

**2. Elige motor, perfil y etapa, y mézclalos en el consumidor.** El envoltorio
de producto es pequeño a propósito. Ejemplo real (`ABDEep/Source/DSP/FX/FXSimpleReverb.h`,
que quedó en 241 líneas tras la extracción; antes eran 475 en dos ficheros):

```cpp
class FXSimpleReverb : public FXBase
{
    void prepare (double sr, int block) override
    {
        engine.prepare (std::max (1.0, sr));
        engine.setInvertLeft (variant->invertLeft);   // el perfil, no los números
        engine.setDecay      (decay);                 // el cache de los mandos
        engine.setDamping    (damping);
        engine.setDiffusion  (diffusion);
        engine.setGeometry   (roomSize, preDelayTime);
    }

    const abd::dsp::ReverbProfile::Variant* variant;  // qué variante del DeepMind
    abd::dsp::SchroederReverb engine;                  // la máquina
};
```

Lo que se queda en el envoltorio es exactamente lo que NO es del módulo: el
reparto de los doce mandos normalizados a controles del motor (que es el orden
del hardware y cambia por variante), y el wet/dry, que lo mezcla `FXSlot`.

**3. Escribe el test de paridad en tu proyecto.** Ver la sección siguiente.

### Política inyectada: cómo se emula un dispositivo

Para hardware emulado el motor es una **plantilla** sobre un perfil (datos) y una
etapa (carácter). Un motor, muchos dispositivos; un dispositivo, una tabla.

```cpp
dsp::MultiHeadEcho<dsp::Re201Profile, dsp::TapeColour> spaceEcho;
```

Añadir una máquina nueva es **un fichero en `profiles/`**, no otra clase con el
95 % del código duplicado. Y el perfil se puede cambiar de fuente sin tocar el
motor: `Re201Profile` se copió de `JunoTapeEcho` (ABDJUNiO601) **y no** del
`FXSpaceEchoRE201` de ABDEep, porque el de ABDEep está roto (más abajo). Ese
cambio de fuente fue sustituir un `struct`, que es justo lo que se gana con que
el perfil sea dato.

Al escribir un perfil nuevo, dos cosas que se olvidan:

- Un modo dice **QUÉ** cabezales suenan, no con qué ganancia. El reparto
  (1/activos) es de la máquina y lo calcula el motor, porque un RE-201 no tiene
  tabla de ganancias: tiene un selector.
- La etapa de carácter **no** lleva el wow/flutter. Eso es del transporte, o
  sea del motor: si lo metes en la etapa, dos motores distintos con la misma
  etapa tendrán distinto barrido.

### La paridad: por qué en el consumidor, y cómo se hace

El módulo no tiene el efecto original a mano, así que **no puede** prometer 0 ulps
con nadie. El consumidor sí lo tenía antes de la migración, así que es quien
compara. Es el mismo reparto que hay entre `DspReverb` y `juce::Reverb` en
ABDNeural/Tests.

Vale la pena hacerlo en **dos capas**, porque con una sola hay un agujero real: si
comparas el código nuevo contra una copia del viejo y **los dos** se rompen a la
vez, el test pasa.

1. **Paridad estructural** — copia literal del kernel pre-extracción, muestra a
   muestra, con 0 ulps. Referencia:
   `ABDEep/Source/DSP/FX/FXUnitTests_ReverbParity.cpp` (el `struct
   FrozenSchroederReverb` es el kernel viejo, sin tocar).
2. **Valores congelados** — un hash FNV-1a por variante, calculado **sobre la
   copia y no sobre el código nuevo**. Esto no es un detalle: si el hash lo
   generase la implementación nueva, regenerarlo sería tautología y el test
   volvería a pasar sin comprobar nada.

**Cuando la paridad falla, el fallo es información, no un obstáculo.** El
barrido de mandos falló con miles de ULPS donde los valores de fábrica pasaban
limpios, y la causa fue que `rebuild()` se fiaba de que `AudioBuffer::setSize`
ya había puesto a cero. **Con el mismo número de muestras `setSize` no toca el
contenido viejo**; el original llamaba a `clear()` después. Solo se notaba al
mover un mando que no cambia el tamaño de los conbs, así que sonaba casi igual y
solo aparecía en el test. Ese tipo de fallo es el que la paridad compra: no son
errores que se oigan, son errores que no se oyen hasta que es tarde.

### Reglas que se aprendieron por el camino

No son estilo: cada una es un descuido que alguien cometió y que sale carísimo.
Todas están ahora documentadas en el propio fichero y fijadas con un test.

| Regla | Por qué |
|---|---|
| Si el estado se comparte entre canales, API **por marco** | Un pre-retardo mono escrito por canal se desplaza medio bloque. Es un cambio de sonido, no un bug de precisión. |
| `clear()` **después** de `setSize`, siempre | `setSize` con el mismo número de muestras deja el contenido viejo. Y con `avoidReallocating` no hay garantía al cambiar de tamaño tampoco. |
| Las rarezas del original se reproducen **tal cual**, no se corrigen de paso | El pre-retardo de la reverb del DeepMind es `segundos × sampleRate × **0.2**`: pedir 100 ms retrasa 20 ms. Es un descuido del efecto publicado, y "arreglarlo" cambia el sonido de diez efectos. |
| Los comportamientos raros se conservan **a propósito** y se documentan | "Reverse" niega **solo el canal izquierdo**: no es inversión de fase, es un efecto Haas. Corregirlo cambia un efecto ya vendido. |
| Redimensionar que borre la cola se documenta, no se arregla de paso | Mover el mando de tamaño en esa reverb deja la reverb en silencio a propósito (así funciona desde que se publicó). Arreglarlo es un cambio de sonido, y va en su propio commit. |
| Un perfil necesita un test de guardia, porque es lo único que cambia sin romper la compilación | Una fila con un número mal no peta: suena a otra cosa. Cada perfil tiene uno (`testRe201Profile`, `testReverbProfile`) que fija ids, nombres y valores de fábrica contra el original. |

### Inventario: qué está extraído y qué no

De los 44 efectos de `ABDEep/Source/DSP/FX` (47 cabeceras menos `FXBase`, `FXEngine`
y `FXSlot`; ~9 900 líneas de `.cpp`):

| Estado | Efectos |
|---|---|
| **Extraído** | Los 10 reverbs del DeepMind 12 (ids 1-6, 22, 26-28) → `SchroederReverb` + `ReverbProfile`. Los 3 híbridos (23-25) lo heredan a través de `FXSimpleReverb(1)`. |
| **Candidato directo** | `RingMod` (id 38): **el motor compartido ya existe y ABDEep tiene el suyo propio, sin usarlo**. Es el coste más barato de la lista. |
| **Cluster con duplicación medida** | Los 4 delays de cinta (`AnalogTape`, `Shimmer`, `Ducking`, `Spectral`): 40-46 % de código compartido, misma línea de retardo + realimentación + mezcla, difieren solo en la etapa de carácter. Es la forma exacta de `MultiHeadEcho`. |
| **Cluster con duplicación medida** | `Chorus` / `Flanger` / `Chorus-D`: 52 % Chorus↔Flanger, mismos parámetros con otro rango. `DspChorus` ya existe. |
| **Sin solape** | 19 efectos de distortion, dynamics, filter, delay y pitch: aportan casi todo por su cuenta. |
| **Reverb restante** | `FDNReverb` (52) y `ZitaReverb` (53) son algoritmos distintos; no comparten nada con `SchroederReverb`. |
| **Emulación de máquina** | Los 7 de "space" (`Space Echo`, `Rotary`, `Edison`, `Pattern Freeze`, `MB Vocoder`, `Nimbus`, `Tree Monster`): son los que más ganarían con política inyectada, y los más caros. |

**Aviso sobre `FXSpaceEchoRE201` (id 39).** Tiene la línea de retardo **muerta**:
usa `kMaxDelay = 70560` como máscara de bits sin que sea potencia de dos, y como
sus cinco bits bajos son cero, `writePos = (writePos + 1) & delayMask` se queda
en 0 para siempre. La línea no avanza nunca, el eco no existe, y el efecto suena
a seca + tanque. El único test que tenía comprobaba el nombre y el número de
parámetros, nunca el audio. **No lo tomes como referencia**: el RE-201 bueno es
`JunoTapeEcho` (ABDJUNiO601), que es de donde salió `Re201Profile`.

**Aviso sobre el chorus BBD.** Había **cuatro** implementaciones en la suite y
solo dos se usaban. Ahora hay **una** en el módulo compartido, y las otras
tres están fuera de juego:

| | Líneas | Estado | Nota |
|---|---|---|---|
| **`DspEffects/JunoBBD.h`** (+ `JunoBbdProfile.h` + `characters/BbdNoise.h`) | — | **Vivo** | El motor. Política inyectada, perfiles J60/J106, paridad a 0 ulps contra la referencia congelada de JUNiO601 |
| `ABDJUNiO601/Source/Synth/ChorusBBD` (+ `BBDFilter.h`) | 681 | JunoEngine | **La fuente de la que se extrajo**. Prefiltrado TPT SVF, saturación en la línea, rizado de red, clics |
| `ABDEep/FXRolandBBDChorus` (slot 36) | — | **Vivo desde 2026-09-28** | Antes tenía su propia simplificación (371 líneas, sin filtro de reconstrucción, sin saturación en la línea, sin clics, y con el mando de velocidad muerto). Ahora es un envoltorio sobre este motor |
| ~~`LutDSP/JunoBBD.h`~~ | 221 | **Movido al ático** | `_Deprecados/LutDSP-JunoBBD.h`, con un `#error` que rompe si alguien lo incluye. Ver abajo |
| ~~`ABDJUNiO601/Source/Core/JunoBBD.h`~~ | 158 | **Borrado** | No lo incluía nadie; cuarto modelo distinto de la misma máquina |
| ~~`ABDAudioLab/src/dsp/JunoBBD.h`~~ | 17 | **Borrado** | Shim que reexportaba el borrador de `LutDSP`. Su único consumidor era un test, y ese test ahora cubre el motor de `DspEffects` |

**El slot 36 de ABDEep, y por qué aquí NO hay un test de paridad.** Es el caso
contrario a JUNiO601, y la diferencia es la razón de que viva en el consumidor.
JUNiO601 extrajo su propio coro, así que el sonido tenía que quedarse igual y
lo que hace falta es un test de 0 ulps. ABDEep tenía una *simplificación*: un
coro con las líneas de retardo y el Hermite, pero sin el filtro de
reconstrucción, sin la saturación en la línea, sin clics, sin fuga, con las dos
líneas alimentadas por la mono sumada y con el ruido inyectado en signo opuesto.
Al sustituirlo por la máquina **el sonido tenía que cambiar**, y un test de
paridad habría sido un test que obliga a no cambiar.

Lo que hay en su lugar es `ABDEep/Source/DSP/FX/FXUnitTests_BbdChorus.cpp`:
una copia congelada del motor anterior dentro del propio test, y preguntas con
margen. Los números de referencia se midieron **antes** del cambio:

- nivel +0.04 dB en RMS, barrido idéntico (la banda modulada mide lo mismo);
- −5.4 dB a 12 kHz, +1.7 a +3.0 dB alrededor de 1 kHz (el filtro de
  reconstrucción del chip): menos aire, algo más de cuerpo;
- el `tanh` de salida se va, así que el pico sube de 0.58 a 0.69 y una señal
  sostenida ya no se limita en 0.97;
- el ruido pasa de un siseo continuo a picos de clic, con el **mismo** nivel
  integrado (−61.4 → −61.5 dBFS en 17 s) y la correlación L/R de −0.79 a +0.29;
- el salto al cambiar de modo baja de 0.82 a 0.13, porque el motor suprime los
  clics y el crossfade de 5 ms sobra.

Y dos defectos que se arreglan de paso porque el reparto de mandos no puede
ignorarlos: el **mando de velocidad estaba muerto** (se guardaba y no se leía en
el bucle; la salida era idéntica bit a bit con el mando en 0.0 y en 1.0), y el
**"Off" no era un bypass** (funde a cero el mojado pero la seca seguía por
`× 0.863` y por el `tanh`: −1.3 dB y recorte).

**Y por qué J106.** Los dos perfiles dan hoy **0 de 4096 muestras distintas**,
porque `JunoBbdJ60Profile` *es* `JunoBbdJ106Profile`: en JUNiO601 los dos modelos
reciben el mismo valor por defecto y el `ChorusModel` del original no se lee
nunca. La elección es gratis hoy. Se pone **J106** porque es la tabla calibrada
que existe en la suite (JUNiO601 es un Juno-106) y porque ABDEep no tiene un
Juno-60 en ninguna parte. El día que haya una calibración real de J60, es
cambiar un parámetro de plantilla.

**Por qué el borrador de `LutDSP` nunca se adoptó**, que es lo que hace que
esté en el ático y no simplemente borrado:

1. **Estaba en el módulo equivocado.** `LutDSP` es JUCE-free (namespace
   `abd::lutdsp`, cabeceras INTERFACE, cero fuentes compiladas). Un BBD no es
   una LUT, y para funcionar este borrador tenía que incluir `<juce_audio_basics>`:
   estaba rompiendo la regla del módulo entero para poder existir.
2. **No era el BBD bueno.** Le faltan las tres cosas que hacen que un coro BBD
   suene a BBD: la **tolerancia de reloj** de las dos líneas (±1.5%, que es el
   batido característico), la **pérdida de transferencia de carga** (que hace
   perder ganancia cuando el reloj va más rápido) y los **clics** de depósito
   vaciado. Sin ellas es un coro digital con un retardo.
3. **Sus números no son los del panel.** 0.4 / 0.6 / 1.0 Hz frente a los 0.513
   / 0.78 / 7.7 Hz del Juno. Un motor con otros números no puede sustituir al
   otro sin cambiar el sonido de un efecto ya publicado.

Y sobre lo que decía esta misma guía antes de hacerlo ("habrá que llevar
`std::sin` / `std::exp` / `std::tanh` a `DspMath`, **y eso cambia bits**"):
**no hizo falta tocar `DspMath`**, porque las dos que faltaban se sale con
identidades exactas y no con polinomios nuevos que mantener —
`tan(x) = sin(x)/cos(x)` y `exp(y) = exp2(y·log2e)`, con un error relativo
medido de ≤ 6.6e-07 y ≤ 6.1e-07 en los rangos que usa el motor. El margen de
paridad sí es de 0 ulps, y lo fija `ABDJUNiO601/Tests/ChorusBBDParityTest.cpp`
contra `Source/DSP/Effects/ChorusBBDFrozen.h`, que es el original con las mismas
transcendentales. Lo que sí costó, y son tres cosas que un test de "suena igual"
no caza:

1. **El 2·π del LFO es el de 32 bits.** El original calcula el incremento de fase
   con `kPi` (float) por 2, así que su 2·π es `6.2831854820251465`, no el exacto
   en doble. La primera versión del motor usaba el 2·π exacto "porque el original
   usaba `twoPi` en doble", que era una suposición y no una medición. La
   diferencia es de 2.8e-08 relativas: el LFO se desincroniza poco a poco hasta
   que el retardo se va 1 ulp. Ahora `kTwoPiDouble` sale de `kPi`.
2. **El LFO acumula la fase en `double` y no puede usar `wrapPhase`**, que es
   `float` y truncaría la fase a 32 bits cada muestra.
3. **La calibración va ANTES de `prepare()`, no después.** El original lee sus
   números de fábrica (retardos, ganancias, frecuencia de reconstrucción)
   dentro de `prepare()`, que es cuando el host ya ha cargado los ajustes. En el
   motor, en cambio, los `set...` son posteriores a `prepare()`. Escribiendo el
   guion de calibración al revés, el motor queda con los 8000 Hz de la J106 y la
   referencia con los 9661 Hz del original, y los dos lados comparan filtros
   distintos: el coeficiente del biquad sale 0.5774 contra 0.7326. **Eso estaba
   tapando el punto 1**: con el corte mal, el fallo aparecía en la muestra 85;
   arreglado el corte, el error de fase aparece en la 18937 si lo reintroduces.

### El arnés que mide `DspMath` en WASM y en nativo, para que ninguna trascendental nueva entre sin medir

`DspCoreTests.cpp` responde a "¿las trascendentales hacen lo que dicen?". No a
las dos preguntas que solo se ven mirando el binario, y que son las que
importan cuando el mismo código corre en un DAW y en un navegador:

- **¿Siguen siendo las MISMAS?** Un `sin` correcto en x86 y un `sin` distinto
  en WASM no son dos implementaciones: son dos instrumentos que suenan distinto,
  y el producto que compila a WASM se lleva el peor.
- **¿Cuánto cuestan?** Una trascendental correcta pero lentísima se cuela igual
  de fácil que una incorrecta, y el síntoma —una voz que va bien en el DAW y se
  arrastra en el navegador— no señala al culpable.

Y hay una tercera, que es la que este arnés existe sobre todo:

- **¿Ha entrado alguna nueva sin medir?** Alguien añade un `tan` a `DspMath.h`,
  lo usa en una voz, y entra sin ruido, sin warning y sin crash. Nadie sabe que
  hay una función más, ni cuánto cuesta, ni si es la misma en los dos
  compiladores.

#### Las cuatro puertas

Se ejecuta con `python tools/run_dsp_math_harness.py`. Los códigos de salida no
son intercambiables: **0** todo pasa y la pata WASM se ha ejecutado de verdad,
**1** alguna puerta falla, y **3** la pata WASM no se ha podido ejecutar porque
falta `clang` o `wasm-ld`. El 3 existe para que un CI no confunda "nadie ha
comprobado la paridad" con "la paridad está bien". Un 0 sin haber corrido WASM
sería mentira.

| | Puerta | Qué atrapa |
|---|---|---|
| **A** | Cobertura | Lee `DspMath.h`, saca las declaraciones de nivel de namespace y las compara con el manifiesto del arnés. Una función nueva sin medir sale aquí. No necesita compilador. |
| **B** | Símbolos | Compila a objeto y mira los símbolos **indefinidos** con `nm`. El módulo tiene que salir con cero. |
| **C** | Informe | Corre el arnés nativo y enseña el coste. Avisa, no falla. |
| **D** | Paridad | Compila a `--target=wasm32 -nostdlib`, lo corre en `node` y compara los **bits** con los del nativo, función a función y caso a caso. |

La puerta B es la que evita el dolor de verdad, y merece la pena entender por
qué. **En nativo, llamar a `expf` de libm compila y funciona.** Da un resultado
razonable, el test de sonido pasa, y nadie se entera hasta que alguien compila a
WASM y descubre que allí no hay biblioteca. La puerta B avisa en nativo, donde
todavía se puede arreglar. Y la puerta D lo remata por el otro lado: en
freestanding una fuga o aparece como un *import* de más o no enlaza, y el
arnés comprueba que el módulo WASM **solo** pide el reloj.

#### El mismo código, dos veces

`DspCore/DspMathHarness.cpp` se compila de dos maneras —con `main` para nativo,
y con `-DDspMathHarnessWasm` sin `main` ni `stdio` ni `vector`— y por eso el
informe se escribe a mano en un buffer estático. Que sea **un** archivo y no dos
es lo que hace que la paridad signifique algo: si las dos listas de casos
estuvieran escritas a mano, bastaría con añadir un caso a una y olvidar el otro
para que la paridad dijera "OK" sin haber comparado nada.

El reloj es la única cosa que cambia entre las dos patas, y a propósito. En
freestanding no hay `time.h` ni `clock()`, así que el módulo **importa** el
reloj del host y `node` le pasa `performance.now()`. Un `time.h` inventado sería
medir un reloj contra sí mismo; importando el del host, el número que sale es el
tiempo de pared real de la ejecución en WASM, que es lo que a un navegador le
importa. Los dos shims que hacen falta —`<cstdint>` y `<limits>`, y solo esos
dos— están en `tools/wasm_freestanding/` y solo se usan en la ruta de include
del build WASM. El de `<limits>` va **sin `constexpr`** a propósito, porque
`__builtin_bit_cast` es de C++20 y el módulo es C++17.

#### Los casos, y por qué no son aleatorios

16 entradas, escritas a mano, elegidas por lo que se aprende midiendo: los
redondos (0, 1, −1, 0,5) donde una serie truncada se nota porque el error
relativo es máximo; los grandes (1000) donde un polinomio se va; y los bordes
del rango de `log2`/`exp2`/`atan`/`atanh`, donde una guarda mal puesta devuelve
algo silenciosamente equivocado en vez de NaN. Un generador con semilla fija
daría más cobertura y **menos** casos útiles: los que el azar no elige son
justamente los de arriba.

#### Las cifras, en esta máquina

Native con 1.000.000 de repeticiones, y el coste **neto** de restar el blanco del
bucle. Esta tabla es **una** corrida, y las demás salen parecidas:

| función | ns netas | × mediana | presupuesto | | función | ns netas | × mediana | presupuesto |
|---|---|---|---|---|---|---|---|---|
| `pow2i` | 3,50 | 0,31 | 0,60 | | `sin` | 12,44 | 1,09 | 1,60 |
| `floorToInt` | 5,69 | 0,50 | 0,80 | | `cos` | 12,94 | 1,14 | 1,60 |
| `wrapPhase` | 6,69 | 0,59 | 1,40 | | `tanh` | 14,38 | 1,26 | 1,80 |
| `atanh` | 5,63 | 0,49 | 1,60 | | `atan` | 16,06 | 1,41 | 2,00 |
| `log2` | 10,88 | 0,96 | 1,60 | | `pow` | 33,50 | 2,95 | 6,00 |
| `exp2` | 11,38 | 1,00 | 1,60 | | | | | |

Blanco del bucle: **1,81 ns**. Mediana: **11,38 ns**. Hay un grupo apretado
alrededor de 1 y dos que se salen: `pow`, que hace `exp` y `log` de verdad, y
`pow2i`, que es una construcción de bits.

**En WASM la mediana sale entre 35 y 36 ns, contra 10,2 – 11,9 ns en nativo:
entre 3,0 y 3,5 veces, medido en cuatro corridas.** Doy el rango porque la
máquina carga y el número suelto no significa nada; la mediana de WASM es la que
se mueve poco y la nativa la que baila. Ese 3 a 3,5 es el número que le diría a
alguien por qué el navegador va justo.

Y los 11 × 16 = 176 resultados salen **idénticos bit a bit** en las dos patas,
que es lo que dice que ese precio se paga solo en velocidad y no en sonido.

#### Dos cosas que la medición obligó a arreglar en el arnés mismo

**El presupuesto va en múltiplos de la mediana, no en nanos.** Con 20.000
repeticiones el bucle entero dura ~4 ms, el reloj nativo mide en milisegundos, y
nueve de las once funciones salían a 9,38 ns — el suelo del reloj, no su coste.
Con 1.000.000 el orden se repite entre corridas. Y si el bucle dura menos de 20
ms, el veredicto sale `sin-medir` en vez de un "ok" que sería un "no he medido
nada" con otra palabra.

**El formateador imprimía `nan` para el cero.** La guarda "esto no es un número"
—`v > 0` y `v < 0`— es falsa también para `0.0`, así que el número más probable
de todos, un coste redondeado, salía marcado como inválido. Y los céntimas
redondeados sin la llevada sacaban `13.006` como `13.001` y `1.999` como `1.00`.
Ninguno de los dos fallos se ve leyendo el código: se ven mirando el informe.

#### Las cuatro puertas muerden

Comprobado reintroduciendo uno a uno cada fallo, no leyendo que haya un test:

| Mutación | Puerta | Resultado |
|---|---|---|
| Un `tan` nuevo en `DspMath.h`, fuera del manifiesto | A | `funciones de DspMath.h que NO estan en el manifiesto: tan` |
| `log2` llamando a `expf` de libm, con su `<cmath>` debajo | B | `el modulo TIRA DE LIBM` |
| El módulo WASM pidiendo un símbolo de más al host | D imports | `el modulo WASM pide al host env.dspMathHarnessFuga` |
| La pata WASM escribiendo un bit distinto en cada valor | D paridad | `sin: 16 de 16 casos difieren` |

Dos de esas cuatro reuniones son la mitad del valor de este arnés, y las dos
salieron de que **la primera versión de la mutación no era la correcta**:

- La fuga a libm no llegaba a la puerta B porque `::expf` **no compila** sin
  `<cmath>`. Una fuga real necesita el include para existir, porque sin él no hay
  ni declaración: el mutador tenía que meter las dos cosas.
- Romper el escritor de bits **no mordía nada**: el escritor es el mismo código
  en las dos patas, así que las dos mienten igual y siguen coincidiendo. Una
  comparación diferencial solo puede cazar una diferencia, y la diferencia tiene
  que existir entre los dos lados. Hubo que meter la corrupción dentro de
  `#if defined (DspMathHarnessWasm)`, que es como se simula un compilador que
  redondea distinto.

Y un tercero, del mismo tipo, que no es del arnés: **`DspMath.h` está en CRLF**
y las sustituciones de las mutaciones buscaban con `\n` a pelo, así que no
mutaban nada y la puerta salía verde. Una mutación que no cambia el fichero no
es una prueba que pasa: es una que no se ha ejecutado.

### El sistema de slots: para poner "el efecto que elija el usuario" en un hueco

`DspEffects` no es solo una coleccion de motores: tiene tambien el **sistema de
slots** que los mete en huecos, con los cuatro huecos y los nueve modos de ruteo
de ABDEep, pero **JUCE-free**, sobre `AudioBuffer<float>` de `DspCore`.

```cpp
#include "DspEffects/FxDefaultCatalogue.h"     // el catalogo de las siete filas

abd::dsp::FxEngine motor;
int num = 0;
const FxEffectInfo* catalogo = fxDefaultCatalogue (num);

motor.setCatalogue (catalogo, num);
motor.prepare (sampleRate, 2, 512);

motor.setRouting (FxRouting::ParallelPairsSeries);
motor.setMode (FxMode::Insert);

motor.getSlot (0).setType (1);                // 1 = la primera fila
motor.getSlot (0).setMix (0.4f);
motor.getSlot (0).setParameter (0, 0.7f);     // normalizado 0..1

motor.process (buffer, numSamples);
```

Los ficheros, en este orden de lectura:

| Fichero | Que es |
|---|---|
| `FxRegistry.h` | El CONTRATO: `FxParamSpec`, `FxEffectInfo` y las tablas `fxNormalise` / `fxDenormalise`. Sin motores, sin JUCE |
| `FxSlot.h` | UN hueco: el tipo que hay, 12 mandos normalizados, `gain`, `mix` y la frontera wet/dry |
| `FxEngine.h` | Los cuatro huecos, los nueve ruteos, el modo insert/send/bypass y la realimentación global |
| `adapters/BasicAdapters.h` | Los siete motores de este modulo, traducidos de unidades físicas a 0..1 |
| `FxDefaultCatalogue.h` | Las siete filas, y las funciones que las consultan |

**POR QUE ESTA EN EL MODULO Y NO EN UN PRODUCTO.** Porque hay dos productos que
lo necesitan, y el de ABDEep es codigo JUCE: su `FXEngine::updateParameters`
toma un `AudioProcessorValueTreeState` y su `process` toma un
`juce::AudioBuffer`. ABDNeural compila su DSP a WebAssembly **sin** JUCE —su
build toma `DspCore` y `DspEffects` como targets INTERFACE y no compila JUCE— y
por tanto copiar el motor obligaba a meter JUCE dentro del motor o a duplicarlo.
Es el mismo patrón que funcionó con la reverb y con el coro BBD.

**LA REGLA QUE SOSTIENE TODO: el hueco mezcla, el efecto no.** Un slot tiene UNA
mezcla igual para todos los efectos, porque el panel tiene una perilla de mezcla
por hueco y el usuario espera que haga lo mismo con cualquiera. Por eso los
adaptadores devuelven la senal **ya mojada** y dejan la combinacion con la seca al
hueco. Donde un motor trae mezcla dentro —la reverb de FreeVerb— el adaptador
pone sus niveles a "solo mojado" y por eso `mix` **no** aparece en su tabla: vive
en el mando del hueco. La unica excepcion es esa reverb, que expone sus dos
niveles internos como parametros suyos (`levels`) en vez de romperle la paridad a
0 ulps quitandole el mezclado interno.

**Y LO QUE ES DE PRODUCTO, que no esta aqui:** qué efectos hay, cómo se llaman en
el panel, y la tabla de unidades de cada mando. Eso entra por `setCatalogue`. Un
producto con efectos propios (ABDEep tiene 48 más) monta su tabla y le añade
estas siete filas.

**Un catálogo, no un `switch`.** La version de ABDEep tiene un `switch` de 50
casos en `FXSlot_Factory.cpp`, y `FXSlot.h` incluye CINCUENTA cabeceras: tocar un
efecto obliga a recompilarlo todo, y saber qué efectos hay exige leer el
`switch`. Aqui el catálogo es una **tabla de descriptores** y el tipo se borra
por **puntero a funcion**, sin nada virtual en el lazo de audio. Añadir un
efecto es añadir una fila, y el número de filas sale de `sizeof`: un catálogo
con una entrada de más que el recuento es la forma bonita de que el último efecto
del panel no exista.

#### Los siete, y por qué no setenta

El único criterio para entrar en el catálogo por defecto es que el motor esté
**en este módulo**. Los 48 efectos privados de ABDEep no entran todavía: son
código JUCE, y meterlos exigiría que este módulo dejara de ser libm-free y
JUCE-free, que es justo lo que sostiene la paridad nativa <-> WASM de ABDNeural.

| # | `name` | Motor | Mandos |
|---|---|---|---|
| 1 | `chorus` | `DspChorus` | rate (0.1–8 Hz), depth |
| 2 | `delay` | `DspDelay` | time (10 ms–2 s), feedback |
| 3 | `reverb` | `DspReverb` (FreeVerb) | size, damping, width, levels |
| 4 | `saturation` | `DspSaturation` | drive (1–8) |
| 5 | `schroeder` | `DspSchroederReverb` | decay, damping, diffusion, predelay |
| 6 | `bbd` | `JunoBBD` + `BbdNoise` | mode (**4 pasos**), rate, depth, wear |
| 7 | `shelf` | `ShelfFilter` | mode (**2 pasos**), freq (20 Hz–20 kHz), gain (±12 dB) |**El 0 es siempre bypass**, esté o no en la tabla, para que un panel permita elegir "nada" sin que ningún producto tenga que acordarlo.

---

### El convertidor exponencial independiente del CA-72

`OscVcoCa72` tiene la puerta del circuito abierta: `setTimingCurrent` / `getTimingCurrent`. Eso permite modular en corriente (FM lineal) sin pasar por Hz, y leer la calibración que la pieza está usando. Pero el VCO no sabe de dónde viene esa corriente: quien pide en Hz ya recorrió el camino voltios → corriente → Hz.

`SynthCore/CvToControl.h` (y su `.cpp`) es **ese camino, separado del VCO**: el convertidor exponencial del CA-72 como pieza independiente, no como parte del núcleo del oscilador. Convierte los voltios del teclado y de VTUNE, los trimpots de escala y de centro, y la temperatura del par, en la corriente de temporización que el VCO recibe.

**Qué es**: el convertidor exponencial de pareja de transistores, modelo completo del aparato (Vbe = kT/q · ln Ic/I0, beta del par, dos trimpots de escala y de centro, temperatura del chip como T/T0 del exponente). Es **independiente**: no tiene muestreo, no tiene forma de onda, no tiene sobremuestreo; solo convierte V → A según el modelo del aparato. El perfil (`CvToControlProfile`) es datos del banco de pruebas, igual que `OscVcoCa72Profile.h` lo es para el VCO.

**Para qué sirve**: para que un consumidor pueda modular el VCO en corriente o en voltios **según el aparato**, no según lo que el motor de frecuencia de la familia pida. El VCO de rampa del CA-72 tiene esa puerta abierta, y este convertidor es quien la cierra con la ley del aparato, no con una recta arbitraria:

```
Vtune, semitonos del teclado, coarse/fine + temperatura → corriente
```

Y viceversa, si el motor lo pide: corriente → lo que el teclado y VTUNE estarían haciendo (las inversas `keyboardSemitonesForCurrent`, `tuneVoltageForCurrent`).

**Qué no es, y es a propósito**: no es el VCO, ni el limitado de banda, ni la tabla del triángulo. No es calibración del módulo: el módulo habla en Hz, y quien pide Hz ya recorrió este camino. Es la **puerta del aparato**, y por eso es pieza independiente.

**Y lo que queda fuera, dicho en voz alta**: la tabla medida de la beta a temperatura, el ruido de la fuente de corriente fija y los microcompensados que el banco de pruebas añadió. Lo que viaja son los parámetros del modelo, no el código de la referencia, que está bajo GPL y de la que **no se ha copiado ni una línea**.

**Verificado**: el convertidor se compila en `ABDShared_SynthCore` y su implementación está en el árbol; la puerta `setTimingCurrent` del VCO que lo recibiría ya existe en `OscVcoCa72.h`. No hay consumidor de producto todavía: es el estado del trabajo, no un descuido.

Lo que dicen los fuentes del árbol:

- **Puerta del circuito del VCO, presente.** `OscVcoCa72` expone `setTimingCurrent` (`SynthCore/OscVcoCa72.h#L226`) y `getTimingCurrent`, y la implementación está en `SynthCore/OscVcoCa72.cpp#L243`. Esa puerta es la que recibiría la corriente del convertidor; está abierta por diseño.
- **Convertidor en el árbol, como pieza independiente.** `SynthCore/CvToControl.h` declara la clase y su perfil `CvToControlProfile`; `SynthCore/CvToControl.cpp` tiene la implementación (incluyendo el modelo Vbe = kT/q · ln Ic/I0, beta del par, trimpots de escala/centro y temperatura). No es parte del núcleo del oscilador: es el camino voltios → corriente que el VCO reconocería.
- **Compilación en `ABDShared_SynthCore`, por la arquitectura del build.** El target `ABDShared_SynthCore` listado en `CMakeLists.txt` agrupa las fuentes de SynthCore, y entre ellas `SynthCore/CvToControl.cpp` y `SynthCore/OscVcoCa72.cpp`; el alias visible es `ABDShared::SynthCore`. Es la disposición del árbol, no un número de corrida: aquí no hay un test o corrida de compilación que haya dado verde en este momento, así que la confirmación es de presencia en el target, no de resultado de build ejecutado.

Qué queda fuera de esta confirmación:

- Un consumidor de producto que cierre la puerta: ningún sintetizador del árbol enlaza el convertidor todavía. Esto es el estado del trabajo, no un defecto del convertidor.
- Un número de corrida concreta de `ABDShared_SynthCore_Tests` que compruebe el convertidor: el árbol tiene el target de tests (`ABDShared_SynthCore_Tests`) y los tests de la familia de osciladores que ejercen `setTimingCurrent`, pero no se ha ejecutado ni mostrado un resultado de esa corrida en este momento, así que no se afirma un verde confirmado por ejecución; se afirma solo que la pieza está en el árbol y en el target.

---

### La familia de osciladores

Ver la sección de SynthCore más abajo. `OscVcoCa72` tiene la puerta del circuito abierta: `setTimingCurrent` / `getTimingCurrent`, para que un consumidor pueda modular en corriente o en voltios según el aparato. Ese convertidor, separado del VCO, está en `SynthCore/CvToControl.h` (ver la sección *El convertidor exponencial independiente del CA-72*).

#### Cuatro cosas que costaron, y que un test de "suena igual" no caza

1. **El último efecto era inalcanzable.** El límite superior de `fxEffectAt` era
   `index >= count` en vez de `index > count`, así que con siete filas el índice 7
   —la repisa— no se podía elegir jamás. El defecto es el mismo desde que hay
   catálogo, pero el que lo sufría cambió: con seis filas era el BBD, con siete
   es la repisa. No lo cazaba ningún test porque los que había elegían efectos
   del principio de la lista. **Medido**: con el defecto, `fxEffectAt(cat, 7, 7)`
   devuelve `nullptr`.
2. **El modo envío se comía el `mix` del usuario.** La primera versión hacía
   `setMix (sendLevel_)` en cada hueco antes de procesar. Suena casi igual, y
   está mal: `setMix` es un mando del usuario, así que un bloque de envío se
   comía el `mix` que había puesto, y no lo devolvía. Lo correcto es lo de
   ABDEep: se guarda la seca, se procesa con los mandos intactos, y la mezcla
   final es `seca·(1−nivel) + procesado·nivel`. **Medido**: con el defecto, tras
   un bloque de envío el `mix` del hueco pasa de 0.85 a 0.30.
3. **Un bloque mayor que el preparado perdía audio.** El hueco procesa como mucho
   `maxBlockSize_` muestras y devuelve el resto **sin tocar**; el motor le pasaba
   el bloque entero, así que de un bloque de 4096 con el motor preparado a 512
   salían 3584 muestras **secas**. El troceo va en el motor. **Medido**: con el
   motor a 64 y un bloque de 512, la diferencia era 0.119 de pico sobre una
   entrada de 0.2. Troceando bien la diferencia es de **0 ulps**, porque los
   motores de este módulo son todos por muestra.
4. **`prepare()` recreaba la instancia sin destruir la anterior.** Se llama más
   de una vez (cambio de sample rate, cambio de catálogo) y el puntero viejo se
   sobrescribía: memoria sin dueño. No se oye, pero el detector de fugas del
   módulo lo dice al final del programa, y con razón.

#### La tabla de mandos: la normalización y su inversa

Un hueco habla normalizado 0..1 y los motores hablan unidades físicas. La
traducción es una tabla, y por regla del módulo es política, no motor. Dos cosas
que se hicieron mal y que solo se ven midiendo la forma de la curva:

- **`fxNormalise` y `fxDenormalise` TIENEN que ser inversas.** Se elevate a
  `1/skew` y a `skew` respectivamente, no las dos a `1/skew`. Con las dos al revés,
  el mando de un retardo de 375 ms salía en `0.0035` y ese mismo mando
  reinterpretado daba 1.63 s: casi todos los mandos agrupados en un extremo del
  recorrido y el otro extremo inalcanzable. **El test que lo caza es el viaje de
  ida y vuelta sobre los 101 puntos de cada mando, no un valor suelto.**
- **El signo de `skew` está al revés de lo que parece**, y por eso se eligieron
  seis mandos inservibles: `skew < 1` da **más recorrido a la parte baja** del
  rango, no menos. Un retardo de 1 s con un rango de 10 ms a 2 s tiene su mitad en
  1 s, así que el recorrido tiene que estar cargado abajo, o el usuario tiene que
  empujar el mando hasta el final para llegar a la mitad del rango.

Y una comprobación que no es de que el motor funcione, sino de que el panel sea
utilizable: **ningún mando por defecto puede caer en un extremo del recorrido**
(`0.15`–`0.95`). Ese test encontró cuatro, y tres eran mandos que el adaptador no
leía —un knob que no hace nada es peor que un knob que no existe, porque el
usuario lo mueve, no pasa nada, y concluye que el plugin está roto— y el cuarto
era el `width` de la reverb, que nacía en 1.0 porque ese es el default del motor.
Por eso el catálogo no declara ningún parámetro que el adaptador no implemente.

#### `DspDelay`: un defecto real de este módulo, encontrado por esto

`wrapReadPosition` decía devolver un valor en `[0, size)`, pero un `float` no
representa todos los reales de ahí: cuando el resultado caía a menos de medio ULP
de `size`, el redondeo lo subía a `size` **exacto**, el índice salía una muestra
más allá del final del buffer, y `getSample` leía de más.

No era un retardo raro. Salió montando el sistema de slots, con un retardo de
**300 ms** —14400.001 muestras, porque el mando es normalizado y el retardo no
cae en la rejilla— y el puntero de escritura encima de la posición de lectura: la
resta da −0.001, al envolver da 97023.999, y al redondear a float da 97024.000
sobre un buffer de 97024. **Medido**: en 72000 muestras sale **una** lectura fuera
de rango, y ocurre en la muestra 14400, que es justo donde la escritura alcanza a
la lectura. Con un retardo de un número entero de muestras nunca habría pasado,
y por eso llevaba aquí sin que nadie lo viera.

La corrección es `if (r >= size) r = 0.0f;` dentro de `wrapReadPosition`, y **no
toca ningún retardo que ya sonara**: para todo retardo dentro de la capacidad, `r`
queda a media ULP o más del final y la rama no se toma, así que el resultado es el
mismo número bit a bit. La paridad a 0 ulps de ABDNeural sigue intacta
(`ABDNeural/Tests/DspEffectsParityTest.cpp`: chorus, delay, saturación, 0 ulps).

#### Los tests muerden

Los tests del módulo (`DspEffects/DspEffectsTests.cpp`, 681 comprobaciones)
incluyen el sistema de slots, y está comprobado que **muerden**: reintroduciendo
uno a uno los siete defectos de arriba, el banco se pone rojo en los siete. Un
test que no falla cuando se rompe el código que prueba no es un test, y dos de
los siete hubo que corregirlos porque el parche de mutación era un no-op que
daba verde.

### El primer consumidor real: ABDNeural encima del sistema de huecos

`DspEffects` tenía el sistema de slots sin que **ningún producto lo usara**: la
tabla, el motor y los seis adaptadores estaban escritos y probados, y el
`audit_unconsumed_sources.py --check` se quedaba en rojo porque nadie los
consumía. ABDNeural es el primero, y al montarlo sevio que el sistema aguanta
—y que la parte difícil nunca fue el motor, fue **el mezclado**.

#### Qué hay ahora en ABDNeural

Cuatro envoltorios de producto (`Source/DSP/Effects/{Saturation,Chorus,Delay,
Reverb}.h`, uno por efecto, cada uno con sus smoothers y su propia ley de
mezclado) y cuatro llamadas seguidas en `BaseEngine::applyGlobalFX`. En su lugar,
**un hueco por efecto** de este módulo, en la misma cadena y en el mismo orden:

```cpp
// ABDNeural/Source/DSP/FxSlots.h — la capa de producto, 1 solo fichero
abd::dsp::FxEngine engine;                       // 4 huecos, 9 ruteos
engine.setCatalogue (fxDefaultCatalogue(), fxDefaultCatalogueSize());
engine.setRouting  (abd::dsp::FxRouting::Series);   // el orden de antes
engine.setMode     (abd::dsp::FxMode::Insert);
```

`FxSlots` es el sitio donde se decide lo que es de NEURONiK: qué efecto va en
qué hueco, y cómo se traducen los doce mandos de `GlobalParams` a los mandos
normalizados de las filas. El motor no puede saberlo (sus rangos —0.10 a 8 Hz de
coro, 10 ms a 2 s de retardo— son del producto, no suyos), así que este fichero
es el único que hay que tocar para cambiar el sonido de la cadena global.

**Los tipos no se cambian en vuelo.** `updateFromGlobalParams` se llama desde el
hilo de audio (los motores llaman a `updateParameters` al principio de su
bloque) y `FxSlot::setType` **crea y destruye** la instancia. Los cuatro tipos se
fijan en `prepare`; lo que cambia en caliente son los mandos, que sí son
baratos. Cuando el panel deje elegir el efecto de cada hueco, ese `setType`
vivirá en el hilo de mensajes, que es donde puede.

**`GlobalParams` no se ha tocado, y es a propósito.** Es el contrato con el
worklet de WebAssembly: `Source/Wasm/NeuronikWasmBridge.cpp` tiene una tabla de
`offsetof` que comprueba el orden de los campos, y el `.wasm` que hay en
`WebUI/public/worklet/` es un binario ya compilado. Añadir un campo al struct
rompe la paridad nativa <-> WASM sin que se note hasta que el worklet lee
basura. Los doce mandos de FX se quedan donde estaban y la traducción vive en
`FxSlots`.

#### El mezclado, que es donde la migración cuesta caro

Un hueco mezcla **siempre igual**: `seca·(1 − mix) + mojado·mix·ganancia`. Los
cuatro efectos de antes no mezclaban así, y cada uno falla por un motivo distinto:

| Hueco | Ley de antes | Ley ahora | Medido |
|---|---|---|---|
| saturación | `out = sat(in)`, bypass con `drive ≤ 1.001` | `mix = 1` (**inserto**); con el mando a cero, `mix = 0` | **0 ulps** en el bloque entero |
| coro | motor: `in·(1 − mix·0.5) + eco·mix·0.5` | **`mix` del hueco = `chorusMix`** | **7.0e-06** (1e-5 de banda) |
| retardo | `out = in + 0.5·eco` — mezcla **fija**, el `mix` se ignoraba | `mix = 0.5`, en **paralelo** | 1.5e-01 · RMS 0.260 → **0.183** |
| reverberación | `mojado = mix·0.5`, `seca = 1 − mix·0.2` — **dos** leyes | `levels` de la fila = el mando, `mix = 0.5` | 5.0e-01 · RMS 0.344 → **0.116** |

Las dos primeras salen bien y no es casualidad:

- **El coro mapea 1:1 porque el `mix` del hueco multiplica el `mix` del motor.**
  El adaptador entrega `0.5·in + 0.5·eco` (pasa `mix = 1` al motor) y el hueco lo
  pesa por su `mix`: `(1−m)·in + m·(0.5·in + 0.5·eco) = in·(1 − 0.5m) + eco·0.5m`,
  que es la misma fórmula con el mismo `m`. El `fxChorusMix` de antes sigue siendo
  el `mix` del hueco, sin renombrar nada. Lo que queda son los dos redondeos del
  viaje normalizado ↔ físico.
- **Los dos bypass son bypass de verdad, bit a bit.** `mix = 0` da
  `in·1.0f + mojado·0.0f`, y `x·1.0f == x`. La saturación con el mando a cero y la
  reverberación con el suyo salen **0 ulps en el bloque entero**, que es la única
  cosa que se puede exigir sin mentir.

Las otras dos **no tienen banda posible, y no es una comodidad**. Sus mezclados
de antes no son puntos de la recta que describe un hueco: el retardo tiene la
seca a unidad con el eco *sumado* —con `mix = 0` no hay eco, y con cualquier
`mix > 0` la seca baja— y la reverberación escribe dos niveles con leyes
distintas. Un hueco puede dar `(seca, mojado) = (1 − m, m·g)`: para tener la seca a
1 hace falta `m = 0`, que es también donde no hay eco. El punto de la recta más
parecido al de antes es la mitad, y por eso van en paralelo al 50 %.

**Lo que cuesta de verdad:** con el retardo puesto —y en NEURONiK lo estaba
siempre, no tenía mando de mezcla— la seca del bus global baja 6 dB. Es el
mezclado en paralelo de toda la vida y es lo que hacen los otros huecos de
cualquier sistema de slots, pero es un cambio de nivel medido, no un redondeo, y
por eso está escrito aquí y medido en el test en vez de escondido detrás de una
tolerancia inventada.

Un detalle de panel: el `fxChorusRate` tenía un recorrido de 0.1 a **10** Hz y la
fila del catálogo llega a **8**. A 10 Hz el motor modula 5..30 ms de retardo, o
sea que la línea da una vuelta cada 100 muestras y se oye como un trémolo. Con el
panel en 10 había un tramo de recorrido sin efecto en el extremo.

#### El test, y las tres trampas que se cayó por el camino

`ABDNeural/Tests/FxSlotsTest.cpp` (21 comprobaciones) sustituye al antiguo
`DspEffectsParityTest.cpp` y lleva dentro una **copia congelada de la cadena de
antes** —los cuatro envoltorios con sus cuatro smoothers— contra la nueva. No
compara el resultado final: compara **etapa a etapa**, y las tres trampas que
aparecieron al escribirlo son las que aparecen siempre:

1. **La diferencia entre dos cadenas solo dice algo si las dos han recibido la
   misma entrada.** La primera versión renderizaba las cuatro etapas en las dos
   cadenas y comparaba al final, y la reverberación "difería" en 1.5e-01 siendo
   las dos la identidad: el retardo de delante había cambiado y su diferencia se
   colaba aquí. Ahora, antes de cada etapa, el estado de la cadena vieja se copia
   a las dos y la etapa se mide sola.
2. **Comparar dos audio en un array plano solo vale si el plano es el mismo.** El
   bloque a 64 y el bloque a 512 salían "diferentes" en 84031 de 98304 muestras:
   el índice plano del test no significaba lo mismo en los dos. Con el índice por
   canal, la diferencia es de **0 de 98304** —el motor trocea internamente y los
   motores de este módulo son todos por muestra, así que trocear no cambia ni un
   bit— y eso es ahora una comprobación del test, no una costumbre.
3. **Un test de determinismo compara dos objetos NUEVOS.** Si uno de los dos
   viene de una pasada anterior, lo que se mide es que el primero tenga cola.

Lo que queda en pie además de la comparación etapa a etapa: el reparto de la
cadena (qué efecto en qué hueco, y con qué `mix` y ganancia), la independencia
del troceado a 64 y a 512 con **0 ulps**, y la cola con retardo realimentado al
0.95 más 0.25 s de silencio sin un NaN.

### Auditoría de `DspEffects`: cinco hallazgos, cuatro arreglos, y un resultado negativo

`DspEffects` tenía tests de comportamiento —que suenan bien y dan 0 NaN— pero
nadie había **medido** si sus mandos se comportaban fuera de su rango, ni si las
etapas de carácter gastaban indirección, ni si el motor de eco leía la cinta más
de la cuenta. Esto no es leer el código buscando signos: es una sonda que
compila los ficheros y saca números. Cinco hallazgos; cuatro eran defectos
reales de verdad y se arreglaron; el quinto resultó no ser un defecto, y eso es
lo que más cuesta escribir.

**1. `EffectPolicy.h` — la etapa base llevaba un vtable entero por nada.** La
clase `CharacterStage` tenía un ancla `virtual void prepareRateImpl (double)`
protected, llamada desde `prepareSampleRate`. Lo primero que hice fue buscar
quién la sobrescribía: **nadie**, ni en este módulo ni en ningún producto del
árbol. Lo segundo fue medir lo que costaba:

```
antes   sizeof(NullStage) = 16   sizeof(DiodeBridge) = 16   sizeof(TapeColour) = 24
        is_polymorphic<NullStage> = sí   (y de las otras dos también)
ahora   sizeof(NullStage) =  8   sizeof(DiodeBridge) =  8   sizeof(TapeColour) = 16
        is_polymorphic<NullStage> = no
```

Ocho bytes y una tabla de funciones por etapa, en la clase cuya única razón de
existir es *no* gastar indirección. Se quitó el ancla. Una etapa que necesite el
sample rate lo lee con `getSampleRate()` —que sigue ahí— y recalcula su
coeficiente una vez, que es lo que hacen todas. Ahora hay un test que lo vigila:
que `CharacterStage`, `NullStage`, `DiodeBridge` y `TapeColour` **no** sean
polimórficas, y que una etapa sin estado ocupe lo que un sample rate y poco más.

**2. `DspSchroederReverb.h` — tres mandos se aceptaban fuera de su rango, y uno
de ellos NO se podía recortar como los otros dos.** El constructor documenta
`decay`, `damping` y `diffusion` en `[0,1]`, pero el código se los guardaba tal
cual. Fuera del 1, los conbs y el filtro de amortiguación se disparaban:
medido, `damping = 2.0` daba un pico de cola de **1e+30** y `diffusion = 2.0` de
**3e+28**, frente a 3.2e-01 con los mandos en rango. Esos dos sí se arreglan con
`jlimit` en los dos extremos.

El `decay` es distinto, y aquí está lo que cuesta no mirarlo antes de escribir.
**Un `decay` negativo no es un valor inválido: es una variante con
significado propio**, y el motor lo traduce con un ternario:

```
feedback = decay < 0 ? 0.3f : decay * 0.9f
escala   = decay < 0 ? 0.5f : decay * 0.7f + 0.3f
```

ABDEep consume este motor con presets de fábrica que traen `decay` negativo, y su
referencia congelada exige ese mismo `feedback = 0.3`. Un `jlimit(0, 1)` —que es
lo que habría puesto "recortar los tres mandos" sin más miramientos— convierte
el negativo en 0, el 0 **no** es el otro caso del ternario, y la realimentación
pasa de 0.3 a 0: **la cola de esa variante desaparece**, y con ella la paridad
que ABDEep comprueba. Por eso aquí va `jmin(decay, 1.0f)`: recorta por arriba,
que es lo que evita que se dispare, y deja el negativo intacto.

Hay dos cosas que hacen que esto no se vuelva a romper. La primera es un test
que fija la semántica: el `decay` negativo tiene que conservar *su* cola, y hay
que medirla **tarde**. Durante los primeros 250 ms el impulso sigue en vuelo por
el pre-retardo y los conbs, así que un `decay = 0` también "suena" ahí (1,12e-01
medidos) y medir solo al principio no distingue los dos casos. La cola tardía sí:

```
decay -0.30 : pico 0-0.25s 1.86e-01   pico 0.25-1s 9.51e-06
decay +0.00 : pico 0-0.25s 1.12e-01   pico 0.25-1s 1.25e-10
```

La segunda es que el recorte por arriba es **exacto**, no aproximado: `decay`
1,7 y `decay` 9 suenan muestra a muestra igual que `decay` 1, y eso también está
comprobado. Con los mandos dentro del rango la salida es idéntica bit a bit —el
recorte solo muerde fuera—, y las 681 comprobaciones previas siguen dando
0 ulps.

**3. `DspSchroederReverb.h` — amortiguación a cero congelaba el estado.** El
filtro de amortiguación es `estado = salida*damp1 + estado*damp2` con
`damp1 = damping` y `damp2 = 1 - damping`. Con `damping = 0` eso es
`estado = estado`: el estado se **congela** en su último valor y la cola no
decae, se queda en un offset de DC. El extremo ya estaba tratado (`damping > 0
? damping : 1.0f`, sin el cual el filtro se iba a NaN), pero el valor 0 exacto
—no NaN, no negativo— se colaba y congelaba la cola. Medido: bajando la
amortiguación a 0 en caliente la cola tras 2 s de silencio es 5.9e-10 y la
media con signo vuelve a cero (+1.18e-07, sin offset). Arreglado el extremo.

**4. `characters/DiodeBridge.h` — el umbral se salía de rango.** `threshold` se
documenta en `[0,1]` y se recortaba con `jlimit`… salvo en la ruta del propio
`threshold`, que se pasaba tal cual. Con `threshold = 1.5` el "puente de
diodos" se volvía transparente: `x = 2.0` salía 2.0 en vez de recortarse a 1.0.
El mismo `jlimit` que ya estaba en las otras dos lecturas del mismo fichero, al
hueco que se le escapaba.

**5. `MultiHeadEcho.h` — el arreglo que NO era un defecto.** El motor de eco de
cabezal múltiple relee la línea de cinta por cabezal, y el código "se lee" como
varias lecturas redundantes por muestra: suena a una mejora obvia guardar la
lectura en una variable. Escribí la sonda, generé una copia del motor con el
"arreglo" aplicado y medí las dos. **Resultado negativo, y por eso el motor no
se tocó:**

- La salida de las dos versiones es **idéntica bit a bit** (diferencia máxima
  0.000e+00). Hacen literalmente lo mismo: el compilador ya elimina las cargas
  redundantes.
- El **tiempo no separa**. Seis rondas, "guardar" contra "releer": −10,2 %,
  −2,4 %, +4,3 %, −4,1 %, +0,3 %, −9,1 %. El signo cambia de una ronda a otra:
  es el ruido de la máquina, no el código. Una sola ronda habría dado un número
  con signo, y ese número habría sido mentira.

Un candidato que la fuente hace *parecer* una mejora y que la medición refuta
es peor que no haberlo mirado, porque se arregla, se mergea y se paga el coste
de la indirección del `float` local para siempre a cambio de nada.

Dos cosas más que miré y que **no** eran defectos, por si alguien vuelve a
ellos: en `TapeColour`, las funciones de wow y flutter son la misma (diferencia
0.000e+00 en 64 posiciones) pero el motor de cinta genera su propia deriva
—7,8e-02 de variación muestra a muestra—, así que no es deriva muerta, es que
las dos funciones de la interfaz no son las que usa el motor. Y el recorte del
`damping`/`diffusion` de la reverb no rompe la paridad de ningún producto: con
los mandos en rango, 0 ulps.

Los cinco casos están en el banco (`DspEffectsTests.cpp`, 709 comprobaciones =
681 + 28 de esta auditoría) y **todos muerden**: reintroduciendo uno a uno cada
arreglo, el banco se pone rojo. Y desde el 2026-09-29 eso no es una frase sino
un script: `tools/ds_effects_mutation_bank.py`.

Ahora el script tiene **un catálogo de arreglos, no una lista de mutaciones**,
que es la diferencia entre una afirmación y una condición. Cada mutación dice
qué arreglo deshace, y el banco **se niega a arrancar** si hay un arreglo del
catálogo sin mutación: el "7 de 7" de antes podía quedarse en 7 de 7 mientras
un octavo arreglo, ya arreglado, se deshacía sin que nadie se enterase. Hoy son
12 arreglos y 13 mutaciones —dos de ellas sobre el mismo arreglo del `decay`—,
y con la migración del ecualizador han entrado seis: el techo de 0,45·fs, la
banda muerta de 0,05 dB, el recorte de índices de la cascada, la independencia
de los dos mandos, la puerta de hercios continuos que se puede volver a
cerrar con la tabla, y que esa puerta sea **una por repisa**.

La sexta merece su propia nota, porque es un fallo que se escondía solo. La
puerta de hercios continuos se implementaba con **una bandera para las dos
repisas**, así que escribir un hercio a mano en la baja apagaba la bandera
común, y al siguiente `setHighIndex` la máquina reescribía **las dos**
frecuencias: el hercio que el producto acababa de poner en la baja se perdía
sin avisar y sin error de ningún tipo. Y solo pasaba cuando el índice de la alta
casualmente no era el que ya estaba, porque la guarda de "no ha cambiado" cortaba
antes — un fallo que depende de **en qué posición tapaba el mando** del usuario
es de los que más tardan en verse. Ahora hay una bandera por repisa,
`aplicaFrecuencias` solo escribe la que la pide, `prepare` reabre las dos
puertas, y la guarda de "no ha cambiado" mira también si manda la tabla (si no,
un hercio continuo con el mismo índice se quedaba puesto para siempre y el
selector del panel parecía muerto).

Y hay un segundo agujero, más tonto, que también está cerrado: **contaba como
detección cualquier rojo**. Un banco que se ponía rojo porque la mutación había
roto *otra* comprobación se contaba como "mordida", y el arreglo parecía
vigilado sin estarlo. Ahora cada mutación declara `muerde`, el texto de la
comprobación que **tiene** que ponerse falsa, y si el banco se pone rojo de
verdad pero por otro sitio sale `ROJA PERO NO POR AQUI` y cuenta como fallo. Es
lo que separa un test que muerde de un test que tropieza.

```
python tools/ds_effects_mutation_bank.py --rapido           # el cruce, en un segundo
python tools/ds_effects_mutation_bank.py --autocomprobacion
python tools/ds_effects_mutation_bank.py --list
python tools/ds_effects_mutation_bank.py --only 8,9,10
```

Los tres niveles, y cada uno comprueba al anterior. `--rapido` no compila ni
toca el árbol: cruza cada anclaje con su fichero y cada `muerde` con
`DspEffectsTests.cpp`, así que si alguien renombra una comprobación sin tocar
la tabla, la herramienta lo dice en un segundo en vez de veinte minutos de
compilación más tarde. `--autocomprobacion` le pasa al revisor una tabla rota a
propósito —un arreglo sin mutación, un `muerde` inexistente, un anclaje
duplicado— y **exige que señale cada caso**; un revisor al que nunca se le ha
visto fallar no es un revisor, y si se calla esto sale en rojo. La corrida de
verdad son unos 30 minutos.

Lo que mide es la SENSIBILIDAD del banco —que cada arreglo tenga **alguna**
comprobación que se rompa al deshacerlo, y que se rompa **la suya**—, no que el
arreglo sea correcto: un banco que se pone rojo porque no compila se cuenta
como fallo del banco, y por eso la mutación que no compila no es una mutación
detectada.

Dos de esas mutaciones merecen nota:

- La del vtable: el fichero está en **CRLF** y la mutación buscaba con `\n` a
  pelo, así que no mutaba nada y salía "[OK]". Una mutación que no cambia el
  fichero no es una prueba que pasa, es una que no se ha ejecutado. El script
  compara el checksum antes y después y **avisa si el fichero no ha cambiado**;
  verde sale de "el banco se puso rojo" y rojo de "no mutamos nada", que son
  cosas distintas.
- La del `decay`: volver al `jlimit(0, 1)` de los tres mandos pone el banco
  rojo en 2 comprobaciones. Es la que más ha merecido la pena escribir, porque
  es el arreglo que parecía correcto.

### ABDMS2000: tres efectos mirados antes de migrarlos, y la decisión

`ABDMS2000/Source/DSP/Effects/` tiene 656 líneas en tres bloques y **ninguno**
consume este módulo: son código JUCE-free pero **libm**, que es justo lo que
este módulo no puede tragarse. Antes de moverlos, lo medido (nada de esto son
opiniones; sale de una sonda que compila los `.cpp` sueltos de ABDMS2000 y los
compara):

#### 1. `DelayFX` — el filtro de amortiguación no es un filtro de 6 kHz

`dampingState += 0.35f * (delayed - dampingState)` es un paso de un polo con
**coeficiente fijo**, y un coeficiente no es una frecuencia: la frecuencia de
corte sale de él multiplicada por la frecuencia de muestreo. El comentario dice
«6,0 kHz cutoff on Motorola DSP56362» y **medido**:

| `sampleRate` | Corte de −3 dB real |
|---|---|
| 32 000 Hz | **2 229 Hz** |
| 44 100 Hz | **3 071 Hz** |
| 48 000 Hz | **3 343 Hz** |

Un 50 % de dispersión para el mismo filtro. En una emulation que además se
prepara al `sampleRate` del host, el mismo retardo suena distinto en cada DAW.
**Decisión: migra**, como motor nuevo `TapeDelay`, con la amortiguación escrita
**en hercios** y el coeficiente calculado por `prepare`. Es la clase de defecto
que justifica por sí sola entrar en el módulo común.

**Y un segundo defecto, de orden.** `setTimeSeconds` calcula el desplazamiento
de 0,75× del canal derecho **solo si el tipo ya es `LeftRight`**, y `prepare` lo
llama una vez. Poner el tipo **después** de `prepare` —que es lo que hace un
panel al cargar un preset— deja el canal derecho sin desplazar hasta que el
tiempo vuelva a moverse. **Medido**: el mismo preset da dos hashes distintos
(`fef9aff7…` y `3b459ce4…`) según el orden, y en el segundo el canal derecho va
al tiempo completo. Un `setType` que reaplica los mandos lo arregla, y es
precisamente el mismo apaño que lleva `FxSlot::setType`.

#### 2. `ModFX` — dos de los tres modos están sanos; el tercero no se sabe todavía

`processChorusFlanger` (coro ↔ flanger por realimentación) y `processPhaser`
usan los tres mandos. `processEnsemble` **no usa ninguno de los dos**: sus dos
LFO están fijos en 0,5 Hz y 6,0 Hz y el bucle de realimentación no existe. Se
comprueba **hashando la salida con objetos nuevos** —reutilizar el mismo objeto
arrastra la fase del LFO y todos los mandos parecen vivos, que es la trampa de
comparar una instancia calentada con otra recién construida—:

```
ChorusFlanger  speed:vive    depth:vive    feedback:vive
Ensemble       speed:MUERTO   depth:vive    feedback:MUERTO
Phaser         speed:vive    depth:vive    feedback:vive
```

**Decisión sobre el ensemble: migra** como `EnsembleFx` —tres líneas y dos LFO en
cuadratura es un efecto de verdad, distinto del coro de una línea que ya está en
el catálogo—, y al migrarlo **se conecta el mando de velocidad a la LFO rápida**
(0,5 Hz a 15 Hz, el rango que ya usa `setSpeed` en los otros dos modos) y **no se
declara el de realimentación en la fila**: un knob que no hace nada es peor que
un knob que no existe, y el catálogo del módulo ya limpió tres de esos.

**Decisión sobre el phaser: NO migra, y hay que decir por qué.** Por muestra
calcula `std::tan` **ocho veces** (cuatro etapas × dos canales) y `std::pow` dos
veces, y **`DspMath` no tiene `tan`**. No es unported que se pueda hacer con un
include: hay que decidir una sustitución de `tan` o reestructurar el paso todo
paso para que no lo necesite, **y** mover el cálculo del coeficiente del lazo de
audio al bloque, porque diez transcendentales por muestra son un precio que
ningún hueco de este sistema va a pagar. Eso es una reescritura, no una
migración, y va en su propio paso con su propia paridad.

#### 3. `Equalizer` — el único que está limpio (MIGRADO, ver más abajo)

Dos repisas de Butterworth en cascada, forma II transpuesta, y dos detalles que
están **bien hechos**: el coeficiente `A = 10^(dB/40)` da identidad exacta a
0 dB (`b = a` cuando `A = 1`, comprobado con la fórmula), y la banda muerta de
0,05 dB en los dos mandos de ganancia evita el tictac de host sin coste.

**Decisión: migra**, y es la migración más barata de las tres. Solo le faltaba una
cosa al módulo: **`DspMath` no tiene `sqrt`**, y el cálculo de la repisa usa
`std::sqrt(A)` dos veces por banda. Las **tablas de 4 pasos del MS2000
(160/250/400/600 y 4000/6000/8000/12000 Hz) NO migran**: son política de
producto, y la fila común toma frecuencias continuas en hercios. Es la misma
regla que el rango de 0,10 a 8 Hz del coro.

> **CORRECCIÓN de este párrafo, que estaba mal medido.** Decía que el sustituto
> `exp2 (0.5 * log2 (A))` valía "para `A ≥ 0,66`", y no vale solo a partir de
> ahí: vale en **todo** el rango. Con ±12 dB, `A` recorre [0,501, 1,995] y el
> peor error son **2 ulps**, alcanzado a **+11,40 dB** y no en el extremo de
> −12 dB, que es donde uno esperaría el peor caso. El 0,66 venía de acotar por
> debajo de donde un `log2` de dominio general empieza a perder precisión, y
> aquí no aplica: `A` vive en un rango estrecho y muy cerca de 1.
> **Migrado**: la sección siguiente tiene las cifras del cambio de sonido.

**Y una advertencia que es de todos, no de este caso.** Migrar a este módulo
cambia la aritmética: `std::sin` → `abd::dsp::sin`, `std::cos` → `abd::dsp::cos`,
`std::sqrt` → `exp2(log2)`. El resultado es idéntico **hasta unos ulps**, no bit
a bit. En ABDNeural eso ya se hizo y está escrito en `DspEffectsParityTest.cpp`:
el cambio de sonido es deliberado y lo que se exige al test es que el **motor
compartido** no cambie, no que el producto siga sonando igual. Para ABDMS2000,
migrar el ecualizador **rompe** la paridad a 0 ulps de su propia referencia
congelada en unos ulps, y esa referencia se actualiza en el mismo commit que
documenta el cambio. Es el precio de que el filtro valga lo mismo a 32, 44,1 y
48 kHz, y es un precio que se paga una vez y se cobra en todos los productos
siguientes.

### La repisa: el ecualizador del MS2000, ya en el módulo

`ABDMS2000/Source/DSP/Effects/Equalizer.{h,cpp}` era el único de sus tres efectos
que estaba limpio: dos repisas de Butterworth en cascada, forma II transpuesta,
`A = 10^(dB/40)` con identidad exacta a 0 dB y banda muerta de 0,05 dB en la
ganancia. Ahora son **dos `ShelfFilter` del módulo en cascada**, y lo que queda
en el producto es un shim de 60 líneas: las tablas de 4 pasos y la API de índices.

Y la cascada, que era lo último que vivía en el shim, ya está también en el
módulo: `CascadeShelfEq<Perfil>` y `profiles/MS2000EqProfile.h`. Ver abajo.

#### Dos decisiones, y las dos están medidas

**Una repisa, no un ecualizador.** El motor se llama `ShelfFilter` y no
`Equalizer` porque un ecualizador que solo sabe hacer dos bandas con un Q fijo y
cuatro frecuencias por banda no es un ecualizador, es un preset. La fila del
catálogo expone **una** repisa con un selector de modo; un producto que quiera
tres bandas mete tres filas iguales. Las tablas de 4 pasos del MS2000 (160/250/400/600
y 4000/6000/8000/12000 Hz) **migran como perfil**, no como motor: son los números
de un sintetizador, y eso en el módulo es exactamente lo que es un perfil
(`JunoBbdProfile.h`, `Re201Profile.h`, `ReverbProfile.h`). Lo que no migra es el
rango de 0,10 a 8 Hz del coro, porque ese sí es política del producto y no de
ningun sintetizador en concreto.

**El límite de frecuencia es 0,45·fs, y no 0,2.** La primera versión puso 0,2
"por prudencia" y al medir resultó que **movía las frecuencias del propio MS2000
a 32 kHz**: las repisas de 8 y de 12 kHz se recortaban a 6,4 kHz y sonaban en otro
sitio. El MS2000 usa 12 kHz, y 12000/32000 = 0,375, así que cualquier límite por
debajo de 0,375 cambia el sonido de un producto ya publicado.

#### La cascada, que era lo último que vivía en el shim

`CascadeShelfEq<Perfil>` son las dos `ShelfFilter` en serie, con los estados
separados por canal, y del perfil toma **solo datos**: `numPositions`,
`lowFreqs`, `highFreqs`, los dos índices de fábrica, el tope de ganancia y el
paso del mando. El recorte de índices, los modos fijos, la puerta de hercios
continuos y la regla "cambiar la ganancia no reescribe la frecuencia" son de la
máquina, no del MS2000: son reglas, y una regla no es de nadie en concreto. Un
producto con dos bandas fijas por hardware instancia esto con su perfil; uno sin
selector usa la puerta de hercios continuos y se salta las tablas.

Esa puerta tiene una política y la política es **manda la última puerta usada**:
`setLowIndex` vuelve a encender la tabla después de que alguien haya escrito un
hercio a mano. Se puso al revés al principio —la puerta era de ida— y el banco
lo cazó en la primera corrida, porque contradecía el propio comentario del
código. Un test que encuentra un defecto en la máquina que está uno escribiendo
vale más que tres párrafos diciendo que no puede pasar.

Las tres están medidas, y las tres miran el sonido y no los libros:

- **La máquina es bit a bit las dos repisas en serie.** Se mide contra dos
  `ShelfFilter` conducidos a mano, no contra una fórmula reescrita en el test:
  comparar contra una reimplementación en vez de contra la que corre de verdad
  es el error clásico de este tipo de test.
- **El orden de las dos etapas da la misma respuesta, y no da igual bit a bit.**
  Las dos son LTI, así que la cascada es `H1(z)·H2(z)` y da igual el orden —
  eso es álgebra, no conmutatividad, que es la confusión que se cuela aquí—. Lo
  medido son de 1,9e-6 con g++ optimizado a 3,5e-6 con MSVC en Debug, 16 a 30
  ULPs, porque dos órdenes distintos del mismo sistema de orden 4 agrupan el
  redondeo de otra manera. El aserto va a 1e-4 con la cifra imprimida al lado: un
  umbral sin el valor medido al lado es un número que nadie revisa.
- **0 dB devuelve la señal bit a bit.** Es la misma identidad exacta que la del
  motor, pero vista a través de dos etapas, y es la razón por la que un preset
  nuevo con los dos mandos a cero no se oye.

Suman 933 comprobaciones, 28 por encima de las 905 de antes. Lo que **no** está
hecho: el shim de `ABDMS2000` sigue con sus propias tablas y no instancia
`CascadeShelfEq`, así que los números están ahora en dos sitios. El cambio de un
lado al otro es de un rato, pero el producto tenía trabajo sin commitear encima y
no se ha tocado.

#### El cambio de sonido, que no es cero

El original usaba `std::sqrt`, `std::pow`, `std::cos` y `std::sin` de libm. El
módulo no puede: solo tiene `DspMath.h`, y `sqrt` no está. Las cuatro
transcendentales se sustituyen, y la cuenta **corrige** a la que estaba escrita
antes, que era mala por partida doble —medía solo lo de `sqrt`, y en un punto:

| sustitución | error |
|---|---|
| `std::pow` → `dsp::pow` | 2 ulps |
| `std::sqrt` → `exp2 (0.5·log2 (A))` | 2 ulps |
| `std::cos` → `dsp::cos` | 5 ulps |
| `std::sin` → `dsp::sin` | 5 ulps |
| los cuatro juntos, en un coeficiente | **hasta 509 ulps** |
| y en la **respuesta** del filtro | **0,010 dB** |
| y en la **señal**, dos bandas | **−70,2 dBFS** de pico |

Los 509 ulps no son un error del filtro: son cuatro aproximaciones entrando en
las cinco fórmulas a la vez. Y los ulps de un coeficiente son un proxy débil —lo
que se oye es la respuesta—, así que las dos filas de abajo son las que valen.
La de la señal está medida **desde el producto**: las 16 combinaciones de las dos
bandas × 9 ganancias × los tres sample rates, contra una copia de las fórmulas
originales con libm. Y a **0 dB la salida es bit a bit la del original**, que es lo
que tiene que pasar: sin ganancia no hay raíz que sustituir.

#### El suavizado de los mandos, y por qué la tasa de control es 16

La fila no empuja los mandos al motor en cada bloque ni en cada muestra.
Recalcular los coeficientes son **cinco transcendentales**: MEDIDO, por muestra
son 194 ns, o sea el **20,7% de un núcleo a 48 kHz** para un biquad. Cada 16
muestras mide 17,9 ns (0,18%), y **entre 16 y 32 no hay diferencia real**: la
máquina estaba midiendo por debajo del ruido.

El umbral del clic, en cambio, sí depende, y hay que medirlo bien. **Un clic no es
"un salto grande": es un salto que no es de la señal**, y las tres primeras
mediciones lo midieron mal por tres motivos distintos, los tres escritos en
`DspEffectsTests.cpp` para que no se repitan:

- comparando el paso de la **salida** con el de la **entrada** se mide la ganancia;
- comparándolo con el de una referencia de ganancia **fija** se mide el
  crecimiento de amplitud del barrido (con +12 dB la salida pasa de 0 a 12 dB de
  punta a punta);
- pasar alta la diferencia a 5 kHz mide el **resonador** de la repisa, que en una
  repisa alta está justo ahí.

El criterio que sí funciona es darle a los dos lados **la misma trayectoria** y
cambiar solo cada cuánto se empujan los coeficientes. La desviación resultante,
barrido de 200 Hz a 16 kHz en 500 ms, es de unos 6 dB por octava:

| cada N muestras | repisa alta | repisa baja |
|---|---|---|
| 16 | −45,3 dBFS | −42,6 dBFS |
| 32 | −38,6 | −36,2 |
| 64 | −30,4 | −28,2 |
| 128 | −22,7 | −18,7 |
| 256 | −16,7 | −12,7 |

La puerta exige −40 dBFS en las dos repisas: 16 pasa con 2,6 dB de margen sobre la
peor, y 32 **ya se pondría roja**. Ese es su trabajo, no solo avisar de que hoy
está bien: impedir que alguien suba la tasa a "optimizar" y se lleve el clic sin
enterarse.

#### Y el `reset` asienta las rampas, que no es lo mismo que vaciar el estado

Un `reset` de host no debe **congelar** el mando —si lo hiciera, el usuario oíría
el mando parado hasta que moviera otra cosa—, así que la rampa de 20 ms sigue
corriendo después. Lo que sí tiene que hacer es vaciar el estado, y asentar la
rampa en su destino para que no siga bajando desde donde se quedó. MEDIDO, sin
el `jumpToTarget` el primer bloque tras un reset sale con un golpe de ganancia de
**más de 7 dB**; con él, −4,0 dB, que es la inercia del propio filtro.

#### Lo que este trabajo se llevó por delante

La puerta del clic y la del `reset` tardaron **cuatro y tres** reescrituras
respectivamente, y en los dos casos la primera versión no era floja: era
incorrecta. Dos cosas de esa lista valen para cualquier migración:

- **una comprobación que no puede fallar hace guardia**, y es peor que no
  tenerla. La de la independencia de bloque comparaba el mismo audio con
  distintos tamaños de bloque **con los mandos quietos**, así que pasaba incluso
  con el contador de control reiniciado por bloque, que es justo el fallo que
  decía cazar;
- **un script de mutación que se traga el error de compilación no mide nada**.
  `2>/dev/null` dejaba el ejecutable viejo de la vuelta anterior, y cinco
  mutaciones "no mordían" cuando en realidad las ocho estaban midiendo la misma
  tasa. La tabla que salió era plausible y completamente falsa. Y otro script
  reponía la constante buscando su valor exacto, así que a partir de la segunda
  vuelta ya no la encontraba: el mismo número ocho veces. Las dos instrumentaciones
  dan un resultado que parece un resultado.

**Verificado**: 754 comprobaciones de DspEffects con `-Wall -Wextra -Werror`, 5 de
5 puertas de la fila mordiendo, y el ecualizador de ABDMS2000 compilando contra
el módulo con su API intacta (`SynthEngine` y `WasmBridge` no se han tocado).

### La mitad JS: el contrato de efectos

Si tu proyecto tiene un WebUI con un desplegable de efectos, los nombres salen
de `ABDSharedAssets/contracts/fx-effects.json` (57 entradas, ids 0-56, campo
`generatedFrom` = `Source/DSP/FX/FXSlot_Factory.cpp`). Referencia de
implementación: `ABDEep/scripts/generate_fx_contract.mjs` +
`WebUI/js/fx_contract.gen.js` + `WebUI/tests/fxContract.test.js`.

Dos cosas que costaron, por si el proyecto siguiente las repite:

- **Los `effects_*.js` son scripts clásicos** (`<script src>`, se hablan por
  `window`), no ESM, así que no pueden importar el contrato. Y el resource
  provider sirve el árbol crudo cuando no hay `dist/`, así que el contrato —que
  vive fuera del árbol— no tiene ruta que servir. La solución es un `.gen`
  clásico generado, que funciona en los tres modos de servicio.
- **El array `effects` del contrato va agrupado por familia, no ordenado por
  id** (al final se leen `55, 39, 16, 19, 43, 49, 54, 56`). Indexarlo por
  posición en vez de por id es un error fácil de cometer.

Y una nota sobre por qué importa: la lista de nombres escrita a mano en la WebUI
estaba **desalineada con la fábrica** — enumeraba los ids 0..56 seguidos mientras
que la fábrica construye `1,2,3,4,5,6,22,26,27,28` para los reverbs. La web
llamaba "Ambience" a un Hall y el 22 (Deep Verb) figuraba como "Delay", y no
daba ningún error, porque eran dos listas de enteros que nadie contrastaba. Con
tres sitios que tienen que coincidir, el que se olvida de actualizar gana
siempre; por eso la regla es **contrato único + test que ate las capas**.

### La familia de filtros: tres filtros, un solo contrato

`DspCore/DspFilterFamily.h` es el contrato que todo filtro del sustrato honra, y
existe por un motivo concreto: que cambiar de filtro en un synth no sea reescribir
su voz. Son nueve métodos, y no es una clase base ni una vtable en el lazo de
audio (es el mismo patrón por convención que `DspEffects/EffectPolicy.h`):

```cpp
void   prepare (double sampleRate) noexcept;
void   reset () noexcept;
void   setCutoff (double hz) noexcept;          // en Hz, no en 0..1
void   setResonance (double amount01) noexcept; // 0 = sin realce, 1 = borde
bool   setMode (FilterMode) noexcept;           // false = no lo honra (no miente)
double processSample (double input) noexcept;
double getCutoff () const noexcept;
double getResonance () const noexcept;
bool   supportsMode (FilterMode) noexcept;
```

Y un renglón para engancharlo:

```cpp
using VoiceFilter = abd::dsp::FilterEquation;   // o FilterTpt, o abd::lutdsp::FilterLut
```

**El idioma de los parámetros es la mitad de la familia.** El corte se pide en Hz
y la resonancia en 0..1 con el mismo significado en todos los miembros (0 sin
realce, 1 en el borde). Si no fuera así, cambiar de filtro cambiaría la afinación
del panel, que es peor que no poder cambiar. La CURVA con la que cada topología
llega a ese borde es suya y la declara: una Q exponencial de 1/√2 a 20 en el TPT,
una realimentación de escalera en el de ecuación.

| Miembro | Dónde | Modos | De dónde sale el corte |
|---|---|---|---|
| `FilterTpt` | `DspCore/DspFilterTpt.h` | paso bajo, paso alto, banda | la curva de mando de la familia |
| `FilterEquation` | `DspCore/DspFilterEquation.h` | paso bajo | la misma curva; escalera de cuatro polos con el lazo resuelto **sin retardo** |
| `FilterLut` | `LutDSP/LutFilter.h` | paso bajo | **medido**: tabla de ABDAudioLab, interpolación bilineal, sobre el núcleo TPT |

El `processBlock` (float y double) es de la familia y no lo implementa cada
miembro. Y el rasgo `IsFilterStage<T>` convierte la convención en algo que el
compilador comprueba: `static_assert (IsFilterStage<MiFiltro>::value, "...")`.

Dos cosas **medidas** que conviene saber antes de tocar esto:

- El miembro TPT repite a propósito la formulación de `ResonantFilterStage` (a la
  etapa compartida no se le tocan los taps: tiene su AGC y hay productos que ya la
  usan), y un test compara las dos salidas a resonancia 0 y exige **1e-12**. La
  repetición está vigilada: si se separan, cae el test, no el oído.
- El tope de resonancia del miembro de ecuación está **por encima** del umbral
  lineal de 4.0 (en 4.5) porque con el `tanh` en el lazo un `k = 4.0` exacto **no
  canta, se apaga**: saturar baja la ganancia en cuanto la señal deja de ser
  infinitesimal. Lo encontró el test de "a tope la escalera canta".

**Verificado**: 2316 comprobaciones de DspCore (las nuevas de la familia incluidas)
y 33 de LutDSP, los dos a C++17 y sin JUCE.

---

### La familia de osciladores: dos osciladores, un solo contrato

`SynthCore/OscillatorFamily.h` es la hermana de la familia de filtros: el contrato
que todo oscilador del módulo honra, para que cambiar de oscilador en un synth no
sea reescribir su voz. Es el mismo patrón por convención (ni clase base ni vtable
en el lazo de audio) y son quince métodos:

```cpp
void         prepare (double sampleRate) noexcept;
void         reset () noexcept;
void         setFrequency (double hz) noexcept;       // en Hz, no en 0..1
bool         setWaveform (OscWaveform) noexcept;      // false = no lo honra
bool         setOversampling (int factor) noexcept;   // 1/2/4/8; false = no lo honra
void         setPulseWidth (double duty01) noexcept;  // 0.5 = cuadrada
double       processSample () noexcept;
double       getFrequency () const noexcept;          // la que SE ESTA usando
double       getPhase () const noexcept;
OscWaveform  getWaveform () const noexcept;
int          getOversampling () const noexcept;
double       getPulseWidth () const noexcept;
double       getLatencySamples () const noexcept;
bool         supportsWaveform (OscWaveform) noexcept;
bool         supportsOversampling (int factor) noexcept;
```

Y un renglón para engancharlo:

```cpp
using VoiceOsc = abd::synth::OscVcoCa72;   // o abd::synth::OscPolyBlep
```

**El idioma de los parámetros** es, otra vez, la mitad de la familia: la
frecuencia en Hz (y `getFrequency()` devuelve la que de verdad está usando, no la
que le pidieron), un enumerado de formas **básicas**, el ancho de pulso en 0..1 con
el mismo significado en todos (0.5 cuadrada; `oscMinPulseWidth` y
`oscMaxPulseWidth` recortan lo que no es un pulso), la salida bipolar y
nominalmente ±1 (`oscNominalPeak`) —para que cambiar de oscilador no cambie el
nivel que ve el mezclador— y el sobremuestreo como factor 1/2/4/8. La CURVA con la
que cada pieza llega a ese idioma es suya y la declara: el VCO resuelve la
corriente de su propio núcleo para dar la frecuencia pedida; el de fase se queda en
0,45 de Nyquist porque su corrección de un salto abarca un periodo de muestreo a
cada lado.

| Miembro | Dónde | `kind` | Formas | De dónde sale su carácter |
|---|---|---|---|---|
| `OscVcoCa72` | `SynthCore/OscVcoCa72.h` | `Circuit` | sierra, triángulo, rectángulo | **medido**: el perfil del banco de pruebas (`OscVcoCa72Profile.h`) |
| `OscPolyBlep` | `SynthCore/OscPolyBlep.h` | `Phase` | las cuatro | las formas básicas exactas, sobre el `PolyBLEP` que ya tenía el módulo |
| `OscReference` | `SynthCore/OscReference.h` | `Phase` | sierra, seno | el esqueleto del contrato: las dos formas que tiene esta versión del prototipo, y lo que le falta dicho en voz alta (triángulo y rectángulo, ningún sobremuestreo). No es una pieza de sonido, es la referencia estandarizada de cómo se escribe un miembro nuevo aquí |

El `processBlock` (float y double) es de la familia y no lo implementa cada
miembro. Y el rasgo `IsOscillator<T>` convierte la convención en algo que el
compilador comprueba: `static_assert (IsOscillator<MiOscilador>::value, "...")`.
Un miembro a medias —le falta `getPhase()`— no pasa el rasgo, y hay un test que lo
comprueba con un oscilador a medias puesto a propósito.

#### La pieza portada: el VCO de rampa

`OscVcoCa72` es el VCO de un sintetizador analógico de los años 70 tal como está
medido en el banco de pruebas de su estudio: un condensador que integra una
corriente, un disparador de Schmitt que reinicia la rampa cuando cruza su umbral
(con el **retardo del comparador** y la espera del transistor) y tres
conformadores que salen del mismo nodo: el separador, su diente de sierra y el
comparador del rectángulo con su histéresis. Su hardware entra como DATOS
(`OscVcoCa72Profile.h`): capacidades, umbrales, retardos y las constantes de cada
conformador, a 25 °C y sin carga. La curva **medida** de la transferencia del
triángulo (891 puntos en la referencia) no se incrusta: se **inyecta**
(`setTriangleTable`), y por defecto el perfil trae los tres puntos de esa curva en
el barrido del núcleo —su pliegue y sus dos extremos—, con los que el triángulo
sale asimétrico, que es como es.

Reescrito limpio desde la idea: la referencia está bajo GPL y **no se ha copiado ni
una línea**; lo único que viaja son los números medidos del aparato, que sin ellos
la pieza no sería el aparato sino un oscilador genérico.

Tres cosas que este trabajo encontró **midiendo**, y que conviene saber antes de
tocar esto:

- **La fase de este núcleo es tiempo, no tensión.** La rampa es cóncava (la
  corriente baja al caer la tensión), así que la mitad de su RECORRIDO la alcanza
  en el 49,26 % de su TIEMPO. Con la fase medida en tensión, el ancho de pulso de
  la familia mentía un 0,7 % en la mitad del ciclo. Lo encontró el test del ancho
  (0.50 daba 0.491); ahora da 0.499, y las dos cosas —`getPhase()` y la curva de
  ancho— pasan por la misma ley despejada, que es lo que impide que se separen.
- **El comparador no llega a cualquier ancho.** Más estrecho que 0,051 de ciclo, su
  umbral de subida queda por encima del techo del separador y el rectángulo se
  queda ABAJO para siempre, que no es un pulso fino sino una avería. La pieza lo
  declara (`minPulseWidth()`) y lo recorta, en vez de sonar mal.
- **La corriente se busca en logaritmo.** La corriente útil del núcleo va de las
  décimas de nanoamperio de la nota más grave al miliamperio del tope: cuatro
  décadas. Bisecar en lineal daba la misma precisión ABSOLUTA en todo el
  recorrido, o sea una precisión relativa pésima abajo, y el test de 0,05 Hz lo
  dijo. Ahora la bisección va en logaritmo y la frecuencia se clava.

El limitado de banda tampoco es una promesa. La pieza corre su núcleo a 1, 2, 4 u 8
veces la frecuencia de salida, coloca los escalones del reinicio y de los flancos
del rectángulo en su instante exacto (polyBLEP, con el área del rizo del triángulo
y del retraso del separador) y baja con `SynthCore/OscHalfbandDecimator.h` (etapas
de media banda: la corta delante y la larga al final, y los coeficientes a
distancia par del centro, que valen cero de verdad, saltados — que es lo que hace
pagable el 8× dentro de una voz). Medido: para un diente de sierra de 7 kHz, la
basura que a 1× queda en 16,1 kHz (el cuarto armónico plegado) vale 2,6e-2, y a 8×
vale 8,0e-7. El diezmador, solo, deja pasar un tono dentro de la banda que
sobrevive a 1,0000 y tapa uno por encima del nuevo Nyquist a 1e-6. Y la pieza
**declara su retardo** en vez de mentir con un oscilador que suena medio periodo
antes: 1,0 muestra de salida a 1× y 23,75 a 8×.

Lo que la pieza NO hace, dicho en voz alta: **no tiene seno** (`supportsWaveform`
dice que no y `setWaveform` lo rechaza sin cambiar nada), y la cadena
voltios→corriente del convertidor exponencial —los trimpots, el par de transistores
y su temperatura— queda fuera a propósito, porque el idioma de la familia es Hz y
esa conversión ya la ha hecho quien pide. Lo que sí se queda es lo que el
convertidor le hace a la rampa: la corriente cae al caer la rampa (el efecto
Early), y por eso la rampa es un poco cóncava y el diente de sierra no es una
recta. La puerta del circuito sigue abierta para quien quiera modular en
corriente —que es como lo hace el aparato—: `setTimingCurrent`.

**Verificado**: 2097 comprobaciones de `ABDShared_SynthCore_Tests` (las nuevas de
la familia incluidas), exit 0. Y la medición que más vale de todas: el peor error
de frecuencia medido entre 55 Hz y 4 kHz es 0,0071 %, y una octava es exactamente el
doble de periodo.

---

## Módulo: SynthCore — Arpeggiator (C++20, sin JUCE)

> **Documentación exhaustiva de integración:**  
> Consulta [`docs/ARPEGGIATOR_INTEGRATION_GUIDE.md`](docs/ARPEGGIATOR_INTEGRATION_GUIDE.md) para el manual completo con ejemplos detallados, tabla de cálculo de frecuencia para sincronización de BPM y el patrón de adaptador para plugins JUCE.

### Qué hay dentro

`SynthCore/Arpeggiator.h` y `SynthCore/Arpeggiator.cpp` implementan el motor de arpegiador algorítmico `abd::synth::Arpeggiator`:
- **100% C++20 puro, agnóstico de frameworks:** sin dependencias de JUCE, GUI ni llamadas al sistema operativo.
- **Zero-alloc en el render loop:** `generate(...)` opera sobre buffers estáticos acotados (`kMaxHeld = 64`, `kMaxNotesPerStep = 8`, `kMaxPending = 32`). Libre de asignaciones dinámicas y seguro para el hilo de audio en tiempo real.
- **Sample-accurate:** cálculo de desplazamientos temporales de muestra exactos para eventos Note-On y Note-Off dentro de cada bloque de proceso.
- **Retrigger sin colisiones:** si una nota vuelve a sonar en el mismo paso antes de apagarse, emite un Note-Off explícito en la misma muestra antes del nuevo Note-On.
- **Generador pseudoaleatorio determinista:** `FastRng` (Xorshift32) integrado para reproducibilidad idéntica bit a bit del modo Random (modo 8) entre plataformas.

### Modos de reproducción (11 modos: 0 a 10)

| Modo | Nombre | Descripción |
|:---:|:---|:---|
| 0 | `Up` | Ascendente: de menor a mayor nota |
| 1 | `Down` | Descendente: de mayor a menor nota |
| 2 | `Up/Down` | Sube y baja sin repetir extremos |
| 3 | `Up-Inv` | Sube y, al completar ciclo, sube invirtiendo |
| 4 | `Down-Inv` | Baja y luego baja invirtiendo |
| 5 | `Up/Down-Inv` | Sube y baja atravesando inversiones de acordes |
| 6 | `Up-Alt` | Alternancia: más baja, más alta, alternando |
| 7 | `Down-Alt` | Alternancia: más alta, más baja, alternando |
| 8 | `Random` | Selección pseudoaleatoria determinista con `FastRng` |
| 9 | `As-Played` | En el orden cronológico en que se tocaron las notas |
| 10 | `Chord` | Todas las notas del pool se disparan simultáneamente al unísono |

### Enlace CMake

```cmake
target_link_libraries(TuProyecto PRIVATE ABDShared::SynthCore)
```

### Consumo directo (Zero-alloc por invocable / lambda)

```cpp
#include "SynthCore/Arpeggiator.h"

// En el lazo de proceso de audio del sintetizador:
arpeggiator.generate(numSamples, [&](const abd::synth::Arpeggiator::NoteEvent& ev) {
    if (ev.isNoteOn)
        motorVoces.dispararNota(ev.note, ev.velocity, ev.sampleOffset);
    else
        motorVoces.apagarNota(ev.note, ev.sampleOffset);
});
```

### Patrón para consumidores JUCE (Shim de compatibilidad)

Si el sintetizador usa JUCE (`juce::MidiBuffer`), hereda de la clase base y utiliza `using` para evitar el *name-hiding*:

```cpp
// Source/DSP/Arpeggiator.h en el proyecto del sintetizador:
#pragma once
#include "SynthCore/Arpeggiator.h"

namespace juce { class MidiBuffer; }

namespace MiSynth
{
    class Arpeggiator : public abd::synth::Arpeggiator
    {
    public:
        using abd::synth::Arpeggiator::Arpeggiator;
        using abd::synth::Arpeggiator::generate; // Expone la plantilla base

        void generate(juce::MidiBuffer& out, int numSamples);
    };
}
```

```cpp
// Source/DSP/Arpeggiator.cpp:
#include "Arpeggiator.h"
#include <JuceHeader.h>

namespace MiSynth
{
    void Arpeggiator::generate(juce::MidiBuffer& out, int numSamples)
    {
        abd::synth::Arpeggiator::generate(numSamples, [&out](const NoteEvent& ev) {
            if (ev.isNoteOn)
                out.addEvent(juce::MidiMessage::noteOn(1, ev.note, ev.velocity), ev.sampleOffset);
            else
                out.addEvent(juce::MidiMessage::noteOff(1, ev.note, 0.0f), ev.sampleOffset);
        });
    }
}
```

---

## Módulo: SynthCore — ControlSequencer (C++20, sin JUCE)

> **Documentación exhaustiva de integración:**  
> Consulta [`docs/CONTROL_SEQUENCER_INTEGRATION_GUIDE.md`](docs/CONTROL_SEQUENCER_INTEGRATION_GUIDE.md) para el manual de referencia completo y la tabla detallada de divisores de reloj.  
> Consulta [`docs/SYNTHCORE_TRIAD_INTEGRATION_GUIDE.md`](docs/SYNTHCORE_TRIAD_INTEGRATION_GUIDE.md) para el SSOT de la tríada de control y modulación (`Arpeggiator` / `ModMatrixT` / `ControlSequencer`), perfiles de shims y consumo en sintetizadores.

### Qué hay dentro

`SynthCore/ControlSequencer.h` y `SynthCore/ControlSequencer.cpp` implementan el motor analógico de secuenciador de control `abd::synth::ControlSequencer`:
- **100% C++20 puro, agnóstico de frameworks:** sin dependencias de JUCE, GUI ni llamadas al sistema operativo.
- **Zero-alloc en el render loop:** `nextSample()` opera sobre memoria estática interna (`float steps[32]`) con cálculo de muestra continua y curvas de deslizamiento analógicas.
- **Doble participación en la arquitectura de modulación:**
  - Actúa como **fuente de modulación** (`ModSource::kControlSequencer` #18 en DeepMind), entregando valores bipolares `[-1.0f, +1.0f]`.
  - Actúa como **destino de modulación** (`ModDestination::kSeqSlew` #72 en DeepMind), permitiendo modular dinámicamente la velocidad de glide mediante `setSlewModulation(float m)`.
- **16 divisores métricos de reloj:** desde 4 notas enteras (16 negras) hasta tresillo de semicorchea (1/6 negra), anclado a `setMasterBpm`.
- **Swing continuo ponderado:** partición rítmica de los pasos pares e impares ($r = 0.5 + 0.25 \times \text{swing}$).
- **Filtro analógico de Slew / Glide:** filtro exponencial de 1 polo ($\tau = s \times 1.0\text{s}$, $\text{coef} = 1 - e^{-1/(\tau \cdot F_s)}$).

### Enlace CMake

```cmake
target_link_libraries(TuProyecto PRIVATE ABDShared::SynthCore)
```

### Consumo directo (Audio loop por muestra)

```cpp
#include "SynthCore/ControlSequencer.h"

// En el lazo de proceso de audio del sintetizador:
for (int i = 0; i < numSamples; ++i)
{
    const float seqBipolar = controlSequencer.nextSample();
    modSources[(int)ModSource::kControlSequencer] = seqBipolar;
}
```

### Patrón para consumidores JUCE (Shim de compatibilidad)

```cpp
// Source/DSP/ControlSequencer.h en el proyecto del sintetizador:
#pragma once
#include "SynthCore/ControlSequencer.h"

namespace MiSynth
{
    class ControlSequencer : public abd::synth::ControlSequencer
    {
    public:
        using abd::synth::ControlSequencer::ControlSequencer;
    };
}
```

---

## Módulo: SynthCore — LfoAnalog (C++20, sin JUCE)

> **Documentación exhaustiva de integración:**  
> Consulta [`docs/LFO_ANALOG_INTEGRATION_GUIDE.md`](docs/LFO_ANALOG_INTEGRATION_GUIDE.md) para el manual de referencia completo, curvas matemáticas de fade-in delay y fórmulas del slew limiter analógico.

### Qué hay dentro

`SynthCore/LfoAnalog.h` y `SynthCore/LfoAnalog.cpp` implementan el oscilador de baja frecuencia modelado analógicamente `abd::synth::LfoAnalog`:
- **100% C++20 puro, agnóstico de frameworks:** sin dependencias de JUCE ni GUI.
- **7 formas de onda continuas:** Seno, Triángulo, Cuadrada, Rampa Arriba, Rampa Abajo, Sample & Hold y Sample & Glide.
- **Curva analógica de delay y fade-in:** 40% inicial de silencio absoluto seguido de un 60% de rampa lineal de amplitud.
- **Limitador de pendiente analógica (*Slew Limiter*):** rampa de variación continua en el dominio temporal dependiente de sample-rate.
- **Rango audio-rate:** de 0.005 Hz a 1280.0 Hz.
- **Zero-alloc en el render loop:** `nextSample()` opera exclusivamente sobre tipos primitivos escalares.

### Enlace CMake

```cmake
target_link_libraries(TuProyecto PRIVATE ABDShared::SynthCore)
```

### Consumo directo

```cpp
#include "SynthCore/LfoAnalog.h"

// Inicialización:
lfo.setSampleRate(44100.0);
lfo.setRate(2.5f); // Hz
lfo.setShape(1);   // Triángulo

// En el lazo de proceso:
float lfoVal = lfo.nextSample(); // Salida bipolar [-1.0f, +1.0f]
float uniVal = lfo.getUnipolar(); // Salida unipolar [0.0f, 1.0f]
```

### Patrón para consumidores JUCE (Shim de compatibilidad)

```cpp
// Source/DSP/LFO.h en el proyecto del sintetizador:
#pragma once
#include "SynthCore/LfoAnalog.h"

namespace MiSynth
{
    class LFO : public abd::synth::LfoAnalog
    {
    public:
        using abd::synth::LfoAnalog::LfoAnalog;
        using Shape = abd::synth::LfoAnalog::Shape;
    };
}
```

---

## Módulo: MidiKeyboard (WebUI/JS)

> **OJO: este módulo tiene DOS mitades y solo una es JS.** El paquete
> `@abdsynths/midi-keyb` (`src/`, `tests/`, `package.json`) se consume desde las WebUI
> de los proyectos vía pnpm workspace, igual que `@abdsynths/shared` (ABDSharedAssets),
> y **no** pasa por CMake. Pero en la misma carpeta hay una capa nativa —
> `JuceMidiKeyboardComponent.h`, `MidiKeyboardFloatingWindow.h` y
> `MidiKeyboardResourceProvider.{h,cpp}`— que monta el WebUI del teclado dentro de un
> plugin y **sí** tiene target: `ABDShared::MidiKeyboardCpp`.
>
> ```cmake
> target_link_libraries(TuPlugin PRIVATE ABDShared::MidiKeyboardCpp)
> ```
>
> Va `Cpp` y no a secas para que no se confunda con el paquete npm: `MidiKeyboard`
> ya es el nombre del paquete pnpm, y un target homónimo haría que un error de
> enlace dijera `MidiKeyboard` sin que quede claro de qué mitad habla.

### Correr las pruebas de este paquete

`vitest`, y se ejecutan desde un clon limpio de `ABDSharedCode` con un comando:

```bash
node tools/bootstrap-workspace.mjs   # pone el hermano donde falta e instala
pnpm test                           # 340 pruebas
```

El bootstrap existe porque el paquete declara `"@abdsynths/shared": "workspace:*"`
y ese paquete vive en **ABDSharedAssets, otro repo**. Sin él, un clon limpio de
ABDSharedCode no puede resolver su propia dependencia, y el error que da pnpm
(`Cannot resolve package from workspace...`) no nombra el paquete que falta.
`--check` comprueba lo mismo sin instalar ni escribir.

**El mismo script arranca el workspace en CI.** La acción
`pnpm-workspace-bootstrap` (la que usan ABDEep, ABDMS2000 y ABDCZ101) hace los tres
checkouts y luego llama a este script con dos flags:

```bash
node ABDSharedCode/tools/bootstrap-workspace.mjs \
  --workspace-root "$GITHUB_WORKSPACE" --project "$ABD_PROJECT"
```

`--workspace-root` dice dónde está el workspace —que en CI no es este repo sino el
directorio que arma la acción— y `--project` quién es el repo llamante, que es lo
único que CI sabe. Con ellos, el script genera `pnpm-workspace.yaml`, instala y
**verifica el layout** (que los paquetes `@abdsynths/*` estén enlazados), que antes
vivía en un `run:` de bash. Un `pnpm install` puede salir en verde y dejar las
dependencias `workspace:*` sin enlazar, y eso antes no se descubría hasta que
Vitest reventaba veinte pasos más tarde con un error que no menciona el workspace.

Los dos flags van **juntos o ninguno**: `--project` sin `--workspace-root` no dice
dónde va el repo llamante, y `--workspace-root` sin `--project` genera un workspace
al que solo le faltan de miembros lo único que CI sabe. Aceptarlos por separado
daría dos formas de pasar que no funcionan.

Esto no es una conciliación de estilos: es que **la lógica del workspace estaba
duplicada**, una copia en bash que solo corría en CI y otra en el script que corre
en local. Dos copias se separan en silencio, y la que se separa es la que nadie
ejecuta. Añadir un paquete pnpm a ABDSharedCode, o cambiar la tabla `allowBuilds`,
ahora toca **un** sitio.

Un fichero de la suite necesita la suite entera: `tests/host-strip-height.test.js`
es el contrato de franja de los cuatro synths que montan el teclado, y lee su
CSS y su código. Desde un clon de ABDSharedCode solo, sus tres bloques de
workspace se **omiten con un motivo impreso** y corren los otros 331 tests;
desde la raíz de la suite corren los 340.

### El CI de este repo (`.github/workflows/shared-code-ci.yml`)

Es el primero de este repo, y su limitación conviene saberla antes de fiarse:
ABDSharedCode no tiene código propio que ejecutar, es código que vive aquí y se
consume desde fuera. Lo único comprobable sin esos repos es que el código está
sano por sí mismo. Cuatro trabajos, **los cuatro puertas** (ninguno en `warning`):

| Trabajo | Qué corre | Por qué |
|---|---|---|
| `tools` | `node --test tools/guard_atributos.test.mjs` (46) | Sin dependencias: `node:test` y `assert` vienen en el runtime, y estas herramientas no están dentro del workspace pnpm. Meter un runner para 46 pruebas sería meter una cadena de dependencias en un guard que vigila los binarios. |
| `midikeyboard` | la acción `pnpm-workspace-bootstrap` + `pnpm test` (340) | Usa la acción de **este** repo, `ajabadia/ABDSharedCode/.github/actions/pnpm-workspace-bootstrap@master`, que a su vez llama a `tools/bootstrap-workspace.mjs`. |
| `foto` | `python tools/verificar_foto.py --check` | Comprueba que la línea base se midió contra los SHA que el workflow clona. No clona nada: son dos datos escritos los comparan. |
| `audit` | 5 hermanos por SHA inmutable + el audit con trinquete | El auditor compara contra la suite; los SHA son lo que hace el veredicto reproducible. |

**Por qué el job nombra el fichero y no el directorio.** `node --test tools/` es
una orden que funciona o no según la versión de Node en un punto que no se ve.
Hasta la 20, `node --test` recibía un **directorio** y lo buscaba recursivamente;
desde la 21 recibe **patrones glob**, y el glob `tools` casa con el propio
directorio, que Node carga como módulo y responde `MODULE_NOT_FOUND`. El glob
explícito tampoco vale como arreglo general: en la 20 el argumento se toma
literal. Como el CI usa la 20 y la máquina de desarrollo ya va por la 24,
cualquier forma con directorio o glob pasa en un lado y falla en el otro.

El precio de nombrar los ficheros es que hay que nombrarlos: un
`tools/nuevo.test.mjs` que nadie nombra **no falla, no avisa y sale con 0**. Eso
no lo comprueba un paso del workflow sino la propia suite, cuyo último bloque
falla si hay más `*.test.mjs` en `tools/` que los que dice la cabecera del test.

Y el job `midikeyboard` comprueba que el motivo del `OMITIDO` aparezca en la
salida: un salto sin motivo se lee como una comprobación que pasó.

El paso de audit **mide** y anota su código de salida con `set +e`; el paso
siguiente es el que **decide** y el que puede fallar. Así el código no se pierde
y no hace falta un `continue-on-error`, que era justo lo que dejaba el job verde
sin mirar.

#### El trabajo `foto`: la línea base tiene que decir contra qué SHA se midió

Es el cuarto trabajo y cierra el agujero por el que el trinquete **empieza a
mentir sin que se note**. La línea base decía 55 nombres, pero no decía contra
qué foto se midieron, y el trinquete solo compara **nombres**. Medido en un clon
con los cinco hermanos en su SHA: un commit nuevo en ABDNeural que no toca
ningún consumidor deja el audit en **0**, porque el conjunto de huérfanas no
cambia —siguen siendo las mismas 55, los consumidores siguen sin commitear— y
nadie puede saber que se ha movido el suelo. El veredicto es verde y ya no
habla de lo que CI va a medir.

La solución es que la foto sea un **dato**: `--write-baseline` escribe en el
JSON la clave `foto`, con el `git rev-parse HEAD` de cada repo hermano en el
disco donde se midió. Y `tools/verificar_foto.py` compara esa foto con los SHA
que este mismo workflow clona, leyendo el YAML en vez de llevar su propia lista
—una lista sería una segunda copia de los SHA, y las copias se separan en
silencio—. Por eso no hay nada que avisar cuando cambia un SHA: cambia lo que
el guard lee.

| Situación | Código |
|---|---|
| La foto de la línea base es la del workflow | **0** |
| Un SHA del workflow no es el que dice la foto (se subió sin regenerar) | **1** |
| La línea base no tiene clave `foto`: no se puede saber contra qué se midió | **1** |
| Un repo de la foto sin SHA, que es una foto con huecos que parece completa | **1** |
| Un repo que el workflow clona y no está en la foto | **1** |
| El workflow no se puede leer | **2** (no es un hallazgo: no se midió nada) |

Va como trabajo aparte y no como paso del job `audit` por una razón medida: si
fuera un paso, su fallo y el del audit caerían en el mismo log y no se podrían
distinguir. Así se sabe que lo que se movió es el suelo.

El cuarto paso de ese trabajo es un aviso, y es el **único paso del repo con
`continue-on-error`**: si un force-push borra el SHA que el workflow clona,
`actions/checkout` falla con un mensaje de refs que no nombra la línea base.
Este paso lo dice antes y en el job que se llama `foto`, que es donde la foto es
el asunto. Es `warning` y no puerta a propósito: un SHA borrado no es un
problema de este repo, y si el remoto no responde lo único que falta es la
*existencia* del SHA, de lo que el checkout informa mucho mejor. El listado sale
de `verificar_foto.py --listar`, no de un parser escrito en el `run:`: dos vistas
de "los SHA del workflow" escritas en dos sitios se separan en silencio, y la
que se separa es la que nadie ejecuta.

El caso al revés, que es el peor y el que el guard hace visible: si la línea
base se regenera **en local** —donde los hermanos tienen el trabajo sin
commitear en el disco— lo que se commitea no son las huérfanas de CI. El audit
daría 1 con 55 huérfanas nuevas y el motivo no nombraría la causa. El orden que
funciona es: dejar cada hermano en el SHA del workflow, `--write-baseline`, y
commitear la línea base **con el workflow ya subido**.

Teclado virtual completo (keybed responsivo, ruedas pitch/mod con filmstrip, pedals,
QWERTY, touch, chord memory, scale filter) + **API de feedback host-driven** (v0.2.0) para
reflejar MIDI externo (hardware/DAW/bridge nativo) en la UI **sin eco**: los moves aplicados
por el host no se re-disparan como input de usuario.

### Requisito: workspace pnpm

El proyecto consumidor debe ser workspace pnpm (o estar en uno) que incluya el paquete y sus
internos como miembros — el paquete declara `"@abdsynths/shared": "workspace:*"`, así que
`ABDSharedAssets` debe ser miembro del MISMO workspace:

```yaml
# pnpm-workspace.yaml del proyecto consumidor (ej. ABDNeural/WebPilot)
packages:
  - '.'
  - '../../ABDSharedAssets'
  - '../../ABDSharedCode/MidiKeyboard'
```

```json
// package.json del proyecto consumidor
"dependencies": {
  "@abdsynths/midi-keyb": "workspace:*",
  "@abdsynths/shared": "workspace:*"
}
```

### Consumo (React/Next — ver ABDNeural/WebPilot/app/page.jsx; en vanilla, ver ABDMS2000)

```js
import { createKeyboard } from '@abdsynths/midi-keyb';
import '@abdsynths/midi-keyb/keyboard.css';

// Contenedores por id (keybed + ruedas); callbacks para el camino de salida.
const keyboard = createKeyboard({
  containerId: 'piano-keyboard',
  wheelPitchId: 'pitch-wheel-container',
  wheelModId: 'mod-wheel-container',
  onNoteOn: (note, velocity) => { /* -> motor */ },
  onNoteOff: (note) => { /* -> motor */ },
  onPitchBend: (value) => { /* -1..+1 -> motor */ },
  onModWheel: (value) => { /* 0..1 (CC1) -> motor */ },
  onPanic: () => { /* -> motor allNotesOff */ },
});

// ... y en el desmontaje: keyboard.destroy();
```

### API de feedback host-driven (v0.2.0, SIN eco)

```js
// Reflejar el MIDI que llega al PLUGIN (hardware, DAW, bridge nativo):
keyboard.setPitchBend(-0.5);            // mueve la rueda SIN re-disparar onPitchBend
keyboard.setModWheel(0.75);             // igual para la rueda de mod (CC1)
keyboard.notesOffVisual([60, 64]);      // apaga resaltes SIN disparar onNoteOff
keyboard.highlightNote(60, 0.9);        // resalta una tecla (input del host)
```

> Detalle fino: el slider del pitch wheel es con signo (`-8192..+8191`, centro 0).
> `setPitchBend` hace la conversión; si mueves el slider a mano, no asumas 0..16383.

### Patrón de integración con bridge nativo (WebView2)

El consumidor tipo (NEURONiK WebPilot) cablea así:

- **Salida (UI → motor):** los callbacks de `createKeyboard` emiten mensajes del protocolo
  del bridge (`midiNoteOn/Off/pitchBend/modWheel/panic`); el plugin valida rangos e inyecta
  por su FIFO MIDI lock-free (el MISMO camino del editor JUCE).
- **Entrada (motor → UI):** el host publica periódicamente el estado externo del plugin
  (notas mantenidas + posiciones de rueda) y la página lo aplica con la API de feedback
  silenciosa de arriba — hardware y DAW se ven en la página sin bucles de eco.

### Pruebas

```bash
cd ABDSharedCode/MidiKeyboard && pnpm install && pnpm test   # 323 tests, vitest + jsdom
```

---

## Módulo: HardwareMidiDetect

Detecta automáticamente hardware MIDI sintetizador conectado, de forma 100% contract-driven: **ninguna consulta SysEx ni mapeo de fabricante/modelo está hardcodeado**. Todo se deriva de los contratos JSON en `ABDSharedAssets/contracts` (single-source).

### Dos capas consumibles

El módulo ofrece **dos arquitecturas**, análogas a cómo se integra ABDScope:

| Capa | Clase | Cuándo usar |
|------|-------|-------------|
| **C++ puro** | `abd::hwid::HardwareMidiDetector` | Detección programática sin UI (tests, headless, lógica previa a mostrar UI) |
| **WebView2 + WebUI** | `abd::hwid::JuceHardwareMidiPicker` | UX completa de detección con modal de selección; el host inyecta el transporte |

### Paso 1: CMakeLists.txt del proyecto

```cmake
# (integración de ABDSharedCode como en el módulo AutoUpdater, arriba)
target_link_libraries(TuPlugin PRIVATE ABDShared::HardwareMidiDetect)
```

> El WebUI del picker embebe `index.html` + JS via `juce_add_binary_data`. Los assets de estilos (`styles/`), imágenes de modelos (`models/`) y logos de marcas (`brands/`) se sirven desde filesystem (`ABDSharedAssets/`) para permitir actualizaciones sin recompilar.

### Paso 2: Contratos desde ABDSharedAssets

Cargá los contratos (single-source en `ABDSharedAssets/contracts`) con el registry compartido:

```cpp
#include <HardwareMidiDetect/HardwareContractRegistry.h>

abd::hwid::HardwareContractRegistry registry;
// Ruta relativa al monorepo; ajustá searchRoots según tu layout.
juce::File contractsDir = juce::File::getCurrentWorkingDirectory()
    .getChildFile("../../../ABDSharedAssets/contracts");
bool ok = registry.loadContractsFromDirectory(contractsDir);
if (ok) {
    auto contracts = registry.getContracts();   // std::vector<abd::hwid::HardwareContract>
}
```

### Capa 1 — C++ puro: `HardwareMidiDetector`

Ideal para tests y detección programática:

```cpp
#include <HardwareMidiDetect/HardwareMidiDetector.h>
using abd::hwid::HardwareMidiDetector, abd::hwid::DiscoveredDevice;

HardwareMidiDetector::DetectionConfig config;
config.allowedHardwareIds = {};           // vacío = todos los contratos
config.maxResults = 1;                    // 1 = single, >1 = multi
config.autoSelectIfSingle = true;         // auto-callback si 1 match
config.includeHeuristic = true;           // incluir matches por nombre puerto
config.requireSysExVerified = false;      // solo SysEx verificado

HardwareMidiDetector detector(registry.getContracts());
auto found = detector.scanAllPorts(config, /*timeoutMs=*/350);
for (auto& dev : found) {
    dev.hardwareId;           // e.g. "korg_ms2000"
    dev.displayName;          // e.g. "Korg MS2000 / MS2000R"
    dev.portIndex;            // índice del puerto de salida
    dev.deviceId;             // deviceId del Identity Reply (0x00-0x7F)
    dev.isSysExVerified;      // true = confirmado por SysEx
    dev.modelImage;           // "models/korg-ms2000.png"
    dev.brandLogo;            // "brands/korg-logo.svg"
}

// Queries derivadas de contratos (sin hardcoding):
auto queries = HardwareMidiDetector::buildDetectionQueries(registry.getContracts());
// Siempre incluye la Universal Identity Inquiry + cada autoDetectSysEx único.
```

### Capa 2 — WebView2 + WebUI: `JuceHardwareMidiPicker`

**El software llamante prepara el puente** (patrón ABDScope). Implementás `MidiHardwareBackend` sobre tus `MidiOutput`/`MidiInput`, lo inyectás al picker, y el WebUI gestiona queries, parse y UX:

```cpp
#include <HardwareMidiDetect/MidiHardwareBackend.h>
#include <HardwareMidiDetect/JuceHardwareMidiPicker.h>
#include <HardwareMidiDetect/HardwareMidiDetector.h>
#include <juce_audio_devices/juce_audio_devices.h>

using abd::hwid::MidiHardwareBackend, abd::hwid::JuceHardwareMidiPicker,
     abd::hwid::HardwareMidiDetector;

class MySynthMidiBackend : public MidiHardwareBackend
{
public:
    std::string getOutputPortName() const override { return outPort ? outPort->getDeviceInfo().name.toStdString() : ""; }
    void sendBytes(const std::vector<uint8_t>& bytes) override
    {
        if (outPort)
        {
            auto msg = juce::MidiMessage::createSysExMessage(bytes.data(), (int)bytes.size());
            outPort->sendMessageNow(msg);
        }
    }
    void setReceiveCallback(std::function<void(const std::vector<uint8_t>&)> cb) override { onBytes = std::move(cb); }
    void startListening() override
    {
        if (inPort == nullptr)
        {
            auto devs = juce::MidiInput::getAvailableDevices();
            inPort = juce::MidiInput::openDevice(devs.isEmpty() ? -1 : devs[0].identifier, this);
        }
    }
    void stopListening() override { inPort.reset(); }
    void refreshPorts() override {}
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (m.isSysEx() && onBytes)
        {
            auto* data = m.getSysExData();
            std::vector<uint8_t> bytes(data, data + m.getSysExDataSize());
            onBytes(bytes);
        }
    }
private:
    std::function<void(const std::vector<uint8_t>&)> onBytes;
    std::unique_ptr<juce::MidiOutput> outPort;
    std::unique_ptr<juce::MidiInput> inPort;  // + juce::MidiInputCallback
};

// Configuración de detección
HardwareMidiDetector::DetectionConfig config;
config.allowedHardwareIds = {};           // whitelist (ej. {"korg_ms2000", "korg_ms2000r"})
config.maxResults = 1;                    // 1 = single, >1 = multi-select
config.autoSelectIfSingle = true;         // auto-callback si 1 match
config.includeHeuristic = true;
config.requireSysExVerified = false;

// En tu editor/UI:
auto backend = std::make_unique<MySynthMidiBackend>();
picker = std::make_unique<JuceHardwareMidiPicker>(*backend,
    [this](const HardwarePickResult& res) {
        if (res.cancelled) return;
        if (config.maxResults == 1) {
            DBG("Detected: " + res.displayName + " (" + res.hardwareId + ")");
        } else {
            for (size_t i = 0; i < res.hardwareIds.size(); ++i) {
                DBG("Selected: " + res.displayNames[i] + " (" + res.hardwareIds[i] + ")");
            }
        }
        applySelection(res);
    },
    registry.getContracts(),
    config);

// Tema visual (ms2000, cz101, deepmind, juno, audiolab)
picker->setTheme("audiolab");

addAndMakeVisible(picker.get());
picker->setBounds(getLocalBounds());
picker->startPick(); // lanza la UI de detección
```

**Contrato con el WebUI (canal `nativeEvent`):**

| Evento | Dirección | Payload | Propósito |
|--------|-----------|---------|-----------|
| `hardware.detect` | WebUI → Host | `{}` | Usuario clicó "Detect" → C++ ejecuta scan |
| `hardware.refreshPorts` | WebUI → Host | `{}` | Usuario clicó "Rescan" → refresca puertos y re-escanea |
| `hardware.result` | WebUI → Host | Ver abajo | Usuario seleccionó dispositivo(s) o canceló |
| `hardware.send` | Host → WebUI | `{payload: <base64>}` | WebUI pide enviar SysEx (legacy, no usado en v2) |
| `hardware.listen` | Host → WebUI | `{}` | WebUI pide armar listener (legacy) |
| `hardware.stop` | Host → WebUI | `{}` | WebUI pide detener listener (legacy) |

**`hardware.result` payload (single / multi):**

```json
// Single (maxResults=1):
{
  "action": "hardware.result",
  "cancelled": false,
  "hardwareId": "korg_ms2000",
  "displayName": "Korg MS2000 / MS2000R",
  "manufacturer": "42",
  "model": "58",
  "firmwareVersion": "01020304"
}

// Multi (maxResults>1):
{
  "action": "hardware.result",
  "cancelled": false,
  "hardwareIds": ["korg_ms2000", "roland_juno106"],
  "displayNames": ["Korg MS2000 / MS2000R", "Roland JUNO-106"],
  "manufacturer": "42",
  "model": "58",
  "firmwareVersion": "01020304"
}
```

**Entrada al WebUI (host → WebUI):**
El C++ empuja la lista de dispositivos detectados via `__setDetectedDevices(devices[])` donde cada item incluye:
`id`, `displayName`, `manufacturer`, `model`, `firmwareVersion`, `inPortName`, `outPortName`, `portIndex`, `deviceId`, `isSysExVerified`, `modelImage`, `brandLogo`.

### Theme System (estilo ABDScope)

El WebUI usa el sistema universal de estilos de `ABDSharedAssets/styles/`:

```cpp
// Temas disponibles: "ms2000", "cz101", "deepmind", "juno", "audiolab"
picker->setTheme("audiolab"); // cambia colores, bordes, scrollbars automáticamente
```

El WebUI importa `<link rel="stylesheet" href="styles/index.css">` que carga tokens + temas + componentes. Las barras de scroll son coherentes con el tema activo.

### Asset Serving (Filesystem)

| Tipo | Origen | Servido por |
|------|--------|-------------|
| `index.html`, JS | Embedded binary data | `HardwareMidiPickerAssets` (juce_add_binary_data) |
| `styles/**/*` | `ABDSharedAssets/styles/` | `HardwareMidiPickerResourceProvider` (filesystem) |
| `models/**/*` | `ABDSharedAssets/models/` | `HardwareMidiPickerResourceProvider` (filesystem) |
| `brands/**/*` | `ABDSharedAssets/brands/` | `HardwareMidiPickerResourceProvider` (filesystem) |

### Notas de migración (desde ABDAudioLab local)

- Los contratos se ven iguales; el parser compartido lee la clave `midiIdentification` (con fallback `midiIdentity`).
- El campo `functions`/`controls` específico de ABDAudioLab **no** está modelado en `abd::hwid::HardwareContract`. Leélos desde `registry.getRawContractJson(id)` en tu adaptador local:
  ```cpp
  auto raw = registry.getRawContractJson("korg_ms2000");
  if (raw) { auto functions = (*raw)["functions"]; /* consume */ }
  ```
- Convertí `abdaudiolab::core::HardwareContract` → base `abd::hwid::HardwareContract` en los call sites (`SlideInDrawer`, `HardwareManager`), o casteá al detector base.
---

## Módulo: HardwareDrivers

Controladores de hardware físico, protocolos de comunicación MIDI/SysEx/FSK y contratos de control en dos niveles (*Two-Tier Hardware Architecture*).

### Arquitectura en Dos Niveles

El módulo establece una separación estricta entre los **drivers agnósticos de protocolo** (compartidos) y los **controladores interactivos de la aplicación** (específicos del host):

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                 NIVEL 1: CORE COMPARTIDO (ABDSharedCode)                    │
│                                                                             │
│  IHardwareController (Contrato base: connect, setParameter, sendMidi...)    │
│  ├── MidiCcController     (Controlador CC estándar y 14-bit NRPN)           │
│  ├── AiraSysExController  (Driver Roland DT1/RQ1, Checksum y Audio FSK)     │
│  ├── RoutingValidator     (Matriz topológica de 31 submódulos Roland AIRA)  │
│  ├── SysExCodec           (Empaquetado/desempaquetado canónico 7-to-8 bit)  │
│  ├── NRPNParser           (Máquina de estados para recepción/envío 14-bit)  │
│  └── FskAudioModem        (Continuous-Phase FSK 12/14 kHz + Goertzel)       │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ (Heredan de IHardwareController)
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                 NIVEL 2: HOST / APLICACIÓN (ej. ABDAudioLab)                 │
│                                                                             │
│  Controladores Específicos del Host:                                        │
│  ├── ManualAnalogueController (Operador humano, diálogos y metrónomo visual)│
│  └── MockHardwareController   (Simulador DSP analógico para CI y CTest)     │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Integración en CMake

```cmake
target_link_libraries(TuProyecto PRIVATE ABDShared::HardwareDrivers)
```

### 1. Contrato Base: `IHardwareController`
Define la interfaz virtual pura para interactuar con cualquier equipo (físico o simulado):
```cpp
#include <HardwareDrivers/HardwareController.h>

class MiControladorCustom : public abd::hw::IHardwareController
{
public:
    bool isAutomatic() const noexcept override { return false; } // false para operador humano
    bool connect() override { /* ... */ return true; }
    void disconnect() override { /* ... */ }
    bool setParameter(int paramIndex, float normalizedValue) override { /* ... */ return true; }
    // ...
};
```

### 2. Controladores de Protocolo Disponibles

- **`abd::hw::MidiCcController`**:
  Envía cambios de parámetro estándar (CC 0..127) o de alta resolución de 14 bits (NRPN MSB/LSB CC 99/98 + Data Entry CC 6/38).
- **`abd::hw::AiraSysExController`**:
  Gestiona la memoria modular de los efectos Roland AIRA (Bitrazer, Demora, Torcido, Scooper) mediante tramas DT1/RQ1 y cálculo automático del checksum Roland.
- **`abd::hw::RoutingValidator`**:
  Valida conexiones entre jacks virtuales y submódulos (RF-25, RF-26) impidiendo cortocircuitos o bucles salida-salida antes de tocar el hardware.
- **`abd::hw::SysExCodec`**:
  Empaquetador/desempaquetador universal 7-to-8 bit. Transforma 7 bytes de memoria cruda de 8 bits en 8 bytes de 7 bits con byte recolector MSB (estándar en Korg, Yamaha y Roland).
- **`abd::hw::NRPNParser`**:
  Máquina de estados reactiva para parsear secuencias entrantes de CC 99, 98, 6 y 38 ensamblando el valor de 14 bits `[0..16383]` en tiempo real.
- **`abd::hw::FskAudioModem`**:
  Módem de audio FSK de fase continua (CP-FSK) a 1200 baudios con frecuencias portadoras a 12 kHz (Mark/0) y 14 kHz (Space/1) y discriminación espectral Goertzel para inyección de parches por audio in (*Remote In*).
- **`abd::hw::JunoTapeModem`**:
  Módem de audio FSK de fase continua para la interfaz de cinta analógica de Roland, con portadoras de 1,3 kHz (Space/0) y 2,6 kHz (Mark/1), detección de tono piloto y demodulación para Juno-60, Juno-6 y HS-60.
- **`abd::hw::CasioCzVirtualController`**:
  Controlador autónomo para interrogar sintetizadores Casio CZ con Phase Distortion emulados (VES / núcleo MAME) o por hardware físico vía loopMIDI. Implementa `IHardwareController` con empaquetado estándar de 4 bits (Manufacturer ID 0x44) y una tabla de opcodes NZ-1 de 1984 con override por mapeo JSON.
  Su **`.cpp` NO está en el target `ABDShared::HardwareDrivers`**, a propósito:
  compilarlo allí convertiría `nlohmann/json` en una dependencia dura de
  *compilación* para todo el que enlace el módulo, que es casi toda la suite y no
  lo necesita para nada. Su **cabecera sí** es superficie pública del target, y el
  target enlaza `nlohmann_json` en `PUBLIC` justo por eso: incluirla no da
  `C1083`. Lo único que queda fuera es la implementación, y quien la use la
  compila desde fuente —es lo que hace `ABDAudioLab`—:
  ```cmake
  target_sources(MiProyecto PRIVATE
      ${ABDSHARED_CODE_DIR}/HardwareDrivers/CasioCzVirtualController.cpp)
  target_link_libraries(MiProyecto PRIVATE nlohmann_json::nlohmann_json)
  ```
- **`abd::hw::CasioNibbleCodec`**:
  Empaquetador/desempaquetador universal de 4 bits (nibbles `0x00..0x0F`) con verificación de checksum de 7 bits Casio.

> Cinco de las seis clases con métodos definidos fuera de línea —`SysExCodec`,
> `NRPNParser`, `FskAudioModem`, `JunoTapeModem` y `CasioNibbleCodec`— se compilan
> en `ABDShared::HardwareDrivers`. Las tres últimas estaban en disco y fuera del
> target: se podían incluir y documentar, pero no enlazar, y el síntoma era un
> `undefined reference` en el consumidor.

---

---

## Módulo: AudioComparator

Motor de alta precisión para comparación acústica A/B, alineamiento temporal y dictamen automático de calidad analógica vs digital.

### Integración en CMake

```cmake
target_link_libraries(TuProyecto PRIVATE ABDShared::AudioComparator)
```

El target arranca en `ON` y se puede apagar con `-DABDSHAREDCODE_BUILD_AUDIOCOMPARATOR=OFF`.
Es **STATIC** (cuatro `.cpp`: `AudioABComparator`, `AudioABComparator_Alignment`,
`AudioABComparator_Spectral` y `AudioABVerdictEngine`) y propaga `juce_core`,
`juce_audio_basics` y `juce_dsp`; este último no es opcional porque la correlación
cruzada FFT que se describe más abajo vive en la unidad de traducción que lo incluye.
Los headers públicos son `AudioABComparator.h` y `AudioABVerdictEngine.h`, con los que
el consumidor hace `#include <AudioComparator/AudioABComparator.h>` y lo equivalente para
el veredicto.

### Componentes    - **`abd::audio::AudioABComparator`**: el comparador, con tres bloques de trabajo:
    - **Alineamiento sub-muestra** por correlación cruzada FFT, que calcula el retardo intrínseco entre las dos señales (`sampleOffset`, `timeOffsetMs`, `correlationPeak`).
    - **Métricas temporales** entre la referencia alineada y la capturada: Peak dBFS, RMS dBFS, MAE y RMSE.
    - **Métricas espectrales**: desviación de magnitud logarítmica (`logMagMeanAbsDiffDb`), centroide espectral y balance de energía en tres bandas (bajos / medios / agudos).
- **`abd::audio::AudioABVerdictEngine`**: evalúa el resultado contra una matriz de tolerancias configurable (`AudioABVerdictTolerances`) y emite un veredicto formal: `pass` (dentro de tolerancia), `warn` o `fail`.

### Ejemplo de Uso

```cpp
#include <AudioComparator/AudioABComparator.h>
#include <AudioComparator/AudioABVerdictEngine.h>

abd::audio::AudioABSignal refSignal; // Audio grabado del hardware real
abd::audio::AudioABSignal capSignal; // Audio renderizado por el plugin emulador

abd::audio::AudioABRunContext ctx;
ctx.runId = "test-verification";

abd::audio::AudioABComparatorConfig config;
config.enableCrossCorrelation = true;

abd::audio::AudioABComparator comparator;
auto result = comparator.compare(refSignal, capSignal, ctx, config);

abd::audio::AudioABVerdictEngine verdictEngine;
abd::audio::AudioABVerdictTolerances tolerances;
auto verdict = verdictEngine.evaluate(result, tolerances);

if (verdict.level == "pass")
{
    // El modelo coincide fielmente con el hardware (o la diferencia está dentro de lo medido).
}
```

> **Qué queda fuera dicho en voz alta.** Hoy el módulo es una fuente lista para un
consumidor por ruta o para unirse como módulo formal; en la tabla de este mismo guide
aparece como **fuente para consumir por ruta o candidato a módulo formal**. Si tu
proyecto lo enlaza como `ABDShared::AudioComparator`, quita esa nota de la tabla y
deja solo el consumidor.

---

## Módulo: LutDSP

Evaluación ultra-rápida de Look-Up Tables multidimensionales con aceleración vectorial SIMD y filtrado analógico polifónico.

### Integración en CMake

```cmake
target_link_libraries(TuProyecto PRIVATE ABDShared::LutDSP)
```

El target enlaza `ABDShared::DspCore` en `INTERFACE`: quien enlaza LutDSP ya tiene
el sustrato y el contrato de la familia de filtros, así que no hay que añadirlo a
mano. Su test standalone se apaga con `-DABDSHAREDCODE_BUILD_LUTDSP_TESTS=OFF`.

### Componentes

- **`abd::lutdsp::LutEvaluatorSimd`**:
  Evaluador SIMD de tablas 1D y 2D (con interpolación bilineal / bicúbica Catmull-Rom) optimizado para llamadas en bloque dentro del callback de audio de tiempo real.
- **`abd::lutdsp::AnalogLutFilterModule`**:
  Módulo de filtrado polifónico de 8 voces con suavizado balístico exponencial (`smoothingRate`) para evitar artefactos en saltos bruscos de modulación analógica.
- **`abd::lutdsp::FilterLut`**:
  El miembro LUT de la familia de filtros (ver la sección de DspCore): el corte y la Q salen de una tabla **medida** (`AbdBatchedPoint`, con interpolación bilineal) y el núcleo que los toca es el TPT del sustrato, así que habla el mismo idioma que `FilterTpt` y `FilterEquation` y se puede cambiar por ellos. Se le da la geometría real del fichero generado (`loadTable (puntos, columnas, filas)`; el modelo del repo es 8×4) y, sin tabla, se comporta como el analítico de la familia, cosa que declara `hasTable()`.


---

## Módulo: Scope

Osciloscopio analítico multi-lane embebido en WebView2: taps nativos C++ que capturan a rate de bloque (master, pre-FX, osc-mix, post-filter...), snapshot lock-free a 60 FPS y WebUI embebida como binary data. Este módulo vivía en el repo hermano `ABDScope`; desde v0.4.0 es un subtree aquí (`Scope/`), con los temas e iconos canónicos en `ABDSharedAssets`.

### Integración en CMake

```cmake
target_link_libraries(TuProyecto PRIVATE ABDShared::ScopeCore)
```

**No** hagas `add_subdirectory(../ABDScope)` ni un `FetchContent` del repo
`ABDScope`: el módulo ya está registrado aquí y dos `add_subdirectory` del
mismo módulo en un mismo build matan la configuración con
`add_library cannot create target ABDScopeCore because another target with the
same name already exists`. Trae tu JUCE **antes** de anadir ABDSharedCode, para
que `juce_add_binary_data` exista en alcance y la WebUI se embeba.

Tres aliases, según lo que necesites:

| Target | Qué da |
|---|---|
| `ABDShared::ScopeCore` | core + glue JUCE (componente WebView2 + resource provider) + WebUI embebida. El que enlaza un producto. |
| `ABDShared::ScopeCoreHeaders` | solo el core C++ header-only, sin JUCE: tests, tools y TUs DSP. |
| `ABDShared::ScopeWebAssets` | los binarios de la WebUI (solo existe si `juce_add_binary_data` está en alcance; sin JUCE la WebUI se sirve por filesystem). |

Los include dirs que propaga `ScopeCoreHeaders` son `Scope/Source` y
`Scope/Source/Core`, así que los includes son a pelo:

```cpp
#include <ScopeDataCollector.h>           // core header-only
#include <JUCE/JuceWebScopeComponent.h>   // componente WebView2
```

Bajo EMSCRIPTEN el glue JUCE no se enlaza (el puente `WebView2Bridge` no
existe sin `juce_gui_extra` y el probe cae al fallback), pero el core
header-only sigue disponible: cualquier TU WASM que quiera capturar
telemetría puede incluir `<ScopeDataCollector.h>` igual que la nativa.

### Componentes

- **`abd::scope::ScopeDataCollector`**: recolector de telemetría lock-free (POE) que expone los taps registrados y sus snapshots a 60 FPS. Es lo que el motor produce y lo que el componente consume.
- **`abd::scope::ScopeTap` / `TapId` / `ScopeTapType`**: canal de captura nativo por bloque (flush al terminar cada buffer de audio), con id estable para nombrar lanes en la WebUI.
- **`abd::scope::ScopeFrameSerializer`**: serializa los frames capturados para el puente WebView2.
- **`abd::scope::TriggerDetector`** y **`SpscRingBuffer`**: armónicas del core (trigger de forma de onda y cola SPSC) usadas por los taps.
- **`abd::scope::JuceWebScopeComponent`** (`JUCE/`): componente JUCE WebView2 que embebe la WebUI del osciloscopio (multi-lane + waterfall) y la alimenta desde un `ScopeDataCollector`.  - **`abd::scope::ScopeResourceProvider`** (`JUCE/`): sirve los assets embebidos (catálogo binario + fallback a `ABDSharedAssets`).  ### Ejemplo de Uso

  ```cpp
  #include <ScopeDataCollector.h>
  #include <JUCE/JuceWebScopeComponent.h>

  // El motor expone su recolector (p. ej. ABDMS2000 SynthEngine::getScopeCollector)
  auto &collector = engine.getScopeCollector();

  // Componente WebView2 con la WebUI embebida: collector, sample rate, FPS
  auto webScope = std::make_unique<abd::scope::JuceWebScopeComponent>(
      collector, engine.getSampleRate(), 30);
  webScope->setTheme("ms2000"); // tema canónico de ABDSharedAssets

  // Activar los lanes (taps) al mostrar la ventana
  for (size_t i = 0; i < collector.getTapCount(); ++i)
      if (auto *tap = collector.getTap(i))
          tap->setActive(true);
  ```

  ---

## Módulo: BankManager

Módulo embebible del Bank Manager (corte desde `ABDBankManager`): core C++ de ValueTree v1 con blobs Base64 y comunicación con el host por callback, adaptador JSON <-> core que no depende de WebView2, loader de factory content y protocolo SysEx del Behringer Pro800. Es el módulo del que se desgajó la app y que `ABDBankManager` hace consumidor; el corte está documentado en `ABDBankManager/DOCS/bank-manager-module-cut.md`.

### Integración en CMake

```cmake
target_link_libraries(TuProyecto PRIVATE ABDShared::BankManagerCore)
```

El target está **OFF por defecto** (`-DABDSHAREDCODE_BUILD_BANKMANAGER=OFF`), y solo lo
activa quien consume el módulo; `ABDBankManager` lo fuerza en su propio bloque de
integración. Cuando no está activo, los builds JUCE/WASM del monorepo que no enlazan el
módulo no cambian de comportamiento por el corte.

Propaga `ABDShared::HardwareDrivers` y las unidades de JUCE que usa por dentro:
`juce_core`, `juce_data_structures`, `juce_cryptography` y `juce_audio_devices`. El
adaptador y el loader son parte de la superficie pública del módulo.

### Estrategia de corte

El módulo usa **dos niveles de adaptación** en vez de meter JUCE o WebView2 en el core:

- **`ABDBankManagerCore`** (lo que exporta `ABDShared::BankManagerCore`): administración
  del estado como `juce::ValueTree` v1 (library, banks, patches, preset actual),
  blobs Base64 para contenidos binarios, y comunicación con el host por callback (`
  handleWebUIMessage` / `sendToWebUI`, `handleHardwareSend`). Es el contrato del corte:
  esto es lo que se llevaba el módulo y lo que `ABDBankManager` consume.
- **`BankManagerWebViewAdapter`** (`BankManagerWebViewAdapter.h`/`.cpp`): adaptador que
  traduce mensajes JSON del host al core y viceversa. También manda la versión del
  esquema con `BankManagerCore::valueTreeSchemaVersion`, lo que hace que la app y el
  módulo lean el mismo `schemaVersion`.

El loader de factory content (`FactoryContentLoader`) y el protocolo del Pro800
(`Pro800Midi`, `HardwareMidiPipe`) vienen con el módulo porque son parte del contrato de
quién consume el banco. Ojo: **no** es el módulo el que inventa el SysEx — el protocolo
viene de `behringer-pro800` — pero sí lo porta como parte de su superficie.

### Lo que queda fuera dicho en voz alta

- El módulo **no** es el runtime del bank manager completo de la app: es la parte que el
  corte movió a compartido y que quedó como INTERFACE porque la superficie pública es el
  conjunto de cabeceras/includes (`<BankManager/ABDBankManagerCore.h>` y las que acompañan).
- El lado de la WebUI más amplio (el host completo) sigue en `ABDBankManager`; este
  módulo es la pieza portable.

### Verificado

- El target, cuando está ON, enlista los mismos archivos que promete el bloque CMake del
  orquestador (`ABDBankManagerCore`, `BankManagerWebViewAdapter`, `FactoryContentLoader`,
  `Pro800Midi`, `HardwareMidiPipe`), con la dependencia en `ABDShared::HardwareDrivers` +
  las unidades de JUCE listadas arriba.

---

## Contratos del Bank Manager: arquitectura contract-driven y el modelo en tres niveles

El Bank Manager no es un único programa con un único formato de SysEx: es un **contrato por sintetizador**. Cada familia (Roland Juno, Korg MS2000/Prophecy, Behringer DeepMind/Pro800, Casio CZ, Yamaha DX7, Roland AIRA…) tiene su propio `ModelContract`, y el core/UI se auto-configuran a partir del registro de contratos que haya cargado, no a partir de `switch` por modelo.

Esto es lo que hace que el mismo código sirva tanto a un gestor universal standalone como a un plugin que solo gestiona su propio synth: la diferencia está en **qué contratos se registran**, no en el núcleo.

### Qué hay dentro

Los contratos viven en `BankManager/Contracts/` y son **TypeScript/JS**: el `ModelContract` es la SSOT del modelo, y los adapters son delegaciones finas sobre ese contrato.

| Fichero | Qué es |
|---|---|
| `ModelContract.ts` | El CONTRATO: `ModelContract` y `validateModelContract(...)`. Define identidad, capacidad del banco, direccionamiento, tamaño de patch, categorías, transporte (hardware/software, `sysex` vs `native`), metadatos de SysEx, detección MIDI y las operaciones de dump/parse/checksum/file. |
| `HardwareLinkContract.ts` | El contrato de comunicación bidireccional con hardware: `detectHardware`, `buildPatchDump`/`buildBankDump`, `buildDumpRequest`/`parseDumpResponse`, edit buffer, tieming (`interMessageDelayMs`, `dumpTimeoutMs`), y la clase base `BaseHardwareLink` con utilidades de cabecera/finalización/checksum. |
| `ContractRegistry.ts` | El registro declarativo (`ContractRegistry` + `createStandaloneRegistry`). Registra modelos, import adapters, export adapters y hardware links, valida al registrar y expone consultas de cobertura (`getCoverage`, `getHardwareIds`, `getCompatibleModels`, `mode` standalone/plugin). |
| `Adapters/index.ts` | Los adapters concretos: `allImportAdapters`, `allExportAdapters`, `allHardwareLinks`. Cada uno es un thin wrapper sobre su `ModelContract`. |
| `Models/` | Los contratos por familia: `behringer-dm12`, `behringer-dm12d`, `behringer-dm6`, `behringer-pro800`, `korg-ms2000`, `korg-prophecy`, `roland-juno`, `roland-aira-*` (desmembrados), `casio-cz`, `yamaha-dx7`, etc. |

El módulo C++ que está en `BankManager/` es un **corte del mismo contrato hacia C++/JUCE**: expone `ModelContract.h`, `ModelContractRegistry.{h,cpp}`, `ImportAdapter.h`, `ExportAdapter.h`, `HardwareLinkContract.h`, `PatchData.h` y los adapters concretos (CasioCZ, RolandJuno, Korg, Behringer, YamahaDX7). No es un segundo contrato distinto: es la cara embebible del registro de contratos. Ver `docs/cmake-bankmanager-block.md`.

### Arquitectura contract-driven: cómo se usa

El flujo normal no es «el código decide por modelo»:

1. **Declaración.** Cada sintetizador se describe con su `ModelContract`: identidad, banco, patch, SysEx, detección MIDI y capacidades de transporte. Si el modelo emula hardware, puede declarar `transport.software.systems: ['sysex']` y los métodos de build/parse necesarios; si tiene su propio formato, usa `native`.
2. **Registro.** Los contratos se registran en `ContractRegistry`. Standalone registra todos (`createStandaloneRegistry()`); un plugin registra solo el suyo (+ compatibles). El registro **valida al registrar**: `modelId` duplicado, `HardwareLink` sin `ModelContract` registrado, y `targetModelIds` huérfanos son errores/avisos explícitos.
3. **Auto-configuración.** El core/UI consulta el registro (`getModels`, `getImportAdapters`, `getExportAdapters`, `getHardwareLinks`, `getCoverage`, `getCompatibleModels`, `getHardwareIds`) y se monta solo: qué adaptadores mostrar, qué modos de transporte ofrecer, qué modelos son compatibles.
4. **Operación.** Import/export/hardware se delegan al adapter y al `ModelContract` del modelo. El contrato es la fuente de verdad para parsear/volcar/checksum/detectar, así que un adapter nuevo suele ser una delegación, no un segundo lugar donde se describen los formatos.

### El modelo en tres niveles

Esta arquitectura encaja en el mismo esquema en tres niveles que el ecosistema:

1. **Nivel 0 — Fuente única de verdad.** El contrato del synth: sus `ModelContract`s y sus `HardwareLinkContract`s. En el lado JS/TS es `BankManager/Contracts/`; en el lado C++ es el corte de `BankManager/Contracts/` más los adapters concretos.
2. **Nivel 1 — Core de detección y transporte.** El registry (`ContractRegistry`) y los contracts de hardware (`HardwareLinkContract` / `BaseHardwareLink`), + los codecs de protocolo del nivel C++ (`SysExCodec`, `NRPNParser`) cuando el host necesita transporte genérico.
3. **Nivel 2 — Aplicaciones consumidoras.** Standalone (todos los contratos) o plugin (solo el suyo). El banco, la UI y la cola MIDI se montan a partir de lo que el registro dice que hay, no al revés.

### Cómo integrarlo

**1. Instalar.** El contrato es un workspace pnpm, no un target CMake. En `pnpm-workspace.yaml` ya está el miembro; `@abdsynths/shared` lo resuelve desde `ABDSharedAssets`. En un proyecto que consuma el monorepo, el contrato entra como dependencia de workspace, no como librería CMake. Ver la sección de `MidiKeyboard` y *Cómo funciona la integración* arriba.

**2. Elegir modo de despliegue.** La diferencia entre standalone y plugin es cuántos contratos registras:
```ts
import { createStandaloneRegistry } from 'BankManager/Contracts/ContractRegistry';

// Standalone: todos los ModelContracts del monorepo.
const registry = createStandaloneRegistry();

// Plugin: solo el contrato del synth que lo hospeda (+ los compatibles que quiera exponer).
const pluginRegistry = new ContractRegistry();
pluginRegistry.registerModel(getModelContract('behringer-deepmind12'));
```

El `registry.mode` sale solo: `'standalone'` si hay más de un modelo registrado, `'plugin'` si solo hay uno. Los adapters, los hardware links y la cobertura (`registry.getCoverage()`) se derivan del mismo registro.

**3. Usar el registry para auto-configurar.** Ejemplo de consulta típica (no un `switch` por modelo):
```ts
const models = registry.getModels();
const cobertura = registry.getCoverage();

for (const entry of cobertura) {
  console.log(`${entry.modelId}: import=${entry.importAdapters.join(',')} export=${entry.exportAdapters.join(',')} hw=${entry.hardwareLinks}`);
}

const compatibles = registry.getCompatibleModels('behringer-deepmind12');
const ids = registry.getHardwareIds('behringer-deepmind12');
```

**4. Añadir un nuevo sintetizador.** Es añadir un `ModelContract` en `BankManager/Contracts/Models/` y, si el host lo necesita, un `HardwareLinkContract`/adapter en `BankManager/Contracts/Adapters/`. El registry lo valida al registrarse, y los adapters existentes no tienen que saber que el nuevo existe: el registro es lo que los conecta.

### Qué queda fuera dicho en voz alta

- **El contrato no es el runtime completo.** Es el descriptor del synth: qué guarda, cómo viaja, cómo se detecta, qué SysEx soporta. La app/consumidor decide cómo usarlo (cola MIDI, UI, sincronización).
- **No es un SysEx codec universal.** Los codecs de protocolo genérico viven en `HardwareDrivers` (C++); el contrato vive aquí y describe un modelo concreto. El `Pro800SysEx` del módulo C++ es el corte de ese contrato concreto hacia C++, no un formato genérico.
- **El formato `sysex` no es siempre correcto.** Un synth emulado que no emite el payload binario del hardware no debe declararlo; el contrato permite `native` y valida que si declara `sysex`, tiene build/parse. Ver la nota en `ModelContract.ts` sobre `abd-sm002`.

---

## Integración Ecosistema: ABDBankManager y Contratos Normativos (Three-Tier Architecture)

El ecosistema ABDSynths adopta una **Arquitectura en Tres Niveles** para unificar el perfilado en laboratorio (`ABDAudioLab`) y la gestión de bancos de patches (`ABDBankManager`):

1. **Nivel 0: Fuente Única de la Verdad (`ABDSharedAssets/contracts`)**:
   - Cada sintetizador se define mediante un archivo JSON que contiene tanto los metadatos de identidad MIDI (`midiIdentification`) como la sección de gestión de bancos y volcados SysEx (`bankManagement`).
   - El esquema normativo está validado por `hardware_profile.schema.json`.

2. **Nivel 1: Core de Detección y Transporte (`ABDSharedCode`)**:
   - `ABDShared::HardwareMidiDetect`: Proporciona detección activa multicanal por *Universal SysEx Identity Inquiry* y monitorización de desconexión/conexión USB en caliente (`HardwareMidiHotplugMonitor`).
   - `ABDShared::HardwareDrivers`: Aporta codecs universales de transporte (`SysExCodec`, `NRPNParser`, `FskAudioModem`).

3. **Nivel 2: Aplicaciones Consumidoras**:
   - **ABDAudioLab**: Carga dinámica mediante `core::HardwareContractRegistry` para calibración y perfilado acústico.
   - **ABDBankManager**: Sincronización e hidratación declarativa de `ModelContract`s mediante `npm run sync-contracts` (`scripts/sync_contracts.mjs`).
