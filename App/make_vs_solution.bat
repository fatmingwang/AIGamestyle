@echo off
rem make_vs_solution.bat -- generate a Visual Studio solution for GameStyleAI (BUILD.md, "Visual Studio solution").
rem
rem   make_vs_solution.bat            generate build_vs\GameStyleAI.sln(x)
rem   make_vs_solution.bat open       ... and open it in Visual Studio
rem
rem Finds the newest Visual Studio with the C++ tools (vswhere), uses the CMake that comes with it (it knows that
rem Visual Studio's generator; an older CMake on PATH may not), and finds Qt 6 under %USERPROFILE%\Qt. Override with:
rem   set QT_DIR=C:\Users\me\Qt\6.9.0\msvc2022_64        Qt's MSVC kit folder
rem   set VS_GENERATOR=Visual Studio 17 2022             a specific Visual Studio
rem No Developer Command Prompt is needed. Running it again refreshes the solution (e.g. after adding a source file
rem to CMakeLists.txt).
setlocal EnableExtensions EnableDelayedExpansion

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"
set "BUILD_DIR=%APP_DIR%\build_vs"

rem ---- Visual Studio ---------------------------------------------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [FAIL] vswhere.exe not found: install Visual Studio 2022 or newer with "Desktop development with C++".
    exit /b 1
)
set "VS_PATH="
set "VS_VER="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do set "VS_VER=%%i"
if not defined VS_PATH (
    echo [FAIL] No Visual Studio with the C++ tools found: add the "Desktop development with C++" workload.
    exit /b 1
)
for /f "tokens=1 delims=." %%m in ("%VS_VER%") do set "VS_MAJOR=%%m"
if not defined VS_GENERATOR (
    if "%VS_MAJOR%"=="18" set "VS_GENERATOR=Visual Studio 18 2026"
    if "%VS_MAJOR%"=="17" set "VS_GENERATOR=Visual Studio 17 2022"
)
if not defined VS_GENERATOR (
    echo [FAIL] Visual Studio %VS_VER% is not one this script knows; set VS_GENERATOR, e.g.
    echo        set VS_GENERATOR=Visual Studio 17 2022
    exit /b 1
)

rem ---- CMake: Visual Studio's own first, then the one on PATH --------------------------------------------------------
set "CMAKE=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
    where cmake >nul 2>nul || (
        echo [FAIL] CMake not found: install the "C++ CMake tools for Windows" component, or CMake 3.25+.
        exit /b 1
    )
    set "CMAKE=cmake"
)

rem ---- Qt 6 (MSVC 64-bit) ---------------------------------------------------------------------------------------------
if not defined QT_DIR (
    if exist "%USERPROFILE%\Qt\6.9.0\msvc2022_64\bin\qmake.exe" set "QT_DIR=%USERPROFILE%\Qt\6.9.0\msvc2022_64"
)
if not defined QT_DIR (
    for /d %%v in ("%USERPROFILE%\Qt\6.*") do for /d %%k in ("%%v\msvc20*_64") do if exist "%%k\bin\qmake.exe" set "QT_DIR=%%k"
)
if not defined QT_DIR (
    for /d %%v in ("C:\Qt\6.*") do for /d %%k in ("%%v\msvc20*_64") do if exist "%%k\bin\qmake.exe" set "QT_DIR=%%k"
)
if not defined QT_DIR (
    echo [FAIL] Qt 6 for MSVC 64-bit not found under %USERPROFILE%\Qt or C:\Qt. Install it ^(BUILD.md, step 1^):
    echo        pip install aqtinstall
    echo        aqt install-qt windows desktop 6.9.0 win64_msvc2022_64 --outputdir %%USERPROFILE%%\Qt
    echo        or set QT_DIR to its kit folder.
    exit /b 1
)

echo [INFO] Visual Studio : %VS_VER%  ^(%VS_GENERATOR%^)
echo [INFO] CMake         : %CMAKE%
echo [INFO] Qt            : %QT_DIR%
echo [INFO] Solution      : %BUILD_DIR%

rem A build folder made by another generator (or another Visual Studio) cannot be reused.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /c:"CMAKE_GENERATOR:INTERNAL=%VS_GENERATOR%" "%BUILD_DIR%\CMakeCache.txt" >nul || (
        echo [INFO] build_vs was made by another generator; starting it again.
        rmdir /s /q "%BUILD_DIR%"
    )
)

"%CMAKE%" -S "%APP_DIR%" -B "%BUILD_DIR%" -G "%VS_GENERATOR%" -A x64 -DCMAKE_PREFIX_PATH="%QT_DIR:\=/%"
if errorlevel 1 (
    echo [FAIL] CMake could not generate the solution; see the messages above.
    exit /b 1
)

set "SLN="
for %%s in ("%BUILD_DIR%\*.slnx" "%BUILD_DIR%\*.sln") do if not defined SLN set "SLN=%%~fs"
if not defined SLN (
    echo [FAIL] CMake finished but no solution file is in %BUILD_DIR%.
    exit /b 1
)
echo [ OK ] %SLN%
echo        Build in Visual Studio ^(set GameStyleAI as the startup project^), or here:
echo        "%CMAKE%" --build "%BUILD_DIR%" --config Release

if /i "%~1"=="open" start "" "%SLN%"
exit /b 0
