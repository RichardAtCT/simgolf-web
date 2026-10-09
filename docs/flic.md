# FLIC sprites, palette swaps and interface masks

Addresses are golf.exe v1.02 (NoCD baseline, `~/ghidra_work/out_v102/`). Statements marked *(inferred)* are reasoned from
how the code uses a value rather than read directly from the code.

## FLC files

All 1,893 files in `Flics/` are Animator Pro FLC files (magic `0xAF12` at +4, depth 8). Every one of them carries a
Firaxis extension in the reserved part of the 128-byte header:

| off | type | field | notes |
|---|---|---|---|
| 0x06 | u16 | frames | standard; not used by the game's frame indexing |
| 0x08 / 0x0a | u16 | width / height | the frame box, e.g. 67×74 for a large maple |
| 0x10 | u32 | speed | ms per frame (standard field); 66–83 in the files sampled |
| 0x1a | u32 | `0xF1F1F2F2` | marks the extension. Without it, `0x481ca0` fills the fields below itself: 1 animation of `frames` frames, origin 0,0, canvas = frame size |
| 0x50 | u32 | oframe1 | standard; frame chunks are walked from here |
| 0x58 | u32 | 0x1c | always 0x1c (also written by the plain-FLC path) |
| 0x60 | u16 | anims | number of animations in the file (4 directions for trees, 8 for golfers) |
| 0x62 | u16 | framesPerAnim | |
| 0x64 / 0x66 | u16 | originX / originY | top-left of the frame inside the canvas |
| 0x68 / 0x6a | u16 | canvasW / canvasH | 480×480 for every file checked. The canvas centre (240,240) is the object's ground point *(inferred: the prerender below crops around it)* |
| 0x6c | u16 | ? | varies per file (1066, 1583, …); not read by the decoder |
| 0x70 | u32 | animMask | bit *a* set = animation *a* exists. 0 = all exist. `0x482490` maps a requested animation through this mask |

**Frame layout.** Animation *a* starts at frame chunk *a*·(framesPerAnim+1). Its first frame is always a BYTE_RUN keyframe
(sometimes with a COLOR_256 chunk), followed by framesPerAnim−1 DELTA_FLC frames and then a ring frame that deltas the last
frame back to the first. `0x481e40` builds the per-animation start table (`flic+0x48`) and a flat frame table (`flic+0x4c`).
`0x482420` advances one frame and, at the end, decodes the ring frame and wraps to frame 1. Two files (`Scenic/ScenicElm*.flc`)
have 4 spare frames at the end, which the game never reaches.

**Chunks** (dispatcher `0x482570`): 4 COLOR_256 and 11 COLOR_64 (`0x4826f0`; both are read as 8-bit components), 7 DELTA_FLC
(`0x4827d0`), 12 DELTA_FLI (`0x487cb0`), 13 BLACK (`0x482940`), 15 BYTE_RUN (`0x482990`), 16 COPY (`0x482a80`). The shipped
files use only types 4, 7 and 15. Type 9 appears twice and is ignored by the game. The palette is constant within a file,
except for `Bldgs/*/shortflagL1.flc` and `dshortflagL1.flc`, which change it between frames.

**Objects.** The loaded file object (`0x481b50`, size 0x84, vtable `0x4ba2e0`) is kept in a global array of 0x24f slots at
`0x4f65c8`. Its player/instance (`0x482b60`, size 0x44) holds the current animation (+0x40), frame (+0x3c) and
next-chunk pointer (+0x38), and the 0x2c-byte sprite wrapper at +8 that frames are decoded into.
`0x481f40` (load) chooses the palette from flags: bit 1 = a private palette object, bit 2 = a shared palette chain, otherwise
the palette passed in. The game's slot loader `0x404120` passes the per-slot palette object `0x4e9aa0 + slot*0x58`, so
each slot's COLOR_256 chunk lands in its own palette.

## How the game uses them

**Prerendering.** The game decodes FLCs once into a frame cache instead of decoding them while playing.
`0x43d740(slot, name, count, zoom, hasShadow)`:

1. Builds the path `flics\<name>.flc` and, if `hasShadow`, `flics\<name>Shadow.flc` (string `0x4c8a58`).
2. For each animation and frame, clears a 480×480 8-bit scratch surface to 0xFF (`0x478af0`), draws the shadow frame and
   then the object frame at their origins (`0x473e60` → sprite Draw), and cuts a rectangle out of it into the next cache
   entry (`0x473bf0`). The rectangle depends on `zoom`: 0 → (210,200) 60×60; ±1 → (100,100) 280×240; 0xF0 → the full
   480×480; anything else z → (240−z, 240−z) 2z × 1.5z. Callers pass 0x3c or 0x8c for scenery.
3. The cache is a global array of 0x2c-byte sprite wrappers at `0x5aaa30`, capped at 0xDD40 entries (the error
   "Too many flics!"). `0x43d6f0(slot, frame, anim)` looks an entry up.

**Shadows.** `*Shadow.flc` files use only indices 0xF8–0xFB on a 0xFF background. Composited under the object they become
the "light level" pixels of `DrawWithLightLevels` / `DrawShadow` (docs/graphsy-sprite.md), which darken whatever is
already on screen through a LUT. They are not drawn as colours.

**Palette swaps.** `Pal*.pcx` / `*_palette.pcx` files (e.g. `flics\trees\PalGreenMaple`) are 480×480 PCX files that are used
only for their palette. `0x475840(path, palObj, 0, 0x100, flags)` loads a PCX (appending `.pcx` when there is no extension)
and, when given a palette object, copies its palette into it. The theme loader (around `0x4400ff`) fills the palette objects at
`0x81d6c8 + n*0x58` with tree and flower colour variants per course theme (`0x5a34e0`: 1 desert (Joshua trees, cacti), 2 tropical, 3 links (its own bridge set);
0 is presumably parkland *(inferred)*). The same frames are then drawn with a different `palOverride`. Bridges and birds load one palette each before
their `0x43d740` calls.

**Golfer colours** *(inferred)*. Golfer frames use flat red, yellow and green placeholder colours, so they are presumably
drawn with `DrawRemapped` (slot 16) and a per-golfer 256-byte remap table. This hasn't been confirmed at a call site.

## Interface masks (`_A` / `_alpha` PCX)

33 masks pair by name with their colour image (`X_A.pcx` → `X.pcx`, `X_alpha.pcx` → `X.pcx`), plus two exceptions:
`ChooseAlphaButtons_A.pcx` is shared by `Choose{Desert,Links,Parkland,Tropical}Buttons.pcx`, and `SGAreport_alpha.pcx`
belongs to `SGA.pcx`.

The game loads both images into an 8-bit scratch surface with `0x475840` and cuts the same rectangles from each into two
parallel sprite arrays with `0x473bf0`. For example, `PopUpIcons.pcx` (5 × 90×80 cells) goes to `0x567a20` and
`PopUpIcons_A.pcx` goes to `0x53de78`. It draws them with `0x473f60`, which is sprite slot 21 `DrawMasked(mask, dst, x, y)`:

- mask index 0 = opaque, 0xFF = skipped, and any other m gives `dst = colour + dst·m/256` per channel.
- The colour art is premultiplied (painted over black, with background index → RGB 0,0,0), so straight alpha = 1 − m/256
  and the straight colour = colour / alpha. The masks' palettes are a grey ramp with index i = grey 255−i, which matches.

## Converter output (`tools/convert-assets`)

- **8-bit PCX → paletted PNG** (colour type 3), so indices 0xF8–0xFF and the palette-swap files keep their meaning.
  Browsers and SDL_image render them unchanged.
- **Mask pairs → `X.rgba.png`**, an RGBA composite with straight alpha, next to `X.png` and `X_A.png`.
- **`X.flc` → `X.png` + `X.json`.** The PNG is a paletted sprite sheet, one row per animation and one column per frame,
  and each cell is the frame box (not the 480 canvas). tRNS makes 0xFE and 0xFF transparent, as the normal Draw does.
  Shadow sheets therefore show their 0xF8–0xFB pixels in the palette's key colours (green/pink) until a port applies the
  light-level LUT. The JSON holds the header fields above plus `shadow` when `XShadow.flc` exists:

  ```json
  {"width":67,"height":74,"anims":4,"framesPerAnim":16,"speedMs":66,"animMask":15,
   "origin":[217,173],"canvas":[480,480],"unknown6c":1066,"shadow":"TreeMapleLargeShadow.png"}
  ```

  To place frame (a, f) the way the game does, draw the shadow cell and then the object cell at `origin`, relative to a
  canvas whose centre is the object's ground point.

## Open questions

- Field 0x6c. Also whether the `count` argument of `0x43d740` ever differs from the header's `anims` (they match in the bridge and hawk files checked).
- Which draw call (and which LUT) renders cached tree, building and golfer frames on the course: DrawWithLightLevels vs
  DrawShadow, and the remap tables used for golfers.
- Whether anything decodes FLCs at play time, through `0x482e20`/`0x404090`, rather than from the prerendered cache.
