@echo off
rem build.cmd [debug|release] -- build ComfyLive (README.md). Output: build\<Config>\ComfyLive.exe, with its Qt DLLs.
rem Finds Visual Studio (vswhere), its CMake and Ninja, and Qt 6 under %USERPROFILE%\Qt. Override Qt with:
rem   set QT_DIR=C:\Users\me\Qt\6.9.0\msvc2022_64
setlocal EnableExtensions EnableDelayedExpansion
set "CONFIG=Release"
if /i "%~1"=="debug" set "CONFIG=Debug"
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo [FAIL] Visual Studio not found: install it with "Desktop development with C++". & goto :fail )
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH ( echo [FAIL] No Visual Studio with the C++ tools. & goto :fail )
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || goto :fail
set "PATH=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

if not defined QT_DIR (
    for /d %%v in ("%USERPROFILE%\Qt\6.*") do if exist "%%v\msvc2022_64\lib\cmake\Qt6" set "QT_DIR=%%v\msvc2022_64"
)
if not defined QT_DIR ( echo [FAIL] Qt 6 not found under %USERPROFILE%\Qt: see ..\App\BUILD.md step 1, or set QT_DIR. & goto :fail )
echo [ComfyLive] Qt: %QT_DIR%

cmake -S . -B "build\%CONFIG%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH="%QT_DIR%" || goto :fail
cmake --build "build\%CONFIG%" || goto :fail
echo [ComfyLive] done: %CD%\build\%CONFIG%\ComfyLive.exe
exit /b 0

:fail
echo [ComfyLive] FAILED
exit /b 1
