# `.pal` palette files

**Summary: the game never reads `.pal` files.** The two in the install are artists' leftovers. Palettes the game does use come
from PCX files (`*Pal.pcx`, see `docs/flic.md` "Palette swaps"), from the PCX/FLC images themselves, and from golfer portraits
(`golfers.md` §2.4).

Evidence: no `.pal`, `JASC` or `PAL data` string occurs in golf_nocd.exe, `jgl.dll`, `jgld.dll`, `Terrain.dll` or `sound.dll`,
and no file-name list in the decompile mentions either file.

## 1. `Heads/m_sidh~1.pal`: JASC-PAL (Paint Shop Pro)

Text, CRLF:

```
JASC-PAL        magic
0100            version
256             number of entries
0 0 0           256 lines of "R G B", decimal 0–255
...
255 0 255
```

It's exactly the palette of `Heads/Head Template.pcx` and `Heads/M_Sid Head.pcx` (byte-for-byte equal to their trailing 768-byte
PCX palettes). The 8.3 name (`~1`) suggests it was copied from a long-named file (probably `M_Sid Head.pal`) through a DOS 8.3 tool **(guess)**.

## 2. `Flics/Flowers/FlowerbedPal.pal`: Microsoft RIFF palette

| Offset | Size | Contents |
|---|---|---|
| 0x00 | 4 | `RIFF` |
| 0x04 | 4 | u32 LE: 1160 (file size − 8) |
| 0x08 | 4 | `PAL ` form type |
| 0x0C | 4 | `data` chunk id |
| 0x10 | 4 | u32 LE: 1028 (chunk size) |
| 0x14 | 2 | u16 LE: palette version 0x0300 |
| 0x16 | 2 | u16 LE: 256 entries |
| 0x18 | 1024 | 256 × `PALETTEENTRY` {R, G, B, flags}; flags are all 0 |
| 0x418 | 3 × 40 | three extra chunks, `offl`, `tran` and `unde`, each 32 bytes of zeros |

The trailing chunks aren't part of the Windows palette format; they're written by some paint/animation tool **(guess: Autodesk
Animator Studio, which also explains the FLC files)**. Readers that only take the `data` chunk ignore them.

The palette doesn't equal any PCX palette in `Flics/Flowers/`; the game takes flower-bed colours from
`flics\flowers\FlowerBedA_<colour>Pal.pcx` instead.

## 3. Port notes

- Don't ship or convert these two files; `tools/convert-assets` can skip `*.pal`.
- If a modding tool wants palettes, both formats above are standard and trivial to parse; but the in-game source of truth is the
  PCX palette (after the `0x0C` marker at file end − 769).
