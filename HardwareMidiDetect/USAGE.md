# HardwareMidiDetect — Guía de Uso v2

Detección automática de hardware MIDI sintetizador, **100% contract-driven**.

- **Ningún SysEx** Query está hardcodeado: se construyen desde los contratos.
- **Ningún mapeo** fabricante/modelo está hardcodeado: se resuelve por contrato.
- Los contratos son single-source en `ABDSharedAssets/contracts/hardware/` (clave `midiIdentification` + `autoDetectSysEx` + `modelImage` + `brandLogo`).

---

## 1. Requisitos

- **C++20** (ya lo exige `ABDSharedCode`).
- **JUCE** `juce_audio_devices`, `juce_gui_extra`, `juce_core` (el target los enlaza).
- **nlohmann_json** (para el parser de contratos; se detecta automáticamente).
- Para el picker WebView2: Windows con WebView2 Runtime (WebBrowserComponent).
- Para embeber el WebUI: `juce_add_binary_data` (JUCE en scope).
- **ABDSharedAssets** accesible en runtime para `styles/`, `models/`, `brands/`.

---

## 2. Integración CMake

```cmake
add_subdirectory("${ABDSHARED_CODE_DIR}" "${CMAKE_BINARY_DIR}/ABDSharedCode")
target_link_libraries(TuPlugin PRIVATE ABDShared::HardwareMidiDetect)
```

---

## 3. Cargar contratos

```cpp
#include <HardwareMidiDetect/HardwareContractRegistry.h>

abd::hwid::HardwareContractRegistry registry;
auto dir = juce::File::getCurrentWorkingDirectory()
               .getChildFile("../../../ABDSharedAssets/contracts/hardware");
if (registry.loadContractsFromDirectory(dir))
{
    auto contracts = registry.getContracts(); // std::vector<abd::hwid::HardwareContract>
}
```

---

## 4. Capa C++ pura: `HardwareMidiDetector`

### Configuración (`DetectionConfig`)

```cpp
HardwareMidiDetector::DetectionConfig config;
config.allowedHardwareIds = {};           // whitelist (ej. {"korg_ms2000"})
config.maxResults = 1;                    // 1 = single, >1 = multi
config.autoSelectIfSingle = true;         // auto-callback si 1 match
config.includeHeuristic = true;           // incluir matches por nombre puerto
config.requireSysExVerified = false;      // solo SysEx verificado
```

### Uso

```cpp
#include <HardwareMidiDetect/HardwareMidiDetector.h>

HardwareMidiDetector detector(registry.getContracts());
auto found = detector.scanAllPorts(config, /*timeoutMs=*/350);

for (const auto& dev : found)
{
    dev.hardwareId;           // "korg_ms2000"
    dev.displayName;          // "Korg MS2000 / MS2000R"
    dev.portIndex;            // índice puerto salida
    dev.deviceId;             // deviceId del Identity Reply (0x00-0x7F)
    dev.midiChannel;          // 0 = unknown
    dev.isSysExVerified;      // true = confirmado por SysEx
    dev.modelImage;           // "models/korg-ms2000.png"
    dev.brandLogo;            // "brands/korg-logo.svg"
}
```

### Utilidades estáticas

| Método | Descripción |
|---|---|
| `buildDetectionQueries(contracts)` | Queries SysEx derivadas (Universal + cada `autoDetectSysEx` único) |
| `parseIdentityReply(msg, out, contracts)` | Parsea Identity Reply contra contratos |
| `matchFromPortNames(inDev, outDev, contracts)` | Heurística fallback por nombre puerto |
| `makeIdentityRequest(deviceId)` | Universal Non-Real Time Identity Inquiry |
| `parseHexBytes("F0 7E 7F 06 01 F7")` | Hex → bytes |

---

## 5. Capa WebView2 + WebUI: `JuceHardwareMidiPicker`

**El software llamante prepara el puente** (patrón ABDScope). Implementás `MidiHardwareBackend` sobre tus `MidiOutput`/`MidiInput`, se lo inyectás al picker, y el WebUI gestiona queries, parse y UX.

### Backend MIDI (ejemplo)

```cpp
#include <HardwareMidiDetect/MidiHardwareBackend.h>
#include <juce_audio_devices/juce_audio_devices.h>

class MySynthMidiBackend : public abd::hwid::MidiHardwareBackend
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
        if (!inPort)
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
```

### Instanciación del Picker

```cpp
#include <HardwareMidiDetect/JuceHardwareMidiPicker.h>
#include <HardwareMidiDetect/HardwareMidiDetector.h>

HardwareMidiDetector::DetectionConfig config;
config.allowedHardwareIds = {};           // whitelist
config.maxResults = 1;                    // 1 = single, >1 = multi
config.autoSelectIfSingle = true;

auto backend = std::make_unique<MySynthMidiBackend>();
picker = std::make_unique<JuceHardwareMidiPicker>(
    *backend,
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
    config
);

// Tema visual
picker->setTheme("audiolab");  // "ms2000" | "cz101" | "deepmind" | "juno" | "audiolab"

addAndMakeVisible(picker.get());
picker->setBounds(getLocalBounds());
picker->startPick(); // lanza la UI de detección
```

### `HardwarePickResult`

```cpp
struct HardwarePickResult {
    bool cancelled { true };
    std::string hardwareId;           // single
    std::string displayName;          // single
    std::vector<std::string> hardwareIds;   // multi
    std::vector<std::string> displayNames;  // multi
    std::string manufacturer;
    std::string model;
    std::string firmwareVersion;
    std::vector<DiscoveredDevice> allDetected;
};
```

### Theme System

```cpp
picker->setTheme("ms2000");   // "cz101" | "deepmind" | "juno" | "audiolab"
```

El WebUI usa `styles/index.css` del sistema universal `ABDSharedAssets/styles/` (tokens + 5 temas + componentes). Scrollbars coherentes automáticamente.

---

## 6. Contrato host ↔ WebUI (canal `nativeEvent`)

| Evento | Dirección | Carga |
|---|---|---|
| `hardware.detect` | WebUI → Host | `{}` |
| `hardware.refreshPorts` | WebUI → Host | `{}` |
| `hardware.result` | WebUI → Host | `{cancelled, hardwareId, displayName, manufacturer, model, firmwareVersion}` (single) o `{cancelled, hardwareIds[], displayNames[], manufacturer, model, firmwareVersion}` (multi) |
| `__setDetectedDevices` | Host → WebUI | `devices[] + config` |
| `__setConfig` | Host → WebUI | `{maxResults}` |

---

## 7. Dependencia de datos (ABDSharedAssets)

El módulo **no copia contratos ni assets**. Deben existir en runtime:

```
ABDSharedAssets/
├── contracts/hardware/*.json
├── styles/           (tokens.css, themes/*.css, components/*.css, index.css)
├── models/           (korg-ms2000.png, roland-juno-106.png, placeholder-synth.svg, ...)
└── brands/           (korg-logo.svg, roland-logo.svg, ...)
```

Forma mínima de un contrato de detección:

```json
{
  "id": "korg_ms2000",
  "displayName": "Korg MS2000 / MS2000R",
  "midiIdentification": {
    "manufacturerIdHex": "42",
    "modelIdHex": "58",
    "sysexHeaderHex": "42 30 58"
  },
  "autoDetectSysEx": "F0 7E 7F 06 01 F7",
  "modelImage": "models/korg-ms2000.png",
  "brandLogo": "brands/korg-logo.svg"
}
```

---

## 8. Extender a un modelo nuevo

1. Agregar (o editar) el contrato JSON en `ABDSharedAssets/contracts/hardware/`.
2. Indicar `midiIdentification` (IDs de fabricante/modelo), `autoDetectSysEx`, `modelImage`, `brandLogo`.
3. Añadir imagen del modelo en `ABDSharedAssets/models/` y logo en `brands/`.
4. **Sin tocar C++ ni WebUI**: el detector y el picker lo reconocen solo.