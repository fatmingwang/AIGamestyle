@echo off
chcp 65001 >nul
title Qwen3.6-35B-A3B 越狱版

set WORKING_DIRECTORY=D:\Work\AI\Qwen\llama-b9305-bin-win-cuda-13.1-x64

:check_dir
if not exist "%WORKING_DIRECTORY%" (
    echo.
    echo [ERROR] WORKING_DIRECTORY does not exist:
    echo   %WORKING_DIRECTORY%
    echo.
    set /p WORKING_DIRECTORY=Please enter the correct path:
    goto check_dir
)

cd /d "%~dp0"

:menu
cls
echo ==========================================
echo      Qwen3.6-35B-A3B
echo ==========================================
echo.
echo 1. Q4_K_P（4090/24GB）
echo 2. Q4_K_M（stable）
echo 3. IQ4_NL（high compress high quality/16GB）
echo 4. IQ2_M（6G/8G）
echo 5. CodeX（IQ2_M for CodeX, no mmproj）
echo.
echo ==========================================

set /p choice=choice 1,2,3,4,5：

if "%choice%"=="1" (
    call :check_model "%WORKING_DIRECTORY%\models\Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-Q4_K_P.gguf"
    if errorlevel 1 goto menu
    "%WORKING_DIRECTORY%\llama-server.exe" ^
    -m "%WORKING_DIRECTORY%\models\Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-Q4_K_P.gguf" ^
    --mmproj "%WORKING_DIRECTORY%\models\mmproj-Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-f16.gguf" ^
    -ngl 999 ^
    -c 131072 ^
    -n 8192 ^
    --host 127.0.0.1 ^
    --port 8666
)

if "%choice%"=="2" (
    call :check_model "%WORKING_DIRECTORY%\models\Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-Q4_K_M.gguf"
    if errorlevel 1 goto menu
    "%WORKING_DIRECTORY%\llama-server.exe" ^
    -m "%WORKING_DIRECTORY%\models\Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-Q4_K_M.gguf" ^
    --mmproj "%WORKING_DIRECTORY%\models\mmproj-Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-f16.gguf" ^
    -ngl 999 ^
    -c 131072 ^
    -n 8192 ^
    --host 127.0.0.1 ^
    --port 8666
)

if "%choice%"=="3" (
    call :check_model "%WORKING_DIRECTORY%\models\Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-IQ4_NL.gguf"
    if errorlevel 1 goto menu
    "%WORKING_DIRECTORY%\llama-server.exe" ^
    -m "%WORKING_DIRECTORY%\models\Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-IQ4_NL.gguf" ^
    --mmproj "%WORKING_DIRECTORY%\models\mmproj-Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-f16.gguf" ^
    -ngl 999 ^
    -c 131072 ^
    -n 8192 ^
    --host 127.0.0.1 ^
    --port 8666
)

if "%choice%"=="4" (
    call :check_model "%WORKING_DIRECTORY%\models\Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-IQ2_M.gguf"
    if errorlevel 1 goto menu
    "%WORKING_DIRECTORY%\llama-server.exe" ^
    -m "%WORKING_DIRECTORY%\models\Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-IQ2_M.gguf" ^
    --mmproj "%WORKING_DIRECTORY%\models\mmproj-Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-f16.gguf" ^
    -ngl 999 ^
    -c 8192 ^
    -n 4096 ^
    --host 127.0.0.1 ^
    --port 8666
)

if "%choice%"=="5" (
    call :check_model "%WORKING_DIRECTORY%\models\Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-IQ2_M.gguf"
    if errorlevel 1 goto menu
    "%WORKING_DIRECTORY%\llama-server.exe" ^
    -m "%WORKING_DIRECTORY%\models\Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-IQ2_M.gguf" ^
    -ngl 999 ^
    -c 16384 ^
    -n 2048 ^
    -fa on ^
    --jinja ^
    --host 127.0.0.1 ^
    --port 8666
)

pause
goto :eof

:check_model
if not exist %1 (
    echo.
    echo [ERROR] Model not found:
    echo   %~1
    echo Please download it first.
    echo.
    pause
    exit /b 1
)
exit /b 0
