#!/usr/bin/env bash
# Build sim/out/wasm/grindtwin.js (WASM embedded, SINGLE_FILE) with emsdk.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
"$here/tools/fetch_deps.sh" > /dev/null
if ! command -v emcc > /dev/null; then
  for env in "${EMSDK:-}/emsdk_env.sh" /opt/sim-tools/emsdk/emsdk_env.sh "$HOME/emsdk/emsdk_env.sh"; do
    if [ -f "$env" ]; then source "$env" > /dev/null 2>&1; break; fi
  done
fi
command -v emcc > /dev/null || { echo "emcc not found: install emsdk (see sim/README.md)"; exit 1; }
emcmake cmake -S "$here/wasm" -B "$here/out/wasm" -G Ninja > /dev/null
ninja -C "$here/out/wasm" grindtwin
ls -la "$here/out/wasm/grindtwin.js"
