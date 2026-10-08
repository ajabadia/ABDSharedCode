# Guía de Integración — EnvelopeAnalog (`abd::synth::EnvelopeAnalog`)

> **Módulo:** `SynthCore`  
> **Espacio de nombres:** `abd::synth`  
> **Target CMake:** `ABDShared::SynthCore`  
> **Estándar C++:** C++20  
> **Dependencias:** Ninguna (100% C++ puro, libre de JUCE, libre de GUI)  
> **Garantía de Tiempo Real:** Zero-alloc en el render loop de audio (`nextSample`), determinismo matemático por muestra, sin exclusiones mutuas ni excepciones.

---

## 1. Filosofía y Arquitectura

El motor `abd::synth::EnvelopeAnalog` es un generador de envolvente ADSR de 4 fases con curvatura continua por etapa, modelado a partir de la respuesta del hardware (DeepMind 12).

Fue promovido a `ABDSharedCode/SynthCore` para completar el quinteto troncal de modulación y control temporal junto a `Arpeggiator`, `ControlSequencer`, `ModMatrixT` y `LfoAnalog`.

### Coexistencia con `SynthCore/ADSREnvelope.h` (Korg MS-2000)
`SynthCore` aloja de manera deliberada dos modelos de envolvente con propósitos diferenciados:
- **`abd::synth::ADSREnvelope` (`SynthCore/ADSREnvelope.h`):** Emulación de la carga/descarga de condensadores del Korg MS-2000 (EG1/EG2) en rango normalizado $0..1$.
- **`abd::synth::EnvelopeAnalog` (`SynthCore/EnvelopeAnalog.h`):** Emulación analógica con tiempos en segundos, curvaturas continuas de $-1..+1$ (exponencial a logarítmica), modulación de curva por matriz, offset dinámico de sustain, escala de tiempo para drift y modos de repetición/one-shot.

Sigue el principio rector de diseño de `ABDSharedCode`:
> **«El motor expone la muestra y el suceso; la política permanece en el consumidor.»**

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
#include "SynthCore/EnvelopeAnalog.h"
```

---

## 3. Fases de la Envolvente (`EnvelopeAnalog::Stage`)

| Estado | Enumerado | Comportamiento | Nivel Objetivo |
|:---:|:---|:---|:---:|
| **0** | `Stage::kIdle` | Reposo. La envolvente no emite señal (`0.0f`). | $0.0f$ |
| **1** | `Stage::kAttack` | Rampa ascendente desde el nivel inicial hasta el pico. | $1.0f$ |
| **2** | `Stage::kDecay` | Caída desde el pico hasta el nivel efectivo de sustain. | $\text{sustainEfectivo}()$ |
| **3** | `Stage::kSustain` | Fase de retención mientras la tecla siga pulsada. | $\text{sustainEfectivo}() + \text{mod}$ |
| **4** | `Stage::kRelease` | Desvanecimiento desde el nivel actual al soltar la tecla hasta cero. | $0.0f$ |

---

## 4. Algoritmos Matemáticos Internos

### 4.1. Curvatura No Lineal Continua (`applyCurve`)

El parámetro de curva para cada etapa toma un valor normalizado entre $-1.0f$ y $+1.0f$:

$$\text{curvedProgress} = \text{applyCurve}(\text{progress}, \text{curveAmount})$$

- Si $|\text{curveAmount}| < 0.005f$: Comportamiento lineal estricto ($\text{curvedProgress} = \text{progress}$).
- Si $\text{curveAmount} < 0.0f$ (Exponencial):  
  $$\text{exponent} = 1.0f - \text{curveAmount} \times 3.0f \quad (\text{hasta } 4.0)$$
  $$\text{curvedProgress} = \text{progress}^{\text{exponent}}$$
- Si $\text{curveAmount} > 0.0f$ (Logarítmica):  
  $$\text{exponent} = \frac{1.0f}{1.0f + \text{curveAmount} \times 3.0f} \quad (\text{hasta } 0.25)$$
  $$\text{curvedProgress} = \text{progress}^{\text{exponent}}$$

### 4.2. Inversión de Polaridad por Etapa

Para mantener la intuición analógica del músico:
- **Ataque:** Se niega la curva internamente para que valores negativos produzcan subida rápida inicial (exponencial).
- **Decay y Release:** Se utiliza el complemento $1.0f - \text{applyCurve}(1.0f - \text{progress}, \text{curve})$, garantizando caída rápida inicial con curvatura negativa.
- **Sustain:** Permite un leve desvanecimiento (*slow fade*) o crecimiento progresivo (*slow swell*) de hasta un $\pm 10\%$ durante la pulsación mantenida.

### 4.3. Escala Temporal Dinámica (`setTimeScale`)

Para aplicar el efecto de *Analog Drift* sin reiniciar la envolvente, la tasa de avance de la fase se escala en tiempo real:

$$\text{progressIncrement} = \frac{1.0}{\text{currentStageDurationSec} \times F_s \times \text{timeScale}}$$

Un guard de hot-path (`scale == timeScale`) evita recálculos innecesarios cuando el drift permanece constante.

### 4.4. Modos de Ejecución Especiales

1. **Loop Mode (`setLoopMode(true)`):**  
   Al concluir la fase de Decay, la envolvente re-dispara automáticamente a Attack en bucle cerrado mientras la tecla permanezca pulsada. Al recibir `release()`, el loop se desactiva para completar el decaimiento hacia Idle.
2. **One-Shot Mode (`setBypassSustain(true)`):**  
   Al terminar la fase de Decay, la envolvente salta directamente a Release, omitiendo la retención de Sustain. Diseñado para sonidos de percusión y disparos de un solo golpe.

---

## 5. API Pública (`abd::synth::EnvelopeAnalog`)

### Ciclo de vida y configuración

```cpp
void setSampleRate(double sampleRate);
void reset();
void trigger(); // Note-On
void release(); // Note-Off
```

### Parámetros de Tiempo y Nivel

```cpp
void setParameters(float attackSec, float decaySec, float sustainLevel, float releaseSec);
```
Define los 4 tiempos y el nivel base de sustain ($[0.0f, 1.0f]$). Cada tiempo cuenta con una cota mínima de protección de $0.001\,\text{s}$ (1 milisegundo).

```cpp
void setCurves(float attackCurve, float decayCurve, float sustainCurve, float releaseCurve);
```
Fija la curvatura de cada una de las 4 etapas ($-1.0f \le \text{curve} \le +1.0f$).

```cpp
void setSustainOffset(float offset);
```
Suma bipolar dinámicamente al nivel de sustain, evaluado mediante `sustainEfectivo()`.

```cpp
void setCurveModulation(Stage stage, float modAmount);
```
Inyecta modulación externa proveniente de la matriz de modulación a la curva de una fase específica.

### Renderizado y Consultas

```cpp
float nextSample(); // Genera y avanza la muestra actual en [0.0f, 1.0f]
bool  isActive() const; // true si stage != kIdle
Stage getCurrentStage() const;
float getCurrentLevel() const;
```

---

## 6. Ejemplos de Integración

### Patrón A: Motor C++ Puro / WebAssembly

```cpp
#include "SynthCore/EnvelopeAnalog.h"

class Voice
{
public:
    void noteOn()  { env.trigger(); }
    void noteOff() { env.release(); }

    void process(float* buffer, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float envGain = env.nextSample();
            buffer[i] *= envGain;
        }
    }

private:
    abd::synth::EnvelopeAnalog env;
};
```

### Patrón B: Shim de compatibilidad para Plugins JUCE (Patrón ABDEep)

#### 1. Cabecera del Shim: `Source/DSP/Envelope.h`

```cpp
#pragma once
#include "SynthCore/EnvelopeAnalog.h"

namespace MiSynth
{
    /**
     * @brief Shim de compatibilidad para Envelope.
     * Hereda el motor canónico C++20 de abd::synth::EnvelopeAnalog.
     */
    class Envelope : public abd::synth::EnvelopeAnalog
    {
    public:
        using abd::synth::EnvelopeAnalog::EnvelopeAnalog;
        using Stage = abd::synth::EnvelopeAnalog::Stage;
    };
}
```

#### 2. Unidad de Traducción del Shim: `Source/DSP/Envelope.cpp`

```cpp
#include "Envelope.h"

namespace MiSynth
{
    // La implementación algorítmica canónica reside en abd::synth::EnvelopeAnalog.
    // Este archivo se mantiene como unidad de traducción para coherencia con DspSources.cmake.
}
```

---

## 7. Separación de Responsabilidades: Motor vs. Política

| Responsabilidad | Dónde reside | Cómo se implementa |
|---|---|---|
| **Cálculo de curvatura, fases, loop y one-shot** | `abd::synth::EnvelopeAnalog` | En el motor de `SynthCore` mediante `nextSample()`. |
| **Ciclo de vida y robo de voces** | Sintetizador consumidor | `SynthVoice` utiliza `env1VCA.isActive()` e `isReleasing()` para determinar si la voz está libre o apagándose. |
| **Trigger Modes (Normal, LFO1-trig, LFO2-trig, Loop)** | Sintetizador consumidor | La voz detecta cruces por cero en los LFOs para invocar `env.trigger()`. |
| **Inyección en ModMatrix** | Sintetizador consumidor | La voz lee `nextSample()` y alimenta las fuentes `kEnv1VCA`, `kEnv2VCF`, `kEnv3MOD`. |
| **Analog Drift** | Sintetizador consumidor | La voz consulta `drift.getEnvTimeDrift()` y escala la velocidad con `env.setTimeScale()`. |
