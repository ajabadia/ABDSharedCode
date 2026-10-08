# Guía de Integración — Arpeggiator (`abd::synth::Arpeggiator`)

> **Módulo:** `SynthCore`  
> **Espacio de nombres:** `abd::synth`  
> **Target CMake:** `ABDShared::SynthCore`  
> **Estándar C++:** C++20  
> **Dependencias:** Ninguna (100% C++ puro, libre de JUCE, libre de GUI, libre de libm)  
> **Garantía de Tiempo Real:** Zero-alloc en el render loop de audio (`generate`), deterministic PRNG, bounds fijos.

---

## 1. Filosofía y Arquitectura

El motor `abd::synth::Arpeggiator` es un arpegiador algorítmico determinista de precisión por muestra (*sample-accurate*) diseñado para ser compartido por todos los sintetizadores del ecosistema **ABDSynths** (ABDEep, ABDMS2000, ABDCZ101, ABDNeural, sintetizadores WASM o nuevos desarrollos).

Sigue el principio rector de diseño de `ABDSharedCode`:
> **«El motor expone la muestra y el suceso; la política permanece en el consumidor.»**

- **Agnóstico de frameworks:** No incluye ni forward-declara tipos de JUCE. Puede compilarse en entornos embebidos, pruebas unitarias headless de consola, aplicaciones JUCE o WebAssembly/AudioWorklet sin cambios.
- **Zero-alloc en el lazo de audio:** Todo el procesamiento en `generate(...)` se realiza sobre buffers estáticos internos con topes acotados (`kMaxHeld = 64`, `kMaxNotesPerStep = 8`, `kMaxPending = 32`). No invoca `malloc`, `new` ni redimensiona vectores.
- **Sample-accurate y retrigger limpio:** Los desplazamientos temporales de los eventos Note-On y Note-Off se calculan con resolución a nivel de muestra dentro del bloque de proceso. Si una nota vuelve a sonar en el mismo paso antes de apagarse, emite un Note-Off explícito en ese mismo `sampleOffset` antes del nuevo Note-On para evitar voces colisionadas o atascadas.
- **Generador de eventos por invocable:** La función de renderizado utiliza una plantilla con callback (`EventConsumer&&`), permitiendo consumir sucesos sin instanciar contenedores intermediarios.

---

## 2. Integración en CMake

Para enlazar el módulo en el proyecto de tu sintetizador:

```cmake
# En el CMakeLists.txt de tu sintetizador:
target_link_libraries(TuSintetizador PRIVATE
    ABDShared::SynthCore
)
```

En tus archivos fuente C++:

```cpp
#include "SynthCore/Arpeggiator.h"
```

---

## 3. Tipos y Estructuras Públicas

Todas las estructuras públicas residen en `abd::synth::Arpeggiator`:

### `HeldNote`
Representa una nota física o MIDI retenida en el teclado:
```cpp
struct HeldNote
{
    int   note = 0;        // Número de nota MIDI (0..127)
    float velocity = 0.0f; // Velocidad normalizada (0.0f..1.0f)
};
```

### `NoteEvent`
Evento emitido en el lazo de procesamiento hacia el sintetizador:
```cpp
struct NoteEvent
{
    int   sampleOffset = 0; // Posición de la muestra en el bloque (0 <= sampleOffset < numSamples)
    int   note         = 0; // Número de nota MIDI (0..127)
    float velocity     = 0.0f;
    bool  isNoteOn     = false; // true: Note-On | false: Note-Off
};
```

### `FastRng`
Generador pseudoaleatorio ultra-rápido y determinista basado en **Xorshift32**. Garantiza que el modo *Random* (8) produzca la misma secuencia de notas en cualquier sistema operativo, arquitectura (x86/ARM/WASM) o renderizado offline:
```cpp
struct FastRng
{
    std::uint32_t state = 0x12345678;

    void setSeed(std::uint32_t seed) noexcept;
    std::uint32_t next() noexcept;
    int nextInt(int maxExclusive) noexcept;
};
```

---

## 4. Modos de Reproducción (0 a 10)

El arpegiador implementa 11 modos de patrón algorítmico seleccionables mediante `setMode(int mode)`:

| Índice | Nombre | Comportamiento del Algoritmo | Notas por paso |
|:---:|:---|:---|:---:|
| **0** | `Up` | Ascendente: ordena las notas retenidas de menor a mayor y las recorre secuencialmente. | 1 |
| **1** | `Down` | Descendente: de mayor a menor nota. | 1 |
| **2** | `Up/Down` | Sube y baja cíclicamente **sin repetir** las notas extremas en los puntos de inflexión. | 1 |
| **3** | `Up-Inv` | Sube a través de las notas y, al completar el ciclo de octava, sube alternando inversiones. | 1 |
| **4** | `Down-Inv` | Baja a través de las notas y luego baja recorriendo inversiones. | 1 |
| **5** | `Up/Down-Inv` | Sube y baja atravesando las inversiones de los acordes. | 1 |
| **6** | `Up-Alt` | Alternancia polar ascendente: intercala la nota más baja, la más alta, la siguiente más baja, etc. | 1 |
| **7** | `Down-Alt` | Alternancia polar descendente: intercala la nota más alta, la más baja, etc. | 1 |
| **8** | `Random` | Selección pseudoaleatoria de notas del grupo retenido usando `FastRng`. | 1 |
| **9** | `As-Played` | Reproduce las notas exactamente en el orden cronológico en que el músico las fue pulsando (`playOrder`). | 1 |
| **10** | `Chord` | Todas las notas retenidas (y sus duplicaciones de octava) se disparan **simultáneamente al unísono** en cada pulso. | 1..8 |

---

## 5. API de Control y Parámetros

### Ciclo de vida y configuración básica

```cpp
void prepare(double sampleRate);
```
Inicializa o actualiza la tasa de muestreo. Resetea los acumuladores de fase de paso y limpia el estado de notas pendientes sin realizar asignaciones dinámicas.

```cpp
void setEnabled(bool on);
```
Activa o desactiva el arpegiador. Si se desactiva mientras existen notas sonando, la siguiente llamada a `generate()` emitirá automáticamente todos los `Note-Off` pendientes.

```cpp
void setMode(int mode);
```
Fija el modo de reproducción (0..10). Los valores fuera de rango son clampeados con `std::clamp(mode, 0, 10)`.

```cpp
void setStepRateHz(float hz);
```
Tasa de pulsos por segundo en Hertz (clamp `0.01f..500.0f`). En sintetizadores sincronizados a tempo, este valor es calculado por el host a partir de los BPM y la división de compás (ver [Sincronización a Reloj / BPM](#7-sincronización-a-reloj--bpm)).

```cpp
void setGate(float gate);
```
Fracción de la duración del paso que permanece encendida la nota (`0.0f..1.0f`). Con `0.5f` la nota dura la mitad del paso. Con `1.0f` dura el paso completo (legato). Con `0.0f` el sonido queda en silencio/staccato absoluto.

```cpp
void setHold(bool on);
```
Modo sostenido (Hold / Latch). Cuando está en `true`, soltar una tecla física del teclado no la elimina del ciclo del arpegio; el patrón sigue sonando hasta que se pulse `allNotesOff()` o se toquen nuevas notas según la política del teclado.

```cpp
void setKeySync(bool on);
```
Sincronización de teclado (Key Sync). Cuando está en `true`, cada vez que el músico pulsa una nueva nota, el arpegiador reinicia inmediatamente su contador de paso al paso 0 (`stepIndex = 0`), permitiendo disparar arpegios en fase exacta con la pulsación.

```cpp
void setOctaves(int octaves);
```
Rango de octavas (1 a 6). Duplica las notas del grupo sumando transposición cromática (`+12 * o`), respetando el límite MIDI máximo de 127.

```cpp
void setSwing(float swing);
```
Cantidad de swing rítmico (`0.0f..1.0f`). Con `0.0f`, los pasos pares e impares duran exactamente el 50% del ciclo (ritmo recto). Con `1.0f`, el paso par dura el 75% y el impar el 25% (máximo swing).

```cpp
void setRandomSeed(std::uint32_t seed);
```
Fija la semilla del PRNG `FastRng` para garantizar reproducibilidad en pruebas o presets de automatización.

---

### Entrada de teclado MIDI

```cpp
void noteOn(int note, float velocity);
```
Registra una pulsación física. Si la nota ya estaba en el pool, actualiza su velocidad sin duplicarla ni alterar el orden cronológico.

```cpp
void noteOff(int note);
```
Registra la liberación de una tecla. Si `hold` está apagado, elimina la nota del pool.

```cpp
void allNotesOff();
```
Limpia todas las notas retenidas y el orden de pulsación.  
> [!IMPORTANT]
> `allNotesOff()` vacía el pool de notas pero **no** muta abruptamente las notas que ya están sonando en las voces de audio; los eventos `Note-Off` de lo que esté activo se emitirán de forma limpia en la siguiente llamada a `generate()` a través de `apagarTodo()`, eliminando completamente el riesgo de notas colgadas (*hung notes*).

---

### Consultas de estado (Getters)

```cpp
bool  isActive() const;    // true si enabled == true y heldCount > 0
bool  isEnabled() const;   // true si el switch de panel está activo
int   getMode() const;     // Modo actual (0..10)
int   getHeldCount() const;// Cuántas teclas físicas están retenidas
float getGate() const;     // Fracción de compuerta actual
```

---

## 6. Integración en el Render Loop

### Patrón A: Motor de audio C++ puro / WebAssembly (Sin JUCE)

En un sintetizador con arquitectura propia o renderizado por voces:

```cpp
#include "SynthCore/Arpeggiator.h"

class PureVoiceEngine
{
public:
    void renderBlock(float* outBuffer, int numSamples)
    {
        // 1. Si el arpegiador está activo, genera eventos sample-accurate
        if (arpeggiator.isActive())
        {
            arpeggiator.generate(numSamples, [this](const abd::synth::Arpeggiator::NoteEvent& ev) {
                if (ev.isNoteOn)
                    voiceManager.triggerNote(ev.note, ev.velocity, ev.sampleOffset);
                else
                    voiceManager.releaseNote(ev.note, ev.sampleOffset);
            });
        }

        // 2. Procesa la síntesis de audio de las voces
        voiceManager.render(outBuffer, numSamples);
    }

private:
    abd::synth::Arpeggiator arpeggiator;
    VoiceManager voiceManager;
};
```

---

### Patrón B: Sintetizador basado en JUCE (como ABDEep, ABDMS2000)

En plugins JUCE, la convención consiste en crear un *shim* de compatibilidad liviano en la carpeta `Source/DSP/` del proyecto. Esto aísla las cabeceras de JUCE dentro del plugin y expone una sobrecarga cómoda que escribe en `juce::MidiBuffer`.

#### 1. Crear el shim de cabecera: `Source/DSP/Arpeggiator.h`

```cpp
#pragma once
#include "SynthCore/Arpeggiator.h"

namespace juce
{
    class MidiBuffer;
}

namespace MiSynth
{
    /**
     * @brief Shim de compatibilidad JUCE para el sintetizador.
     * Hereda el núcleo puro C++20 de abd::synth::Arpeggiator y añade la
     * sobrecarga para juce::MidiBuffer.
     */
    class Arpeggiator : public abd::synth::Arpeggiator
    {
    public:
        using abd::synth::Arpeggiator::Arpeggiator;

        // CRÍTICO: Evita el name hiding en C++, exponiendo tanto la plantilla base
        // como la sobrecarga de conveniencia con juce::MidiBuffer.
        using abd::synth::Arpeggiator::generate;

        /** Escribe en out los note-on y note-off de este bloque. */
        void generate(juce::MidiBuffer& out, int numSamples);
    };

    // Alias opcionales de conveniencia
    using NoteEvent = Arpeggiator::NoteEvent;
    using FastRng   = Arpeggiator::FastRng;
}
```

#### 2. Implementar el adaptador: `Source/DSP/Arpeggiator.cpp`

```cpp
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

#### 3. Consumo en `processBlock` del plugin

```cpp
void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    const int numSamples = buffer.getNumSamples();

    // 1. Filtrar o enrutar eventos del teclado físico
    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            if (arpeggiator.isEnabled())
                arpeggiator.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
            else
                synthEngine.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
        }
        else if (msg.isNoteOff())
        {
            if (arpeggiator.isEnabled())
                arpeggiator.noteOff(msg.getNoteNumber());
            else
                synthEngine.noteOff(msg.getNoteNumber());
        }
    }

    // 2. Si el arpegiador está activo, reemplaza el buffer de notas hacia el motor
    if (arpeggiator.isEnabled())
    {
        midiMessages.clear(); // Limpia los eventos crudos para que no suenen en paralelo
        arpeggiator.generate(midiMessages, numSamples);
    }

    // 3. Renderizar voces con los mensajes generados por el arpegiador
    synthEngine.renderAudio(buffer, midiMessages);
}
```

---

## 7. Sincronización a Reloj / BPM

Para sincronizar la velocidad de los pasos con el tempo del proyecto o DAW host, convierte el BPM y la subdivisión métrica a Hertz antes de llamar a `setStepRateHz`:

$$\text{Hz} = \left(\frac{\text{BPM}}{60.0}\right) \times \text{Factor de Subdivisión}$$

| Subdivisión de Nota | Factor Multiplicador | Cálculo en C++ |
|:---|:---:|:---|
| **1/4** (Negra) | $1.0$ | `rateHz = (bpm / 60.0f) * 1.0f;` |
| **1/4 D** (Negra con puntillo) | $1 / 1.5 \approx 0.6667$ | `rateHz = (bpm / 60.0f) * (2.0f / 3.0f);` |
| **1/4 T** (Tresillo de negra) | $1.5$ | `rateHz = (bpm / 60.0f) * 1.5f;` |
| **1/8** (Corchea) | $2.0$ | `rateHz = (bpm / 60.0f) * 2.0f;` |
| **1/8 D** (Corchea con puntillo) | $2 / 1.5 \approx 1.3333$ | `rateHz = (bpm / 60.0f) * (4.0f / 3.0f);` |
| **1/8 T** (Tresillo de corchea) | $3.0$ | `rateHz = (bpm / 60.0f) * 3.0f;` |
| **1/16** (Semicorchea) | $4.0$ | `rateHz = (bpm / 60.0f) * 4.0f;` |
| **1/16 D** (Semicorchea con puntillo) | $4 / 1.5 \approx 2.6667$ | `rateHz = (bpm / 60.0f) * (8.0f / 3.0f);` |
| **1/16 T** (Tresillo de semicorchea) | $6.0$ | `rateHz = (bpm / 60.0f) * 6.0f;` |
| **1/32** (Fusa) | $8.0$ | `rateHz = (bpm / 60.0f) * 8.0f;` |

Una vez calculado, simplemente aplica:
```cpp
arpeggiator.setStepRateHz(rateHz);
```

---

## 8. Verificación y Reglas de Calidad

Cualquier cambio o extensión al arpegiador debe respetar las siguientes invariantes:

1. **Compilación sin advertencias con `/W4` / `-Wall` y C++20.**
2. **Determinismo:** Dos ejecuciones con los mismos eventos de entrada, el mismo samplerate y la misma semilla RNG deben generar exactamente los mismos `NoteEvent` muestra a muestra.
3. **Sin fugas de notas:** Probar siempre la transición `allNotesOff()` y la desactivación `setEnabled(false)` durante la reproducción rápida; el arpegiador debe emitir todos los `Note-Off` correspondientes sin dejar voces colgadas en el sintetizador.
4. **Respeto a las reglas de tiempo real:** No utilizar `std::vector`, `std::map`, asignaciones de memoria ni sincronización bloqueante (`std::mutex`) dentro de `generate()`.
