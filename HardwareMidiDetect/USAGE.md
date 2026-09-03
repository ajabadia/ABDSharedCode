# HardwareMidiDetect — Guía de Uso

Detección automática de hardware MIDI sintetizador, **100% contract-driven**.

- **Ningún SysEx** Query está hardcodeado: se construyen desde los contratos.
- **Ningún mapeo** fabricante/modelo está hardcodeado: se resuelve por contrato.
- Los contratos son single-source en `ABDSharedAssets/contracts` (clave `midiIdentification` + `autoDetectSysEx`).

---

## 1. Requisitos

- **C++20** (ya lo exige `ABDSharedCode`).
- **JUCE** `juce_audio_devices`, `juce_gui_extra`, `juce_core` (el target los enlaza).
- **nlohmann_json** (para el parser de contratos; se detecta automáticamente).
- Para el picker WebView2: Windows con WebView2 Runtime (WebBrowserComponent).
- Para embeber el WebUI: `juce_add_binary_data` (JUCE en scope).

## 2. Integración CMake

```cmake
add_subdirectory("${ABDSHARED_CODE_DIR}" "${CMAKE_BINARY_DIR}/ABDSharedCode")
# ...
target_link_libraries(TuPlugin PRIVATE ABDShared::HardwareMidiDetect)
```

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

## 4. Capa C++ pura (detección programática / tests)

```cpp
#include <HardwareMidiDetect/HardwareMidiDetector.h>

abd::hwid::HardwareMidiDetector detector(registry.getContracts());
auto found = detector.scanAllPorts(/*timeoutMs=*/350);

for (const auto& dev : found)
{
    if (dev.isSysExVerified)          // confirmado por Identity Reply
        DBG(dev.displayName);         // "Korg MS2000 / MS2000R"
}
```

Utilidades estáticas ansiosas de test:

| Método | Descripción |
|---|---|
| `buildDetectionQueries(contracts)` | Queries SysEx derivadas (Universal + cada `autoDetectSysEx` único) |
| `parseIdentityReply(msg, out, contracts)` | Parsea un Identity Reply contra contratos |
| `matchFromPortNames(inDev, outDev, contracts)` | Heurística de fallback por nombre de puerto |
| `makeIdentityRequest(deviceId)` | Universal Non-Real Time Identity Inquiry |
| `parseHexBytes("F0 7E 7F 06 01 F7")` | Hex → bytes |

## 5. Capa WebView2 + WebUI (picker con UX)

**El software llamante prepara el puente** (patrón ABDScope). Implementás
`abd::hwid::MidiHardwareBackend` sobre tus `MidiOutput`/`MidiInput` reales y se
lo inyectás al picker. El WebUI embebido gestiona queries, parse del Identity
Reply y la presentación.

Ver el ejemplo completo de `MySynthMidiBackend` + `JuceHardwareMidiPicker` en
[INTEGRATION_GUIDE.md](../INTEGRATION_GUIDE.md#capa-2--webview2--webui-jucehardwaremidipicker).

### Contrato host ↔ WebUI (canal `nativeEvent`)

| Evento | Dirección | Carga |
|---|---|---|
| `hardware.send` | WebUI → host | `{ payload: <base64 SysEx> }` |
| `hardware.listen` | WebUI → host | arma listener + callback |
| `hardware.stop` | WebUI → host | detiene escucha |
| `hardware.result` | WebUI → host | `{ cancelled, hardwareId, displayName, ... }` |
| `__pushMidiBytes(b64)` | host → WebUI | bytes MIDI entrantes (Identity Reply) |

## 6. Dependencia de datos (ABDSharedAssets)

El módulo **no copia contratos**. Debe existir una ruta hacia
`ABDSharedAssets/contracts` (configurable, como en ABDAudioLab con `searchRoots`).

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
  "autoDetectSysEx": "F0 7E 7F 06 01 F7"
}
```

## 7. Extender a un modelo nuevo

1. Agregar (o editar) el contrato JSON en `ABDSharedAssets/contracts`.
2. Indicar `midiIdentification` (IDs de fabricante/modelo) y `autoDetectSysEx`.
3. **Sin tocar C++ ni WebUI**: el detector y el picker lo reconocen solo.