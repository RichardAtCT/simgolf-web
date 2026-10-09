# Golfer files: `.glf`, `.chr`, `.pro` and `.dta`

Sources: the files under `Themes/` in the user's install, and the golf_nocd.exe (v1.02) decompile. All addresses are golf_nocd.exe
addresses. **(guess)** marks inferences the code doesn't prove directly.

CRT helpers used below (FID didn't name them; identified from their use): `FUN_004a614d` fopen, `FUN_004a63a1` fread,
`FUN_004a6268` fwrite, `FUN_004a609f` fclose, `FUN_004a65ee` fgets, `FUN_004a678b` strtok, `FUN_004a6650` toupper.
`FUN_0043d2a0(pattern, …)` lists a directory into `DAT_0080b130` (entries of 100 bytes) and returns the count.

## 1. Where the files live and who loads them

`<theme>` is the current theme name, `DAT_00567328` (e.g. `Standard`, `Firaxis`, `The Sims`, `More Stories`).

| File | Path | Loaded by | Golfer slot | Notes |
|---|---|---|---|---|
| `Joe Pro.glf` | `Themes\<theme>\` | `FUN_004659a0` (theme init) → `FUN_00437fa0(name, 0x4c, -1)` | 0x4c | the club pro |
| `I.M.Picky.glf`, `Ivana Richman.glf`, `J.P.Bigdome.glf` | `Themes\Standard\` | same, slots 0x4d–0x4f | 0x4d–0x4f | the commissioner, the heiress and the CEO |
| `*.chr` | `Themes\<theme>\` | `FUN_004658b0`: lists `Themes\<theme>\*.chr` and loads file *n* into slot *n* (1, 2, …) | 1…N | theme characters (club regulars). Also sets bit 0 of `DAT_00584a0e[n*0x2c]` **(guess: "is a theme character")** |
| `*.pro` | `Themes\<theme>\` | `FUN_004659a0`: lists `Themes\<theme>\*.pro`, loads each into slot 0x4c, then copies its name, appearance and skills into the next free pro-golfer table entry (§4) | 0x4c (temporarily) | extra touring pros. `Themes\Default.pro` and `Themes\Championship\*.pro` are the player's own pro (§1.1) |
| `progolfers.dta` | `Themes\<theme>\`, falling back to `Themes\Standard\` | `FUN_004659a0` | — | text, §4 |
| `celebrities.dta` | `Themes\<theme>\`, falling back to `Themes\Standard\` | `FUN_004659a0` | — | text, §5 |

`FUN_00437fa0(name, slot, skillSlot)` builds the path `Themes\<theme>\<name>` when `slot < 0x4d`, otherwise
`Themes\Standard\<name>`. So only `Joe Pro.glf` is per-theme; the other three always come from `Standard` even when the theme folder
has its own copy (the Firaxis theme ships all four). If the file is missing the call returns 0 and the slot keeps its previous
contents. **(verify: the More Stories and The Sims themes have no `Joe Pro.glf`)**

All binary golfer files are opened `"rb"`/`"wb"`, so they can be read byte for byte on any platform.

### 1.1 Writing (character editor)

`FUN_00437910(slot, mode, championship)` writes a golfer file. The path is:

| `mode` | Path |
|---|---|
| −2 | `Themes\Default.pro` |
| −1 | `Themes\<theme>\<name>.chr` (where `<name>` is the golfer's name field, +0x10) |
| other | `Themes\<theme>\<name>.pro` |
| any, with `championship ≠ 0` | `Themes\Championship\<name>.pro` |

It asks before overwriting (except for `Default.pro`) and rejects names that `FUN_00405ac0` says aren't valid file names. The
editor itself is `FUN_004385d0`; it saves with "Character saved as Themes\…".

`.glf` files are never written by the game; they're just `.chr`-format files with a fixed name.

## 2. Binary layout (`.glf`, `.chr`, `.pro`)

All four extensions share one format. Little-endian, no header magic, no version, no checksum.

| Offset | Size | Contents |
|---|---|---|
| 0x000 | 0x230 | **Golfer record**: the golfer's entry in the global golfer table `DAT_004d6088` (stride 0x230), §2.1 |
| 0x230 | 0x4E2 | **Quote table**: 25 × 50-byte NUL-padded strings, the golfer's own lines for in-game events, §2.2. Copied to/from `DAT_00543d10 + slot*0x4e2` |
| 0x712 | 0x10 | **Skill levels**, §2.3 |
| 0x722 | 8 | ASCII `*PCXFILE` (no NUL). Written by the editor; the reader skips 8 bytes without checking them |
| 0x72A | rest | **Portrait**: a complete PCX file, §2.4 |

A file that ends at 0x722 (1826 bytes) has no portrait. That's valid: the reader's PCX loader (`FUN_00485790`) sees a short read,
the header check fails, and it returns. All `Themes\Standard\*.glf` files and four of the six `The Sims` `.chr` files are like this.

### 2.1 Golfer record (0x230 bytes)

In files, everything after +0x2F is zero. At run time the rest of the 0x230 bytes hold live state that the save game stores
(see `sve.md`), but a golfer file only needs the identity and appearance fields below.

| Offset | Type | Field | Values / notes |
|---|---|---|---|
| +0x00 | char[16] | Profession / title | e.g. `Pro Golfer`, `Commissioner`, `Artist`. Shown in the bio. Editor label "Profession..." |
| +0x10 | char[16] | Name | e.g. `Joe Pro`. Also the `.chr`/`.pro` file name. (The record for slot 0 is the player's default, "Gary Golf" at `0x4d6098`.) |
| +0x20 | u8 | Personality bits | 0x01 Neat, 0x02 Outgoing, 0x04 Active, 0x08 Playful, 0x10 Nice (labels from the pointer table at `0x4c2858`, entries 3–7; editor hotspots 3–7 toggle them). Used by story selection (§`text.md`) and chat. |
| +0x21 | u8 | Age, marital status, gender | bits 0–2 one-hot age: 1 Young, 2 Middle Aged, 4 Mature. Bits 3–6 one-hot marital status: 0x08 Single, 0x10 Married, 0x20 Divorced, 0x40 Widowed. Bit 7: **1 = female** (`FUN_0046c940` returns "is male" as `~b >> 7 & 1`; the editor picks `interface\CustGlfBckMale` when bit 7 is clear). At run time a 16-bit copy lives at `DAT_00579570 + golfer*0x100`, and `FUN_00453260` turns the age bits into years (+20, +30 or +45). |
| +0x22 | s8 | Face (head) | 0–0x13: a built-in head from the head table at `DAT_004d55e8` (stride 0x44; its +0 and +3 bytes give that head's default skin and hair). **≥ 0x14: use the embedded portrait.** On load the reader replaces such a value with the next free custom-head slot for that gender (`DAT_0059b76c[isMale]`, max 0x48) and blits the 140×420 portrait into the head sheet `DAT_005a7148`. |
| +0x23 | u8 | Hat colour + body type | low nibble: hat colour 0–9 (`progolfers.dta` hat codes). Bits 4–5: body type 0–3 (editor hotspot 10). The female flag in +0x21 selects the female body set. |
| +0x24 | u8 | Shirt colour | 0–9 (editor hotspot 13). Codes as in the `.dta` comment headers: 0 white, 1 yellow, 2 orange, 3 peach, 4 red, 5 green, 6 teal, 7 blue, 8 purple, 9 black |
| +0x25 | u8 | Pants colour | 0–9 (hotspot 14): 0 black, 1 blue, 2 light blue, 3 green, 4 light green, 5 brown, 6 red, 7 tan, 8 yellow, 9 white |
| +0x26 | u8 | unknown | 0, 1 or 4 in shipped files. Copied together with the colour fields when a golfer's sprite is built (`FUN_00462020`); forced to 4 for touring pros. **(guess: outfit/headwear style)** |
| +0x27 | u8 | Skin tone | 0–3 (hotspot 16): 0 caucasian, 1 asian/tanned, 2 latino/very tanned, 3 black |
| +0x28 | u8 | Hair colour | 0–4 (hotspot 15, female only): 0 grey, 1 blonde, 2 red, 3 brown, 4 black |
| +0x29 | 3 bytes | padding | 0 |
| +0x2C | u32 | Flags | bits 0–2: course preferences (1 length, 2 accuracy, 4 imagination; editor hotspots 0–2, saved by case 0x13). Bit 3: child **(guess: hotspot 11 is "Adult/child")**. **Bit 7: custom appearance**: the editor sets it whenever a colour, body or gender changes. When the whole dword is non-zero, the colour fields above override the head's defaults when sprites are built. |
| +0x30 | 0x200 | run-time state | zero in files |

Shipped examples (bytes +0x20…+0x2C):

```
                      +20 +21 +22 +23 +24 +25 +26 +27 +28   +2C
Standard/Joe Pro.glf   0c  12  08  28  08  08  04  01  00    01
Firaxis/Kelley.chr     0c  8a  15  22  03  05  04  00  00    80
```

- Joe Pro: Active + Playful; male, Married, Middle Aged; built-in head 8; hat 8, body type 2; shirt 8, pants 8; skin 1;
  prefers length.
- Kelley: Active + Playful; female, Single, Middle Aged; custom portrait (0x15); hat 2, body type 2; shirt 3, pants 5; skin 0;
  custom appearance.

### 2.2 Quote table (0x4E2 bytes)

25 slots × 50 bytes, each a NUL-terminated string (empty = use the built-in line). `FUN_00469b00(event, …, golfer)` looks for the
event in the table at `0x4c2d10` (signed bytes, −1 terminated); if the golfer's slot for that event is non-empty, it says that
line instead of the default. Only 20 slots are reachable; 20–24 are always empty.

The placeholders `MYNAME`, `PARTNER` and `DATA` are replaced (first occurrence only) by `FUN_0045b7c0`: the speaker's name, the
other golfer's name, and the event's subject (a hazard, a building, an animal…).

| Slot | Event | Built-in line (example) | Shipped custom example |
|---|---|---|---|
| 0 | 0x3E | "Praise the Lord for this fabulous day." | "BOOYAA!" |
| 1 | 0x01 good shot | "How'd you like that shot?" | "I.M. fabulous." |
| 2 | 0x04 easy shot | "This shot looks pretty easy." | "Even a hacker could make this shot." |
| 3 | 0x05 hazard in the way | "Eeek, I gotta stay away from the DATA" | "I certainly won't put this in the DATA." |
| 4 | 0x1F striking feature | "I've never seen such DATA!" | "You'd hit this shot into the DATA, PARTNER." |
| 5 | 0x02 bad result | "…you're DATA" / "Darn, I'm DATA!" | "How did that end up DATA?" |
| 6 | 0x03 | "You look even better DATA" | "I hope the cameras didn't catch that shot." |
| 7 | 0x09 nearly hit by a ball | "Hey, that ball almost hit me!" | "Memo to self: course unsafe." |
| 8 | 0x0C hit an obstacle | "Darn that stupid DATA!" | "Chop that tree immediately." |
| 9 | 0x0D water | "Did I hear a splash?" | "What a stupid water hazard." |
| 10 | 0x1C likes a feature | "Look at that lovely DATA" | "How about more DATAs." |
| 11 | 0x14 dislikes a feature | "Gee what an ugly DATA" | "Ugly, ugly, ugly DATA." |
| 12 | 0x27 animal | "I guess I scared that little DATA" | "Is this a golf course or a zoo?" |
| 13 | 0x15 slow play | "I'm tired of waiting for these b…" | "Hey, let the pro play through." |
| 14 | 0x0E thirsty | "I'm getting a little thirsty." | "Memo to self: terrible service here." |
| 15 | 0x19 had a drink | "Ahhh, a cool foamy beverage." | "I'm refreshed now, but it won't last." |
| 16 | 0x0F hungry | "I'm starting to get hungry." | "Memo to self: no food." |
| 17 | 0x12 ate | "There's nothing like a good snack" | "Finally some food around here." |
| 18 | 0x1A tired | "I'm starting to get tired." | "If I weren't so tired, I'd walk off this course" |
| 19 | 0x1B sat on a bench | "This bench is so comfortable." | "Finally, a stinking bench to sit on." |

The event names are inferred from the built-in lines in `FUN_00469b00`'s `switch` **(guess for 0x02, 0x03, 0x1F, 0x3E)**.

### 2.3 Skill levels (16 bytes)

Read into a local and, when the caller passes a skill slot (`skillSlot ≠ −1`), copied to the first 16 bytes of the run-time
golfer record `DAT_005795a8 + skillSlot*0x100` (and to `DAT_005a5a04` for slot 0, but only if flag 0x04000000 of `DAT_0059e7b8`
is set). When writing, the editor stores the same 16 bytes back, or zeros.

Bytes 0–9 are the ten skills in `progolfers.dta` order (power hitter, long driver, accurate driver, accurate irons, accurate
putter, draw, fade, high backspin, recovery, luck); bytes 10–15 are zero in every shipped file **(guess: unused)**. Values use the
`.dta` scale (§4.1). Only `Firaxis/Joe Pro.glf` has non-zero skills (`00 01 09 09 …`), matching "Judicious Pro" in
`progolfers.dta` (`0199000000`).

### 2.4 Portrait

A standard ZSoft PCX: version 5, RLE, 8 bpp, one plane, **140 × 420**, followed by the usual `0x0C` marker and 768-byte VGA
palette at the end of the file. It's three 140×140 frames stacked vertically (the reader blits rows 0, 140 and 280 separately,
**(guess: three expressions)**).

`FUN_00485790` decodes it itself, reading one byte at a time from the open file: it requires `manufacturer == 0x0A`,
`encoding == 1`, `bpp > 3`, `bpp × planes == 8`, then reads the 256-colour palette after one skipped byte. `FUN_00485aa0` is the
matching writer. Every shipped portrait passes these checks.

## 3. Port notes

- Parse with explicit offsets; don't `fread` a C struct. Nothing in the file is a pointer, and everything is little-endian.
- Run the portrait through the same PCX → RGBA path as the other PCX assets (`tools/convert-assets`). The portrait's
  "transparent" colour is whatever the head sheet uses **(verify against `Heads/Head Template.pcx`)**.
- Text is Windows-1252 (the shipped files contain `’` as 0x92). Convert to UTF-8 for display.

## 4. `progolfers.dta` (touring pros)

Plain text, CRLF. Parsed by `FUN_004659a0` with `fgets` (250 chars) and `strtok(",")`:

- Lines starting with `*` are comments. Lines of 9 characters or fewer are skipped (so blank lines are fine).
- Each data line is `name,bodyType,skin,hat,shirt,pants,skills`. The name field keeps its trailing spaces (the shipped file pads
  names with spaces to line the columns up); the last character of the line (the `\r`) is removed first.
- `bodyType = (c − '0') & 7`: 0 long sleeves, 1 knickers, 2 short sleeves, 3 short pants, 4–7 the female outfits (4 long sleeves
  and pants, 5 short sleeves and shorts, 6 short sleeves and pants, 7 tank top and skirt). Bit 2 therefore means female.
- `skin = (c − '0') & 3`; `hat`, `shirt`, `pants` are `(c − '0') % 10`. Only the first character of each field is used.
- `skills`: 10 characters, one per skill (§2.3 order). Each is `0`–`9` → 0–9, or a letter → `10 + (letter − 'A') % 10`
  (case-insensitive), so `A` = 10 … `J` = 19 and `K` wraps back to 10. Anything after the tenth character (`  30`, `  40` in the
  shipped file) is ignored **(guess: author's notes of the total)**.
- Up to 100 entries, stored at `DAT_0058dd50` with stride 0x38: name at +0x00 (32 bytes), body/skin/hat/shirt/pants at +0x20…+0x24,
  skills at +0x25…+0x2E, the sum of the skills as a 16-bit value at +0x36. Unused entries have 0xFF at +0x20.
- Then every `Themes\<theme>\*.pro` is appended (§1).

## 5. `celebrities.dta` (celebrity visitors)

Read by the first half of `FUN_004659a0`, with the same rules (comment lines `*`, lines of ≤ 9 characters skipped,
`strtok(",")`). Entries are 0x25 bytes at `DAT_0055d738` (name at +0x00, then type, skin, hair, shirt, pants at
+0x20…+0x24), at most 100:

`name,type,skin,hair,shirt,pants`

- `type = (c − 'A') % 11`: A action movie star, B female pop star, C politician, D male comedian, E supermodel, F fitness female,
  G female comedian, H leading man, I female movie star, J rock and roller, K athlete.
- `skin`: `1`–`3` → 1–3, anything else → 0. `hair`, `shirt`, `pants`: `(c − '0') % 10`. Codes as in the file's own comment header
  (hair 0 grey, 1 blonde, 2 red, 3 brown, 4 black).
- A trailing comma (`Bruce Springstone,J,0,4,9,0,`) is harmless.
