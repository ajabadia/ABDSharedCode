cmake
# --- BankManager Module (requires JUCE) ---
# Contract-driven bank management embeddable in any JUCE-based synth.
# Two consumable layers:
#   1. ABDBankManagerCore   - pure C++ static lib (Library/Bank/Patch ValueTree v1,
#                              blobs Base64, IPC via callbacks). Host-agnostic.
#   2. BankManagerWebAssets - JUCE binary data block for the embeddable modal UI
#                              (BankManagerModal.js + index.html). Styles, models,
#                              icons and contract JSON are served from ABDSharedAssets.
#
# Scope: the embeddable, contract-driven core only. The standalone Tauri/Rust/SQLite
# app and its multi-model tree view, hex editor, MIDI command bridge and build scripts
# are NOT part of this module (see ABDBankManager/README.md §5 "Cut: app vs module").
option(ABDSHAREDCODE_BUILD_BANKMANAGER "Build BankManager module (requires JUCE)" ON)
if(ABDSHAREDCODE_BUILD_BANKMANAGER)
    add_library(ABDShared_BankManager INTERFACE)

    add_library(ABDShared::BankManager ALIAS ABDShared_BankManager)

    target_include_directories(ABDShared_BankManager INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/cpp
    )

    target_sources(ABDShared_BankManager INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/cpp/ABDBankManagerCore.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/cpp/ABDBankManagerCore.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/cpp/ABDBankManagerCore_fwd.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/cpp/MidiSysExQueue.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/cpp/MidiSysExQueue.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/Contracts/ModelContract.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/Contracts/ModelContractRegistry.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/Contracts/ModelContractRegistry.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/Contracts/ImportAdapter.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/Contracts/ExportAdapter.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/Contracts/HardwareLinkContract.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/Contracts/PatchData.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/Contracts/ValidationSchemas.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/ImportExport/CasioCZAdapter.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/ImportExport/CasioCZAdapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/ImportExport/RolandJunoAdapter.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/ImportExport/RolandJunoAdapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/ImportExport/KorgAdapter.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/ImportExport/KorgAdapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/ImportExport/BehringerAdapter.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/ImportExport/BehringerAdapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/ImportExport/YamahaDX7Adapter.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/ImportExport/YamahaDX7Adapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/Fingerprint/Fingerprint.h
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/Fingerprint/Fingerprint.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/WebUI/index.html
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/WebUI/BankManagerModal.js
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/WebUI/BankManagerModal.css
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/WebUI/BankManagerModal.html
    )

    target_link_libraries(ABDShared_BankManager INTERFACE
        juce_core
        juce_audio_basics
        juce_gui_extra
    )

    # --- ABI compatibility aliases for consumers of the old standalone ---
    # Projects that previously used `find_package(ABDBankManager)` or linked
    # `ABDBankManager::ABDBankManagerCore` directly get the same targets here.
    add_library(ABDShared_BankManagerCore STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/cpp/ABDBankManagerCore.cpp
    )
    add_library(ABDShared::BankManagerCore ALIAS ABDShared_BankManagerCore)
    target_include_directories(ABDShared_BankManagerCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/BankManager/cpp
    )
    target_link_libraries(ABDShared_BankManagerCore PUBLIC
        juce_core
    )

    # --- WebUI binary assets (optional, per HardwareMidiDetect pattern) ---
    # Embed the modal UI as binary data so hosts do not need a filesystem WebUI
    # install. Styles, models, brands, icons and contract JSON are served from
    # filesystem (ABDSharedAssets) — only the host page + modal logic are embedded.
    if(COMMAND juce_add_binary_data)
        file(GLOB BM_WEB_ASSETS
            "${CMAKE_CURRENT_SOURCE_DIR}/BankManager/WebUI/index.html"
            "${CMAKE_CURRENT_SOURCE_DIR}/BankManager/WebUI/BankManagerModal.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/BankManager/WebUI/BankManagerModal.css"
        )
        list(FILTER BM_WEB_ASSETS EXCLUDE REGEX "/node_modules/")
        if(BM_WEB_ASSETS)
            juce_add_binary_data(BankManagerWebAssets
                HEADER_NAME "BankManagerWebAssets.h"
                NAMESPACE "BankManagerWebAssets"
                SOURCES ${BM_WEB_ASSETS}
            )

            target_include_directories(BankManagerWebAssets INTERFACE
                "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_BankManagerWebAssets/JuceLibraryCode>"
            )
            target_link_libraries(ABDShared_BankManager INTERFACE BankManagerWebAssets)
            target_link_libraries(ABDShared_BankManagerCore INTERFACE BankManagerWebAssets)
            add_library(ABDShared::BankManagerWebAssets ALIAS BankManagerWebAssets)
        endif()
    endif() # COMMAND juce_add_binary_data

    # --- Standalone unit tests for the pure C++ core (no JUCE WebUI) ---
    option(ABDSHAREDCODE_BUILD_BANKMANAGER_TESTS "Build BankManager standalone tests" ON)
    if(ABDSHAREDCODE_BUILD_BANKMANAGER_TESTS)
        add_executable(ABDShared_BankManager_Tests
            BankManager/tests/BankManagerCoreTests.cpp
        )
        target_link_libraries(ABDShared_BankManager_Tests PRIVATE ABDShared_BankManagerCore)
        target_compile_features(ABDShared_BankManager_Tests PRIVATE cxx_std_20)
    endif()
endif() # ABDSHAREDCODE_BUILD_BANKMANAGER
