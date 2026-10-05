# Fuentes de ABDSharedCode sin ningún proyecto consumidor

> **Fecha:** 2026-09-28
> **Alcance:** los `.h` / `.cpp` de `ABDSharedCode/` del workspace `ABDSynths`.
> **Objetivo:** que una extracción a medias deje de esconderse. El fichero está
> escrito, está documentado, tiene sus propios tests, compila — y no lo usa
> ningún producto. Eso no se ve en ningún build, porque no falla nada.
>
> Documento hermano: `homonimias-cabeceras.md`, que mide lo contrario
> (nombres de fichero repetidos entre proyectos). Este mide conexiones rotas.

---

## 1. Método, y por qué no basta con `grep`

Buscar el nombre del fichero en el suite **no funciona**, y por tres motivos
concretos, todos comprobados aquí:

| Trampa | Qué pasa en realidad | Ejemplo medido |
|---|---|---|
| **Ruta relativa que gana al include path** | El consumidor escribe `#include "../hardware/AiraSysExController.h"`. El path del módulo también está en el include path, pero lo relativo se resuelve primero: se compila la **copia privada**, y el módulo compartido queda sin usar aunque parezca enlazado. | `ABDAudioLab/src/core/HardwareManager.h:14` |
| **El include se resuelve por el include path, no por el directorio** | `#include "dsp/VoiceDispersionModel.h"` desde `src/tests/` significa `src/dsp/…`, no `src/tests/dsp/…`. Por eso un include que "parece" apuntar al módulo puede estar resolviendo a la copia privada. | `ABDAudioLab/src/tests/test_VoiceDispersionAndBBD.cpp:5` |
| **Un target INTERFACE propaga sus `.cpp`** | `ABDShared::HardwareMidiDetect` no compila nada, pero `target_sources(... INTERFACE ...)` mete los `.cpp` en el target del consumidor, que los compila. Enlazar **sí** consume, aunque no haya ni un `#include`. | `ABDSharedCode/CMakeLists.txt:582` |

Por eso la herramienta no busca nombres: **resuelve cada `#include` y cada
ruta de fuente de CMake a un path real**, y luego pregunta qué se alcanza desde
un target de producto. Las cabeceras solo se consumen por `include`; los `.cpp`
también por compilación directa o por enlace de un target que los propague.

## 2. Cómo repetirlo

```bash
python tools/audit_unconsumed_sources.py           # informe
python tools/audit_unconsumed_sources.py --check   # solo código de salida
```

`--check` devuelve **1** si aparece un huérfano que no esté en la lista blanca
del propio script, o si una entrada de la lista blanca ya no hace falta
(alguien conectó lo que estaba suelto). Es apta para CI tal cual. La lista
blanca lleva **el motivo** de cada entrada, porque una excepción sin motivo
es deuda con mejor prensa.

Comprobado: con el árbol limpio devuelve 0; al añadir un `.h` suelto en
`DspEffects/` lo reporta como `*** HUÉRFANAS NUEVAS ***` y devuelve 1.

## 3. Lo que hay

**117 fuentes, 101 alcanzables desde un producto, 16 huérfanas.** Las 16 están
en la lista blanca con su motivo. No hay ninguna unbekannte.

### 3.1 `DspEffects` — la familia "motor de máquina" (5 ficheros)

`MultiHeadEcho.h`, `RingMod.h`, `characters/TapeColour.h`,
`characters/DiodeBridge.h`, `profiles/Re201Profile.h`.

`EffectPolicy.h` salió de esta lista el 2026-09-28: el motor BBD
(`JunoBBD.h`) lo incluye, o sea que el contrato ya tiene un consumidor
de verdad — el test de ABDAudioLab lo instancia con `BbdNoiseStage` como etapa
de carácter, y desde el 28 el slot 36 de ABDEep (`FXRolandBBDChorus`) es otro
consumo de verdad, con su test propio. Ese es el orden que funciona: el contrato
se define con un motor detrás, no antes.

Los dos motores genéricos (`DspChorus`, `DspDelay`, `DspSaturation`,
`DspReverb`) los consume ABDNeural; el `SchroederReverb` y su perfil los
consume ABDEep. Los **motores de máquina emulada, no**. Solo se usan entre sí y
desde `DspEffectsTests.cpp`.

Es la partida abierta más grande del módulo, y la razón por la que se corrigieron
los tres defectos de `MultiHeadEcho` sin miedo a la paridad: **sin consumidor no
hay paridad que romper**, así que se puede arreglar de verdad en vez de
reproducir un error. Cuando llegue el consumidor, este es el trabajo pendiente:
extraer el RE-201 y el Juno Ring Mod a ABDEep.

### 3.2 `SynthCore/ModMatrix.h` (1)

El resto de `SynthCore` lo consume ABDMS2000 a través de shims
(`ABDMS2000/Source/DSP/Envelopes/ADSREnvelope.h` → `SynthCore/ADSREnvelope.h`).
`ModMatrix.h` no tiene shim: el `ModMatrix` que usan ABDEep, ABDCZ101 y ABDNeural
es suyo. El del módulo está escrito y sin conectar.

### 3.3 `LcdDisplay` — módulo muerto, y el caso más claro (3)

`LcdDisplay.h`, `LcdMenuManager.h`, `LcdDisplayProbe.cpp`.

La migración al WebUI **terminó** — el comentario de
`ABDNeural/Source/Main/NEURONiKEditor.h:102` lo dice: *"la migración del
LcdDisplay/LcdMenuManager del interface C++"* — pero los ficheros se quedaron, y
con ellos el target `ABDShared::LcdDisplay` en el `CMakeLists.txt`. No lo
incluye nadie, y `LcdDisplayProbe.cpp` **no está en ningún target de
compilación, ni propio**: ni siquiera se puede romper, porque no existe el
build. Es el único caso del módulo que es basura pura en vez de trabajo
pendiente. **Propuesta: borrar el módulo y su target.**

### 3.4 `Segmented` — puerto nativo esperando consumidor (2)

`Segmented.h` y `SegmentedProbe.cpp`. A diferencia de `LcdDisplay`, este sí está
vivo: tiene target `ABDShared::Segmented`, el probe **se compila**, y su propio
`CMakeLists.txt` lo documenta como *"the native sibling of the shared Segmented
component (ABDSharedAssets/…)"*. Falta el paso de que un producto lo enlace.

### 3.5 Arranques de auditoría y tests propios (5)

`DspCore/DspMathAudit.cpp`, `DspCore/DspMathBitIdent.cpp` (arnas de medición de
`DspMath`, fuera de CMake a propósito: se compilan a mano) y los tres tests
standalone del módulo (`DspCoreTests.cpp`, `DspEffectsTests.cpp`,
`SynthCoreTests.cpp`), que los compila el CMake del propio módulo y ningún
producto. Los dos primeros están marcados `INERTE` a propósito: son pruebas de
que las cifras de la cabecera de `DspMath` están medidas, no código de producto.

## 4. Lo que este inventario NO dice

- **No dice que un fichero sobra.** Dice que hoy no lo conecta nadie. La
  diferencia es la decisión, y es de quien conoce la hoja de ruta.
- **No cuenta las copias privadas que divergen.** Eso es
  `homonimias-cabeceras.md`, y ahí hay 76 nombres repetidos entre el módulo y
  los proyectos: 25 de autoría propia, y casi todos divergen. Aquí solo se
  anota el patrón que hace que una extracción pase desapercibida: un consumidor
  con copia propia **sombreando** la del módulo. `LutEvaluatorSimd.h`,
  `VoiceDispersionModel.h` y `LutEvaluatorSimd.h` lo hacen **bien** (su copia es
  un  shim que hace `#include <LutDSP/...>`); `JunoBBD.h` tenía el shim en un
  sentido y la copia en el otro — con tres ficheros de nombre igual vivos a la
  vez (el motor de JUNiO601, una copia muerta en `Source/Core/` y un shim de
  17 líneas en ABDAudioLab) más el borrador de `LutDSP/`. Se resolvió el
  2026-09-28: el motor real es `DspEffects/JunoBBD.h`, las dos copias muertas
  se borraron y el borrador al ático. Sigue pendiente `SysexPresetGenerator.h`.
- **No cubre JS/CSS/presets**, ni `ABDSharedAssets`, ni los proyectos que no
  compilan C++ (`ABDBankManager`, `ABDScope`, `ABDPro008`, `ABDOmegaUnified` en
  lo que respecta a su parte nativa).
