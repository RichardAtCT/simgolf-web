# Boxedwine spike

Runs the unmodified game in the browser under the Boxedwine 26R1 web build.

```sh
tools/boxedwine/setup.sh                       # downloads Boxedwine, builds build/boxedwine/serve
node tools/boxedwine/serve.mjs build/boxedwine/serve 8080   # static server with COOP/COEP headers
open 'http://localhost:8080/boxedwine.html?app=simgolf&p=golf.exe&overlay=glu'
```

Headless runs (system Chrome via playwright-core; output in `build/boxedwine/runs/<tag>/`):

```sh
# screenshot every 15 s for 150 s, optionally clicking "x,y@seconds;..."
node tools/boxedwine/drive.mjs "app=simgolf&p=golf.exe&sound=false&overlay=glu" 150 mytag "720,140@140"
# interactive: append lines like "click 720 140", "wait 5", "shot name", "quit" to runs/<tag>/cmd
node tools/boxedwine/idrive.mjs "app=simgolf&p=golf.exe&sound=false&overlay=glu" live
```

Page coordinates: the 800×600 game canvas sits at (150, 31) in the 1100×900 viewport.

## Results (2026-10-07)

- The installed v1.0 `golf.exe` is SafeDisc-protected and isn't tried; the spike uses the
  decrypted v1.02 NoCD build.
- golf.exe imports `Terrain.dll` statically, which imports `GLU32.dll`. The web root
  filesystem has no `glu32.dll.so`, so the exe fails to load until `glu.zip` is overlaid.
- With the overlay, the game boots to the main menu in about 2 minutes (headless, multithreaded
  build), and the menus, difficulty screen and course-site globe all work with mouse input.
- Picking a course site fails with **"Can't Find A Suitable PixelFormat."** The web build has
  no OpenGL (`libGL.so.1` is absent, and Boxedwine's host-GL syscalls aren't compiled into
  the wasm), so the 3D course view, which is the actual game, can't run.
- Using the full Wine 6.0 filesystem as root instead brings in Boxedwine's guest `libGL`,
  which then crashes the worker on an unimplemented GL call (`Uknown int 99 call: 2897`).

Getting further would need OpenGL in Boxedwine's web build (GL 1.1 → WebGL, e.g. via
Emscripten's `LEGACY_GL_EMULATION` or a guest-side software Mesa). That is the same
problem the source port has to solve for `Terrain.dll`, so it's not worth doing twice.
