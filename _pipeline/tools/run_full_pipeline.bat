@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ============================================================
::  SkinBuilder — Full Pipeline Runner (Milestones 1-5)
::  Does NOT start ComfyUI. Start it separately before running.
:: ============================================================

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "PROJECT_ROOT=%%~fI"
set "PIPELINE_DIR=%PROJECT_ROOT%\_pipeline"
set "TOOLS_DIR=%PIPELINE_DIR%\tools"
set "COMFYUI_URL=http://127.0.0.1:8188"

cls
echo.
echo ================================================================
echo   SkinBuilder Full Pipeline Runner
echo   Milestones 1 (Scan) through 5 (ComfyUI Batch Execution)
echo ================================================================
echo   Project root : %PROJECT_ROOT%
echo   ComfyUI      : %COMFYUI_URL%  (must already be running)
echo ================================================================
echo.

:: ── Locate Python ─────────────────────────────────────────────────────────────

set "PYTHON="
where python >nul 2>nul
if not errorlevel 1 (
    set "PYTHON=python"
    goto :python_found
)
where python3 >nul 2>nul
if not errorlevel 1 (
    set "PYTHON=python3"
    goto :python_found
)
echo [FAIL] Python not found on PATH.
echo        Install Python 3.11+ from https://www.python.org/downloads/windows/
echo        and make sure it is added to your PATH.
pause
exit /b 1

:python_found
echo [ OK ] Python found: %PYTHON%
%PYTHON% --version
echo.

:: ── Prompt for parameters ─────────────────────────────────────────────────────

set "BATCH_ID=batch_demo"
set /p "BATCH_ID=Batch ID [%BATCH_ID%]: "
if "!BATCH_ID!"=="" set "BATCH_ID=batch_demo"

set "ASSET_ROOT=game"
set /p "ASSET_ROOT=Asset root folder [%ASSET_ROOT%]: "
if "!ASSET_ROOT!"=="" set "ASSET_ROOT=game"

set "STYLE_PROFILE=default_parameters"
set /p "STYLE_PROFILE=Style profile ID [%STYLE_PROFILE%]: "
if "!STYLE_PROFILE!"=="" set "STYLE_PROFILE=default_parameters"

echo.
echo   Batch ID     : !BATCH_ID!
echo   Asset root   : !ASSET_ROOT!
echo   Style profile: !STYLE_PROFILE!
echo.
pause

:: ── Check ComfyUI is reachable ─────────────────────────────────────────────────

echo.
echo ── Checking ComfyUI connection ──────────────────────────────────
powershell -NoProfile -Command ^
  "try { $r = Invoke-WebRequest -Uri '%COMFYUI_URL%/system_stats' -UseBasicParsing -TimeoutSec 5; Write-Host '[ OK ] ComfyUI is running.' } catch { Write-Host '[FAIL] Cannot reach ComfyUI at %COMFYUI_URL%'; exit 1 }"
if errorlevel 1 (
    echo.
    echo [FAIL] ComfyUI is not reachable at %COMFYUI_URL%
    echo        Start it first with:  _pipeline\tools\start_comfyui.bat
    echo        Then re-run this script.
    pause
    exit /b 1
)
echo.

:: ── Milestone 1: Scan ─────────────────────────────────────────────────────────

echo ── Milestone 1: Scan assets ─────────────────────────────────────
%PYTHON% "%TOOLS_DIR%\asset_pipeline.py" scan "!ASSET_ROOT!"
if errorlevel 1 (
    echo [FAIL] Scan failed. Check ASSET_ROOT path: !ASSET_ROOT!
    pause
    exit /b 1
)
echo [ OK ] Scan complete.
echo.

:: ── Milestone 2: Classify ─────────────────────────────────────────────────────

echo ── Milestone 2: Classify static vs spine ────────────────────────
%PYTHON% "%TOOLS_DIR%\asset_pipeline.py" classify
if errorlevel 1 (
    echo [FAIL] Classify failed.
    pause
    exit /b 1
)
echo [ OK ] Classify complete.
echo.

:: ── Milestone 3: Link ─────────────────────────────────────────────────────────

echo ── Milestone 3: Link source groups ──────────────────────────────
%PYTHON% "%TOOLS_DIR%\asset_pipeline.py" link
if errorlevel 1 (
    echo [FAIL] Link failed.
    pause
    exit /b 1
)
echo [ OK ] Link complete.
echo.

:: ── Milestone 4: Style Plan ───────────────────────────────────────────────────

echo ── Milestone 4: Generate style plan ─────────────────────────────
%PYTHON% "%TOOLS_DIR%\asset_pipeline.py" plan-style ^
    --style-profile-id "!STYLE_PROFILE!" ^
    --batch-id "!BATCH_ID!"
if errorlevel 1 (
    echo [FAIL] Style plan failed.
    pause
    exit /b 1
)
echo [ OK ] Style plan written to _pipeline\batches\!BATCH_ID!\style_plan.json
echo.

:: ── Milestone 5: ComfyUI Batch Execution ──────────────────────────────────────

echo ── Milestone 5: ComfyUI batch execution ─────────────────────────
echo    Outputs will be saved to: output\!BATCH_ID!\
echo.
%PYTHON% "%TOOLS_DIR%\run_comfyui_batch.py" ^
    --batch-id "!BATCH_ID!" ^
    --asset-root "!ASSET_ROOT!"
if errorlevel 1 (
    echo [FAIL] ComfyUI batch execution failed.
    pause
    exit /b 1
)

:: ── Done ──────────────────────────────────────────────────────────────────────

echo.
echo ================================================================
echo   Pipeline complete.
echo   Batch ID  : !BATCH_ID!
echo   Outputs   : %PROJECT_ROOT%\output\!BATCH_ID!\
echo ================================================================
echo.
pause
exit /b 0
