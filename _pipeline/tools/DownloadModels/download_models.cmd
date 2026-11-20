@echo off
rem Double-click: downloads the AI models listed in download_models.ps1 into this folder.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0download_models.ps1" %*
echo.
pause
