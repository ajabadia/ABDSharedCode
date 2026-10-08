# Guía de Integración — LfoAnalog (`abd::synth::LfoAnalog`)

> **Módulo:** `SynthCore`  
> **Espacio de nombres:** `abd::synth`  
> **Target CMake:** `ABDShared::SynthCore`  
> **Estándar C++:** C++20  
> **Dependencias:** Ninguna (100% C++ puro, libre de JUCE, libre de GUI)  
> **Garantía de Tiempo Real:** Zero-alloc en el render loop de audio (`nextSample`), sin bloqueos ni excepciones, determinismo bit a bit.

---

## 1. Filosofía y Arquitectura

El motor `abd::synth::LfoAnalog` es un oscilador de baja frecuencia multionda de precisión a nivel de muestra (*sample-accurate*) modelado a partir del comportamiento analógico del hardware (DeepMind 12). 

Fue extraído y promovido a `ABDSharedCode/SynthCore` para estar a disposición de toda la suite **ABDSynths** (ABDEep, ABDMS2000, ABDCZ101, ABDNeural, WASM o nuevas arquitecturas).

### Coexistencia con `SynthCore/LFO.h` (Korg MS-2000)
`SynthCore` contiene intencionadamente dos implementaciones de LFO por perfil de máquina:
- **`abd::synth::LFO` (`SynthCore/LFO.h`):** Emulación digital del Korg MS-2000 (formas LFO1/LFO2, `SquarePlus` con ancho aleatorio por ciclo, y tabla fija de divisiones métricas SysEx Korg).
- **`abd::synth::LfoAnalog` (`SynthCore/LfoAnalog.h`):** Emulación analógica con 7 formas continuas, retardo de fade-in no lineal, limitador de pendiente analógica (*slew*) y rango extendido hasta banda de audio (1280 Hz).

Sigue el principio rector de diseño de `ABDSharedCode`:
> **«El motor expone la muestra y el suceso; la política permanece en el consumidor.»**

- **Agnóstico de frameworks:** No depende de JUCE ni de bibliotecas del sistema operativo.
- **Zero-alloc en el lazo de audio:** Todo su estado se mantiene en variables escalares primitivas contiguas (`double`, `float`, `uint32_t`).
- **Rango audio-rate continuo:** Soporta frecuencias desde 0.005 Hz (ciclos de más de 3 minutos) hasta 1280.0 Hz (modulación en banda audible y síntesis FM).
- **Salida bipolar y unipolar:** `nextSample()` entrega la señal bipolar $[-1.0f, +1.0f]$ atenuada por el fade-in; `getUnipolar()` expone la versión desplazada $[0.0f, 1.0f]$.

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
#include "SynthCore/LfoAnalog.h"
```

---

## 3. Formas de Onda (`LfoAnalog::Shape`)

El método `setShape(int shapeIndex)` selecciona una de las 7 formas de onda disponibles:

| Índice | Enumerado | Comportamiento del Algoritmo | Rango Crudo |
|:---:|:---|:---|:---:|
| **0** | `Shape::kSine` | Seno trigonométrico puro: $\sin(2\pi \cdot p)$ | $[-1.0f, +1.0f]$ |
| **1** | `Shape::kTriangle` | Triángulo simétrico continuo sin discontinuidades | $[-1.0f, +1.0f]$ |
| **2** | `Shape::kSquare` | Cuadrada bipolar: $+1.0$ si $p < 0.5$, $-1.0$ si $p \ge 0.5$ | $\{-1.0f, +1.0f\}$ |
| **3** | `Shape::kRampUp` | Rampa ascendente (diente de sierra positiva): $-1.0 + 2p$ | $[-1.0f, +1.0f]$ |
| **4** | `Shape::kRampDown` | Rampa descendente (diente de sierra negativa): $+1.0 - 2p$ | $[-1.0f, +1.0f]$ |
| **5** | `Shape::kSampleHold` | Muestreo y retención pseudoaleatorio por ciclo (LCG determinista) | $[-1.0f, +1.0f]$ |
| **6** | `Shape::kSampleGlide` | S&H con filtro analógico de transición exponencial dependiente del período | $[-1.0f, +1.0f]$ |

---

## 4. Algoritmos Matemáticos Analógicos Internos

### 4.1. Curva Analógica de Delay y Fade-In

El hardware analógico no introduce una rampa lineal simple desde el inicio de la nota. Aplica una partición temporal en dos fases:

$$\text{delaySamples} = \text{delayTime} \times \text{delayScale} \times F_s$$

1. **Fase Silenciosa (40% inicial):**  
   Si $\text{samplesElapsed} < 0.4 \times \text{delaySamples}$, el multiplicador de amplitud es cero:
   $$\text{fadeGain} = 0.0f$$
2. **Fase de Rampa Lineal (60% restante):**  
   Si $\text{samplesElapsed} \ge 0.4 \times \text{delaySamples}$, la ganancia crece linealmente de $0.0$ a $1.0$:
   $$\text{fadeGain} = \frac{\text{samplesElapsed} - 0.4 \cdot \text{delaySamples}}{0.6 \cdot \text{delaySamples}}$$

### 4.2. Limitador de Pendiente Analógica (*Slew Limiter*)

A diferencia de un filtro IIR común, el slew analógico limita la **velocidad máxima de cambio de voltaje por unidad de tiempo**:

$$\tau = \text{slew} \times \text{slewScale} \times 0.5\,\text{segundos}$$
$$\Delta_{\max} = \frac{2.0}{\tau \cdot F_s}$$

Para cada muestra, la variación entre la salida bruta (`rawOutput`) y la muestra anterior (`lastOutput`) se restringe estrictamente:
$$\Delta = \text{clamp}(\text{rawOutput} - \text{lastOutput}, -\Delta_{\max}, +\Delta_{\max})$$
$$\text{output} = \text{lastOutput} + \Delta$$

### 4.3. Curva de Transición en Sample & Glide

En el modo `Shape::kSampleGlide`, la constante de tiempo de transición es proporcional al **período actual del LFO**, garantizando que el deslizamiento conserve su carácter musical tanto a frecuencias muy bajas como en audio-rate:

$$T = \frac{1.0}{\max(0.01, \text{rate})}$$
$$t_{\text{glide}} = T \times 0.1\,\text{segundos}$$
$$k = 1.0 - e^{-\frac{1.0}{t_{\text{glide}} \cdot F_s}}$$
$$\text{currentSH}[n] = \text{currentSH}[n-1] + (\text{targetSH} - \text{currentSH}[n-1]) \times \text{clamp}(k, 0.001f, 1.0f)$$

---

## 5. API Pública (`abd::synth::LfoAnalog`)

### Ciclo de vida y configuración

```cpp
void setSampleRate(double sampleRate);
```
Actualiza la tasa de muestreo y recalcula el incremento de fase interno.

```cpp
void reset();
```
Reinicia la fase a 0.0, borra el contador de fade-in y resetea las memorias de S&H y slew.

```cpp
void trigger();
```
Disparo sincronizado por pulsación de tecla (*Key Sync*). Si `keySync` está activo (`true`), reinicia la fase a 0.0 y reanuda el ciclo de delay/fade-in.

```cpp
void setKeySync(bool sync);
```
Habilita o deshabilita la sincronización por pulsación de tecla.

### Parámetros de Frecuencia y Forma

```cpp
void setRate(float rateHz);
```
Ajusta la frecuencia del oscilador en Hertz (acotado internamente a $[0.005\text{ Hz}, 1280.0\text{ Hz}]$). Cuenta con protección contra recálculos redundantes (*hot-path guard*).

```cpp
void setShape(int shapeIndex);
```
Fija la forma de onda (0 a 6).

### Parámetros Analógicos y Modulación

```cpp
void setDelay(float delaySec);
```
Tiempo de retraso / fade-in en segundos antes de alcanzar la amplitud completa.

```cpp
void setSlew(float slewAmount);
```
Intensidad del limitador de pendiente ($0.0f \le \text{slew} \le 1.0f$).

### Escalas de Calibración / Deep-Dive

```cpp
void setRateScale(float scale);   // Multiplicador de frecuencia [0.05, 20.0]
void setDelayScale(float scale);  // Multiplicador de tiempo de delay [0.0, 20.0]
void setSlewScale(float scale);   // Multiplicador de tiempo de slew [0.0, 20.0]
```

### Control de Fase y Renderizado

```cpp
void setPhase(double newPhase);
double getPhase() const;
```
Fija o consulta la fase actual normalizada $[0.0, 1.0)$. Utilizado por el sintetizador para modos de distribución de fase por voz (*Spread*).

```cpp
float nextSample();
```
Genera la siguiente muestra bipolar $[-1.0f, +1.0f]$ multiplicada por el fade-in analógico.

```cpp
float getUnipolar() const;
```
Devuelve la última muestra generada convertida a rango unipolar $[0.0f, 1.0f]$.

---

## 6. Ejemplos de Integración

### Patrón A: Motor C++ Puro / WebAssembly

```cpp
#include "SynthCore/LfoAnalog.h"

class Voice
{
public:
    void prepare(double sampleRate)
    {
        lfo.setSampleRate(sampleRate);
        lfo.setRate(5.0f); // 5 Hz
        lfo.setShape((int)abd::synth::LfoAnalog::Shape::kTriangle);
    }

    void onNoteOn()
    {
        lfo.trigger();
    }

    void render(float* buffer, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float lfoVal = lfo.nextSample();
            buffer[i] = osc.renderSample(lfoVal * 0.1f);
        }
    }

private:
    abd::synth::LfoAnalog lfo;
    Oscillator osc;
};
```

### Patrón B: Shim de compatibilidad en Plugins JUCE (Patrón ABDEep)

#### 1. Cabecera del Shim: `Source/DSP/LFO.h`

```cpp
#pragma once
#include "SynthCore/LfoAnalog.h"

namespace MiSynth
{
    /**
     * @brief Shim de compatibilidad para el LFO en el plugin.
     * Hereda el motor canónico C++20 de abd::synth::LfoAnalog.
     */
    class LFO : public abd::synth::LfoAnalog
    {
    public:
        using abd::synth::LfoAnalog::LfoAnalog;
        using Shape = abd::synth::LfoAnalog::Shape;
    };
}
```

#### 2. Unidad de Traducción del Shim: `Source/DSP/LFO.cpp`

```cpp
#include "LFO.h"

namespace MiSynth
{
    // La implementación algorítmica canónica reside en abd::synth::LfoAnalog.
    // Este archivo se mantiene como unidad de traducción para coherencia con
    // las listas estáticas de compilación CMake (DspSources.cmake).
}
```

---

## 7. Separación de Responsabilidades: Motor vs. Política

Para mantener el motor compartido limpio y portable, la política del sintetizador no debe contaminar `LfoAnalog`:

| Responsabilidad | Dónde reside | Cómo se implementa |
|---|---|---|
| **Cálculo de forma de onda, delay y slew** | `abd::synth::LfoAnalog` | En el motor de `SynthCore` mediante `nextSample()`. |
| **Modo Poly vs. Mono vs. Spread** | Sintetizador consumidor | La voz decide si invoca su instancia local `lfo.nextSample()`, lee el buffer global `globalLfo[sampleIndex]` o aplica desfase con `lfo.setPhase(voiceIndex * spreadCycles)`. |
| **Sincronización a BPM / Arpegiador** | Sintetizador consumidor | El engine convierte la subdivisión rítmica a Hz (`bpm / 60.0 * factor`) y llama a `lfo.setRate(hz)`. |
| **Mapeo en Matriz de Modulación** | Sintetizador consumidor | El plugin inyecta `lfo.nextSample()` a los slots de fuentes y aplica las modulaciones de rate/delay/slew recibidas de la matriz. |
| **Detección de Cruce por Cero (Zero-Crossing)** | Sintetizador consumidor | El lazo de voces comprueba `prevSample < 0.0f && sample >= 0.0f` para disparar el retrigger de envolventes. |

---

## 8. Verificación y Reglas de Calidad

1. **Cero Asignaciones en Tiempo Real:** Prohibido realizar asignaciones en el heap o llamadas bloqueantes en `nextSample()`.
2. **Determinismo:** El generador de números pseudoaleatorios interno (LCG) produce secuencias bit a bit reproducibles para modos S&H.
3. **Compilación Limpia:** Compilar sin advertencias en `/W4` (MSVC) y `-Wall -Wextra` (Clang/GCC).
