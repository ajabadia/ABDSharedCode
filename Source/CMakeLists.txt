project(ABDScopeCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Pure header-only C++20 core (no JUCE dependency). Safe for JUCE-free consumers
# (e.g. the standalone smoke test) and the base for the JUCE-integration target.
add_library(ABDScopeCoreHeaders INTERFACE)

target_include_directories(ABDScopeCoreHeaders INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Core>
)

target_sources(ABDScopeCoreHeaders INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/Core/ScopeTapType.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Core/SpscRingBuffer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Core/ScopeTap.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Core/ScopeDataCollector.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Core/TapId.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Core/TriggerDetector.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Core/ScopeFrameSerializer.h
)

target_compile_definitions(ABDScopeCoreHeaders INTERFACE
  ABD_SCOPE_EXPORTS
)

target_compile_options(ABDScopeCoreHeaders INTERFACE
  $<$<CXX_COMPILER_ID:MSVC>:/W4;/wd4324;/permissive-;/Zc:__cplusplus;/Zc:inline>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(ABDScopeCoreHeaders INTERFACE /MD)
else()
  target_compile_options(ABDScopeCoreHeaders INTERFACE -fPIC)
endif()

# JUCE-integration core: pure core + JUCE glue (native/WebView2 components and the
# embedded-asset resource provider). JUCE consumers link this one; the JUCE glue
# sources propagate and get compiled into the consumer alongside its JUCE modules.
add_library(ABDScopeCore INTERFACE)
target_link_libraries(ABDScopeCore INTERFACE ABDScopeCoreHeaders)

target_include_directories(ABDScopeCore INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/JUCE>
)

target_sources(ABDScopeCore INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/JuceScopeComponent.h
  ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/JuceWebScopeComponent.h
  ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/ScopeResourceProvider.h
  ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/ScopeResourceProvider.cpp
)

set(ABD_SCOPE_VERSION "0.3.1")
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/Core/ABDScopeConfig.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/ABDScopeConfig.h
  @ONLY
)

target_include_directories(ABDScopeCoreHeaders INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ABDScopeConfigVersion.cmake
  VERSION ${ABD_SCOPE_VERSION}
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/ABDScopeConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ABDScopeConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ABDScope
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/ABDScopeConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/ABDScopeConfigVersion.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/ABDScopeConfig.h
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ABDScope
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Core/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/abd/scope/Core
  FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/abd/scope/JUCE
  FILES_MATCHING PATTERN "*.h"
)

# Tests - only enable if GTest is available
if(BUILD_TESTING)
  find_package(GTest QUIET)
  if(GTest_FOUND)
    enable_testing()
    add_subdirectory(tests)
  else()
    message(WARNING "GTest not found - unit tests disabled. Install GTest to enable.")
  endif()
endif()