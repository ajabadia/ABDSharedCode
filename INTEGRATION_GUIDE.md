# Guía de Integración — ABDSharedCode

> **Propósito:** Librería de código compartido entre proyectos ABDSynths. Módulos reutilizables que se integran via CMake.

---

## Filosofía

- **Zero-copy:** Los proyectos nunca copian el código de ABDSharedCode
- **CMake nativo:** Se integra via `add_subdirectory` o `FetchContent`
- **Modular:** Cada módulo es una librería estática independiente
- **Configurable:** Cada proyecto define su propia configuración

---

## Estructura

```
ABDSharedCode/
├── CMakeLists.txt              ← Orquestador
├── INTEGRATION_GUIDE.md        ← Este archivo
├── AutoUpdater/
│   ├── AutoUpdaterConfig.h     ← Config por proyecto
│   ├── AutoUpdater.h           ← Interfaz pública
│   └── AutoUpdater.cpp         ← Implementación
└── HardwareMidiDetect/
    ├── HardwareContract.h          ← Contrato de identidad base
    ├── HardwareContractRegistry.*  ← Parser JSON de contratos (ABDSharedAssets)
    ├── HardwareMidiDetector.*      ← Detector C++ contract-driven (sin GUI)
    ├── MidiHardwareBackend.h       ← Interfaz de transporte inyectado por el host
    ├── JuceHardwareMidiPicker.h    ← Componente WebView2 (pick UX) estilo ABDScope
    ├── HardwareMidiPickerResourceProvider.* ← Sirve el WebUI embebido + assets
    └── WebUI/index.html            ← WebUI de detección (queries por contrato)
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

Cargá los contratos (single-source en `ABDSharedAssets/contracts/hardware`) con el registry compartido:

```cpp
#include <HardwareMidiDetect/HardwareContractRegistry.h>

abd::hwid::HardwareContractRegistry registry;
// Ruta relativa al monorepo; ajustá searchRoots según tu layout.
juce::File contractsDir = juce::File::getCurrentWorkingDirectory()
    .getChildFile("../../../ABDSharedAssets/contracts/hardware");
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

---

## Módulo: AudioComparator

Motor de alta precisión para comparación acústica A/B, alineamiento temporal y dictamen automático de calidad analógica vs digital.

### Integración en CMake

```cmake
target_link_libraries(TuProyecto PRIVATE ABDShared::AudioComparator)
```

### Componentes

- **`abd::audio::AudioABComparator`**:
  - **Alineamiento Sub-Muestra**: Correlación cruzada FFT para cálculo de retardo intrínseco (`sampleOffset`, `timeOffsetMs`, `correlationPeak`).
  - **Métricas Temporales**: Comparativa de Peak dBFS, RMS dBFS, MAE y RMSE.
  - **Métricas Espectrales**: Desviación de magnitud logarítmica (`logMagMeanAbsDiffDb`), centroide espectral y balance de energía en 3 bandas (bajos, medios, agudos).
- **`abd::audio::AudioABVerdictEngine`**:
  Evalúa el resultado frente a una matriz de tolerancias configurables (`AudioABVerdictTolerances`) emitiendo un resultado formal: `pass` (dentro de tolerancia), `warn` o `fail`.

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
    // El modelo coincide fielmente con el hardware
}
```

---

## Módulo: LutDSP

Evaluación ultra-rápida de Look-Up Tables multidimensionales con aceleración vectorial SIMD y filtrado analógico polifónico.

### Integración en CMake

```cmake
target_link_libraries(TuProyecto PRIVATE ABDShared::LutDSP)
```

### Componentes

- **`abd::lutdsp::LutEvaluatorSimd`**:
  Evaluador SIMD de tablas 1D y 2D (con interpolación bilineal / bicúbica Catmull-Rom) optimizado para llamadas en bloque dentro del callback de audio de tiempo real.
- **`abd::lutdsp::AnalogLutFilterModule`**:
  Módulo de filtrado polifónico de 8 voces con suavizado balístico exponencial (`smoothingRate`) para evitar artefactos en saltos bruscos de modulación analógica.


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
