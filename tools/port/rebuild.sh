#!/bin/sh
# Re-translate (from the existing Ghidra exports) and rebuild the game.
set -e
cd "$(dirname "$0")/../.."
tools/translate/run.sh | tail -2
. ~/projects/emsdk/emsdk_env.sh >/dev/null 2>&1
cmake --build build/web --target golf -j8 2>&1 | grep -E "error|undefined symbol" | grep -v BinkOpenDirectSound | head -20 || true
ls -la build/web/golf.wasm
