# Guía de Integración — DriftEngine (`abd::synth::DriftEngine`)

> **Módulo:** `SynthCore`  
> **Espacio de nombres:** `abd::synth`  
> **Target CMake:** `ABDShared::SynthCore`  
> **Estándar C++:** C++20  
> **Dependencias:** Ninguna (100% C++ puro, libre de JUCE, libre de GUI)  
> **Garantía de Tiempo Real:** Zero-alloc en el render loop de audio (`nextSample`), determinismo matemático por muestra, sin exclusiones mutuas ni excepciones.

---

## 1. Filosofía y Arquitectura

El motor `abd::synth::DriftEngine` es un generador de fluctuaciones lentas y pseudoaleatorias de ruido browniano (*random-walk* suavizado con filtro de 1 polo), diseñado para emular la inestabilidad física y micro-variaciones de temperatura de componentes analógicos en sintetizadores virtuales:
- Deriva de afinación independiente para OSC1 y OSC2 (`getOsc1PitchDrift()`, `getOsc2PitchDrift()`).
- Deriva de frecuencia de corte del filtro VCF (`getVcfCutoffDrift()`).
- Deriva de resonancia del filtro VCF (`getVcfResonanceDrift()`).
- Deriva de constante de tiempo para envolventes ADSR (`getEnvTimeDrift()`).

Fue promovido desde `ABDEep` a `ABDSharedCode/SynthCore` para estar disponible de forma transversal en todos los sintetizadores analógicos virtuales del ecosistema (`ABDEep`, `ABDJUNiO601`, `ABDMS2000`, `ABDPro800`).

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
#include "SynthCore/DriftEngine.h"
```

---

## 3. Algoritmo y Modelo Matemático

Cada aspecto del sintetizador dispone de una estructura interna `DriftOsc`:

### 3.1. Intervalo de Target Exponencial (`computeTargetInterval`)
El parámetro normalizado `driftRate` ($0..1$) mapea el período temporal entre nuevos objetivos pseudoaleatorios:
- `driftRate = 0.0`: período lento (~10 segundos, deriva lenta y sutil).
- `driftRate = 0.5`: período medio (~1 segundo).
- `driftRate = 1.0`: período rápido (~0.05 segundos = 50 ms, micro-shimmer).

$$T_{\text{interval}} = T_{\text{max}} \cdot \left(\frac{T_{\text{min}}}{T_{\text{max}}}\right)^{\text{driftRate}}$$

### 3.2. Slew Rate Invariante al Sample Rate
Para garantizar que la respuesta física del glide aleatorio sea idéntica a cualquier frecuencia de muestreo del DAW (44.1 kHz, 48 kHz, 96 kHz, 192 kHz), el factor de slew por segundo se normaliza frente a `sampleRate`:

$$\text{slewBase} = \frac{kSlewBasePerSec + \text{driftRate} \cdot kSlewRatePerSec}{\text{sampleRate}}$$

### 3.3. Random-Walk Suavizado y Saturación
Cada muestra avanza el oscilador hacia su target mediante un filtro de 1 polo y satura suavemente con tangente hiperbólica para prevenir desviaciones extremas:

$$v_{\text{current}} = v_{\text{current}} + (v_{\text{target}} - v_{\text{current}}) \cdot \text{slewFactor}$$
$$y = \frac{\tanh(v_{\text{current}} \cdot 2.0)}{2.0}$$

### 3.4. Determinismo LCG Local
Para cumplir la estricta garantía de tiempo real sin contención de hilos y con determinismo absoluto bit-a-bit en pruebas de calibración:
- Generador lineal congruencial (LCG) interno de 32 bits ($a = 1664525$, $c = 1013904223$).
- Cero llamadas a `std::rand()` o generadores con bloqueo de hilos.

---

## 4. API Pública

```cpp
namespace abd::synth
{
    class DriftEngine
    {
    public:
        DriftEngine();

        void setSampleRate(double sampleRate);
        void setDriftParams(float voiceDriftNorm, float paramDriftNorm, float driftRateNorm);
        void resetForNote(float voiceIndex = 0.0f);
        void nextSample();

        // Salidas normalizadas / escaladas
        float getOsc1PitchDrift() const;
        float getOsc2PitchDrift() const;
        float getVcfCutoffDrift() const;
        float getVcfResonanceDrift() const;
        float getEnvTimeDrift() const;
    };
}
```

---

## 5. Shim de Compatibilidad en Sintetizadores Existentes

Para mantener compatibilidad binaria y evitar modificaciones en listas de fuentes como `DspSources.cmake`:

```cpp
// ABDEep/Source/DSP/DriftEngine.h
#pragma once
#include <SynthCore/DriftEngine.h>

namespace ABD
{
    using DriftEngine = abd::synth::DriftEngine;
}
```
