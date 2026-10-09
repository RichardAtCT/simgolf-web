#!/bin/sh
# Runs the game headless and prints the first page error's stack with each
# wasm frame mapped to build/gen/*.c:line (needs -gsource-map, on by default).
cd "$(dirname "$0")/../.."
. ~/projects/emsdk/emsdk_env.sh >/dev/null 2>&1
node tools/port/run.mjs --ms ${MS:-8000} --every 100000 "$@" 2>&1 > /tmp/golf-run.log
grep -v "^ *[0-9.]* \[error\]   " /tmp/golf-run.log | grep -v "^\s*at " | tail -${TAIL:-6}
grep -A14 "pageerror" /tmp/golf-run.log | grep "golf.wasm" | head -12 | while read -r line; do
  fn=$(echo "$line" | sed -E 's/.*golf\.wasm\.([^ ]+) .*/\1/')
  off=$(echo "$line" | grep -oE ':0x[0-9a-f]+\)' | tr -d ':)')
  loc=$(cd build/web && emsymbolizer -s sourcemap golf.wasm "$off" 2>/dev/null | tail -1)
  echo "  $fn  $loc"
done
