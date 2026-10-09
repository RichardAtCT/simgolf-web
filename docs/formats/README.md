# Game data file formats

Reverse-engineered from the user's install and the golf_nocd.exe (v1.02) decompile. Addresses are golf_nocd.exe unless noted.
**(guess)** marks inferences the code doesn't prove directly.

| Doc | Files | Status |
|---|---|---|
| [sve.md](sve.md) | `Saved Games\*.sve`, `top10.sve` | Block layout complete and checked against all four saves (sizes add up, dates and hole totals decode). Many fields inside golfer, hole and staff records are still unidentified. |
| [golfers.md](golfers.md) | `.glf`, `.chr`, `.pro` (binary golfer files), `progolfers.dta`, `celebrities.dta` | Complete for everything the files contain; one appearance byte (+0x26) unknown. |
| [text.md](text.md) | `*Lighting.txt`, theme story `.txt` (including the 8-letter name filter), `Interface\<course>.txt` | Complete. |
| [pal.md](pal.md) | `.pal` | Not used by the game (artist leftovers); both formats described. Real palettes come from `*Pal.pcx`, see `../flic.md`. |

Common to all of them: little-endian, no pointers, Windows-1252 text with CRLF (strip `\r` when porting), and no checksums.
