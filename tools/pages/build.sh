#!/bin/sh
# Builds the GitHub Pages site into build/pages/site: the game plus the
# in-browser installer, with no game files in it. Needs the translated game in
# build/gen (tools/port/rebuild.sh) and emsdk. Leaves build/web alone.
set -e
cd "$(dirname "$0")/../.."
. ~/projects/emsdk/emsdk_env.sh >/dev/null 2>&1 || true
# (re)configure every time: CMake copies the web/pages files at configure time
if [ -f build/pages/CMakeCache.txt ]; then cmake build/pages >/dev/null; else emcmake cmake -B build/pages -DSIMGOLF_PAGES=ON >/dev/null; fi
cmake --build build/pages --target golf -j8 2>&1 | grep -E "error|undefined symbol" | grep -v BinkOpenDirectSound | head -20 || true
src/install/build.sh build/pages >/dev/null
src/install/build-bink.sh build/pages >/dev/null
site=build/pages/site
rm -rf "$site"; mkdir -p "$site"
sed "s/__BUILD__/$(git rev-parse --short HEAD)$(git diff --quiet HEAD -- . ":!HANDOFF.md" || echo +)/" build/pages/golf.html > "$site/index.html"
cp build/pages/golf.js build/pages/golf.wasm build/pages/golf.data \
   build/pages/autosave.js build/pages/install-worker.js build/pages/jgld-nops.json \
   build/pages/unshield.js build/pages/unshield.wasm build/pages/og.png build/pages/report.js third_party/7z-wasm/7zz.umd.js third_party/7z-wasm/7zz.wasm \
   build/pages/bink.js build/pages/bink.wasm third_party/webm-muxer/webm-muxer.js "$site/"
touch "$site/.nojekyll"
du -sh "$site"; ls "$site"
