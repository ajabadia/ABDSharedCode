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
├── CMakeLists.txt              ← Orchestrador
├── INTEGRATION_GUIDE.md        ← Este archivo
├── AutoUpdater/
│   ├── AutoUpdaterConfig.h     ← Config por proyecto
│   ├── AutoUpdater.h           ← Interfaz pública
│   └── AutoUpdater.cpp         ← Implementación
└── (futuros módulos)
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
  GIT_TAG        main    # o una versión específica: v1.0.0
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
      GIT_TAG        main
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
