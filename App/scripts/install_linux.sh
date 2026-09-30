#!/usr/bin/env bash
# GameStyleAI — Linux dependency installer.
# Auto-detects whether you have root: uses apt if you do (Section 1a of
# BUILD_LINUX.md), otherwise installs everything into $HOME (Sections 1b/1c/1d).
set -euo pipefail

CMAKE_MIN=3.25
QT_VERSION=6.9.0
QT_ARCH=linux_gcc_64

log() { echo "==> $*"; }

version_ge() {
  # true if $1 >= $2 (dotted version compare)
  [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]
}

cmake_version() {
  command -v cmake >/dev/null 2>&1 && cmake --version | head -n1 | awk '{print $3}' || echo "0"
}

HAVE_ROOT=0
if [ "$(id -u)" = "0" ]; then
  HAVE_ROOT=1
  SUDO=""
elif command -v sudo >/dev/null 2>&1 && sudo -v >/dev/null 2>&1; then
  HAVE_ROOT=1
  SUDO=sudo
fi

if [ "$HAVE_ROOT" = "1" ]; then
  log "Root available — installing via apt"
  $SUDO apt-get update
  $SUDO apt-get install -y g++ cmake qt6-base-dev qt6-base-dev-tools \
      libqt6opengl6-dev libqt6network6 libgl1-mesa-dev \
      libglu1-mesa-dev libxcb-cursor0 libxkbcommon-x11-0 libxcb-icccm4 \
      libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-xkb1 \
      libxkbcommon0 python3

  CUR_CMAKE="$(cmake_version)"
  log "apt cmake version: $CUR_CMAKE"
  if ! version_ge "$CUR_CMAKE" "$CMAKE_MIN"; then
    log "apt cmake is older than $CMAKE_MIN — adding Kitware's official repo for a current one"
    $SUDO apt-get install -y ca-certificates gpg wget
    wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc \
        | gpg --dearmor | $SUDO tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
    echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main" \
        | $SUDO tee /etc/apt/sources.list.d/kitware.list >/dev/null
    $SUDO apt-get update
    $SUDO apt-get install -y cmake
    log "new cmake version: $(cmake --version | head -n1)"
  fi
else
  log "No root — installing everything into \$HOME"
  export PATH="$HOME/.local/bin:$PATH"

  if ! version_ge "$(cmake_version)" "$CMAKE_MIN"; then
    log "Installing a current CMake via pip --user"
    pip3 install --user --upgrade cmake
  fi
  log "cmake: $(cmake --version | head -n1)"

  if [ ! -d "$HOME/opt/qt6" ]; then
    log "Installing aqtinstall and Qt $QT_VERSION ($QT_ARCH) into ~/opt/qt6"
    pip3 install --user --upgrade aqtinstall
    aqt install-qt linux desktop "$QT_VERSION" "$QT_ARCH" -O "$HOME/opt/qt6"
  else
    log "Qt6 already present at ~/opt/qt6, skipping"
  fi

  if [ ! -d "$HOME/opt/sysroot" ]; then
    log "Fetching OpenGL/xcb .deb files into ~/opt/sysroot (download only, no root needed)"
    mkdir -p "$HOME/opt/sysroot"
    TMP_DIR="$(mktemp -d)"
    (cd "$TMP_DIR" && apt-get download libgl1-mesa-dev libglu1-mesa-dev libglvnd-dev \
        mesa-common-dev libglvnd-core-dev libxcb-cursor0 libxkbcommon-x11-0 \
        libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 \
        libxcb-xkb1 libxkbcommon0)
    for d in "$TMP_DIR"/*.deb; do dpkg-deb -x "$d" "$HOME/opt/sysroot"; done
    rm -rf "$TMP_DIR"

    mkdir -p "$HOME/opt/sysroot/usr/include/GL"
    if [ ! -f "$HOME/opt/sysroot/usr/include/GL/gl.h" ]; then
      curl -fsSL https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/master/api/GL/glcorearb.h \
          -o "$HOME/opt/sysroot/usr/include/GL/glcorearb.h"
      printf '#ifndef __gl_h_\n#define __gl_h_\n#include <GL/glcorearb.h>\n#endif\n' \
          > "$HOME/opt/sysroot/usr/include/GL/gl.h"
    fi
  else
    log "Sysroot already present at ~/opt/sysroot, skipping"
  fi
fi

log "Install complete. Next: scripts/build_linux.sh"
