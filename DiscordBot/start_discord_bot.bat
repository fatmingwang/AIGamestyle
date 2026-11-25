@echo off
setlocal
rem Starts the ComfyLive Discord bot (README.md). Extra arguments go to the bot, e.g. --config=other.json
cd /d "%~dp0"

where node >nul 2>nul
if errorlevel 1 (
    echo [FAIL] Node.js is not installed or not on PATH. Install Node.js 22 LTS or newer: https://nodejs.org
    pause
    exit /b 1
)

if not exist "config.json" (
    echo [FAIL] config.json not found. Copy config.example.json to config.json and fill in the bot token and user IDs.
    pause
    exit /b 1
)

node src\main.mjs %*
echo.
echo [INFO] The bot stopped.
pause
