#!/bin/sh
# Builds the in-browser InstallShield extractor (unshield) into $1 (default build/pages).
set -e
cd "$(dirname "$0")/../.."
out=${1:-build/pages}
mkdir -p "$out"
. ~/projects/emsdk/emsdk_env.sh >/dev/null 2>&1 || true
U=third_party/unshield
emcc -O2 -sUSE_ZLIB=1 -I$U -I$U/lib -Wno-everything \
  src/install/unshield_wasm.c $U/lib/component.c $U/lib/converter.c $U/lib/directory.c \
  $U/lib/file.c $U/lib/file_group.c $U/lib/helper.c $U/lib/libunshield.c $U/lib/log.c \
  $U/lib/md5/md5c.c \
  -sMODULARIZE=1 -sEXPORT_NAME=createUnshield -sENVIRONMENT=worker -sALLOW_MEMORY_GROWTH=1 \
  -lworkerfs.js -sFORCE_FILESYSTEM=1 \
  -sEXPORTED_RUNTIME_METHODS=ccall,FS,WORKERFS \
  -o "$out/unshield.js"
ls -la "$out/unshield.js" "$out/unshield.wasm"
