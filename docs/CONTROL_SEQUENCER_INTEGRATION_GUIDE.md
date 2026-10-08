# Guía de Integración — ControlSequencer (`abd::synth::ControlSequencer`)

> **Módulo:** `SynthCore`  
> **Espacio de nombres:** `abd::synth`  
> **Target CMake:** `ABDShared::SynthCore`  
> **Estándar C++:** C++20  
> **Dependencias:** Ninguna (100% C++ puro, libre de JUCE, libre de GUI)  
> **Garantía de Tiempo Real:** Zero-alloc en el render loop de audio (`nextSample`), sin bloqueos, procesamiento determinista de punto flotante por muestra.

---

## 1. Filosofía y Arquitectura

El motor `abd::synth::ControlSequencer` es un generador algorítmico por pasos (*step sequencer*) de modulación de baja frecuencia con precisión a nivel de muestra (*sample-accurate*) y filtrado analógico de caída/deslizamiento (*slew/glide*).

Fue diseñado originalmente para emular las capacidades del secuenciador de control de 32 pasos del hardware (DeepMind 12) y promovido a `ABDSharedCode/SynthCore` como componente agnóstico para ser compartido por toda la suite **ABDSynths** (ABDEep, ABDMS2000, ABDCZ101, ABDNeural, WASM o nuevas arquitecturas).

Sigue los principios de diseño de `ABDSharedCode`:
> **«El motor expone la muestra y el suceso; la política permanece en el consumidor.»**

- **Agnóstico de frameworks:** No depende de JUCE, VST ni bibliotecas del sistema operativo. Es apto para síntesis nativa de escritorio, entornos embebidos y AudioWorklet en WebAssembly.
- **Doble rol en la arquitectura de modulación:**
  1. **Fuente de modulación (*Modulation Source*):** Su salida continua producida por `nextSample()` alimenta la matriz de modulación (ej. `ModSource::kControlSequencer = 18` en ABDEep) con un rango bipolar $[-1.0f, +1.0f]$.
  2. **Destino de modulación (*Modulation Destination*):** Su tasa de deslizamiento (*slew rate*) puede ser modulada en tiempo real desde la matriz (ej. `ModDestination::kSeqSlew = 72` en ABDEep) mediante `setSlewModulation(float m)`.
- **Zero-alloc en el lazo de audio:** Todo el estado interno de 32 pasos se almacena en memoria estática contigua dentro de la instancia (`float steps[32]`). No realiza llamadas a `malloc`, `new` ni redimensionamientos.
- **Curva de Slew analógica:** Implementa un filtro de retardo exponencial de 1 polo ($\tau = s \times 1.0\,\text{s}$) que aproxima el comportamiento analógico del glide de hardware.
- **Swing continuo:** Permite desplazar la duración relativa de los pasos pares e impares sin alterar la duración global del compás.

---

## 2. Integración en CMake

Para enlazar el módulo en el `CMakeLists.txt` de tu sintetizador:

```cmake
target_link_libraries(TuSintetizador PRIVATE
    ABDShared::SynthCore
)
```

En tus archivos fuente C++:

```cpp
#include "SynthCore/ControlSequencer.h"
```

---

## 3. Divisores de Reloj y Sincronización Métrica (NRPN 0..15)

El método `setClockDivider(int div)` acepta un índice entero de $0$ a $15$. La duración del paso se calcula en función del tempo maestro (`setMasterBpm`):

$$\text{duraciónBase} = \text{quartersPerStep}(\text{div}) \times \frac{60.0}{\text{BPM}} \times F_s$$

| Índice (`div`) | Razón musical | Negras por paso (`quartersPerStep`) | Equivalencia rítmica |
|:---:|:---|:---:|:---|
| **0** | 4 notas enteras | $16.0$ | 4 Redondas (cuadrante lento) |
| **1** | 3 notas enteras | $12.0$ | 3 Redondas |
| **2** | 2 notas enteras | $8.0$ | 2 Redondas |
| **3** | 1 nota entera | $4.0$ | Redonda |
| **4** | 1/2 | $2.0$ | Blanca |
| **5** | 3/8 | $1.5$ | Negra con puntillo |
| **6** | 1/3 | $4/3 \approx 1.3333$ | Tresillo de blanca |
| **7** | 1/4 | $1.0$ | Negra |
| **8** | 3/16 | $0.75$ | Corchea con puntillo |
| **9** | 1/6 | $2/3 \approx 0.6667$ | Tresillo de negra |
| **10** | 1/8 | $0.5$ | Corchea |
| **11** | 3/32 | $0.375$ | Semicorchea con puntillo |
| **12** | 1/12 | $1/3 \approx 0.3333$ | Tresillo de corchea |
| **13** | **1/16** | **0.25** | **Semicorchea (valor por defecto)** |
| **14** | 3/64 | $3/16 = 0.1875$ | Fusa con puntillo |
| **15** | 1/24 | $1/6 \approx 0.1667$ | Tresillo de semicorchea |

---

## 4. Algoritmos Matemáticos Internos

### 4.1. Partición de Swing

El parámetro `setSwing(float swing)` toma un valor normalizado entre $0.0f$ y $1.0f$:

$$r = 0.5 + 0.25 \times \text{swing}$$

- Con `swing = 0.0f`: $r = 0.5$ (ambos pasos duran el 50% del ciclo base: ritmo estricto y recto).
- Con `swing = 1.0f`: $r = 0.75$ (el paso par dura el 75% y el paso impar dura el 25%: swing máximo).

La duración en muestras de cada paso se determina como:
$$\text{duración} = \text{duraciónBase} \times \begin{cases} r & \text{si el paso actual es par} \\ 2.0 - r & \text{si el paso actual es impar} \end{cases}$$

### 4.2. Filtrado Exponencial de Slew / Glide

El slew se compone del ajuste estático del panel más la modulación dinámica proveniente de la matriz de modulación:

$$s = \text{clamp}(\text{slewRate} + \text{slewMod}, 0.0f, 1.0f)$$

- Si $s \le 0.0f$: Se desactiva el glide; la señal pasa instantáneamente al valor objetivo del paso (`currentValue = targetValue`).
- Si $s > 0.0f$: Se aplica un filtro de primer orden continuo:

$$\tau = s \times 1.0\,\text{segundos}$$
$$\text{coef} = 1.0 - e^{-\frac{1.0}{\tau \cdot F_s}}$$
$$\text{currentValue}[n] = \text{currentValue}[n - 1] + (\text{targetValue} - \text{currentValue}[n - 1]) \times \text{coef}$$

---

## 5. API Pública (`abd::synth::ControlSequencer`)

### Ciclo de vida y configuración básica

```cpp
void prepare(double sampleRate);
```
Inicializa el motor a la tasa de muestreo dada. Resetea los acumuladores de fase y ubica el cursor en el paso 0.

```cpp
void reset();
```
Reinicia inmediatamente el índice de paso a 0 (`stepIndex = 0`), la fase acumulada a 0.0 y el flag de una sola pasada. Es el punto de entrada para sincronización por pulsación de tecla (*Key Sync*).

```cpp
void setEnabled(bool on);
bool isEnabled() const;
```
Habilita o deshabilita el secuenciador. Si está deshabilitado, `nextSample()` devuelve $0.0f$ inmediatamente.

### Parámetros de Secuencia

```cpp
void setStep(int stepOneBased, float bipolar);
```
Configura el valor de modulación de un paso específico.
- `stepOneBased`: Índice de paso en base 1 ($1 \le \text{step} \le 32$).
- `bipolar`: Valor normalizado entre $-1.0f$ y $+1.0f$.
- *Nota:* Si se edita el paso que se encuentra actualmente en reproducción, el `targetValue` se actualiza inmediatamente en el lazo.

```cpp
void setLength(int steps);
```
Define el número de pasos activos ($1 \le \text{length} \le 32$).

```cpp
void setClockDivider(int div);
```
Ajusta el divisor métrico ($0 \le \text{div} \le 15$).

```cpp
void setMasterBpm(float bpm);
```
Tempo maestro en pulsos por minuto (clamp mínimo de 20.0 BPM).

```cpp
void setSwing(float swing);
```
Cantidad de swing ($0.0f \le \text{swing} \le 1.0f$).

```cpp
void setKeyLoopMode(int mode);
```
Modo de reproducción y bucle:
- `0`: Bucle continuo (*Loop*).
- `1`: Un solo pase (*One-Shot* / *Key Sync On* — se reinicia con la tecla y se detiene en 0 tras completar los pasos).
- `2`: Bucle continuo con reinicio por tecla.

### Parámetros de Deslizamiento (Slew)

```cpp
void setSlewRate(float rate);
```
Velocidad de glide manual fijada desde el panel del sinte ($0.0f \le \text{rate} \le 1.0f$).

```cpp
void setSlewModulation(float m);
```
Cantidad de modulación adicional inyectada por la matriz de modulación hacia el parámetro de slew.

### Renderizado y Consulta

```cpp
float nextSample();
```
Genera y avanza una muestra en el lazo de audio. Devuelve el valor bipolar actual con la curva de slew aplicada.

```cpp
int getCurrentStep() const;
```
Devuelve el índice base 0 del paso actual ($0 \le \text{step} < \text{length}$).

---

## 6. Ejemplos de Integración

### Patrón A: Motor de Audio C++ Puro / WebAssembly

```cpp
#include "SynthCore/ControlSequencer.h"

class SynthVoiceEngine
{
public:
    void prepare(double sampleRate)
    {
        controlSeq.prepare(sampleRate);
        controlSeq.setClockDivider(13); // 1/16
        controlSeq.setLength(16);
        controlSeq.setEnabled(true);
    }

    void processBlock(float* outL, float* outR, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // 1. Obtener el valor de modulación de esta muestra
            const float seqMod = controlSeq.nextSample();

            // 2. Modular el corte del filtro u osciladores
            filter.setCutoffModulation(seqMod * 0.5f);

            // 3. Renderizar muestra de audio de la voz
            float s = voice.renderSample();
            outL[i] = s;
            outR[i] = s;
        }
    }

private:
    abd::synth::ControlSequencer controlSeq;
};
```

### Patrón B: Shim de compatibilidad para Plugins JUCE (Patrón ABDEep)

En plugins basados en JUCE, el motor compartido se consume heredando de `abd::synth::ControlSequencer` dentro del namespace del plugin.

#### 1. Cabecera del Shim: `Source/DSP/ControlSequencer.h`

```cpp
#pragma once
#include "SynthCore/ControlSequencer.h"

namespace MiSynth
{
    /**
     * @brief Shim de compatibilidad para ControlSequencer en el sintetizador.
     * Hereda el motor canónico C++20 de abd::synth::ControlSequencer.
     */
    class ControlSequencer : public abd::synth::ControlSequencer
    {
    public:
        using abd::synth::ControlSequencer::ControlSequencer;
    };
}
```

#### 2. Unidad de Traducción del Shim: `Source/DSP/ControlSequencer.cpp`

```cpp
#include "ControlSequencer.h"

namespace MiSynth
{
    // La implementación algorítmica canónica reside en abd::synth::ControlSequencer
    // (ABDSharedCode/SynthCore). Este archivo se mantiene como unidad de traducción
    // para coherencia con la lista de fuentes CMake de MiSynth (DspSources.cmake).
}
```

---

## 7. Interacción con la Matriz de Modulación (`ModMatrixT`)

El secuenciador de control participa simultáneamente como **fuente** y como **destino** en la matriz de modulación:

```cpp
// En el bucle de procesamiento por muestra (sample-rate) o por bloque (control-rate):

// 1. Inyectar modulación externa HACIA el Slew del secuenciador
const float slewFromMatrix = modMatrix.get(ModDestination::kSeqSlew);
controlSequencer.setSlewModulation(slewFromMatrix);

// 2. Extraer la muestra generada por el secuenciador
const float seqSample = controlSequencer.nextSample();

// 3. Alimentar el secuenciador como FUENTE para otros destinos de la matriz
modSources[(int)ModSource::kControlSequencer] = seqSample;
```

---

## 8. Reglas de Calidad e Invariantes

1. **C++20 Limpio sin Advertencias:** Compatible con MSVC (`/W4`), Clang y GCC (`-Wall -Wextra`).
2. **Determinismo Muestra a Muestra:** A igualdad de BPM, samplerate, división métrica y swing, dos ejecuciones producen secuencias de flotantes idénticas bit a bit.
3. **Seguridad en Tiempo Real:** Prohibido realizar asignaciones en el heap, accesos bloqueantes de mutex o lanzamiento de excepciones dentro de `nextSample()`.
