# graphsy drawing surface — vtable 0x1011d0b0 (jgld.dll)

Class created by graphsy slot 31 (`operator new(0x4d4)`, ctor `FUN_10007970(this, owner)`).
It is a GDI DIB-section-backed drawing surface. The base class vtable is 0x1011d220 (ctor FUN_10009690 -> FUN_10006ab0).
Its `this` pointer is always the **source** in blit-style methods; the other surface passed in is the **destination**.

Sources: decompiles of the vtable slots and their helpers, kept outside the repo in `~/ghidra_work/graphsy/`
(`vt_1011d0b0_clean.c`, `agent_surface/batch1..4.c`).

Conventions used below:
- `Surface*` = another object of this class (or a compatible one). `Palette*` = graphsy palette object (see notes).
- "colour16" = a 16-bit colour argument. If **bit 31 is set**, the low 16 bits are a raw 555 pixel value.
  If bit 31 is clear, the low 8 bits are a **palette index**, converted through the 16-bit LUT of this surface's
  palette (`+0x7c`), or the global default palette (`*(DAT_1012873c+4)`) if none is set. `MakeColor(r,g,b)`
  (FUN_1000a660) returns `0x80000000 | rgb555` (or rgb565 when the system object says 565), so values made with it are raw.
- Rect = Win32 `RECT {left, top, right, bottom}`, right/bottom exclusive. `FUN_10008360(rc,x,y,w,h)` builds one from x,y,w,h.
- Return code `0x18` (24) means "not supported" and `0x17` (23) means "unsupported format" in this library.
- Lock/Unlock: every pointer-returning call (slots 3–8) takes the **lock count** up by one. Drawing helpers balance it with `Unlock(1)` (slot 9).

## Field layout (byte offsets; size 0x4d4)

| off | type | meaning |
|---|---|---|
| 0x000 | vtbl* | 0x1011d0b0 |
| 0x004 | ? | base-class field (FUN_10006ab0) — unknown |
| 0x008 | int32 | set to 0 in ctor — unknown |
| 0x00c | CRITICAL_SECTION (0x18 bytes) | initialised in ctor and deleted in dtor; no slot here uses it |
| 0x024 | int32 bpp | 8/16/24/32. Slot 57 returns `&this->bpp`, a "pixel format" struct starting here |
| 0x028 | int32 fmt | 0 = 555, 1 = 565. Always forced to 0 (555) in Create. Selects palette LUT slot 6 (555) or 7 (565) |
| 0x02c | int16 ×3 | shifts R=10, G=5, B=0 (555) |
| 0x032 | uint16 ×3 | masks R=0x7c00, G=0x03e0, B=0x001f (at 0x32, 0x34, 0x36) |
| 0x038 | int32 | width |
| 0x03c | int32 | height |
| 0x040 | int32 | **pitch in pixels** (not bytes): 8bpp is rounded up to 4, 16bpp up to 2, 24bpp is `w + (w&1)` (guess: buggy unless w%4==0), 32bpp is `w` |
| 0x044 | RECT | clip rect (always ⊆ bounds) |
| 0x054 | RECT | bounds = {0,0,w,h} |
| 0x064–0x073 | ? | unknown/unused here |
| 0x074 | uint8 | 0 in ctor — unknown |
| 0x078 | int32 | cached palette serial (palette `+0x810`), used by SetPalette to skip redundant uploads |
| 0x07c | Palette* | palette (no addref). NULL means use the global default palette |
| 0x080 | Owner* | owning wrapper or handle (ctor arg). Dtor clears `owner->+4`. LoadPNG calls `owner->vtbl[1](w,h,bpp)` to re-create |
| 0x084 | BITMAPINFOHEADER | biSize=0x28, biWidth=w, biHeight=**-h (top-down)**, planes=1, biBitCount=bpp, BI_RGB, biClrUsed=0 after Create |
| 0x0ac | RGBQUAD[256] | bmiColors (to 0x4ab) |
| 0x4ac | HRGN | clip region selected into the DC (NULL when clip == bounds) |
| 0x4b0 | HBITMAP | previous bitmap returned by SelectObject (restored on destroy) |
| 0x4b4 | HBITMAP | the DIB section |
| 0x4b8 | HDC | "current DC" handed out by GetDC (copy of 0x4bc while refcount>0) |
| 0x4bc | HDC | memory DC (CreateCompatibleDC(NULL)) |
| 0x4c0 | uint8_t* | DIB bits (ppvBits) |
| 0x4c4 | int32 | GetDC refcount |
| 0x4c8 | int32 | Lock refcount |
| 0x4cc | uint8_t* | locked bits pointer (copy of 0x4c0 while locked) |
| 0x4d0 | uint32 | **colour key** for transparent draws. Default `MakeColor(255,0,255)` = 0x80007c1f (magenta, raw flag set) |

Globals that affect drawing: `DAT_10128420` = graphsy system object (vtbl+0xb0 = default display bpp,
vtbl+0xb4 = "is 565" flag). `DAT_1012873c+4` = default Palette*. `DAT_10122dc0/dc4/dc8` = global draw scale
numerator X/Y and denominator (a negative numerator mirrors on that axis), used by the image-draw paths (slots 33/35).

## Slots

### 0 (+0x000) `~Surface` (scalar deleting dtor)
`void* __thiscall Destroy(uint32_t flags)` — dtor: clears `owner->+4`, DeleteCriticalSection, Release (slot 2), base dtor. Frees memory if `flags&1`.

### 1 (+0x004) `Create`
`void __thiscall Create(int32_t width, int32_t height, int32_t bpp)` — does nothing if w<0 or h<0, or if size and bpp are unchanged (bpp 0 counts as "any"). Otherwise frees (slot 2). bpp 0 means system default (sys vtbl+0xb0), falling back to 8. Creates the memory DC and a top-down DIB section with that bpp. If that fails it shows a MessageBox "Unable to allocate draw-buffer" and calls exit(4). Selects the DIB into the DC and computes pitch (see layout). Sets the 555 format, SetBkMode(TRANSPARENT), SetTextAlign(TA_BASELINE|TA_LEFT), bounds={0,0,w,h} and SetClipRect(bounds). Leaves the bits uninitialised. bpp switch accepts only 8/16/24/32; others return early after the DIB is made.

### 2 (+0x008) `Release` / FreeGDI
`void __thiscall Release(void)` — if there is an HDC: zeroes both refcounts, reselects the old bitmap, DeleteDC. Then DeleteObject on the DIB (0x4b4) and on the clip region (0x4ac). Does not clear 0x4c0 or the dimensions.

### 3 (+0x00c) `GetPixelPtr`
`uint8_t* __thiscall GetPixelPtr(int32_t x, int32_t y)` — returns NULL if `x >= boundsW || y >= boundsH` (no negative check). Otherwise Lock() (refcount++), then `bits + (x + y*pitch)*Bpp` for Bpp 1/2/3/4. Returns NULL for any other bpp. Caller must Unlock(1).

### 4 (+0x010) `Lock`
`uint8_t* __thiscall Lock(void)` — `lockedPtr = bits`. If non-NULL, lockCount++. Returns the pointer to the top-left pixel.

### 5 (+0x014) `GetPixelPtr8` (guess name)
`uint8_t* __thiscall GetPixelPtr8(int32_t x, int32_t y)` — forwards to slot 3. The 8bpp code paths use it (guess: the `uint8_t*` overload).

### 6 (+0x018) `Lock8` (guess name)
`uint8_t* __thiscall Lock8(void)` — forwards to slot 4. Used by the 8bpp paths.

### 7 (+0x01c) `GetPixelPtr16` (guess name)
`uint16_t* __thiscall GetPixelPtr16(int32_t x, int32_t y)` — forwards to slot 3. The 16bpp paths use it.

### 8 (+0x020) `Lock16` (guess name)
`uint16_t* __thiscall Lock16(void)` — forwards to slot 4. Used by the 16bpp paths.

### 9 (+0x024) `Unlock`
`void __thiscall Unlock(int32_t n)` — `lockCount -= n`. If the result is <1, sets count=0 and lockedPtr=NULL. Always called with n=1.

### 10 (+0x028) `GetDC`
`HDC __thiscall GetDC(void)` — `curDC = memDC`, dcCount++. Returns the HDC (can be NULL before Create).

### 11 (+0x02c) `ReleaseDC`
`void __thiscall ReleaseDC(int32_t n)` — `dcCount -= n`. If the result is <1, sets count=0 and curDC=NULL. The HDC itself stays alive.

### 12 (+0x030) `Nop12`
`void __thiscall Nop12(int32_t unused)` — empty (RET 4). Guess: Flush or Update placeholder.

### 13 (+0x034) `SetClipRect`
`void __thiscall SetClipRect(const RECT* r)` — ignores NULL. `clip = r ∩ bounds`. If the intersection is empty, clip becomes the empty rect and nothing else happens. Otherwise: GetDC, delete the old HRGN. If clip ≠ bounds it creates a rect region and SelectClipRgn; else SelectClipRgn(NULL). Then ReleaseDC(1). The GDI clip only affects text and StretchBlt. The software paths read `clip` directly.

### 14 (+0x038) `BlitBehindTo` (dest-keyed blit)
`void __thiscall BlitBehindTo(Surface* dst, int32_t srcX, int32_t srcY, int32_t dstX, int32_t dstY, int32_t w, int32_t h, uint8_t key)` — this surface (8bpp only) is the source. Writes a source pixel **only where the destination pixel == key** (draw "behind" existing content). If dst is 8bpp the compare is on the index. If dst is 16bpp it compares against `pal16[key]` and writes `pal16[src]` (palette = this +0x7c or the global, LUT picked by dst format). Clipping: a negative src origin is shifted into the dst, w/h are clamped to the source size, and the dst rect is intersected with dst's **clip** rect.

### 15 (+0x03c) `BlitTo` (opaque copy)
`void __thiscall BlitTo(Surface* dst, int32_t srcX, int32_t srcY, int32_t dstX, int32_t dstY, int32_t w, int32_t h)` — plain copy, clipped as in slot 14. Supports 8→8 (overlap-safe memmove direction when dst==this), 8→16 (through the palette LUT), and 16→dst (dst bpp not checked, assumed 16, overlap-safe). Other depths do nothing.

### 16 (+0x040) `StretchTo`
`void __thiscall StretchTo(Surface* dst, const RECT* srcRect, const RECT* dstRect)` — GDI `StretchBlt(dst.DC, dstRect…, this.DC, srcRect…, SRCCOPY)`. Any bpp. GDI clip region of dst applies. Takes and releases both DCs.

### 17 (+0x044) `FillRect`
`void __thiscall FillRect(const RECT* r /*NULL = whole surface*/, uint32_t colour)` — 8bpp: `colour` low byte is the index. 16bpp: colour16 rules (bit31 raw, else palette index). A NULL rect fills the whole buffer fast when clip==bounds, otherwise it recurses with the clip rect. A non-NULL rect is intersected with clip. Other bpp do nothing.

### 18 (+0x048) `BlendFillRect` (16bpp only)
`void __thiscall BlendFillRect(const RECT* r /*NULL=bounds*/, uint32_t colour16, int32_t keepPct)` — per channel (8-bit expanded) `out = colour*(256-a)/256 + dst*a/256` with `a = clamp(keepPct,0,100)*256/100`. So keepPct is the **percentage of the existing pixel kept** (100 = no change, 0 = solid fill). Clipped to clip. Packs through MakeColor. Returns early with 0x18 if fmt is not 0/1.

### 19 (+0x04c) `FillRectChecker` (stipple 50%)
`void __thiscall FillRectChecker(const RECT* r /*NULL=bounds*/, uint32_t colour)` — fills every other pixel in a checkerboard. 8bpp: index. 16bpp: colour16. Row phase comes from the *row counter relative to the rect* (it depends on rect height parity), not from absolute x+y. Clipped to clip.

### 20 (+0x050) `BlendFillRectChecker` (16bpp only)
`void __thiscall BlendFillRectChecker(const RECT* r, uint32_t colour16, int32_t keepPct)` — slot 18's blend applied only to the checkerboard pixels of slot 19.

### 21 (+0x054) `ShadeFillHoles` (16bpp only; name and params partly guessed)
`void __thiscall ShadeFillHoles(RECT* r /*in/out, clipped to bounds*/, Surface* src, float level, const uint16_t* shadeLut)` — both surfaces 16bpp. `L = (int)level`, `base = (15-L)<<15`. For each pixel in r: if `dst == 0x7c1f` (magenta 555) then `dst = shadeLut[base | src]`, else `dst = shadeLut[base | dst]`. So the LUT is 16 levels × 32768 entries. Uses dst pitch for both. No Unlock calls are visible (possible lock leak). The coordinates passed to the pointer getters were lost in the decompile; assumed (r.left, r.top).

### 22 (+0x058) `ReplaceColor`
`void __thiscall ReplaceColor(const RECT* r, uint32_t from, uint32_t to)` — every pixel == from becomes to, inside `r ∩ clip`. 8bpp compares bytes. 16bpp compares raw 16-bit values (no palette conversion). Ignores a NULL rect.

### 23 (+0x05c) `RemapRect` (8bpp only)
`void __thiscall RemapRect(const RECT* r, const uint8_t* lut256)` — `p = lut[p]` over `r ∩ clip`. Does nothing if lut is NULL.

### 24 (+0x060) `DrawDashedLine`
`void __thiscall DrawDashedLine(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t colourA, uint32_t colourB, int32_t lenA, int32_t lenB, int32_t phase)` — Bresenham line, endpoints inclusive, clipped to clip. Repeating pattern of lenA pixels of colourA then lenB pixels of colourB, starting `phase` into the period. If x2<x1 the endpoints swap and phase becomes `lenA+lenB-phase`. x1==x2 and y1==y2 use special helpers. 8bpp: colour index, **-1 skips** that dash. 16bpp: colour16. Here -1 does not skip: it draws 0xffff. **Bug:** with fmt 0, colourB's palette index is converted through the 565 LUT (slot 7) instead of the 555 LUT.

### 25 (+0x064) `DrawLine`
`void __thiscall DrawLine(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t colour)` — solid Bresenham line, inclusive, clipped to clip. 8bpp index. 16bpp colour16. Vertical and horizontal lines use fast paths (memset or stride loop). Those paths draw **nothing when both endpoints are equal** (single point).

### 26 (+0x068) `Unimplemented26`
`int32_t __thiscall Unimplemented26(a, b, c, d)` — returns 0x18. Takes 4 args (RET 0x10). Guess: another image-load format, like slot 29.

### 27 (+0x06c) `Unimplemented27`
`int32_t __thiscall Unimplemented27(a, b, c, d)` — returns 0x18. 4 args. Guess: loader for another format.

### 28 (+0x070) `Unimplemented28`
`int32_t __thiscall Unimplemented28(a)` — returns 0x18. 1 arg. Guess: saver for another format (cf. slot 30).

### 29 (+0x074) `LoadPNG`
`int32_t __thiscall LoadPNG(const char* filename, Palette* pal, int32_t p3, int32_t p4)` — uses libpng 1.0.5. Accepts only colour_type 2 (RGB) or pixel depth 24. Strips 16-bit samples to 8 and strips alpha. Applies what looks like set_bgr (FUN_1006d600). Re-creates the surface through `owner->Create(w, h, pixel_depth)` (24bpp). Then calls `FillRect(NULL, 2)` (a no-op at 24bpp), reads rows into `GetPixelPtr(0,row)` and leaks the lock counts. The palette branch (colour_type 3, sends PLTE to `pal->vtbl[5](rgb, p3, p4)` then SetPalette) is unreachable because of the first test. Returns 0 on success, 6 if the file can't open, 1 on a libpng error, 0x17 for an unsupported type.

### 30 (+0x078) `SavePCX` (8bpp only)
`int32_t __thiscall SavePCX(const char* filename)` — replaces the extension with a fixed string (DAT_1011d354, presumably ".pcx"). Writes a 128-byte PCX header (manufacturer 10, ver 5, RLE, 8 bits, xmax=w-1, ymax=h-1, 1 plane, bytesPerLine=pitch). Body is RLE with 0xC0|run (max 63). Single bytes ≥0x40 are written as a run of 1. Ends with 0x0C and the 768-byte palette from `pal->vtbl[4](buf,0,256)`. Returns 0, or 7 (lock failed) / 4 (file buffer failed).

### 31 (+0x07c) `Unimplemented31`
`int32_t __thiscall Unimplemented31(a)` — returns 0x18. 1 arg. Guess: saver for another format.

### 32 (+0x080) `Unimplemented32`
`int32_t __thiscall Unimplemented32(a, b, c, d)` — returns 0x18. 4 args. Guess: a draw-to variant (dst, x, y, extra).

### 33 (+0x084) `DrawTransparentTo`
`int32_t __thiscall DrawTransparentTo(Surface* dst, int32_t x, int32_t y)` — wraps this surface in a temporary Image (vtbl 0x1011d380: bits, pitch, w, h, bpp, key = this+0x4d0, palette = slot 58) and calls `Image::Draw(dst, x, y, palette)` (image slot 17, FUN_10015480). Result: a colour-keyed copy onto dst at (x,y), clipped to dst clip, scaled and mirrored by the global DAT_10122dc0/dc4/dc8. Source 8bpp: indices **0xFE and 0xFF are transparent** (key field ignored); goes to 8 or 16bpp dst. Source 16bpp: pixels == key are skipped (key bit31 clear means a palette index converted with dst's palette); dst 8/16 assumed 16. Unlocks once.

### 34 (+0x088) `Unimplemented34`
`int32_t __thiscall Unimplemented34(a, b, c, d)` — returns 0x18. 4 args.

### 35 (+0x08c) `DrawShadowedTo`
`int32_t __thiscall DrawShadowedTo(Surface* dst, int32_t x, int32_t y, const uint16_t* shadowLut)` — same wrapper as 33, but calls image slot 27 (FUN_100165a0) with the LUT. 8bpp src → 16bpp dst: 0xFF is transparent, **0xF8 is a shadow pixel** (`dst = lut[dst]`), anything else gives `dst = pal16[src]`. 16bpp src → 16bpp dst: skip where the low byte == low byte of key; where the low byte == 0 do `dst = lut[dst]`; else copy. 8→8 hits an assert (unsupported).

### 36 (+0x090) `Unimplemented36` — returns 0x18, 4 args.
### 37 (+0x094) `Unimplemented37` — returns 0x18, 4 args.
### 38 (+0x098) `Unimplemented38` — returns 0x18, 4 args.
### 39 (+0x09c) `Unimplemented39` — returns 0x18, 4 args.
### 40 (+0x0a0) `Unimplemented40` — returns 0x18, 4 args.
### 41 (+0x0a4) `Unimplemented41` — returns 0x18, 4 args.
`int32_t __thiscall UnimplementedNN(a, b, c, d)` for all of 36–41. Guess: more image-draw variants (dst, x, y, extra) that this class doesn't support.

### 42 (+0x0a8) `SelectFont`
`void __thiscall SelectFont(Font** font)` — `SelectObject(GetDC(), (*font)->hfont /* +0x18 */)`, then ReleaseDC(1).

### 43 (+0x0ac) `SelectDefaultFont`
`void __thiscall SelectDefaultFont(void)` — `SelectObject(hdc, GetStockObject(SYSTEM_FONT /*13*/))`.

### 44 (+0x0b0) `SetTextColorIndex`
`void __thiscall SetTextColorIndex(uint32_t index)` — `SetTextColor(hdc, 0x10ff0000 | (index & 0xffff))` (DIBINDEX, an entry of the DIB colour table).

### 45 (+0x0b4) `SetTextColorRGB`
`void __thiscall SetTextColorRGB(uint8_t r, uint8_t g, uint8_t b)` — `SetTextColor(hdc, RGB(r,g,b))`.

### 46 (+0x0b8) `TextOut`
`int32_t __thiscall TextOut(int32_t x, int32_t y, const char* str, int32_t len)` — GDI TextOutA on the surface DC. y is the **baseline** (TA_BASELINE), background transparent, GDI clip region applies. Returns 0.

### 47 (+0x0bc) `SetColorKey`
`int32_t __thiscall SetColorKey(uint32_t key)` — `this+0x4d0 = key`. Returns 0. Default is 0x80007c1f (magenta).

### 48 (+0x0c0) `GetColorKey`
`uint32_t __thiscall GetColorKey(void)` — returns `this+0x4d0`.

### 49 (+0x0c4) `ResetClip`
`void __thiscall ResetClip(void)` — `SetClipRect(&bounds)`.

### 50 (+0x0c8) `GetClipRect`
`void __thiscall GetClipRect(RECT* out)` — copies the clip rect.

### 51 (+0x0cc) `GetClipRectPtr`
`RECT* __thiscall GetClipRectPtr(void)` — returns `this+0x44`.

### 52 (+0x0d0) `GetBounds`
`void __thiscall GetBounds(RECT* out)` — copies bounds {0,0,w,h}.

### 53 (+0x0d4) `GetBoundsPtr`
`RECT* __thiscall GetBoundsPtr(void)` — returns `this+0x54`.

### 54 (+0x0d8) `GetWidth`
`int32_t __thiscall GetWidth(void)` — `bounds.right - bounds.left`.

### 55 (+0x0dc) `GetHeight`
`int32_t __thiscall GetHeight(void)` — `bounds.bottom - bounds.top`.

### 56 (+0x0e0) `GetPitch`
`int32_t __thiscall GetPitch(void)` — pitch **in pixels** (`+0x40`).

### 57 (+0x0e4) `GetPixelFormat`
`PixelFormat* __thiscall GetPixelFormat(void)` — returns `this+0x24`: `{int32 bpp; int32 is565; int16 shiftR,shiftG,shiftB; uint16 maskR,maskG,maskB}`. Callers read `->bpp` and `->is565`.

### 58 (+0x0e8) `GetPalette`
`Palette* __thiscall GetPalette(void)` — returns `+0x7c` (may be NULL).

### 59 (+0x0ec) `SetPalette`
`void __thiscall SetPalette(Palette* pal)` — ignores NULL. Stores pal (no refcount). If `pal->serial (+0x810)` ≠ the cached serial (+0x78): converts the 256 entries (palette stores R,G,B at `+0xc + i*4`) to RGBQUAD and calls `SetDIBColorTable(curDC, 0, 256, …)` inside GetDC/ReleaseDC. This matters for 8bpp GDI text/StretchBlt. Software 8→16 paths use the palette's 16-bit LUTs (pal vtbl slot 6 = 555 LUT, slot 7 = 565 LUT).

## Notes for reimplementation
- Pixel storage: top-down, `ptr = bits + (x + y*pitch) * bytesPerPixel`. 16bpp is always **RGB555** in practice (fmt forced to 0, BI_RGB DIB).
- Magic values: colour key default magenta 0x7c1f (555). 8bpp sprite transparency = 0xFE/0xFF. 8bpp shadow pixel = 0xF8. Slot 21 "hole" colour = 0x7c1f.
- Software drawing clips to the `clip` rect (0x44). GDI drawing (text, StretchBlt) clips with the HRGN that SetClipRect selects.
- The game only uses 8 and 16 bpp in the software paths. 24bpp appears only through LoadPNG; 32bpp is only accepted by Create and GetPixelPtr.
