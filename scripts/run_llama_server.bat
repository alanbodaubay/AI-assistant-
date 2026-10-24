@echo off
setlocal enabledelayedexpansion

:: Determine executable path
set LLAMA_BIN=llama-server.exe
if exist "%~dp0..\llama-server.exe" (
    set LLAMA_BIN="%~dp0..\llama-server.exe"
) else if exist "%~dp0llama-server.exe" (
    set LLAMA_BIN="%~dp0llama-server.exe"
) else if exist "%~dp0..\tools\llama-server.exe" (
    set LLAMA_BIN="%~dp0..\tools\llama-server.exe"
)

:: Model path argument or fallback to models directory
set MODEL_PATH=%~1
if "%MODEL_PATH%"=="" (
    for %%F in ("%~dp0..\models\*.gguf") do (
        set MODEL_PATH=%%F
        goto :model_found
    )
)

:model_found
if "%MODEL_PATH%"=="" (
    echo =====================================================================
    echo ScreenMate: No GGUF model specified.
    echo.
    echo Usage:
    echo   scripts\run_llama_server.bat ^<path_to_model.gguf^> [path_to_mmproj.gguf]
    echo.
    echo Or place any .gguf model in the 'models\' folder and rerun this script.
    echo =====================================================================
    exit /b 1
)

set MMPROJ_PATH=%~2
set MMPROJ_ARG=
if not "%MMPROJ_PATH%"=="" (
    set MMPROJ_ARG=--mmproj "%MMPROJ_PATH%"
)

echo Starting llama-server with 2-slot prompt caching configuration...
echo Binary:   %LLAMA_BIN%
echo Model:    %MODEL_PATH%
echo Slots:    2 (Slot 0 = Dialogue, Slot 1 = Background Consolidation)
echo Context:  8192 total (4096 tokens per slot)
echo GPU:      RTX 4060 (offloading 99 layers via -ngl 99)
echo Endpoint: http://127.0.0.1:8080/v1/chat/completions
echo.

%LLAMA_BIN% ^
    -m "%MODEL_PATH%" ^
    %MMPROJ_ARG% ^
    --ctx-size 8192 ^
    -np 2 ^
    -ngl 99 ^
    --port 8080 ^
    --host 127.0.0.1

endlocal
