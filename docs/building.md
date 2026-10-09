# Building

You only need this to work on the port. To play, use the hosted page (see the
README).

## Requirements

- Your own copy of the game: the SimGolf CD image and the v1.02 no-CD `golf.exe`.
- Ghidra 12.1 with OpenJDK 21, emsdk (Emscripten 6.0), CMake, Python 3, Node 20+,
  ffmpeg (only for the disc videos in the dev build).

## From the game to a build

1. Install or unpack the game into `game/` (gitignored). `unshield x data1.cab`
   on the disc gives the same files as an install.
2. Import `golf_nocd.exe` and `jgld.dll` into a Ghidra project and analyse them,
   then run `tools/ghidra/prepare-golf.sh` and `tools/ghidra/prepare-jgld.sh`
   (see `docs/porting.md`). This produces the decompiler export.
3. `tools/port/rebuild.sh` translates the export into C under `build/gen`
   (never committed) and builds the development version in `build/web`.
   Serve it with `node tools/boxedwine/serve.mjs build/web 8081` and open
   `http://localhost:8081/golf.html`.

## The GitHub Pages version

`tools/pages/build.sh` builds `build/pages/site`: the game, the in-browser
installer (`web/pages/`, `src/install/`, unshield in `third_party/unshield`)
and no game files. On first visit the page asks for the player's disc image
and no-CD exe, unpacks the installer cabinets in a worker, maps `golf.exe` and
`jgld.dll` the way Ghidra's loader does (plus the NOP runs in
`web/pages/jgld-nops.json`, regenerated with `tools/pages/make-jgld-nops.mjs`),
and stores everything in the browser's origin private file system.

Test it headlessly with
`node tools/boxedwine/serve.mjs build/pages/site 8090` and
`node tools/pages/test-install.mjs out.png <disc.iso> <nocd.zip>`.

Publish with `tools/publish/sync-public.sh --site` from the development repo:
it mirrors the committed code to the public repo's `main` (minus the paths in
`.publicignore`, refusing anything that looks like game data or decompiler
output) and pushes `build/pages/site` to `gh-pages`. Without `--site` it syncs
the code only; `tools/publish/install-hook.sh` runs that after every commit.
