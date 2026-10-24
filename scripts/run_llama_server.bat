@echo off
setlocal enabledelayedexpansion

:: 1. Locate llama-server binary
set LLAMA_BIN=
set LLAMA_DIR=

if exist "D:\llama\cuda\llama-server.exe" (
    set LLAMA_DIR=D:\llama\cuda
    set LLAMA_BIN=D:\llama\cuda\llama-server.exe
) else if exist "%~dp0..\llama-server.exe" (
    set LLAMA_DIR=%~dp0..
    set LLAMA_BIN=%~dp0..\llama-server.exe
) else if exist "%~dp0llama-server.exe" (
    set LLAMA_DIR=%~dp0
    set LLAMA_BIN=%~dp0llama-server.exe
) else (
    set LLAMA_BIN=llama-server.exe
)

:: Ensure DLL directory is in PATH for this session
if not "%LLAMA_DIR%"=="" (
    set "PATH=%LLAMA_DIR%;%PATH%"
)

:: 2. Locate GGUF Model
set MODEL_PATH=%~1
set MMPROJ_PATH=%~2

if "%MODEL_PATH%"=="" (
    if exist "D:\llama-cache\models--unsloth--gemma-4-E4B-it-qat-GGUF\snapshots\8c5a9e4fd5482e2be20fe0bf013b4c262a8f4265\gemma-4-E4B-it-qat-UD-Q4_K_XL.gguf" (
        set MODEL_PATH=D:\llama-cache\models--unsloth--gemma-4-E4B-it-qat-GGUF\snapshots\8c5a9e4fd5482e2be20fe0bf013b4c262a8f4265\gemma-4-E4B-it-qat-UD-Q4_K_XL.gguf
        if "%MMPROJ_PATH%"=="" (
            if exist "D:\llama-cache\models--unsloth--gemma-4-E4B-it-qat-GGUF\snapshots\8c5a9e4fd5482e2be20fe0bf013b4c262a8f4265\mmproj-BF16.gguf" (
                set MMPROJ_PATH=D:\llama-cache\models--unsloth--gemma-4-E4B-it-qat-GGUF\snapshots\8c5a9e4fd5482e2be20fe0bf013b4c262a8f4265\mmproj-BF16.gguf
            )
        )
    ) else (
        for %%F in ("%~dp0..\models\*.gguf") do (
            set MODEL_PATH=%%F
            goto :model_found
        )
    )
)

:model_found
if "%MODEL_PATH%"=="" (
    echo =====================================================================
    echo ScreenMate: No GGUF model found.
    echo.
    echo Usage:
    echo   scripts\run_llama_server.bat ^<path_to_model.gguf^> [path_to_mmproj.gguf]
    echo =====================================================================
    exit /b 1
)

set MMPROJ_ARG=
if not "%MMPROJ_PATH%"=="" (
    set MMPROJ_ARG=--mmproj "%MMPROJ_PATH%"
)

echo =====================================================================
echo ScreenMate: Starting Local llama-server Sidecar
echo Binary:   %LLAMA_BIN%
echo Model:    %MODEL_PATH%
if not "%MMPROJ_PATH%"=="" (
    echo MMProj:   %MMPROJ_PATH%
)
echo Slots:    2 (Slot 0 = Dialogue with cache_prompt, Slot 1 = Background Consolidation)
echo Context:  8192 tokens (4096 per slot)
echo GPU:      NVIDIA RTX 4060 (-ngl 99)
echo Endpoint: http://127.0.0.1:8080/v1/chat/completions
echo =====================================================================
echo.

"%LLAMA_BIN%" ^
    -m "%MODEL_PATH%" ^
    %MMPROJ_ARG% ^
    --ctx-size 8192 ^
    -np 2 ^
    -ngl 99 ^
    --port 8080 ^
    --host 127.0.0.1

endlocal
