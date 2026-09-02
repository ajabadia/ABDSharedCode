# ABDSharedCode

Librería de código compartido entre proyectos ABDSynths.

## Módulos disponibles

| Módulo | Descripción | Target CMake |
|---|---|---|
| AutoUpdater | Auto-actualización via GitHub Releases | `ABDShared::AutoUpdater` |

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
      GIT_TAG        main
    )
    FetchContent_MakeAvailable(ABDSharedCode)
endif()

target_link_libraries(TuPlugin PRIVATE ABDShared::AutoUpdater)
```

## Documentación

Ver [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md) para guía completa.
