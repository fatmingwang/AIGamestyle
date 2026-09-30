@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Usage:
REM   create_venv.bat
REM   create_venv.bat "D:\path\to\repo"
REM   create_venv.bat "D:\path\to\repo" "python"

set "TARGET_DIR=%~1"
set "PY_CMD=%~2"

if "%TARGET_DIR%"=="" (
    for %%I in ("%~dp0..\..") do set "TARGET_DIR=%%~fI"
)

if "%PY_CMD%"=="" set "PY_CMD=python"

echo [INFO] Target directory: %TARGET_DIR%
echo [INFO] Python command  : %PY_CMD%

if not exist "%TARGET_DIR%" (
    echo [FAIL] Target directory does not exist: %TARGET_DIR%
    echo [FIX ] Pass a valid path, for example:
    echo        create_venv.bat "D:\Work\YourRepo"
    exit /b 1
)

where %PY_CMD% >nul 2>nul
if errorlevel 1 (
    echo [FAIL] Python command not found: %PY_CMD%
    echo [FIX ] Install Python 3.11+ or pass full path to python.exe
    exit /b 1
)

pushd "%TARGET_DIR%"

if exist ".venv\Scripts\python.exe" (
    echo [WARN] .venv already exists. Skipping creation.
) else (
    echo [INFO] Creating virtual environment...
    %PY_CMD% -m venv .venv
    if errorlevel 1 (
        echo [FAIL] Failed to create .venv
        echo [FIX ] Check Python installation and folder permissions.
        popd
        exit /b 1
    )
    echo [ OK ] .venv created.
)

if not exist ".venv\Scripts\Activate.ps1" (
    echo [FAIL] Activation script missing: .venv\Scripts\Activate.ps1
    popd
    exit /b 1
)

echo [INFO] Upgrading pip inside .venv...
call ".venv\Scripts\Activate.bat"
python -m pip install --upgrade pip >nul 2>nul
if errorlevel 1 (
    echo [WARN] Could not upgrade pip now. You can do it later.
) else (
    echo [ OK ] pip upgraded.
)

echo.
echo [DONE] Virtual environment is ready.
echo [NEXT] In PowerShell, run:
echo        cd "%TARGET_DIR%"
echo        .\.venv\Scripts\Activate.ps1
echo        python -c "import sys; print(sys.executable)"
echo.

popd
exit /b 0
