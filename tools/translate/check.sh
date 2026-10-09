#!/bin/bash
# Compiles one module's generated C (build/gen/<module>) and prints the errors.
# usage: tools/translate/check.sh <module> [file-glob]
mod=$1
glob=${2:-*}
source ~/projects/emsdk/emsdk_env.sh >/dev/null 2>&1
FLAGS=(-c -w -std=gnu11 -fno-strict-aliasing -fwrapv -Wno-error=int-conversion
  -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration
  -Wno-error=incompatible-function-pointer-types -Wno-error=return-type
  -Isrc -Ibuild/gen/$mod -Ibuild/gen -ferror-limit=0)
out=$(mktemp -d)
for f in build/gen/$mod/$glob.c; do
  emcc "${FLAGS[@]}" "$f" -o "$out/$(basename "$f").o" 2>&1 | grep -E "error:" &
done
wait
rm -rf "$out"
