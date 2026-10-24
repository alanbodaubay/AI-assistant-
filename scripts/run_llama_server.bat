@echo off
setlocal

set MODEL_PATH=%~1
set MMPROJ_PATH=%~2

if "%MODEL_PATH%"=="" (
    echo Usage: run_llama_server.bat ^<path_to_model.gguf^> [path_to_mmproj.gguf]
    echo Example: run_llama_server.bat models/gemma-4-E4B-it-Q4_K_M.gguf models/gemma-4-E4B-mmproj.gguf
    exit /b 1
)

set MMPROJ_ARG=
if not "%MMPROJ_PATH%"=="" (
    set MMPROJ_ARG=--mmproj "%MMPROJ_PATH%"
)

echo Starting llama-server with 2-slot prompt caching configuration...
echo Model: %MODEL_PATH%
echo Slots: 2 (Slot 0 = Chat/Glance, Slot 1 = Background Extraction)
echo Context: 8192 total (4096 per slot)

llama-server.exe ^
    -m "%MODEL_PATH%" ^
    %MMPROJ_ARG% ^
    --ctx-size 8192 ^
    -np 2 ^
    -ngl 99 ^
    --port 8080 ^
    --host 127.0.0.1

endlocal
