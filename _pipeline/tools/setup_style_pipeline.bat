@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "PROJECT_ROOT=%%~fI"
set "COMFY_ROOT=%PROJECT_ROOT%\_comfyui"
set "COMFY_DIR=%COMFY_ROOT%\ComfyUI"
set "VENV_DIR=%COMFY_DIR%\.venv"
set "PYTHON_IN_VENV=%VENV_DIR%\Scripts\python.exe"
set "MANAGER_DIR=%COMFY_DIR%\custom_nodes\ComfyUI-Manager"

set "MODELS_DIR=%COMFY_DIR%\models"
set "CHECKPOINTS_DIR=%MODELS_DIR%\checkpoints"
set "CONTROLNET_DIR=%MODELS_DIR%\controlnet"
set "IPADAPTER_DIR=%MODELS_DIR%\ipadapter"
set "CLIPVISION_DIR=%MODELS_DIR%\clip_vision"

call :log "==============================================================="
call :log " SkinBuilder Style Pipeline Setup (Windows)"
call :log "==============================================================="
call :log "Project root : %PROJECT_ROOT%"
call :log "ComfyUI root : %COMFY_DIR%"

call :require_cmd git "Install Git from https://git-scm.com/download/win"
if errorlevel 1 goto :end_fail
call :require_cmd python "Install Python 3.11+ from https://www.python.org/downloads/windows/"
if errorlevel 1 goto :end_fail
call :require_cmd powershell "Enable Windows PowerShell 5.1"
if errorlevel 1 goto :end_fail

if not exist "%COMFY_ROOT%" mkdir "%COMFY_ROOT%"

if not exist "%COMFY_DIR%\main.py" (
    call :log "[1/8] Cloning ComfyUI..."
    git clone https://github.com/comfyanonymous/ComfyUI.git "%COMFY_DIR%"
    if errorlevel 1 (
        call :fail "Failed to clone ComfyUI. Check internet/firewall and Git credentials."
        goto :end_fail
    )
    call :ok "ComfyUI cloned."
) else (
    call :warn "[1/8] ComfyUI already exists; pulling latest."
    pushd "%COMFY_DIR%"
    git pull
    if errorlevel 1 call :warn "git pull failed; continuing with existing files."
    popd
)

if not exist "%VENV_DIR%\Scripts\python.exe" (
    call :log "[2/8] Creating Python virtual environment..."
    python -m venv "%VENV_DIR%"
    if errorlevel 1 (
        call :fail "Failed to create venv. Verify Python installation and permissions."
        goto :end_fail
    )
    call :ok "Virtual environment created."
) else (
    call :warn "[2/8] venv already exists; reusing it."
)

call :log "[3/8] Installing ComfyUI Python dependencies..."
"%PYTHON_IN_VENV%" -m pip install --upgrade pip
if errorlevel 1 (
    call :warn "pip upgrade failed; continuing."
)
"%PYTHON_IN_VENV%" -m pip install -r "%COMFY_DIR%\requirements.txt"
if errorlevel 1 (
    call :fail "Dependency install failed. Try: run as admin, or check proxy/SSL settings."
    goto :end_fail
)
call :ok "ComfyUI dependencies installed."

if not exist "%MANAGER_DIR%\__init__.py" (
    call :log "[4/8] Installing ComfyUI-Manager custom node..."
    git clone https://github.com/ltdrdata/ComfyUI-Manager.git "%MANAGER_DIR%"
    if errorlevel 1 (
        call :warn "Could not install ComfyUI-Manager now; you can install later manually."
    ) else (
        call :ok "ComfyUI-Manager installed."
    )
) else (
    call :warn "[4/8] ComfyUI-Manager already installed."
)

call :log "[5/8] Ensuring model folders exist..."
if not exist "%CHECKPOINTS_DIR%" mkdir "%CHECKPOINTS_DIR%"
if not exist "%CONTROLNET_DIR%" mkdir "%CONTROLNET_DIR%"
if not exist "%IPADAPTER_DIR%" mkdir "%IPADAPTER_DIR%"
if not exist "%CLIPVISION_DIR%" mkdir "%CLIPVISION_DIR%"
call :ok "Model folders are ready."

call :log "[6/8] Downloading helper workflow/template files..."
set "TEMPLATE_SRC=%PROJECT_ROOT%\_pipeline\tools\workflow_api_template.example.json"
set "TEMPLATE_DST=%PROJECT_ROOT%\_pipeline\tools\workflow_api_template.json"
if exist "%TEMPLATE_SRC%" (
    if not exist "%TEMPLATE_DST%" (
        copy /Y "%TEMPLATE_SRC%" "%TEMPLATE_DST%" >nul
        call :ok "Created workflow template: %TEMPLATE_DST%"
    ) else (
        call :warn "workflow_api_template.json already exists; kept current file."
    )
) else (
    call :warn "Template source not found. Re-check pipeline files."
)

call :log "[7/8] Optional model downloads (skipped by default)."
call :warn "No large models auto-downloaded to avoid unexpected bandwidth use."
call :warn "Place your model files manually into:"
call :warn "  %CHECKPOINTS_DIR%"
call :warn "  %CONTROLNET_DIR%"
call :warn "  %IPADAPTER_DIR%"
call :warn "  %CLIPVISION_DIR%"

call :log "[8/8] Running readiness checker..."
"%PYTHON_IN_VENV%" "%PROJECT_ROOT%\_pipeline\tools\check_style_pipeline.py" --project-root "%PROJECT_ROOT%"
if errorlevel 1 (
    call :warn "Checker found missing items. Read messages above to fix quickly."
) else (
    call :ok "Everything looks ready for style generation."
)

call :log "Done."
exit /b 0

:end_fail
call :fail "Setup failed. See messages above for exact fix steps."
exit /b 1

:require_cmd
where %~1 >nul 2>nul
if errorlevel 1 (
    call :fail "Missing command '%~1'. %~2"
    exit /b 1
)
call :ok "Found command: %~1"
exit /b 0

:log
echo [INFO] %~1
exit /b 0

:ok
echo [ OK ] %~1
exit /b 0

:warn
echo [WARN] %~1
exit /b 0

:fail
echo [FAIL] %~1
exit /b 0
