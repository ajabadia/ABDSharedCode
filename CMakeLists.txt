cmake_minimum_required(VERSION 3.22)
project(ABDSharedCode VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- AutoUpdater Module ---
add_library(ABDShared_AutoUpdater STATIC
    AutoUpdater/AutoUpdater.cpp
)

add_library(ABDShared::AutoUpdater ALIAS ABDShared_AutoUpdater)

target_include_directories(ABDShared_AutoUpdater PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ABDShared_AutoUpdater PUBLIC
    juce_core
    juce_events
)

# --- HardwareMidiDetect Module ---
# Contract-driven MIDI hardware detection.
# Two consumable layers:
#   1. HardwareMidiDetector  - pure C++, contract-driven scanning (no GUI).
#   2. JuceHardwareMidiPicker- WebView2 component (ABDScope-style) whose host
#      injects a MidiHardwareBackend; the embedded WebUI does the picking UX.
add_library(ABDShared_HardwareMidiDetect INTERFACE)

add_library(ABDShared::HardwareMidiDetect ALIAS ABDShared_HardwareMidiDetect)

target_include_directories(ABDShared_HardwareMidiDetect INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect
)

target_sources(ABDShared_HardwareMidiDetect INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareContract.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareContractRegistry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareContractRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareMidiDetector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareMidiDetector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/MidiHardwareBackend.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/JuceHardwareMidiPicker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareMidiPickerResourceProvider.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/HardwareMidiPickerResourceProvider.cpp
)

target_link_libraries(ABDShared_HardwareMidiDetect INTERFACE
    juce_audio_devices
    juce_gui_extra
    juce_core
)

# nlohmann_json is used by HardwareContractRegistry. Prefer a fetched/bundled
# target, else fall back to a header-only include if available on the system.
if(NOT TARGET nlohmann_json::nlohmann_json AND NOT TARGET nlohmann_json)
    find_package(nlohmann_json QUIET)
endif()
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(ABDShared_HardwareMidiDetect INTERFACE nlohmann_json::nlohmann_json)
elseif(TARGET nlohmann_json)
    target_link_libraries(ABDShared_HardwareMidiDetect INTERFACE nlohmann_json)
endif()

# Optional: embed the picker WebUI as binary assets so hosts do not need a
# filesystem WebUI install. When the caller has NOT configured a consumer's
# CMake, these assets are generated only if JUCE's juce_add_binary_data is
# available in scope (i.e. when ABDSharedCode is consumed as a subdirectory
# after JUCE is added, or via the helper below).
if(COMMAND juce_add_binary_data)
    # Only embed index.html and any JS files in WebUI root.
    # Styles, models, brands are served from filesystem (ABDSharedAssets).
    file(GLOB HWD_WEB_ASSETS
        "${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/WebUI/index.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/HardwareMidiDetect/WebUI/*.js"
    )
    list(FILTER HWD_WEB_ASSETS EXCLUDE REGEX "/node_modules/")
    if(HWD_WEB_ASSETS)
        juce_add_binary_data(HardwareMidiPickerAssets
            HEADER_NAME "HardwareMidiPickerAssets.h"
            NAMESPACE "HardwareMidiPickerAssets"
            SOURCES ${HWD_WEB_ASSETS}
        )

        target_include_directories(HardwareMidiPickerAssets INTERFACE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_HardwareMidiPickerAssets/JuceLibraryCode>"
        )
        target_link_libraries(ABDShared_HardwareMidiDetect INTERFACE HardwareMidiPickerAssets)
        add_library(ABDShared::HardwareMidiPickerAssets ALIAS HardwareMidiPickerAssets)
    endif()
endif()
