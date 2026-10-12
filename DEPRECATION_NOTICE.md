# 📢 Aviso Formal de Deprecación y Archivo — ABDSharedCode

> **Fecha:** Octubre 2026  
> **Estado:** ARCHIVADO (DEPRECATED) — MODO SOLO LECTURA  
> **Destino:** [`ABDShared`](https://github.com/ajabadia/ABDShared.git)

---

## 🧭 ¿Por qué este repositorio está en desuso?

Como parte de la **Estandarización de la Suite ABDSynths**, se ha unificado la infraestructura compartida en un único repositorio canónico universal: **`ABDShared`**.

Anteriormente, el código C++ vivía en `ABDSharedCode` y los assets/componentes en `ABDSharedAssets`. Esta división fragmentaba el control de versiones, requería submódulos o dependencias cruzadas y complicaba la paridad entre contratos y motores nativos.

Todo el código C++ ha sido extraído, probado y consolidado bajo el cuadrante **`Code/`** en `ABDShared`:
- `DspCore/`
- `SynthCore/`
- `DspEffects/` (los 56 algoritmos FX analógicos)
- `LutDSP/`
- `AutoUpdater/`
- `Scope/`
- `BankManager/`
- `HardwareDrivers/`
- `WebView2Bridge/`

---

## 🛠️ Cómo migrar un proyecto consumidor

### En CMakeLists.txt:
Reemplaza la inclusión de `ABDSharedCode` por `ABDShared/Code`:

```cmake
# Antes:
set(ABDSHARED_CODE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ABDSharedCode")
add_subdirectory("${ABDSHARED_CODE_DIR}" "${CMAKE_BINARY_DIR}/ABDSharedCode")

# Ahora (canónico):
set(ABDSHARED_CODE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ABDShared/Code")
add_subdirectory("${ABDSHARED_CODE_DIR}" "${CMAKE_BINARY_DIR}/ABDShared_build")
```

Los alias de los targets se mantienen idénticos para garantizar retrocompatibilidad total:
- `ABDShared::SynthCore` / `abd_synth_core`
- `ABDShared::DspCore` / `abd_dsp_core`
- `ABDShared::DspEffects` / `abd_dsp_effects`
- `ABDShared::LutDSP` / `abd_lut_dsp`
- `ABDShared::AutoUpdater` / `abd_autoupdater`
- `ABDShared::ScopeCore` / `abd_scope_core`
- `ABDShared::BankManagerCore` / `abd_bank_manager_core`
- `ABDShared::HardwareDrivers` / `abd_hardware_drivers`
- `ABDShared::WebView2Bridge` / `abd_webview2_bridge`

### Repositorio canónico:
👉 https://github.com/ajabadia/ABDShared.git
