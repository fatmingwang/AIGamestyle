#!/usr/bin/env bash
# GameStyleAI — Linux build script.
# Auto-detects Qt6 (home-dir install or system apt package) and OpenGL
# (local sysroot if present) so the same command works for any Section 1
# install path. See ../BUILD_LINUX.md.
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$APP_DIR"

export PATH="$HOME/.local/bin:$PATH"

log() { echo "==> $*"; }

# --- Locate Qt6 ---
if [ -d "$HOME/opt/qt6" ]; then
  QT6_CMAKE_DIR="$(find "$HOME/opt/qt6" -name Qt6Config.cmake 2>/dev/null | head -n1 | xargs -r dirname)"
else
  QT6_CMAKE_DIR="$(dpkg -L qt6-base-dev 2>/dev/null | grep Qt6Config.cmake | xargs -r dirname)"
fi
if [ -z "$QT6_CMAKE_DIR" ]; then
  echo "ERROR: Qt6Config.cmake not found. Run scripts/install_linux.sh first." >&2
  exit 1
fi
export Qt6_DIR="$QT6_CMAKE_DIR"
export CMAKE_PREFIX_PATH="$(dirname "$(dirname "$(dirname "$QT6_CMAKE_DIR")")")"
log "Using Qt6_DIR=$Qt6_DIR"

# --- Locate OpenGL, only if a local sysroot exists; system installs need nothing extra.
# Explicitly -U (unset) the OpenGL cache vars when not using a sysroot, so a stale
# OPENGL_INCLUDE_DIR left over from a previous configure (e.g. sysroot later removed)
# can't linger in build-linux/CMakeCache.txt and break the build.
CMAKE_EXTRA_ARGS=(-UOPENGL_INCLUDE_DIR -UOPENGL_opengl_LIBRARY -UOPENGL_gl_LIBRARY)
if [ -d "$HOME/opt/sysroot" ]; then
  GL_LIB="$(find "$HOME/opt/sysroot" -name 'libGL.so*' 2>/dev/null | head -n1)"
  GL_HEADER="$(find "$HOME/opt/sysroot" -name 'gl.h' 2>/dev/null | head -n1)"
  if [ -n "$GL_LIB" ] && [ -n "$GL_HEADER" ]; then
    GL_INC="$(dirname "$(dirname "$GL_HEADER")")"
    CMAKE_EXTRA_ARGS=(-DOPENGL_INCLUDE_DIR="$GL_INC" -DOPENGL_opengl_LIBRARY="$GL_LIB" -DOPENGL_gl_LIBRARY="$GL_LIB")
    log "Using local OpenGL sysroot: $GL_INC"
  fi
fi

cmake -S . -B build-linux -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH" "${CMAKE_EXTRA_ARGS[@]}"
cmake --build build-linux -j"$(nproc)"

log "Build complete: build-linux/GameStyleAI"
