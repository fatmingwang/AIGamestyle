@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "PROJECT_ROOT=%%~fI"
set "COMFY_ROOT=%PROJECT_ROOT%\_comfyui"
set "COMFY_DIR=%COMFY_ROOT%\ComfyUI"
set "VENV_DIR=%COMFY_DIR%\.venv"
set "PYTHON_IN_VENV=%VENV_DIR%\Scripts\python.exe"

cls
echo.
echo ===============================================================
echo  Starting ComfyUI Server
echo ===============================================================
echo  Project root : %PROJECT_ROOT%
echo  ComfyUI root : %COMFY_DIR%
echo.

if not exist "%COMFY_DIR%\main.py" (
    echo [FAIL] ComfyUI not found at %COMFY_DIR%
    echo Run setup_style_pipeline.bat first to install ComfyUI.
    pause
    exit /b 1
)

if not exist "%PYTHON_IN_VENV%" (
    echo [FAIL] Python venv not found at %VENV_DIR%
    echo Run setup_style_pipeline.bat first to create venv.
    pause
    exit /b 1
)

set "HAS_NVIDIA_GPU=0"
where nvidia-smi >nul 2>nul
if not errorlevel 1 set "HAS_NVIDIA_GPU=1"

if "%PYTORCH_CUDA_INDEX_URL%"=="" set "PYTORCH_CUDA_INDEX_URL=https://download.pytorch.org/whl/cu124"
if "%PYTORCH_CPU_INDEX_URL%"=="" set "PYTORCH_CPU_INDEX_URL=https://download.pytorch.org/whl/cpu"
if "%PYTORCH_INDEX_URL%"=="" (
    if "!HAS_NVIDIA_GPU!"=="1" (
        set "PYTORCH_INDEX_URL=!PYTORCH_CUDA_INDEX_URL!"
    ) else (
        set "PYTORCH_INDEX_URL=!PYTORCH_CPU_INDEX_URL!"
    )
)
if "%AUTO_FIX_TORCH_CUDA%"=="" set "AUTO_FIX_TORCH_CUDA=1"

echo [INFO] Checking PyTorch installation...
"%PYTHON_IN_VENV%" -c "import torch; print(torch.__version__)" >nul 2>nul
if errorlevel 1 (
    echo [WARN] PyTorch is not installed in this venv.
    echo [INFO] Installing PyTorch automatically...
    echo [INFO] Using index: !PYTORCH_INDEX_URL!

    "%PYTHON_IN_VENV%" -m pip install --upgrade torch torchvision torchaudio --index-url "!PYTORCH_INDEX_URL!"
    if errorlevel 1 (
        echo [WARN] Install from selected index failed, trying default PyPI...
        "%PYTHON_IN_VENV%" -m pip install --upgrade torch torchvision torchaudio
        if errorlevel 1 (
            echo [FAIL] Could not install PyTorch automatically.
            echo [FIX ] Run one of these commands manually:
            echo        "%PYTHON_IN_VENV%" -m pip install torch torchvision torchaudio --index-url !PYTORCH_CUDA_INDEX_URL!
            echo        "%PYTHON_IN_VENV%" -m pip install torch torchvision torchaudio --index-url !PYTORCH_CPU_INDEX_URL!
            pause
            exit /b 1
        )
    )

    "%PYTHON_IN_VENV%" -c "import torch; print(torch.__version__)" >nul 2>nul
    if errorlevel 1 (
        echo [FAIL] PyTorch install command finished, but import still fails.
        pause
        exit /b 1
    )
    echo [ OK ] PyTorch installed successfully.
) else (
    echo [ OK ] PyTorch is already installed.
)

set "COMFY_DEVICE_ARG="
"%PYTHON_IN_VENV%" -c "import torch, sys; sys.exit(0 if torch.cuda.is_available() else 1)" >nul 2>nul
if errorlevel 1 (
    echo [WARN] CUDA is not available in current PyTorch environment.

    if "!HAS_NVIDIA_GPU!"=="1" (
        if "!AUTO_FIX_TORCH_CUDA!"=="1" (
            echo [INFO] NVIDIA GPU detected. Reinstalling CUDA-enabled PyTorch...
            echo [INFO] Running equivalent of PowerShell steps in this venv.
            "%PYTHON_IN_VENV%" -m pip uninstall -y torch torchvision torchaudio >nul 2>nul
            "%PYTHON_IN_VENV%" -m pip install --upgrade torch torchvision torchaudio --index-url "!PYTORCH_CUDA_INDEX_URL!"

            if errorlevel 1 (
                echo [WARN] CUDA wheel install failed. Falling back to CPU mode.
                set "COMFY_DEVICE_ARG=--cpu"
            ) else (
                "%PYTHON_IN_VENV%" -c "import torch, sys; sys.exit(0 if torch.cuda.is_available() else 1)" >nul 2>nul
                if errorlevel 1 (
                    echo [WARN] PyTorch reinstall finished, but CUDA is still unavailable.
                    set "COMFY_DEVICE_ARG=--cpu"
                ) else (
                    echo [ OK ] CUDA-enabled PyTorch is active.
                )
            )
        ) else (
            echo [WARN] AUTO_FIX_TORCH_CUDA is disabled. Keeping CPU mode.
            set "COMFY_DEVICE_ARG=--cpu"
        )
    ) else (
        echo [INFO] No NVIDIA GPU detected. Starting in CPU mode.
        set "COMFY_DEVICE_ARG=--cpu"
    )
) else (
    echo [ OK ] CUDA is available. Starting with GPU mode.
)

if "%COMFY_DEVICE_ARG%"=="--cpu" (
    echo [INFO] Starting ComfyUI in CPU mode ^(--cpu^).
)

echo [INFO] Activating venv and starting ComfyUI...
echo [INFO] ComfyUI will be accessible at: http://127.0.0.1:8188
echo [INFO] Press Ctrl+C to stop.
echo.

cd /d "%COMFY_DIR%"
"%PYTHON_IN_VENV%" main.py %COMFY_DEVICE_ARG% --listen 127.0.0.1 --port 8188

echo.
echo [INFO] ComfyUI stopped.
pause
exit /b 0
