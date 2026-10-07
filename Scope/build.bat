@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   ABDScope Universal Visualizer Build and Test Pipeline
echo ========================================================
echo.

:: 1. C++ Core Build with CMake & MSVC
echo [1/3] Configuring CMake (C++20 MSVC)...
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configuration failed!
    exit /b %ERRORLEVEL%
)

echo.
echo [2/3] Building C++ Core and Smoke Test Target...
cmake --build build --config Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] C++ build failed!
    exit /b %ERRORLEVEL%
)

echo.
echo Running C++ Standalone Sanity Verification (real checks, Debug+Release safe)...
build\Release\ABDScope_CppSmoke.exe
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] C++ sanity verification failed!
    exit /b %ERRORLEVEL%
)

:: 2. WebUI Unit Tests
echo.
echo [3/3] Running WebUI Vitest Suite (56 Tests)...
call npm test -- --run
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] WebUI test suite failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ========================================================
echo   [SUCCESS] ALL BUILDS AND TESTS PASSED (100%)
echo ========================================================
