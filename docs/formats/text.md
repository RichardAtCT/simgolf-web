# Text formats: lighting, story (theme) and terrain-description files

Sources: the files in the user's install, the golf_nocd.exe (v1.02) decompile and the Terrain.dll decompile
(`~/ghidra_work/terrain/out/`). Addresses are golf_nocd.exe unless they start with `0x1000`, which are Terrain.dll.
**(guess)** marks inferences the code doesn't prove directly. See `golfers.md` for the CRT helper names.

All of these files are Windows-1252 text with CRLF line endings. The original reads them with the MSVC CRT in text mode, which
turns CRLF into LF. **A port must strip `\r` itself**, or every comparison against a section name fails.

## 1. Lighting: `<Course>Lighting.txt`

Files: `ParklandLighting.txt`, `DesertLighting.txt`, `TropicalLighting.txt`, `LinksLighting.txt` in the game root. Read by
Terrain.dll, not golf.exe (golf.exe has no reference to them).

```
#AMBIENT
200 200 180
#DIFFUSE
240 240 240
#SPECULAR
255 255 245
#HIGHLIGHT
xa37dd45ffe100bfffcc9753aabac…x
```

- Chosen by `FUN_10003980` from the course type `DAT_10070a0c`: 0 Parkland, 1 Desert,
  2 Tropical, 3 Links, anything else `lighting.txt` (which doesn't ship). It's called from `initSystem` and `loadNewCourseType`;
  see `docs/terrain.md` §3.4 for what the values do to GL_LIGHT0.
- The parser is `FUN_10006dd0`. It opens the file with an `ifstream` relative to the current directory and reads lines with
  `getline(buf, 256, '\n')`. For each section in the fixed order `#AMBIENT`, `#DIFFUSE`, `#SPECULAR` it:
  1. skips lines until `strtok(line, " ")` equals the section name exactly,
  2. reads the **next line** and takes three tokens with `strtok(…, " ")` + `atoi`, each divided by 255.0.
- Only a space is a delimiter. Trailing tabs (DesertLighting has `200 200 160\t\t\t`) work only because `atoi` stops at them; a
  tab *between* numbers would break the parse.
- The loop has **no end-of-file check**: a file missing one of the three sections makes the original spin forever. A missing file
  is fine (the lights keep their previous values).
- `#HIGHLIGHT` and the hex blob after it are never read. The same blob appears in `jackal.txt`'s header as a "Version" line, so it's
  most likely a Firaxis tool stamp **(guess)**. Alpha is not in the file: the fourth float of each colour is uninitialised
  stack, `0xCCCCCCCC` in this debug-built DLL. That's harmless because fixed-function GL takes a lit vertex's alpha from the
  material's diffuse alpha only. Use 1.0 in a port.

Shipped values (R G B, 0–255):

| File | Ambient | Diffuse | Specular |
|---|---|---|---|
| Parkland | 200 200 180 | 240 240 240 | 255 255 245 |
| Desert | 200 200 160 | 240 240 240 | 255 255 235 |
| Tropical | 200 200 160 | 240 240 240 | 255 255 235 |
| Links | 200 200 200 | 240 240 240 | 255 255 255 |

## 2. Story files: `Themes\<theme>\<8 letters><Title>.txt`

Each `.txt` in a theme folder is one "story": a conversation between two golfers that plays out over several rounds. Shipped in
`Themes\Standard`, `Themes\More Stories` and `Themes\Firaxis`.

### 2.1 Discovery

`FUN_004659a0` (theme init) lists `Themes\<theme>\*.txt`; if there are none it lists `Themes\Standard\*.txt` instead and sets flag
0x10000000 in `DAT_0059e7b8`. The names (at most 100, 50 bytes each including the `.txt`) are copied to `DAT_0053a454`. The index
of the first name containing `OpeningDay` goes to `DAT_00838a9c`; that story is reserved for the course opening and is never picked
at random.

### 2.2 The eight-letter prefix

The first eight characters of the file name are a filter. `FUN_0045de80` picks a random story for golfers A (initiator) and B, and
rejects it unless every letter matches (`toupper` is applied; `x` and any letter not listed means "any"):

| Pos | Applies to | Letter → condition |
|---|---|---|
| 0 | story | Category. Not a filter. When the story finishes, `FUN_004722c0` maps it to an id: C→0x168, P→0x169, A→0x16A, M→0x16B, L→0x16C, H→0x16D, G→0x16F, F or R→0x170, X→0x171, S→0x173, anything else → 0x168 + random(10). It sets bit (id − 0x168) in `DAT_00822c70` and `DAT_00543cfc` **(guess: unlocks a reward item)** |
| 1 | A | Gender: `F` A is female, `M` A is male, `O` A and B are opposite sexes, `S` same sex |
| 2 | A | Marital status (bits of golfer record +0x21): `S` single, `M` married, `D` divorced, `W` widowed, `N` not married |
| 3 | A | Personality (bits of record +0x20): `T` neat, `M` not neat (messy), `O` outgoing, `S` not outgoing (shy), `A` active, `L` not active (lazy), `P` playful, `B` not playful, `N` nice, `G` not nice |
| 4 | A vs B | Age (`FUN_00453260`): `O` A is at least 10 years older than B, `Y` A is at least 10 years younger, `S` the gap is at most a third of A's age |
| 5 | B | Gender, as position 1 but for B (`O`/`S` compare the two) |
| 6 | B | Marital status, as position 2 |
| 7 | B | Personality, as position 3 |

Example: `GxxTxxxMBadChemistry.txt` — category G; A must be neat; B must be messy.
`ROSxSOSxLoveontheLinks.txt` — romance; A outgoing and of the opposite sex to B; similar ages; B single and outgoing.

Before this, `FUN_0045de80` also scores the pair by how many of the five personality bits differ. The rest of the name after the
eighth character is the story's file title; the displayed title is the first line of the file (§2.3).

### 2.3 Body

`FUN_00466b70(story, stage, response, …)` reads the file with `fgets` (250 chars), stopping after 200 lines:

```
 Opening Day                                       <- title: the first line; starts with a space
 - Barry Caudill                                   <- optional author credit (also starts with a space; ignored)
                                                   <- blank lines (≤ 1 char) are skipped
PARTNER, our friendship is the first story ...    <- stage 1 prompt: no leading space, said by A
 Yes, PARTNER, we're making some golf magic.       <- stage 1 responses: leading space, said by B
 Yeah, but it's 5 a.m.
                                                   
I'm just so enthusiastic about these new ...      <- stage 2 prompt
 Joe Cool should name a landmark after you.        <- stage 2 responses …
```

- With `stage = −1` it returns the first line (the title).
- Otherwise a line without a leading space starts the next stage (counted from 1), and the lines with a leading space after it are
  that stage's possible replies, picked by index. The leading space is not part of the text.
- Placeholders `MYNAME`, `PARTNER` and `DATA` are replaced by `FUN_0045b7c0` as in golfer quotes (`golfers.md` §2.2).
- Every shipped story has exactly 4 stages, with 2–6 replies each. The game's reply choice is driven by the pair's relationship
  **(guess: the first replies are the friendly ones and the last ones the hostile ones, judging by the text)**.

## 3. Terrain descriptions: `Interface\<course>.txt`

`Interface\parkland.txt`, `desert.txt`, `tropical.txt`, `links.txt`: the help text shown in the build palette.

```
Terrain descriptions
	
Parklands

*Tees
Flat, green, and well tended. ...
*Green
...
*END
```

- Shown in the tooltip drawn by `FUN_00433190` when the cursor rests on a terrain button. The file is picked from the course type
  `DAT_005a34e0` (0 parkland, 1 desert, 2 tropical, 3 links; paths `interface\<name>.txt`, lower case).
- Lookup is by name, so entry order doesn't matter. `FUN_0045b660(file, key)` opens the file `"rt"`, reads lines with `fgets`
  (250 chars, last character removed), and compares each line with `key` = `"*"` + the terrain's name from the run-time name table
  `DAT_00578350` (12-byte entries) using `FUN_004ad4b0` **(guess: `_stricmp`)**. It gives up after 100 lines without a match.
- After the match it joins the following lines with a single space into the global text buffer `DAT_0051a068` until it reaches a
  line starting with `*` (the next entry or `*END`) or 100 lines. So a description can span several lines.
- The first lines (`Terrain descriptions`, a tab, the course name) are never matched and are effectively a comment.
- 16 entries per course: Tees, Green, Sand Trap, Rough, Pot Bunker, Stream, Water, Tree, Pine tree, Palm tree, Fairway,
  Firm Fairway, Deep rough, Waste bunker, Brush, Rocks. The wording differs per course; the names must match the game's own
  terrain names.
- The text uses Windows-1252 punctuation (0x85 `…`, 0x92 `’`, 0x93/0x94 quotes) and the hint `(press <tab> to …)`.

## 4. Other text files in the game root (not in scope, noted for completeness)

- `jackal.txt`: dialog-box definitions for Firaxis's "JACKAL" UI library (`#ID`, `#xs`, `#caption`, `#editbox`, `#itemlist`, `$FILENAME0`
  substitutions, `;` comments). Mostly multiplayer/file dialogs.
- `credits.txt`: the credits roll.
- `logfile.txt` (root and two `Flics` subfolders): empty debug logs.
