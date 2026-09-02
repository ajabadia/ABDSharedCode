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
