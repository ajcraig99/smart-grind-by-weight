#!/usr/bin/env bash
# Fetch build dependencies of the digital twin into sim/.deps (gitignored).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$here/.deps"
if [ ! -f "$here/.deps/lvgl-9.5.0/lvgl.h" ]; then
  git clone --depth 1 --branch v9.5.0 https://github.com/lvgl/lvgl.git "$here/.deps/lvgl-9.5.0"
fi
echo "LVGL: $here/.deps/lvgl-9.5.0"
