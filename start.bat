@echo off
REM ============================================================
REM start.bat - Servidor de desarrollo local para ABDScope WebUI
REM ============================================================

echo ============================================================
echo   ABDScope - Universal Audio Visualizer Engine
echo   Iniciando Servidor Web Local en Puerto 8391...
echo   URL de Acceso: http://localhost:8391/
echo ============================================================

REM Abre el navegador automaticamente tras 1 segundo
start "" http://localhost:8391/

REM Inicia el servidor estatico sirv con CORS y hot-reload
npx -y sirv-cli WebUI --port 8391 --cors --dev

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Reintentando con servidor Python fallback...
    python -m http.server 8391 --directory WebUI
)
