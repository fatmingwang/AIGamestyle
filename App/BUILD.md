# GameStyleAI â€” Build Guide

## Overview

This is a Qt6 Widgets C++ application built with CMake and MSVC (Visual Studio 2022).
It wraps the Python batch-reskinning pipeline in a native Windows GUI.

---

## Prerequisites

| Tool               | Version | Notes                                                         |
| ------------------ | ------- | ------------------------------------------------------------- |
| Visual Studio 2022 | 17.x    | "Desktop development with C++" workload required              |
| CMake              | 3.25+   | Already installed at `C:\Program Files\CMake\bin\cmake.exe` |
| Qt6                | 6.9.0   | Installed via `aqtinstall` â€” see Step 1                    |
| Python             | 3.11+   | Already installed                                             |

---

## Step 1 â€” Install Qt6

Qt6 is installed using **`aqtinstall`**, a Python tool that downloads Qt directly from
Qt's official mirrors. **No Qt account or browser required.**

### 1a. Install aqtinstall

Open any Command Prompt or PowerShell (no admin needed):

```bat
pip install aqtinstall
```

You may see a warning like:

```
urllib3 (1.26.x) doesn't match a supported version!
```

This is harmless â€” `aqt` works fine regardless.

### 1b. Check the available architecture name for your Qt version

The architecture identifier used in the install command **changes between Qt versions**.
Always check before installing:

```bat
aqt list-qt windows desktop --arch 6.9.0
```

Example output for Qt 6.9.0:

```
win64_llvm_mingw  win64_mingw  win64_msvc2022_64  win64_msvc2022_arm64_cross_compiled
```

Pick `win64_msvc2022_64` for a standard 64-bit Visual Studio build.

> **Note:** Older Qt versions (â‰¤ 6.7) used `win64_msvc2019_64`. Qt 6.8+ renamed it to
> `win64_msvc2022_64`. Always run the `--arch` check above if installing a different version.

### 1c. Install Qt 6.9.0 MSVC 64-bit

Install to your user folder â€” **no administrator rights required**:

```bat
aqt install-qt windows desktop 6.9.0 win64_msvc2022_64 --outputdir %USERPROFILE%\Qt
```

> - Downloads ~500 MB. Takes 3â€“5 minutes depending on connection speed.
> - Installs to `C:\Users\<you>\Qt\6.9.0\msvc2022_64\`

> **Why not `C:\Qt`?** Writing directly to `C:\` requires administrator rights.
> `%USERPROFILE%\Qt` is always writable with no downsides.

### 1d. Verify the install

```bat
dir %USERPROFILE%\Qt\6.9.0\msvc2022_64\bin\qmake.exe
dir %USERPROFILE%\Qt\6.9.0\msvc2022_64\bin\windeployqt.exe
```

Both files must exist before proceeding.

> **Alternative (official installer with account):** If you prefer a GUI installer,
> download from https://www.qt.io/download-qt-installer â€” a free account is required but no payment.

---

## Step 2 â€” Open a Visual Studio Developer Command Prompt

Qt's MSVC build requires the MSVC compiler on PATH.

1. Press **Win**, search **"Developer Command Prompt for VS 2022"**, open it.
2. Or open **"x64 Native Tools Command Prompt for VS 2022"** (preferred for 64-bit).

> All `cmake` commands in this guide must be run in that prompt.

---

## Step 3 â€” Configure the Project

From the `App/` directory inside the developer prompt:

```bat
cd /d <path-to-your-clone>\App

cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%USERPROFILE%/Qt/6.9.0/msvc2022_64"
```

> Replace `<path-to-your-clone>` with wherever you cloned this repository (e.g.
> `d:\Work\GamestyleAI`). All paths in this guide are relative to that folder.

> If you installed a different Qt version, replace `6.9.0/msvc2022_64` with the folder name
> from your `%USERPROFILE%\Qt\` directory.

**Expected output (last lines):**

```
-- Found Qt6 version 6.9.0
-- Configuring done
-- Generating done
-- Build files have been written to: .../App/build
```

If CMake cannot find Qt6, see [Troubleshooting](#troubleshooting).

---

## Step 4 â€” Build

```bat
cmake --build build --config Release
```

First build takes 2â€“4 minutes (compiles Qt MOC + all sources).
Subsequent builds are incremental and fast.

**Output:**

```
App\build\Release\GameStyleAI.exe
```

> The `CMakeLists.txt` runs `windeployqt` automatically after each build,
> which copies the required Qt DLLs next to the `.exe`.

---

## Step 5 â€” Run

```bat
build\Release\GameStyleAI.exe
```

Or double-click `GameStyleAI.exe` in Explorer.

The app reads/writes settings via Windows Registry under:

```
HKCU\Software\MyWork\GameStyleAI
```

---

## Using the App

### First Launch

1. **Source Folder** â€” folder containing images to reskin (used by Simple Batch mode)
2. **Output Folder** â€” where reskinned images are saved
3. **Asset Root** â€” the game asset directory scanned by the Full Pipeline (e.g. `game`)
4. **ComfyUI URL** â€” defaults to `localhost:8188`; change if your server differs
5. **Style Profile** â€” name of a profile in `_pipeline/style_profiles/` (e.g. `default_parameters`)
6. **Batch ID** â€” identifier for this batch run (e.g. `batch_demo`)

### Running the Pipeline

| Button             | Action                                                                                 |
| ------------------ | -------------------------------------------------------------------------------------- |
| **Run All**  | Runs all 5 steps in sequence (Scan â†’ Classify â†’ Link â†’ Plan Style â†’ ComfyUI Batch) |
| **Run Step** | Runs only the selected step in the left panel                                          |
| **Stop**     | Kills the running Python process                                                       |

### Pipeline Modes

| Mode          | What runs                                                               |
| ------------- | ----------------------------------------------------------------------- |
| Full Pipeline | `asset_pipeline.py` steps 1â€“4, then `run_comfyui_batch.py`         |
| Simple Batch  | `TestData/Reskin/batch_reskin.py` directly (quick, no pipeline infra) |

### Log Colors

| Color  | Meaning                     |
| ------ | --------------------------- |
| White  | Normal output               |
| Yellow | Step starting               |
| Green  | Step completed successfully |
| Orange | Stderr / warnings           |
| Red    | Errors                      |
| Cyan   | Per-asset progress updates  |

---

## Distributing / Sharing the Executable

Run `windeployqt` manually if needed to ensure all Qt DLLs are bundled:

```bat
"%USERPROFILE%\Qt\6.9.0\msvc2022_64\bin\windeployqt.exe" build\Release\GameStyleAI.exe
```

Then zip and share the entire `build\Release\` folder.
The recipient does **not** need Qt installed â€” all DLLs are included.

---

## Troubleshooting

### CMake error: `Could not find Qt6`

Set the prefix path explicitly:

```bat
cmake -B build -S . -DCMAKE_PREFIX_PATH="%USERPROFILE%\Qt\6.9.0\msvc2022_64"
```

Or set the environment variable permanently:

```bat
setx Qt6_DIR "%USERPROFILE%\Qt\6.9.0\msvc2022_64\lib\cmake\Qt6"
```

### Build error: `cl is not recognized`

You are not in a VS Developer Command Prompt. Close the terminal and reopen
**"x64 Native Tools Command Prompt for VS 2022"**.

### `windeployqt` error during build: `Qt6_DIR` path issue

Edit [CMakeLists.txt](CMakeLists.txt) and hard-code the `windeployqt` path explicitly:

```cmake
set(WINDEPLOYQT "$ENV{USERPROFILE}/Qt/6.9.0/msvc2022_64/bin/windeployqt.exe")
add_custom_command(TARGET GameStyleAI POST_BUILD
    COMMAND "${WINDEPLOYQT}" "$<TARGET_FILE:GameStyleAI>")
```

### App launches but Python subprocess fails immediately

- Confirm Python is on PATH: open a normal Command Prompt and run `python --version`
- If Python is from the Windows Store (`WindowsApps`), set the **Python Exe** field in the app
  to the full path, e.g.:
  `C:\Users\<you>\AppData\Local\Programs\Python\Python311\python.exe`
- The **Project Root** field defaults to `.` and is resolved relative to the app's own
  executable directory at startup (see `AppConfig::resolveRelativePaths()`), so it should not
  need updating if the repo is moved — but confirm it still points at your repo's root folder
  (the one containing `_pipeline/`, `TestData/`, `_comfyui/`) in the Pipeline tab.

### ComfyUI steps time out or hang

- Verify ComfyUI is running and reachable at the URL shown in the app
- Test: open a browser and visit `http://localhost:8188`
- Change the URL in the app's **ComfyUI URL** field to match your server

---

## Project File Reference

```
App/
â”œâ”€â”€ BUILD.md                  â† this file
â”œâ”€â”€ CMakeLists.txt            â† build configuration
â””â”€â”€ src/
    â”œâ”€â”€ main.cpp              â† QApplication entry point
    â”œâ”€â”€ MainWindow.h/.cpp     â† main window, UI layout, state machine, QProcess
    â”œâ”€â”€ AppConfig.h/.cpp      â† settings struct + QSettings persistence
    â”œâ”€â”€ PipelineCommands.h/.cpp  â† builds Python subprocess args per step
    â”œâ”€â”€ LogParser.h/.cpp      â† parses [GS:...] marker lines from Python stdout
    â””â”€â”€ StepListWidget.h/.cpp â† pipeline step list with colored status icons
```
