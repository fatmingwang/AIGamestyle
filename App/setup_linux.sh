#!/usr/bin/env bash
# GameStyleAI — one-click Linux setup: installs dependencies, then builds.
# Usage: bash setup_linux.sh   (or: chmod +x setup_linux.sh && ./setup_linux.sh)
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

bash "$DIR/scripts/install_linux.sh"
bash "$DIR/scripts/build_linux.sh"

echo
echo "Done. Run the app with: $DIR/build-linux/GameStyleAI"
