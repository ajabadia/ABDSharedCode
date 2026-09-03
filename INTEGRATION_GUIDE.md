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