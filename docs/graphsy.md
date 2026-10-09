# graphsy: the 2D graphics interface

golf.exe draws everything 2D through jgl.dll (release) or jgld.dll (debug), a Firaxis library its
window class calls "JackalClass". This page summarises what was recovered on 2026-10-07 from the
**v1.02 NoCD golf.exe** and the **debug jgld.dll**. The declarations are in
`src/platform/graphsy.h` and `src/platform/graphsy_surface.h`. Per-slot notes are in
`graphsy-surface.md` and `graphsy-sprite.md`.

## Shape of the interface

`get_graphsy_object_ptr()` (the DLL's only export) returns a `Graphsy`. Everything else is virtual:

| Class | jgl vtable | Size | Slots | Created by | What it is |
|---|---|---|---|---|---|
| `Graphsy` | 0x1011d640 | 0x14c | 86 | the export | Window, display modes, message pump, factories |
| `GsFont` | 0x1011da6c | 0x24 | 5 | `newFont` (29) | GDI font from a face name or a TTF file |
| `GsPalette` | 0x1011db10 | 0x820 | 9 | `newPalette` (30) | 256 RGB entries plus 8→16-bit lookup tables |
| `GsSurface` | 0x1011d0b0 | 0x4d4 | 60 | `newSurface` (31) | DIB-section render target: blits, fills, lines, text, PNG load, PCX save |
| `GsSprite` | 0x1011d380 | 0x50 | 41 | `newSprite` (32) | 8/16-bit image with ~25 draw variants (shadow, tint, translucency, depth test) |

golf.exe also implements two interfaces that jgl calls back:

- **`GsApp`** (golf global 0x0083a4f8, vtable 0x004ba87c). jgl's window procedure turns Win32
  messages into 14 calls: size, paint, hit-test, key, char, mouse move, wheel, left/right button
  down/up, activate, and two custom user messages. **This is the whole input interface**, so the port
  only has to feed SDL events into these methods.
- **`GsScreen`** (golf global 0x0083a7b8). golf's screen back buffer. jgl calls `resize(w,h,bpp,1)`
  when the display mode changes and blits its `GsSurface` (at +4) to the window in `present`.

In golf.exe (v1.02) the `Graphsy*` lives at **0x0083ad50**. The handoff's 0x00838408 was the v1.0
address. WinMain loads `jgld.dll` through `FUN_004855b0` → `FUN_004a00a0`, then calls
`setMode(800, 600, 16)`.

## What golf.exe uses

- Direct calls on `Graphsy` hit 34 of the 86 slots, from 141 call sites in the decompile. Most
  used: `slot45` (always returns 0) 39 times, `setDrawScale` 31, `width` 12, `hwnd` 10,
  `getDrawScale` 7, `height` 5. Most other slots are called once or twice.
- golf.exe wraps each jgl object in its own small C++ class that stores the jgl pointer at +4
  (`FUN_00483800` font, `FUN_00483030` palette, `FUN_00474dd0` surface, `FUN_004744c0` sprite).
  Drawing calls go through those wrappers. Which surface and sprite slots golf.exe actually calls
  hasn't been tallied yet; that needs a per-wrapper pass over the decompile.
- The game renders at **16 bpp RGB555**, with 8-bit paletted sprites drawn onto it through palette
  lookup tables. 0xFF (and usually 0xFE) are transparent, and indices 0xF8–0xFE are special
  shadow/light pixels that index lookup tables using the pixel underneath.
- `setDrawScale(x, y, den)` is global state that scales and mirrors every sprite draw. It's
  presumably how the zoom levels work.

## Notes for the browser port

- Keep a 16-bit RGB555 software framebuffer and port the blitters as plain C. The effects
  (shadow LUTs, light planes, translucency, depth-tested outlines) are per-pixel functions of the
  destination pixel and don't map onto Canvas2D compositing. Upload the framebuffer to a WebGL texture
  (or SDL texture) once per `present`.
- GDI-only features to replace: text (`selectFont`/`textOut`, baseline-aligned, the two bundled TTFs)
  and `stretchTo` (StretchBlt). Display-mode functions become no-ops that report 800×600.
- The original has bugs the port may want to keep for fidelity or fix deliberately: the 565 table used
  for colour B in 16-bit dashed lines, single-point lines drawing nothing, palette overrides that
  stick after some sprite draws, and a 16-bit sprite colour key that gets converted twice. Details are
  in the per-class notes.
- jgld.dll statically includes libpng 1.0.5 and zlib, but `GsSurface::loadPNG` only accepts 24-bit RGB.
  The game's own assets are PCX/BMP/TGA/FLC, so they're presumably decoded in golf.exe (not yet checked).

## Gaps and next steps

- **Ghidra missed functions in golf.exe.** Cross-references to the screen global include code at
  0x0047e7eb, 0x00480e51, 0x00479a74 and others that belong to no function, and 7 of the 157 references to
  the `Graphsy*` are in such code. These are probably methods reached only through golf's own vtables.
  Before porting subsystems, run a pass that creates functions at every vtable target and undefined
  code block, then redump `decomp_all.c`.
- Tally surface/sprite slot usage per golf wrapper class, to prioritise which of the ~100 drawing
  methods to implement first.
- Not yet decompiled: sprite slots 26, 28 and 32 (8→8 variants, likely unused at 16 bpp), and the
  release jgl.dll. jgl.dll should match jgld.dll's slot layout, but that hasn't been confirmed.

## Reproducing

Ghidra scripts are in `tools/ghidra/`:

```sh
export JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home
H=/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless
# every slot of a vtable (follows incremental-link thunks, stops at the first non-code pointer)
$H ~/ghidra_work/projects SimGolfDeps -process jgld.dll -noanalysis -readOnly \
  -scriptPath tools/ghidra -postScript DumpVtable.java /tmp/vt.c vt 1011d640 200
# individual functions, and cross-references to addresses
$H ... -postScript DumpVtable.java /tmp/f.c fn 100654b0 10069ee0
$H ... -process golf_nocd.exe ... -postScript DumpRefs.java /tmp/refs.txt 0083ad50
```

Raw decompiles live outside the repo in `~/ghidra_work/graphsy/` and must not be committed.
