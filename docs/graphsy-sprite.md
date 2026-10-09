# graphsy sprite class (jgld.dll vtable 0x1011d380)

Source: `jglsprite.cpp` (plus `jglsprite_8_16c.cpp` for the compressed 8->16 blitters).
Created by graphsy slot 32: `operator new(0x50)`, ctor `0x10014cd0(this, ownerHandle)`.
Base-class vtable 0x1011d444 (base ctor 0x10014c70 -> 0x10006ab0), base dtor 0x100168e0.

Conventions used below:
- `Surface*` = drawing-surface object (vtable 0x1011d0b0).
- `Palette*` = palette / colour-converter object. When the sprite's own palette (+0x10) is NULL, the
  blitters fall back to the global default `*(DAT_1012873c + 4)`. If both are NULL the draw is aborted.
  Palette vtable +0x18 returns a `u16[256]` index->RGB555 table, +0x1c returns a `u16[256]` index->RGB565
  table. +0x20(index) returns a single colour (used only by slot 22).
- Return values: the draw wrappers compute an error code (`0` ok, `3` bad argument, `0x17` = LIBERR_NOT_THIS_DEPTH)
  but never return it (Ghidra shows `void`; the code is kept in a local). The game should treat them as void.
- Most draw wrappers take a trailing `Palette* palOverride`: if non-NULL it temporarily replaces +0x10 for
  the call, then +0x10 is restored and slot 9 (`Unlock(1)`) is called. (Exception: slots 18–22 and 34–35
  save the old palette but never restore it — **the override stays in place after the call**, whether by design or by mistake.)
- Bit-depth dispatch: `this->bpp` (+0x20) and the destination surface bpp (`*(int*)surface->vt[0xe4]()`)
  select the inner blitter. Unsupported combinations hit a `_CrtDbgReport` assert (debug strings
  "NOT WRITTEN FOR COMPRESSION", "NOT AVAILABLE TO THIS SURFACE", "LIBERR_NOT_THIS_DEPTH") and do nothing.
  The `*_c` variants (compressed source, `flags & 1`) exist only for 8-bit sprite -> 16-bit surface.

## Pixel-value conventions (8-bit sprites)

| index | meaning in the 8->16 blitters |
|---|---|
| 0x00..0xF7 | normal palette colour (`pal16[idx]`) |
| 0xF8..0xFE | "special" indices. Ordinary Draw (slot 17) treats 0xF8..0xFD as normal colours and **skips 0xFE**. Effect blitters use them for shadow/light: 0xF8 = shadow pixel (dest := LUT[dest]) in slots 25/27; 0xF8..0xFE = LUT planes 0..6 in slot 30; 0xFE = silhouette/outline pixel in slot 39. The tint/blend slots 18/19/37 (when its skip flag is set) skip all of 0xF8..0xFF. |
| 0xFF | always transparent |

Note the ctor default colour key (+0x28) = 0xFF, and the game calls slot 6 (SetColorKey) with 0xFF. For
8-bit sprites the key is used only by the RLE trimming in Create; the blitters hard-code 0xFE/0xFF.
For 16-bit sprites the blitter (0x1001a6b0) compares each u16 pixel with +0x28. If bit 31 of +0x28 is clear,
it first converts the low byte through the palette (index -> colour) and stores the result back into +0x28.
That changes the stored key, so a second draw would convert the already-converted value again. Probable bug.
To avoid it, set the key with bit 31 set to give a literal 16-bit colour (inferred).

## Global state shared by all blitters

- `DAT_10122dc0` = X scale numerator, `DAT_10122dc4` = Y scale numerator, `DAT_10122dc8` = denominator
  (statically 1/1/1). Drawn size = `w*|sx|/den` x `h*|sy|/den`. A negative numerator **mirrors** that axis.
  The unscaled path is taken when |sx|==|sy|==|den|; otherwise it uses 16.16 fixed-point nearest-neighbour stepping
  (`step = (den<<16)/|s|`). The compressed blitters assert on any mirroring ("NOT WRITTEN FOR COMPRESSION").
  These values are probably set by a graphsy-level "set scale" call (not in this class).
- `DAT_101284c4` = global "allow RLE compression" flag (statically 0).
- `DAT_101284c8` = running total of sprite pixels allocated; `DAT_101284cc` = running total of bytes saved by
  compression (both stats only).
- All blitters clip the destination rectangle `SetRect(x, y, x+dw, y+dh)` (0x10008360) against the surface clip
  rect (`surface->vt[0xcc]()` returns RECT*) with IntersectRect.

Surface methods used by the blitters (vtable 0x1011d0b0; meanings inferred from how they are used):
`+0x10` Lock() -> nonzero ok · `+0x18` bits pointer (8-bit / byte view) · `+0x20` bits pointer (16-bit view) ·
`+0x24(n)` Unlock · `+0x28` bpp (same slot as sprite slot 10) · `+0xcc` clip RECT* · `+0xd8`/`+0xdc` width/height (guess) ·
`+0xc(x,y)` pixel address (guess) · `+0xe0` pitch in pixels · `+0xe4` -> `PixelFormat*` with `[0]`=bpp, `[1]`=0 for RGB555 / 1 for RGB565 ·
`+0xe8` attached palette (guess).

## Object field layout (size 0x50)

| off | type | name | notes |
|---|---|---|---|
| 0x00 | void** | vtable | 0x1011d380 |
| 0x04 | ? | (base class) | set by base ctor 0x10006ab0 (probably refcount/type tag — guess) |
| 0x08 | int | (base) | ctor sets 0 |
| 0x0c | void** | ownerSlot | ctor arg. Dtor does `if (ownerSlot) *ownerSlot = 0` (clears the owner's back-reference) |
| 0x10 | Palette* | palette | slot 4/5. NULL = use global default palette |
| 0x14 | u8*/u16* | pixels | pixel data (malloc). Slot 8 returns it. Non-NULL = "has image" |
| 0x18 | u32 | flags | bit0 = RLE-compressed (0x100180e0 `IsCompressed`) |
| 0x1c | RowInfo* | rowTable | `height` x 4-byte entries, only when compressed |
| 0x20 | int | bpp | 8 or 16 (slot 10) |
| 0x24 | int | dataSize | size of `pixels` allocation, in bytes for uncompressed 8-bit data (see the format section) |
| 0x28 | u32 | colorKey | ctor default 0xFF (slot 6/7) |
| 0x2c | int | pitch | row stride in pixels (= width) (slot 11) |
| 0x30 | int | width | (slot 12) |
| 0x34 | int | height | (slot 13) |
| 0x38 | CRITICAL_SECTION | lock | 0x18 bytes, Init in ctor / Delete in dtor. Never used by the vtable methods themselves |

## In-memory RLE ("compressed") format

Built by Create (slot 1 -> 0x1001b690 for 8-bit, 0x1001a250 for 16-bit). Compression happens only if
`DAT_101284c4 != 0 && 5 <= width <= 255 && width*height <= 0xFFFF` and a source buffer is given. It is abandoned
(falling back to a raw copy) as soon as the trimmed data plus the row table would not be smaller than the raw image.
This is per-row trimming of leading and trailing colour-key pixels, not real RLE:

```
struct RowInfo {           // one per row, rowTable = malloc(height*4)
    u8  skip;              // # leading key pixels trimmed (0..width)
    u8  count;             // # pixels stored for this row (0 = empty row)
    u16 offset;            // index (in pixels) into `pixels` where this row's span starts
};
pixels = concatenation of each row's [skip, skip+count) span (key pixels inside the span are kept).
dataSize = width*height - bytesSaved; pixels is _expand()-ed down to dataSize.
```
Row y, column x (0<=x<width) is opaque-candidate iff `skip <= x < skip+count`. Its value is
`pixels[offset + x - skip]`. Everything else is transparent. Inner key pixels (and 0xFE) are still tested per pixel.
The 16-bit variant stores u16 pixels with `offset` in pixels. However, both its raw-copy fallback and its span copy
pass a pixel count, not a byte count, to memcpy (0x1007f3a0), so only half the data is copied. Probably a bug, and
compression is off by default anyway. A browser reimplementation can decode this to a plain RGBA/indexed image at load time.

Compressed-draw quirk (0x100594d0, scaled path): a row with `count==0` does not advance the source-row
accumulator or the destination pointer. The visible result is that rows after an empty row shift up. Probably a bug.
In the unscaled path empty rows are handled correctly.

## Slots

| slot | off | proposed name | signature | semantics |
|---|---|---|---|---|
| 0 | 0x00 | `~Sprite` (scalar deleting dtor) | `void* Sprite::`scalar deleting dtor`(uint flags)` | Runs dtor 0x10014de0 (clear `*ownerSlot`, DeleteCriticalSection, `Free()`, base dtor). Frees `this` if `flags&1`. |
| 1 | 0x04 | `Create` | `void Create(const void* srcPixels, uint width, int height, int bpp, int unused, Palette* pal)` | bpp 8 -> 0x1001b690, 16 -> 0x1001a250. Calls Free(), sets w/h/pitch/bpp, mallocs `w*h*(bpp/8)` and copies srcPixels if non-NULL (it may RLE-compress, see above). If the inner create returns 0 (guess: success) it calls `SetPalette(pal)`. `unused` (param 5) is ignored. |
| 2 | 0x08 | `Free` | `void Free()` | Frees pixels and rowTable and updates the memory stats. Zeroes pitch, width, height, flags and dataSize. Leaves bpp, colorKey and palette as they are. |
| 3 | 0x0c | `CopyFrom` | `void CopyFrom(const Sprite* src)` | Free(), then copies palette, flags, w, h, pitch, colorKey, bpp and dataSize from src (src's colorKey replaces the one set by slot 6). Deep-copies `pixels` (dataSize bytes) and `rowTable` (h*4) if present. |
| 4 | 0x10 | `SetPalette` | `void SetPalette(Palette* pal)` | `+0x10 = pal`. |
| 5 | 0x14 | `GetPalette` | `Palette* GetPalette()` | returns +0x10. |
| 6 | 0x18 | `SetColorKey` | `void SetColorKey(u32 key)` | `+0x28 = key` (game passes 0xFF before Create/CopyFrom). |
| 7 | 0x1c | `GetColorKey` | `u32 GetColorKey()` | returns +0x28. |
| 8 | 0x20 | `GetBits` / `Lock` | `void* GetBits()` | returns +0x14 (pixel pointer). Draw wrappers call it as an "is loaded" check. Same slot as the surface's bits getter. |
| 9 | 0x24 | `Unlock` | `void Unlock(int flags)` | No-op. Called with 1 at the end of every draw wrapper (mirrors the Surface interface). |
| 10 | 0x28 | `GetBpp` | `int GetBpp()` | returns +0x20 (8/16). |
| 11 | 0x2c | `GetPitch` | `int GetPitch()` | returns +0x2c (row stride in pixels). |
| 12 | 0x30 | `GetWidth` | `int GetWidth()` | returns +0x30. |
| 13 | 0x34 | `GetHeight` | `int GetHeight()` | returns +0x34. |
| 14 | 0x38 | `ReplaceIndex` | `void ReplaceIndex(u8 from, u8 to)` | 8-bit only (other depths assert). Replaces every byte == `from` in the pixel data with `to`, walking dataSize (compressed) or pitch*height bytes. |
| 15 | 0x3c | `DrawColorLUT` | `void DrawColorLUT(Surface* dst, int x, int y, const u16* lut, Palette* palOverride)` | 8->16 only: `dst = lut[pal16[idx]]` (lut indexed by a 16-bit colour, e.g. a 64K tint/darken table). Only 0xFF is transparent. Compressed variant 0x10058970. |
| 16 | 0x40 | `DrawRemapped` | `void DrawRemapped(Surface* dst, int x, int y, const u8* remap, Palette* palOverride)` | 8->16: `dst = pal16[remap[idx]]` (256-entry index remap, e.g. team or shirt colours). 8->8 variant 0x10061db0. No compressed variant. 0xFF transparent. |
| 17 | 0x44 | `Draw` | `void Draw(Surface* dst, int x, int y, Palette* palOverride)` | The main blit. 8->16: `dst = pal16[idx]` for idx < 0xFE (0x1001bc40, compressed 0x100594d0). 8->8: raw index copy for idx < 0xFE (0x1005ed80). 16->16: copy pixels != colorKey (0x1001a6b0, also accepts an 8-bit dst surface). Honours the global scale and mirroring. |
| 18 | 0x48 | `DrawTintHalf` | `void DrawTintHalf(Surface* dst, int x, int y, u16 color555, Palette* palOverride)` | 8->16 only: `dst = (pal16[idx]>>1 per channel) + (color>>1 per channel)`, a 50/50 blend with a constant colour (RGB555 maths). Skips 0xFF and >=0xF8. (0x10022a10) |
| 19 | 0x4c | `DrawColorize` | `void DrawColorize(Surface* dst, int x, int y, u16 color555, Palette* palOverride)` | 8->16 only: `lum = (R8+G8+B8)>>2` of the palette colour, then each output channel = `color_ch * lum >> 8` (monochrome tint). Skips 0xFF and >=0xF8. (0x10024140) |
| 20 | 0x50 | `DrawMaskedOver` | `void DrawMaskedOver(Sprite* mask8, Surface* bgSrc, Surface* dst, int x, int y, Palette* palOverride)` | 8->16. `mask8` is an 8-bit sprite of the same size. Per pixel, skip if idx==0xFF or m==0xFF. If m==0: `dst = pal16[idx]`. Otherwise `dst = pal16[idx] + bg*m/256` per channel, unclamped, reading bg from `bgSrc` and writing `dst` (guess: premultiplied colour with an inverse-alpha mask, used for anti-aliased edges). Hard-coded RGB555. (0x10025900) |
| 21 | 0x54 | `DrawMasked` | `void DrawMasked(Sprite* mask8, Surface* dst, int x, int y, Palette* palOverride)` | Same as slot 20 but reads and writes the same surface. (0x10025dd0) |
| 22 | 0x58 | `DrawMaskedGeneric` | `void DrawMaskedGeneric(Sprite* mask8, Surface* dst, int x, int y, Palette* palOverride)` | Slow generic version of slot 21, done inline per pixel: colour = `palette->vt[0x20](idx)`. Mask 0 = opaque, 0xFF = skip, otherwise blend with weight (m+1) using colour unpack/pack helpers (0x1000e540/0x1000a660). No clipping, only an end-of-buffer check. Addresses dst via `dst->vt[0xc](x,y)`. The exact weights are a guess. Used once by the game. |
| 23 | 0x5c | `DrawWithFE` | `void DrawWithFE(Surface* dst, int x, int y, Palette* palOverride)` | Like Draw, but only 0xFF is transparent, so 0xFE pixels are drawn. 8->16 0x1001cde0, 8->8 0x1005dde0. Uncompressed only. |
| 24 | 0x60 | (unimplemented) | `void Stub60(Surface* dst, int a, int b, int c, int d, Palette* palOverride)` | Every bpp path asserts. No blitter exists. |
| 25 | 0x64 | `DrawRemappedShadow` | `void DrawRemappedShadow(Surface* dst, int x, int y, const u8* remap, const u16* shadowLut, Palette* palOverride)` | 8->16: for idx < 0xFE, idx==0xF8 does `dst = shadowLut[dst]` (darken what is underneath) and any other idx gives `dst = pal16[remap[idx]]`. Compressed 0x1005c600. (0x10020430) |
| 26 | 0x68 | `DrawRemappedShadow8` (guess) | `void DrawRemappedShadow8(Surface* dst8, int x, int y, const u8* remap, const u8* shadowLut, Palette* palOverride)` | 8->8 only (0x10062dd0), presumably the 8-bit-surface analogue of slot 25 (not decompiled). |
| 27 | 0x6c | `DrawShadow` | `void DrawShadow(Surface* dst, int x, int y, const u16* shadowLut, Palette* palOverride)` | 8->16: idx 0xF8 gives `dst = shadowLut[dst]`; other idx != 0xFF gives `dst = pal16[idx]` (0xFE is drawn). Compressed 0x1005af00. A 16->16 variant (0x1001ab40) also exists. |
| 28 | 0x70 | `DrawShadow8` (guess) | `void DrawShadow8(Surface* dst8, int x, int y, const u8* lut, Palette* palOverride)` | 8->8 only (0x10060cf0), presumably the 8-bit analogue of slot 27 (not decompiled). |
| 29 | 0x74 | `DrawSilhouette` | `void DrawSilhouette(Surface* dst, int x, int y, u32 color, Palette* palOverride)` | Every pixel with idx < 0xFE is set to one solid colour. If `color` has bit 31 set, its low 16 bits are a literal 16-bit colour; otherwise it is the palette index `color&0xFF`. 8->16 0x1001f2d0, compressed 0x1005bac0, 8->8 0x1005fd20. |
| 30 | 0x78 | `DrawWithLightLevels` | `void DrawWithLightLevels(Surface* dst, int x, int y, const u16* lut, Palette* palOverride)` | 8->16: idx < 0xF8 gives `pal16[idx]`. idx 0xF8..0xFE gives `dst = lut[((idx-0xF8)<<15) \| dst]`: 7 planes of 32768 entries applied to the existing (15-bit) dest pixel, for shadow or light levels. 0xFF transparent. (0x100298b0) |
| 31 | 0x7c | `DrawLightMap` | `void DrawLightMap(Surface* dst, int x, int y, const u16* lut, Palette* palOverride)` | Whole sprite used as a light map. For idx < 0xFE, `dst = lut[(((idx+8)&0xFF)<<15) \| dst]` (indices 0xF8..0xFD map to planes 0..5 and 0..0xF7 to planes 8..). The palette is not used. 8->16 0x1002ab90. 16->16 0x1001b060 sets colorKey=0xFF and does `dst = *(u16*)(lut + (src16<<16 \| dst))`, a guess. |
| 32 | 0x80 | `DrawLightMap8` (guess) | `void DrawLightMap8(Surface* dst8, int x, int y, const void* lut, Palette* palOverride)` | 8->8 only (0x10063f60, not decompiled). |
| 33 | 0x84 | `DrawShadeMap` | `void DrawShadeMap(Surface* dst, int x, int y, const u16* lut, Palette* palOverride)` | 8->16 (0x1002bcf0), idx < 0xFE: 0 gives `dst = 0` (black); 1..15 gives `dst = lut[((idx-1)<<15) \| dst]` (15 shade levels); 16..0xFD leave dst unchanged. The 16->16 path (0x1001b590) is an empty stub. |
| 34 | 0x88 | `DrawEffectScaled` | `void DrawEffectScaled(Surface* dst, Surface* bg, int x, int y, int scaleX, int scaleY, int scaleDen, const u16* lut, Palette* palOverride)` | Only when scaleX==scaleY. Calls 0x1002d390(dst, x, y, scale, den, lut, bg): the scaled form of slot 35 with an explicit scale instead of the globals. |
| 35 | 0x8c | `DrawEffect` | `void DrawEffect(Surface* dst, Surface* bg, int x, int y, const u16* lut, Palette* palOverride)` | 0x1002cec0, 8->16. idx < 0xE0 gives `pal16[idx]`. idx 0xE0..0xEF use LUT plane `idx-0xE0+15` and 0xF0..0xFE use plane `idx-0xF0`, as `dst = lut[(plane<<15) \| base]`, where base = the dst pixel, or the `bg` pixel at the same spot if dst == 0x7C1F (magenta key). 0xFF transparent. No bpp checks. |
| 36 | 0x90 | `DrawEffectNoBg` | `void DrawEffectNoBg(Surface* dst, int x, int y, const u16* lut, Palette* palOverride)` | Slot 35 with `bg=NULL`, plus bpp checks (8->16 only; 16->16 0x1001b5d0 is a stub). |
| 37 | 0x94 | `DrawTranslucent` | `void DrawTranslucent(Surface* dst, int x, int y, float opacity, Palette* palOverride, bool skipSpecial)` | 8->16. If opacity is outside [0,1], error 3. If opacity <= 1/32, nothing is drawn. Otherwise opacity is quantised to n/16: multiples of 1/8 within ±1/32 use their own blitter and the bands between them use the odd sixteenths. Each level has a 555 and a 565 blitter, chosen by `PixelFormat[1]`. Example: 0.5 gives `dst = (src+dst)/2` per channel (0x1002d890 for 555, 0x10041f80 for 565). If skipSpecial, idx >= 0xF8 are skipped; 0xFF is always skipped. The top band is bounded by float const 0x1011d34c (it reads as 100.0f, which looks wrong; probably meant 1.0), so the fall-back to plain `Draw` (slot 17) above it is effectively unreachable. |
| 38 | 0x98 | `DrawDithered` | `void DrawDithered(Surface* dst, int x, int y, int percent, Palette* palOverride)` | 8->16 screen-door translucency. percent 100 calls Draw. 50 draws pixels whose column counter and row counter are both even (0x10056450). 25 adds the condition `((col+row)&2)==0` (0x100576a0). Any other value gives error 3. The counters run down from the clipped width/height, so the parity is anchored at the clip rect's right/bottom edge. 16->16 variants are stubs. |
| 39 | 0x9c | `DrawDepthOutline` | `void DrawDepthOutline(Surface* dst, int x, int y, Surface* depth, u8 threshold, u16 outlineColor, Palette* palOverride)` | 8->16 (0x1001df80, compressed 0x1005a000). `depth` is a 16-bpp surface read via `+0x18`, using the low byte of each pixel. For idx != 0xFF: where depth < threshold (sprite hidden), only 0xFE pixels are drawn, as `outlineColor`. Elsewhere, pixels other than 0xFE are drawn normally. This gives an "x-ray outline when occluded" effect (interpretation). |
| 40 | 0xa0 | `DrawDepthTested` | `void DrawDepthTested(Surface* dst, int x, int y, Surface* depth, u8 threshold, Palette* palOverride)` | 8->16 (0x1001e970, compressed 0x1005a790). Draws idx < 0xFE only where `depthLowByte >= threshold`. |

## Notes / uncertainties

- Slots 18–22 were decompiled without `this` (Ghidra lost ECX). `local_8` in them is `this`. Their first
  stack argument is shown as `param_1`.
- Slot 1's `unused` arg and the return value of the inner create functions are unverified.
- 8->8 helpers for slots 26/28/32 and the 16-bit stubs were not decompiled. Their names are guesses by analogy.
- RGB555 vs 565 choice: `PixelFormat[1]==0` selects palette table +0x18 and the "555" blend code. Slots 18–20
  always use 555 bit maths.
