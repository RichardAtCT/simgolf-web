#!/bin/sh
# Build a local Boxedwine web serve directory for SimGolf under build/boxedwine/serve.
#
#   tools/boxedwine/setup.sh [path/to/decrypted/golf.exe]
#
# The exe argument should be a SafeDisc-free v1.02 golf.exe (the official patch
# exe is still SafeDisc-encrypted). Game files come from ./game. Nothing here is
# committed: build/ is gitignored.
set -eu
REPO=$(cd "$(dirname "$0")/../.." && pwd)
EXE=${1:-$HOME/ghidra_work/nocd/golf_nocd.exe}
B=$REPO/build/boxedwine
REL=https://github.com/danoon2/Boxedwine/releases/download/26R1.0
mkdir -p "$B/dl" "$B/serve"

[ -f "$B/dl/web.zip" ] || curl -fL -o "$B/dl/web.zip" "$REL/Boxedwine26R1Web.zip"
[ -d "$B/dl/web" ] || unzip -q "$B/dl/web.zip" -d "$B/dl/web"
for f in "$B"/dl/web/MultiThreaded/*; do ln -sf "$f" "$B/serve/"; done

# The web root filesystem ships a glu32 stub without its .so, so Terrain.dll
# (and therefore golf.exe) fails to load. Pull glu32.dll.so from the full
# Wine 6.0 filesystem into an overlay zip.
[ -f "$B/dl/wine6.zip" ] || curl -fL -o "$B/dl/wine6.zip" http://boxedwine.org/v2/2/TinyCore15Wine6.0.zip
rm -rf "$B/dl/ov" && mkdir "$B/dl/ov"
(cd "$B/dl/ov" && unzip -q ../wine6.zip opt/wine/lib/wine/glu32.dll.so && rm -f "$B/serve/glu.zip" && zip -qr "$B/serve/glu.zip" opt)

# App zip: the game folder under simgolf/, with the decrypted exe as golf.exe.
rm -rf "$B/dl/stage" && mkdir -p "$B/dl/stage"
cp -R "$REPO/game/" "$B/dl/stage/simgolf"
cp "$EXE" "$B/dl/stage/simgolf/golf.exe"
(cd "$B/dl/stage" && rm -f "$B/serve/simgolf.zip" && zip -qr "$B/serve/simgolf.zip" simgolf)
rm -rf "$B/dl/stage"

(cd "$REPO/tools/boxedwine" && [ -d node_modules ] || npm i --silent)
echo "Ready. Run: node tools/boxedwine/serve.mjs build/boxedwine/serve 8080"
echo "Then open: http://localhost:8080/boxedwine.html?app=simgolf&p=golf.exe&overlay=glu"
