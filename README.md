# ABDSharedCode

Librería de código compartido entre proyectos ABDSynths.

Repositorio: https://github.com/ajabadia/ABDSharedCode.git

## Módulos disponibles

| Módulo | Descripción | Target CMake |
|---|---|---|
| AutoUpdater | Auto-actualización via GitHub Releases | `ABDShared::AutoUpdater` |
| HardwareMidiDetect | Detección contract-driven de hardware MIDI (C++ puro + picker WebView2 estilo ABDScope) | `ABDShared::HardwareMidiDetect` |

> **HardwareMidiDetect** consume los contratos single-source de `ABDSharedAssets/contracts`. Ninguna consulta SysEx ni mapeo fabricante/modelo está hardcodeado: todo se deriva de `midiIdentification` y `autoDetectSysEx` de cada contrato.

## Integración rápida

```cmake
# En CMakeLists.txt del proyecto
set(ABDSHARED_CODE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ABDSharedCode")
if(EXISTS "${ABDSHARED_CODE_DIR}/CMakeLists.txt")
    add_subdirectory("${ABDSHARED_CODE_DIR}" "${CMAKE_BINARY_DIR}/ABDSharedCode")
else()
    include(FetchContent)
    FetchContent_Declare(
      ABDSharedCode
      GIT_REPOSITORY https://github.com/ajabadia/ABDSharedCode.git
      GIT_TAG        master
    )
    FetchContent_MakeAvailable(ABDSharedCode)
endif()

target_link_libraries(TuPlugin PRIVATE ABDShared::AutoUpdater)
target_link_libraries(TuPlugin PRIVATE ABDShared::HardwareMidiDetect)  # opcional
```

> **Nota:** El `GIT_TAG` puede ser una rama (`master`), un tag de versión (`v1.0.0`) o un hash. En producción es recomendable fijarlo a un tag de versión concreto (`vX.Y.Z`), no a `master`, para evitar cambios inesperados.

## Publicación y versionado

Este repo se publica en GitHub. Para releases estables:

1. Nueva funcionalidad → `git add`, `git commit`, `git push`
2. Crear tag de versión:
   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```
3. Los consumidores fijan el tag en el `GIT_TAG` de `FetchContent`

## Documentación

Ver [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md) para guía completa.
