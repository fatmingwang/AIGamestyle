# GameStyleAI — Linux Install & Build Guide

This document explains how to build and run **GameStyleAI** (a Qt6 Widgets C++ desktop
app) on Linux. It was originally written for Windows/MSVC; the changes needed for Linux
are small and are already applied in this repository.

> **No root / sudo required.** Everything below installs into your home directory.
> If you *do* have root, the OS-package variants are noted as shortcuts.

---

## 0. One-click setup (recommended)

```bash
cd GamestyleAI/App
bash setup_linux.sh
```

This runs [`scripts/install_linux.sh`](scripts/install_linux.sh) then
[`scripts/build_linux.sh`](scripts/build_linux.sh):

- **`install_linux.sh`** auto-detects whether you have root. With root it apt-installs
  everything (Section 1a) and upgrades CMake via Kitware's repo if apt's is too old.
  Without root it installs a current CMake and Qt6 into `$HOME` (Sections 1b–1d),
  skipping any step whose target directory already exists (safe to re-run).
- **`build_linux.sh`** auto-detects wherever Qt6/OpenGL ended up (home-dir install or
  system apt package) and configures + builds — no paths to hand-edit.

Result: **`build-linux/GameStyleAI`**. Run it with `./build-linux/GameStyleAI`.

Read on for what each script does and how to do it manually, or if `setup_linux.sh`
fails partway and you need to diagnose which step broke.

---

## 1. What is required

| Requirement        | Version                 | Purpose                                         | How to get (no root)                                                         |
| ------------------ | ----------------------- | ----------------------------------------------- | ---------------------------------------------------------------------------- |
| C++20 compiler     | g++ ≥ 11 (tested 13.3) | Compile the app                                 | Already on Ubuntu 24.04 (`g++ --version`)                                  |
| CMake              | ≥ 3.25 (tested 3.30)   | Configure/build                                 | Use your own`~/opt/cmake` or `pip install cmake`                         |
| Qt 6.9.0`gcc_64` | 6.9.0                   | Widgets + Network libs                          | `aqtinstall` → `~/opt/qt6`                                              |
| OpenGL headers     | any (dev)               | Qt6Gui needs`GL/gl.h` at **build** time | `apt-get download` mesa/glvnd `.deb`s, or OS package `libgl1-mesa-dev` |
| xcb runtime libs   | —                      | Qt xcb platform plugin at**run** time     | `apt-get download` the `.deb`s, or OS package `libxcb-cursor0` etc.    |
| Python 3           | ≥ 3.11                 | The pipeline subprocesses                       | `python3` on PATH (used as `pythonExe` default on Linux)                 |

### 1a. If you have root (Ubuntu/Debian) — the fast path

```bash
sudo apt-get update
sudo apt-get install -y g++ cmake qt6-base-dev qt6-base-dev-tools \
    libqt6opengl6-dev libqt6network6 libgl1-mesa-dev \
    libglu1-mesa-dev libxcb-cursor0 libxkbcommon-x11-0 libxcb-icccm4 \
    libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-xkb1 \
    libxkbcommon0 python3
```

Check what you got: `cmake --version` and `qmake6 --version` (or
`dpkg -s qt6-base-dev | grep Version`). Ubuntu 24.04's repos ship CMake 3.28 and
Qt 6.4 — both satisfy this project's minimums (CMake ≥ 3.25, Qt 6.x with Widgets +
Network). **Ubuntu 22.04's repo CMake is 3.22 — too old.** If `cmake --version`
reports less than 3.25, don't fight apt: use one of the two fixes below instead of
trying to force an apt upgrade.

**Fix 1 — Kitware's official APT repo (root, recommended, gets you the newest CMake
and keeps it updated via `apt upgrade`):**

```bash
sudo apt-get install -y ca-certificates gpg wget
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc \
    | gpg --dearmor | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] \
https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main" \
    | sudo tee /etc/apt/sources.list.d/kitware.list
sudo apt-get update
sudo apt-get install -y cmake
cmake --version   # now the latest upstream release, not the distro's frozen one
```

**Fix 2 — no separate repo, just replace the apt package (works on any Debian/Ubuntu,
still needs root once):**

```bash
sudo apt-get remove -y cmake   # drop the old one to avoid PATH conflicts
sudo pip3 install --break-system-packages cmake   # or: pipx install cmake --force
hash -r
cmake --version
```

Then skip to **Section 3** (set `Qt6_DIR` to the system Qt, e.g.
`/usr/lib/x86_64-linux-gnu/cmake/Qt6`).

> Qt6 module packages differ by distro/version. If `qt6-base-dev` alone leaves CMake
> unable to find `Qt6Network` or `Qt6Widgets`, also install `qt6-tools-dev`,
> `qt6-tools-dev-tools`, and `libqt6widgets6`. On Ubuntu 24.04 the package names above
> are sufficient; on older releases you may need `qt6-base-dev-tools` split out or a
> PPA (e.g. `ppa:okirby/qt6-backports` on 22.04) since Qt 6.9 isn't packaged there at
> all — in that case use the no-root `aqtinstall` path in **1b** instead, even with
> root.

### 1b. Without root — install an up-to-date CMake into your home

Never use a distro CMake older than 3.25 as a workaround (Qt6's CMake config files
rely on features from newer CMake policies and will fail to configure). Pick one:

```bash
# Option 1 (simplest): pip/pipx, no compilation, works on any glibc-recent Linux
uv tool install cmake                 # or: pip install --user cmake
export PATH="$HOME/.local/bin:$PATH"
cmake --version                       # installs the latest release from PyPI

# Option 2: official prebuilt binary tarball from cmake.org (no Python needed)
CMAKE_VER=3.30.5
curl -fsSL "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VER}/cmake-${CMAKE_VER}-linux-x86_64.tar.gz" \
    | tar -xz -C ~/opt
mv ~/opt/cmake-${CMAKE_VER}-linux-x86_64 ~/opt/cmake
export PATH="$HOME/opt/cmake/bin:$PATH"
cmake --version
```

> Check https://cmake.org/download/ (or the GitHub Releases page linked above) for the
> current version number before running Option 2 — `3.30.5` above is just an example,
> substitute whatever is latest. Add the `export PATH=...` line to your `~/.bashrc` so
> it persists across shells.

### 1c. Without root — install Qt6 into your home

```bash
# aqtinstall fetches Qt directly from Qt's mirrors (no account, no root)
uv tool install aqtinstall          # or: pip install --user aqtinstall
export PATH="$HOME/.local/bin:$PATH"

aqt install-qt linux desktop 6.9.0 linux_gcc_64 -O ~/opt/qt6
# -> installs to ~/opt/qt6/6.9.0/gcc_64
```

> The architecture is `linux_gcc_64` (NOT `gcc_64`). Do **not** pass `-m qtbase qtdeclarative qtnetwork` — those module names are invalid for Linux; `qtbase` is the
> default and already includes Widgets + Network.
>
> To see what versions/modules are available before installing:
> `aqt list-qt linux desktop` (versions) and
> `aqt list-qt linux desktop --modules 6.9.0 linux_gcc_64` (modules for a version).

### 1d. Without root — provide OpenGL headers + xcb libs

The build only needs `GL/gl.h`; the run needs the xcb support libraries. If you can't
`apt-get install`, fetch the `.deb`s and extract them into a local sysroot:

```bash
mkdir -p ~/opt/sysroot && cd /tmp
apt-get download libgl1-mesa-dev libglu1-mesa-dev libglvnd-dev mesa-common-dev \
    libglvnd-core-dev libxcb-cursor0 libxkbcommon-x11-0 libxcb-icccm4 \
    libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-xkb1 libxkbcommon0
for d in *.deb; do dpkg-deb -x "$d" ~/opt/sysroot; done
```

The crucial header is `GL/gl.h`. On Ubuntu 24.04 it is no longer shipped by those debs,
so download the canonical one from Khronos:

```bash
mkdir -p ~/opt/sysroot/usr/include/GL
curl -fsSL https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/master/api/GL/glcorearb.h \
     -o ~/opt/sysroot/usr/include/GL/glcorearb.h
printf '#ifndef __gl_h_\n#define __gl_h_\n#include <GL/glcorearb.h>\n#endif\n' \
     > ~/opt/sysroot/usr/include/GL/gl.h
```

---

## 2. Source changes already applied (Linux port)

These are already in the repo — listed so you know what differs from the Windows build:

- **`src/MainWindow.cpp`**
  - `#include <windows.h>` is guarded by `#ifdef Q_OS_WIN`.
  - The "Launch ComfyUI" block (runs `cmd.exe` + `start_comfyui_with_flags.bat` via
    `QProcess::CreateProcessArguments`) is wrapped in `#ifdef Q_OS_WIN`. On Linux it
    logs that local ComfyUI launch is Windows-only and expects ComfyUI reachable at the
    configured URL.
- **`src/AppConfig.h`**
  - Platform-aware defaults: on Linux `pythonExe` = `python3` and paths default to
    `~/GamestyleAI/...`; on Windows the original `D:/Work/...` + `python` are kept.
- **No registry use.** `QSettings` falls back to an INI file under
  `~/.config/MyWork/GameStyleAI.conf` on Linux — fully cross-platform.

The `.bat`/`.cmd` string references in `Text2ImageWidget.cpp` (LLaMA launcher) are just
literals; they degrade gracefully on Linux (the file won't be found, logged as a
failure) and do **not** affect compilation.

---

## 3. Build

```bash
cd GamestyleAI/App
bash scripts/build_linux.sh
```

This is the same script `setup_linux.sh` calls (Section 0). It works no matter which
install path you took in Section 1 — it auto-detects Qt6 (home-dir install from **1c**,
or system apt package from **1a**) and only adds `OPENGL_*` overrides if you actually
built a local sysroot (**1d**). Read [`scripts/build_linux.sh`](scripts/build_linux.sh)
if you need to see or tweak exactly what it configures.

Result: **`build-linux/GameStyleAI`**.

> If CMake still can't find OpenGL and you're on a system install (1a), you're likely
> missing headers: `sudo apt-get install -y libgl1-mesa-dev libglu1-mesa-dev`.

---

## 4. Run

### Option A — system Qt installed (Section 1a)

```bash
./build-linux/GameStyleAI
```

### Option B — self-contained bundle (no system Qt needed)

A ready-made bundle is produced at **`App/dist/`**: it contains the executable plus all
Qt6 libs, the `xcb` platform plugin, and the xcb support libraries. Use the launcher:

```bash
cd App/dist
./run.sh
```

`run.sh` simply sets `LD_LIBRARY_PATH` to the bundled libraries and execs the binary.
You can copy `dist/` to any Linux machine (with a display) and run it there.

### First-run notes

- Set **Project Root**, **Output Folder**, **Asset Root**, and **Python Exe** in the UI
  to match your environment (the Linux defaults are `~/GamestyleAI/...` placeholders).
- The **ComfyUI Launch** button is a no-op on Linux. Start ComfyUI yourself and put its
  URL in the **ComfyUI URL** field (default `127.0.0.1:8188`).
- The log will warn `cannot open reskin_categories.json` until you point the app at real
  pipeline data — that is expected, not a crash.

---

## 5. Troubleshooting

| Symptom                                              | Cause                                                | Fix                                                                                  |
| ---------------------------------------------------- | ---------------------------------------------------- | ------------------------------------------------------------------------------------ |
| `Could NOT find WrapOpenGL` / `Qt6Gui not found` | Missing`GL/gl.h` at build time                     | Provide`OPENGL_INCLUDE_DIR` (Section 1d)                                           |
| `Could not load the Qt platform plugin "xcb"`      | Missing`libxcb-cursor0` / xcb libs at runtime      | Install them (Section 1a/1d) or use the`dist/` bundle                              |
| `libQt6XcbQpa.so.6: cannot open shared object`     | xcb plugin's Qt dependency not on`LD_LIBRARY_PATH` | Use`dist/run.sh`, or add Qt `lib/` to `LD_LIBRARY_PATH`                        |
| `python not found` when running a pipeline         | Linux uses`python3`                                | Set**Python Exe** in the UI to your interpreter (e.g. a venv with torch/comfy) |
| `cmd.exe` / `.bat` errors in the log             | Windows-only launcher invoked                        | Harmless on Linux; use the manual/remote ComfyUI + LLaMA instead                     |

---

## 6. File layout after building

```
GamestyleAI/
├── App/
│   ├── CMakeLists.txt
│   ├── setup_linux.sh             # one-click install + build (Section 0)
│   ├── scripts/
│   │   ├── install_linux.sh       # Section 1 install, root or no-root auto-detected
│   │   └── build_linux.sh         # Section 3 build, Qt6/OpenGL auto-detected
│   ├── src/                      # patched for Linux (see Section 2)
│   ├── build-linux/GameStyleAI  # the executable (Option A)
│   └── dist/                     # self-contained bundle (Option B)
│       ├── GameStyleAI
│       ├── run.sh
│       ├── platforms/libqxcb.so
│       └── libQt6*.so.6, libxcb-*, libxkbcommon*
└── README / BUILD.md            # original Windows guide
```
