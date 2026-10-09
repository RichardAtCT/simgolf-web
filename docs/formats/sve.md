# SimGolf `.sve` save games (and `top10.sve`)

Sources (kept outside the repo, never commit them): `~/ghidra_work/out_v102_before_recover/decomp_all.c` (the golf_nocd.exe v1.02 decompile; same addresses as `out_v102`) and `~/ghidra_work/out/decomp_all.c` (v1.0, for comparison). Disassembly of the region Ghidra did not decompile (0x40F5C0–0x421B60, the main game loop) came from `objdump` on `~/ghidra_work/nocd/golf_nocd.exe`.
The checks used the four saves in `Saved Games/` and `top10.sve` from the user's install. The scratch scripts (`dec.py`, `verify.py`, `table.py`) were not committed.

Conventions: all addresses are golf_nocd.exe (v1.02). **(guess)** marks inferences that the code does not prove directly. Grids are indexed `x*50 + y`. That is the order golf uses (`param_1*0x32 + param_2`), and Terrain.dll's `tileAt` uses the transpose.

---

## 0. The big picture

* A `.sve` is a **raw memory dump of about 86 golf.exe globals**, written back to back, with no padding, alignment, tags, lengths, magic, version field, checksum or compression. The only header is a 100-byte display string.
* All values are **little-endian x86**: int = 4 B, short = 2 B, char = 1 B, and structs use MSVC natural packing. Nothing in the file is a pointer. Every cross-reference is an array index (golfer → profile, building → type table, string-pool offsets). A scan of the saved blocks for 0x4xxxxx/0x5xxxxx values found only text and coordinate pairs.
* The size is **fixed per flag combination**:
  * base 294,733 B (0x47F4D). All four sample saves have this size.
  * +152,040 when `flags & 0x200000` (profile snapshot), giving 446,773.
  * **v1.02 only:** +0x8C0 (2,240) when `flags & 0x40000000`, giving 296,973 (or 449,013 with both). v1.02 sets 0x40000000 on every load (FUN_0040b9b0), so a v1.02 game that was loaded from a save writes the larger form. v1.0 has the same base list but not this block. The base layout is otherwise identical in v1.0 and v1.02.
* The flag word itself is in the stream (offset 0x44E09) and is restored *before* the optional blocks are reached. A reader therefore reads the base part, then looks at the flags to decide whether more follows. Readers never check the length. Extra trailing data is ignored, and a short file just leaves globals partly stale.

## 1. Files, names and when they are written

| file (relative to the game dir) | written by | read by |
|---|---|---|
| `saved games\<name>.sve` | **FUN_0040b4a0(name)** = save. Callers: FUN_00405b10 (in-game "Save game" prompt → "Game Saved!") and FUN_0046f550 (save prompt "Save File Name:" at the site/career screen) | **FUN_0040b9b0(name, applyTheme, headerOnly, championship)** = load |
| `saved games\&AutoSave1.sve` | game loop at 0x417C5C when `(clock & 0x3FF) == 0x200`, i.e. on the **16th of each month** | the load screen. Names starting with `&` are listed as `autosave: <header>`. The game loop also probes the literal `saved games\&AutoSave1.sve` at 0x40F9EC via `[0x4ba090]` **(guess: FindFirstFileA, an existence check)** |
| `saved games\&AutoSave2.sve` | 0x417C82 when `(clock & 0x3FF) == 0`, i.e. on the **1st of each month** | the same |
| `saved games\&QuitSave.sve` | quit path at 0x420E1B. It first runs FUN_00405b10 (the offer to save), then clears flag 0x4000000, then saves | the load screen |
| `saved games\While_Browsing.sve` | FUN_0043b610 (the Load Game screen), on entry from inside a running game (`FUN_0040b4a0("While_Browsing")`). The browser previews a save by **fully loading it** (`FUN_0040b9b0(file,0,0,0)` on hover), so it first parks the live game here | on Cancel the screen reloads it (0x43CACB: `FUN_0040b9b0("While_Browsing.sve",1,0,0)`) |
| `saved games\One.sve` | the tutorial script FUN_004604f0 (`FUN_0040b4a0("One")`) | — |
| `saved games\Tutorial Save.sve` | not written by golf.exe (no writer found) | the tutorial: Esc (key 0x1B) with flag 0x8000 → `FUN_0040b9b0("Tutorial Save.sve",1,0,0)` at 0x41C102 **(guess: a file that ships with the tutorial; it is absent from this install)** |
| `Themes\Championship\*.cse` | not written by golf.exe | "Select Championship Course" mode of the load screen → **FUN_0040b840** (the course loader, §4) |
| `top10.sve` (game root) | **never written by v1.02** (no `_open` for write exists). If it is missing, FUN_0040ad60 fills the table in memory with defaults | **FUN_0040ad60** at startup (§5) |
| `<DAT_00587da0 path, extension replaced>.srf` (+ `…Thumb.jpg` / `…Full.jpg` **(guess)**) | FUN_00432170 → FUN_00431fa0 / FUN_00431ee0. A text `[Course]` card (`Name=`, `Holes=`, `Par=`, `Yards=`, `Type=`, `Theme=`, `Designer=`, `Record=%d by %s`, `Cash=`, `FunRating=`, `SkillRating=`… via fprintf FUN_004a622c) | not part of the save. It is a course-sharing card **(guess)** |

Directory: the hard-coded relative path `saved games\` (0x4c3f4c); `.sve` is appended (0x4c3f44). The Load screen lists `Saved Games\*.sve` (0x4c891c) through FUN_0043d2a0. It keeps a 100-byte name per entry at 0x80b130 and is passed `While_Browsing` as a third argument **(guess: the name to exclude)**.

### CRT functions (all statically linked MSVC6 CRT, low-level I/O)
| addr | function | notes |
|---|---|---|
| FUN_004a5d48 | `_open(path, oflag, pmode)` | save: `0x8301` = `_O_BINARY|_O_CREAT|_O_TRUNC|_O_WRONLY`, pmode 0x80 = `_S_IWRITE`. Load: `0x8000` = `_O_BINARY|_O_RDONLY` |
| FUN_004a583a | `_read(fd, buf, n)` | |
| FUN_004a5b58 | `_write(fd, buf, n)` | |
| FUN_004a5a78 | `_close(fd)` | |
| FUN_004a6057 | `_tell(fd)` (= `_lseek(fd,0,SEEK_CUR)` via FUN_004a95bb) | result unused |
| FUN_004a614d / 004a6268 / 004a63a1 / 004a609f / 004a622c | `fopen` / `fwrite` / `fread` / `fclose` / `fprintf` | used for `.pro`, `.srf` and BMP files, not for `.sve` |
| FUN_004a64b8 | `remove` (DeleteFileA) | the load screen's delete-save button |

The file handle is the global **DAT_00568d08**.

## 2. Save and load functions

* **FUN_0040b4a0(name)**: builds `saved games\<name>.sve` in the scratch buffer DAT_0051a068 and calls `_open(...,0x8301,0x80)`. It then builds the **header string** in the same scratch buffer: `"-" + courseName + ", " + day + " " + Month + " " + year` (FUN_0040daa0(0) appends the course name, FUN_0040d7b0 appends month and year). It writes **exactly 100 bytes** of that buffer, then calls **FUN_0040afa0(0)** and `_close`.
* **FUN_0040afa0(mode)** is the serialiser. It sets DAT_0053e638 = mode (0 = write, 1 = read) and calls **FUN_0040af70(ptr, size)** for each block in the fixed order of §3. FUN_0040af70 is `mode ? _read(fd,ptr,size) : _write(fd,ptr,size)`. The same list does both directions, so the format is symmetric.
* **FUN_0040b9b0(name, applyTheme, headerOnly, championship)**:
  1. Builds the path (`saved games\` or, if `championship`, `Themes\Championship\`) and calls `_open(...,0x8000)`.
  2. Reads 100 bytes into DAT_0051a068 (the header). If `championship`, it reads a second 100-byte header.
  3. If `headerOnly` (the load screen listing), it closes the file and returns.
  4. Otherwise it calls FUN_0040afa0(1) and closes the file.
  5. It fixes up the flags: `flags = (flags & ~0xC) | (oldFlags & 4) | 0x40000000`.
  6. It resets the UI state (DAT_00567afc = 0, DAT_004c2854 = DAT_004c2848 = −1) and rebuilds derived state: FUN_0042f7a0, FUN_0042f340, FUN_0042f2c0 (path, terrain and render resync **(guess)**).
  7. If `applyTheme`, it shows "Loading Game" (unless this is While_Browsing) and calls FUN_00449400(courseType). If the course type changed, it calls FUN_0043dbe0 (theme reload), then FUN_00449520.
* There is no separate "post-load fix-up of pointers", because there are none (§0). Everything else the game needs (Terrain.dll tiles, wall bits 0x5619A0, sprites) is **derived** after load. The port must recompute it the same way.

## 3. Byte layout

### 3.1 Header (0x00–0x63, 100 bytes)
| off | size | type | meaning |
|---|---|---|---|
| 0x00 | 100 | char[100] | NUL-terminated display string, e.g. `-Jurassic Springs GC, 16 August 2001`. It starts with `-`. The course name is string-pool slot 0 if one is set, otherwise the site template name (0x4c1ea8) plus a suffix chosen by hole count: ` MC` Municipal, ` GC` Golf Club, ` CC` Country Club, Championship. The bytes after the NUL are **uninitialised garbage** left over from the scratch buffer DAT_0051a068 (other UI strings). Ignore them, and zero-fill them when writing |

The load screen shows this string. No other part of the header carries meaning.

### 3.2 Body (FUN_0040afa0 order)
Every row is one `FUN_0040af70(&global, size)` call. Offsets are absolute file offsets. All four sample saves were decoded with this table: hole par/yard sums match the totals, the clock matches the header date, and the theme, cash, site names, string pool, buildings and profile names all decode.

| # | off | size | global | type | meaning |
|---|---|---|---|---|---|
| 0 | 0x00064 | 0x4 | `DAT_00822c78` | int | Career ID. It is the same at every site of one career (30133 in three saves, 5165 in While_Browsing). The top-10 code compares it so one career gets only one entry. Where it is set is in the undecompiled region **(guess: random at new career)** |
| 1 | 0x00068 | 0x4 | `DAT_005685f0` | int | Hole count + 1 (the `.srf` writer prints `Holes=` as this − 1; hole slot 0 is unused) |
| 2 | 0x0006C | 0x4 | `DAT_0058d36c` | int | Total yards (sum of hole +0x04) |
| 3 | 0x00070 | 0x4 | `DAT_0059aafc` | int | Total par (sum of hole +0x00) |
| 4 | 0x00074 | 0x1 | `DAT_005a34e0` | u8 | Course type: `&3` indexes {Park, Desert, Tropical, Links} (table 0x4c3078). Also picks the music set via FUN_00449400 |
| 5 | 0x00075 | 0x4 | `DAT_0059ae78` | int | Fun rating |
| 6 | 0x00079 | 0x4 | `DAT_00541cd8` | int | Skill rating (scaled: the `.srf` writer prints it × the double at 0x4ba488) |
| 7 | 0x0007D | 0x4 | `DAT_005a882c` | int | Length-skill rating |
| 8 | 0x00081 | 0x4 | `DAT_0056949c` | int | Accuracy-skill rating |
| 9 | 0x00085 | 0x4 | `DAT_005a636c` | int | Imagination rating |
| 10 | 0x00089 | 0x4 | `DAT_005a9cac` | int | Income/expense counter shown in the finance panel FUN_0045f870 **(guess)** |
| 11 | 0x0008D | 0x4 | `DAT_005a9cb0` | int | The same kind of counter as the row above **(guess)** |
| 12 | 0x00091 | 0x4 | `DAT_005787cc` | int | Counter, reset at new course and incremented in FUN_00427380/FUN_0045a090 **(guess: rounds or visits this period)** |
| 13 | 0x00095 | 0x4 | `DAT_00561258` | int | Counter, reset at new course and summed into the finance panel **(guess)** |
| 14 | 0x00099 | 0x4 | `DAT_00571fd4` | int | **Cash in units of $100** (the `.srf` writer prints `Cash=` as this × 100) |
| 15 | 0x0009D | 0x4 | `DAT_0059bf90` | int | Index of the current site in the site table (0x571fd8 block below) |
| 16 | 0x000A1 | 0x28A0 | `DAT_00575ab0` | 20 × 0x208 | **Holes.** Slot 0 is unused, slots 1..19 are used. +0x00 u8 par (0 = no hole). +0x04 i16 yards. +0x20/+0x24 int round counters used for the fun rating. +0x158 i16 fun points. The rest is unknown |
| 17 | 0x02941 | 0x9C4 | `DAT_005722e8` | u8[50][50] | **Terrain type grid**, index `x*50+y`. Type ids are in terrain.md §2.4. 20 = land not yet bought (off-course) |
| 18 | 0x03305 | 0x9C4 | `DAT_0056988c` | u8[50][50] | **Variation grid** (see terrain.md Tile +0x28). Building tiles hold the building index |
| 19 | 0x03CC9 | 0x1388 | `DAT_0053caf0` | u16[50][50] | **Tile flag grid** (terrain.md calls it "path flags"). Low bits = building type + 1 on building tiles. Also 0x20, 0x80, 0x100, 0x800, 0x1000, 0x4000 (mown?) **(guess)** |
| 20 | 0x05051 | 0x9C4 | `DAT_0053bbac` | i8[50][50] | Per-tile value copied from the terrain-type table byte +5 (0x578375) when a tile is laid. Used by FUN_0042e7e0 (ball lie) **(guess: lie or roughness)** |
| 21 | 0x05A15 | 0xA29 | `DAT_005a4998` | u8[51][51] | **Corner height grid** (vertices). FUN_0040c170(x,y) = `[x*51+y]`. 3 = ground level 0 (see terrain.md elevateCorner) |
| 22 | 0x0643E | 0x169 | `DAT_00838c1c` | i8[19][19] | Coarse 19×19 field that FUN_004674c0 interpolates bilinearly over 32-unit cells **(guess: wind or ambient noise map)** |
| 23 | 0x065A7 | 0x1000 | `DAT_0058bcb8` | 256 × 16 | **Buildings.** +0 i16 type (−1 = free; names at 0x4c26b0, stride 0x14: 0 Pathway … 15 Clubhouse … 19 Scenic Bridge). +2 i16 x. +4 i16 y. +6 u8 rotation (&3). +7 u8. +8 int. +0xC int |
| 24 | 0x075A7 | 0x4 | `DAT_004c284c` | int | Flag, reset at new course |
| 25 | 0x075AB | 0x4 | `DAT_00561250` | int | Value set by FUN_0044fb30 (end of year) **(guess: last year's figure)** |
| 26 | 0x075AF | 0x9C00 | `DAT_005794b8` | 156 × 0x100 | **Golfers on the course.** +0x00/+0x04 int x,y in 1/1024 tile. +0x21 u8 kind. +0xA2 i16 group/slot link (−1 = free; init loop covers 152 slots). +0xB6 i16 profile index (into 0x4d6088 and 0x5849e0). +0xBE i16 arrival sequence. +0xD4/+0xD8 int target x,y. The rest is unknown |
| 27 | 0x111AF | 0x1300 | `DAT_00585850` | 64 × 0x4C | **Staff/walkers** spawned at the clubhouse door (FUN_00402970, which hire code calls with a negative type). +0/+4 int x,y (1/1024 tile). +0x12 u8 in-use. +0x13 i8 type **(guess: employees)** |
| 28 | 0x124AF | 0x4 | `DAT_00834170` | int | **Game clock.** 1024 ticks per month, 8 months per year (March–October). Day = `(t&0x3FF)*30/1024+1`, month = `(t>>10)&7`, year = `2001+(t>>13)`. Autosaves fire at `t&0x3FF` = 0x200 and 0 |
| 29 | 0x124B3 | 0x4 | `DAT_005a6364` | int | Progress/stage counter (FUN_0040e720 switch; 0x11 checked in FUN_0046f550) **(guess: tutorial or career stage)** |
| 30 | 0x124B7 | 0x4 | `DAT_0053a450` | int | Level used in land/tract pricing (`(v+3)*50`) and in a bit shift **(guess: membership level)** |
| 31 | 0x124BB | 0x4 | `DAT_00822c88` | int | **Difficulty** 0..3. It is also stored in top-10 entries |
| 32 | 0x124BF | 0x3E8 | `DAT_005409ac` | i16[500] | Per-year history: skill rating (end-of-year screen FUN_0044cff0) |
| 33 | 0x128A7 | 0x3E8 | `DAT_0051b388` | i16[500] | Per-year history: cash reserves / 100 |
| 34 | 0x12C8F | 0x3E8 | `DAT_0053e63c` | i16[500] | Per-year history: fun rating |
| 35 | 0x13077 | 0x4 | `DAT_004c2850` | int | Fee/score accumulator in FUN_00427380 **(guess)** |
| 36 | 0x1307B | 0x4 | `DAT_0059b730` | int | Counter, reset at new course (FUN_00427380 decrements it, and the value is scaled by 2000/100) **(guess: loan or advertising)** |
| 37 | 0x1307F | 0x4 | `DAT_0059ae7c` | int | Golfer ring-buffer cursor, mod 0x98 (152) |
| 38 | 0x13083 | 0x80 | `DAT_0059dea0` | int[4][8] | Golfer counts per category **(guess)** |
| 39 | 0x13103 | 0x4 | `DAT_0059c08c` | u32 | Bit set of golfer "thought" messages already shown (FUN_00467a00), part 1 |
| 40 | 0x13107 | 0x3E8 | `DAT_00568600` | u16[500] | **Highlight/event log** for the end-of-year screen (FUN_0040c6f0 writes `type|arg`) |
| 41 | 0x134EF | 0x4 | `DAT_00822c70` | u32 | Unlock bit set (FUN_004266b0 / FUN_004722c0) |
| 42 | 0x134F3 | 0x4 | `DAT_005685f8` | u32 | Bit set of rating messages already shown (FUN_0042dea0) |
| 43 | 0x134F7 | 0x4 | `DAT_005a5a24` | int | Golfer ring index, mod 0x98 |
| 44 | 0x134FB | 0x7D0 | `DAT_00584210` | 100 × 20 | Per-year stats rows, indexed by `year%100`, 10 × i16 each |
| 45 | 0x13CCB | 0x4 | `DAT_00543cfc` | u32 | Unlocked-buildings/features bit set (0x3FFF = most) |
| 46 | 0x13CCF | 0x4 | `DAT_004c2e14` | int | Followed/selected golfer for a camera mode (−1 = none) **(guess)** |
| 47 | 0x13CD3 | 0x4 | `DAT_00572cac` | int | Celebrity-visit counter, 0x80 = in progress **(guess)** |
| 48 | 0x13CD7 | 0x4 | `DAT_0059aaf8` | int | Counter, reset at new course, incremented in FUN_004266b0 **(guess: unlocks earned)** |
| 49 | 0x13CDB | 0x1700 | `DAT_0056ae70` | 32 × 0xB8 | Per-something stat records (FUN_004289e0 accumulates +0) **(guess)** |
| 50 | 0x153DB | 0xF0 | `DAT_005422f8` | 10 × 24 | Golfer-mix table: 10 rows of int[6], randomised at new course (FUN_00442180) **(guess: visitor demographics)** |
| 51 | 0x154CB | 0x1C8 | `DAT_0059ae80` | 38 × 12 | Records initialised to −1 (FUN_00456be0) **(guess: tournament or match schedule)** |
| 52 | 0x15693 | 0x4 | `DAT_0059b048` | int | Clock value of the last event in FUN_00466370 |
| 53 | 0x15697 | 0x2400 | `DAT_005736b0` | 256 × 36 | **Balls in flight** (FUN_00409a90 spawns them from the golfer, FUN_00409bf0 moves them): +0/+4 start x,y. +8/+0xC current x,y (+8 = −1 means free). +0x10/+0x14 copied from golfer +0xDC/+0xE0. +0x18 = golfer +0xE4 / 2. +0x1C = golfer +0xE8 / 3. +0x20 clock at launch. **(guess: +0x10.. are height, heading, speed, vertical speed)** |
| 54 | 0x17A97 | 0x3880 | `DAT_0059fc60` | 16 × 0x388 | **Shot-trace recorders** (FUN_004099f0/FUN_00409950): +0 i16 golfer (−1 = free), +2 i16, +4 i16 n (≤31), +8 int x[32], +0x88 int y[32], +0x108 int z[32], +0x188 copy of the golfer record (0x100), then 0x100 unknown |
| 55 | 0x1B317 | 0xE70 | `DAT_005849e0` | 84 × 0x2C | Per-profile stats (indexed by golfer +0xB6): +0 u8 best score, +1 u8, +2 u8 rounds played (&7 used), +3… |
| 56 | 0x1C187 | 0x4 | `DAT_005a59f8` | int | Selected golfer index (−1 = none) |
| 57 | 0x1C18B | 0x20 | `DAT_005a5a04` | u8[32] | Per-golfer-kind small counters / selection (FUN_00407e00, FUN_0040f190) **(guess)** |
| 58 | 0x1C1AB | 0x1770 | `DAT_0056d1b8` | 6000 B | Only cleared in FUN_004011e0. Its users are in the undecompiled region **(unknown)** |
| 59 | 0x1D91B | 0x12 | `DAT_004c2c94` | 18 B | Initialised data. Contains u16 0x4c2c9c = bit set of "golfers met" (FUN_0045f0f0) **(guess)** |
| 60 | 0x1D92D | 0x320 | `DAT_005689e8` | int[200] | Initialised to −1 (FUN_00426670) **(unknown)** |
| 61 | 0x1DC4D | 0x40 | `DAT_005419d0` | 4 × 16 | 4 moving objects: int x,y (1/1024 tile), −1 = inactive (FUN_00430360) **(guess: carts or animals)** |
| 62 | 0x1DC8D | 0xA00 | `DAT_00572cb0` | 128 × 20 | Ambient sprites spawned at a tile centre: +0/+4 x,y, +8 int −20, +0xC i16 −1, +0xE i16 0x10C, +0x11 u8 rand(4), +0x12 u8 type (−1 = free) (FUN_00405970) **(guess: wildlife)** |
| 63 | 0x1E68D | 0xA640 | `DAT_004d6088` | 76 × 0x230 | **Golfer profiles** (personalities). +0x00 char[16] title (e.g. "Golf Pro"), +0x10 char[] name (profile 0 = the player's golf pro, "Gary Golf"). The same 0x230 record is the first part of a `.pro` file (FUN_00437910 writes, FUN_00437fa0 reads). In memory the array is 84 entries (0xB7C0 bytes); v1.0 saves only 76 of them |
| 64 | 0x28CCD | 0x19A28 | `DAT_00543d10` | 84 × 0x4E2 | Per-profile 25×50-byte table (rows of 50, FUN_004385d0). It is the second part of a `.pro` file **(guess: per-profile hole memories or comments)** |
| 65 | 0x426F5 | 0x4 | `DAT_00567b04` | int | Tournament purse / prize money (FUN_0045a090; 1000/100 is added to cash) |
| 66 | 0x426F9 | 0x9C4 | `DAT_005a6378` | u8[50][50] | Positive golfer reactions per tile (heat map, FUN_00467a00 → FUN_004616f0 ×20000) |
| 67 | 0x430BD | 0x9C4 | `DAT_0056c7e4` | u8[50][50] | Negative golfer reactions per tile (×−40000) |
| 68 | 0x43A81 | 0x9C4 | `DAT_0053ea24` | u8[50][50] | Foot-traffic counter per tile (FUN_004289e0; 0xFF = saturated) |
| 69 | 0x44445 | 0x9C4 | `DAT_00542414` | u8[50][50] | **Natural terrain of the whole site.** FUN_00470a60 generates it. Buying a tract copies it into 0x5722e8 where that grid is 20 (FUN_004587a0) |
| 70 | 0x44E09 | 0x4 | `DAT_0059e7b8` | u32 | **Game-state flags.** Notable bits: 0x4 (kept from before the load), 0x8 (cleared on load), 0x2000, 0x8000 tutorial, 0x200000 = "profile snapshot" (adds a block), 0x1000000, 0x4000000 (milestones suppressed; cleared before QuitSave), 0x40000000 = v1.02 extra profiles present |
| 71 | 0x44E0D | 0x420 | `004c1578` | 22 × 0x30 | **Milestones.** The exe data holds the 22 names. New-game init zeroes the whole block, so names are not saved meaningfully. +0x28 int clock when achieved (0 = not yet), +0x2C int site index (FUN_0046e7b0) |
| 72 | 0x4522D | 0x30E | `DAT_00571fd8` | 17 × 0x2E | **Career sites** (map screen FUN_0046f2b0). +0x00 char[24] region ("San Diego"…). +0x18/+0x1A i16 map x,y. +0x1C u8 site template (static table 0x4c1ea8, stride 0x82: name at +1 e.g. "Ocean's Edge"). +0x1D u8 price/requirement (0xFA once taken). +0x1E u8. +0x1F u8 course type 0..3. +0x20 u8 challenge kind. +0x22 i8 year played (−1 = never; display +2001). +0x23 u8 result. The tail is unknown. 16 used |
| 73 | 0x4553B | 0x3E8 | `DAT_00520640` | i16[500] | Per-year history: membership |
| 74 | 0x45923 | 0x64 | `DAT_00567328` | char[100] | **Theme name** (e.g. "Standard", folder under Themes\) |
| 75 | 0x45987 | 0x4 | `DAT_00571d38` | u32 | Bit set of golfer thought messages already shown, part 2 |
| 76 | 0x4598B | 0x1002 | `DAT_0056fcb0` | char[0x1002] | **String pool** heap (NUL-terminated strings) |
| 77 | 0x4698D | 0x100 | `DAT_0059d81c` | i16[128] | String pool: offset of string k in the heap (−1 = empty). Slot 0 = custom course name (FUN_0040daa0). Slot 0x14.. = names (e.g. course-record holder) |
| 78 | 0x46A8D | 0x100 | `DAT_005a46b8` | i16[128] | String pool: per-slot length/size bookkeeping (FUN_0045b8b0) |
| 79 | 0x46B8D | 0x4 | `DAT_0056a51c` | int | Golfer arrival sequence counter |
| 80 | 0x46B91 | 0x4 | `DAT_005a6374` | int | End-of-year screen page counter |
| 81 | 0x46B95 | 0x28 | `DAT_0056a524` | int[10] | **Course record**: [0] score (`Record=%d`). The other values are unknown ([1] = 23 in AutoSave1). The holder's name is string-pool slot 0x14 |
| 82 | 0x46BBD | 0x4 | `DAT_005a47e0` | int | Mode bits (FUN_00424120 tests &2, &4) |
| 83 | 0x46BC1 | 0x9C4 | `DAT_005830b8` | u8[50][50] | **Queued terrain edits** ("blueprint") per tile, 0xFF = none (FUN_0040a130/FUN_0040a4e0) **(guess)** |
| 84 | 0x47585 | 0x9C4 | `DAT_0059c090` | i8[50][50] | Cost or refund of the queued edit per tile |
| 85 | 0x47F49 | 0x4 | `DAT_005a5a00` | u32 | **Options** bit set (Options dialog FUN_00432560): &1, &2, &4, &8, &0x20 toggles |

Base layout ends at **0x47F4D = 294,733**.

### 3.3 Optional trailing blocks (in this order, after `DAT_005a5a00`)
| condition (in the flags just read at 0x44E09) | global | size | meaning |
|---|---|---|---|
| `flags & 0x40000000` (**v1.02 only**) | `DAT_004e06c8` | 0x8C0 | profiles 76..79 (= 0x4d6088 + 76·0x230). The v1.02 patch extends the saved profile range by 4. FUN_0040f190 writes "Golf Pro" into slot 76 |
| `flags & 0x200000` | `DAT_0058f338` | 0xB7C0 | snapshot copy of all 84 profiles, taken by FUN_0046c970 (FUN_0046d0c0 restores it) **(guess: during a tournament or celebrity event the profile table is temporarily replaced)** |
| `flags & 0x200000` | `DAT_00520a28` | 0x19A28 | snapshot copy of the 84 × 0x4E2 profile tables |

### 3.4 Sample values (from the user's saves)
| file | header | clock → date | holes (DAT_005685f0−1) / par / yards | type | cash | flags |
|---|---|---|---|---|---|---|
| &AutoSave1 | -Jurassic Springs GC, 16 August 2001 | 5632 → 16 Aug 2001 | 6 / 20 / 1261 | Park | $1,000,000 | 0x01442000 |
| &AutoSave2 | -Jurassic Springs GC, 1 August 2001 | 5120 → 1 Aug 2001 | 6 / 20 / 1261 | Park | $1,000,000 | 0x01402000 |
| &QuitSave | -Thistle Runes MC, 1 May 2001 | 2059 → 1 May 2001 | 2 / 6 / 416 | Links | $1,000,000 | 0x01040000 |
| While_Browsing | -Ocean's Edge MC, 9 March 2001 | 305 → 9 Mar 2001 | 0 / 0 / 0 | Park | $50,000 | 0 |

Theme = "Standard" in all four. AutoSave1 has 7 buildings (Clubhouse, Resort Hotel, Landmark ×3, Cart Garage, Swim Club) and 19 active golfers. Profile names start "Gary Golf", "Jim", "Clarence"…; the course record is 18. The `.sve` saves themselves contain no flags with the optional blocks.

## 4. Championship course files (`Themes\Championship\*.cse`): FUN_0040b840
These use the same primitives and a **shorter list, FUN_0040bbf0**:
1. 100-byte header, then a second 100-byte header (the course info line).
2. 57 blocks, 95,569 bytes in total. All of them are also in the `.sve` list, but the order is different: for example, `DAT_0056a524` comes right after the cash/site scalars, and `DAT_00585850` comes near the end. The blocks left out are:
   * golfers, balls and shot traces
   * per-year histories and stats
   * per-profile tables (0x4E2) and stats
   * heat maps and the natural-terrain grid
   * milestones
   * the queued-edit grids
   * options
   * the optional blocks

   The first 76 profiles (`DAT_004d6088`) are included. `.cse` is therefore **not** a prefix of `.sve`.
3. 8 trailing bytes, read into a stack buffer and ignored.

The game only reads `.cse` (there is no writer). This install ships no `.cse` files: `Themes\Championship\` holds only `Gary Golf.pro`. FUN_0040b840(mode) has four modes:
* mode 2 stops after the first header
* mode 3 stops after the second header
* mode 0 shows "Loading Course" and resets the game (FUN_00480c80, FUN_00442180(0), FUN_004315e0) before it reads
* mode 1 reads without the reset (this is the browser's hover preview)

After a mode 0 load it calls FUN_0042f7a0.

## 5. `top10.sve` (1560 bytes = 10 × 156): the high-score table
Read whole into **DAT_00541ce0** by FUN_0040ad60 (`_open(...,0x8000)`, `_read 0x618`). If the file is missing (FindFirstFileA fails), FUN_0040ad60 generates 10 default rows in memory. **No code path in v1.02 or v1.0 writes it back**, so new scores persist only for the session **(guess: the shipped file was produced by a developer build)**. The user's file is exactly the generated default pattern, with random `x.y. dye` names and "Harbour Lights GC".

Entry (0x9C bytes, stride 0x27 ints). Field meanings come from the insert function FUN_004732d0 and the display function FUN_00473470:
| off | size | type | meaning |
|---|---|---|---|
| 0x00 | 64 | char[64] | designer = profile 0 name (`s_Gary_Golf_004d6098`); defaults `"?.?. dye"` with random letters |
| 0x40 | 64 | char[64] | course name (FUN_0040daa0(0)) |
| 0x80 | 4 | int | fun rating (DAT_0059ae78); defaults 900+rand(100), each row −100 |
| 0x84 | 4 | int | skill rating (DAT_00541cd8); defaults 810+rand(90), each row −90 |
| 0x88 | 4 | int | cash / $100 (DAT_00571fd4); defaults 7200+rand(800), each row −800 |
| 0x8C | 10 | — | zero |
| 0x96 | 2 | i16 | difficulty (DAT_00822c88); defaults `row/3` → 3,2,2,2,1,1,1,0,0,0 |
| 0x98 | 4 | int | career ID (DAT_00822c78); −1 in defaults. One entry per career |

Rank score = `(cash/10 + skill + fun) × (difficulty + 1)`, sorted descending. Decoded row 0 of the user's file: `b.l. dye`, Harbour Lights GC, 949 / 877 / 7459, difficulty 3.

## 6. What a browser port needs
* **Endianness and packing:** little-endian, MSVC packing, no alignment between blocks. Odd offsets are normal (the 1-byte `DAT_005a34e0` at 0x74 shifts everything after it by one byte). Read with `DataView` at absolute offsets and do **not** map the file onto aligned typed arrays. Copy each block into its own buffer, or keep the game state as one linear "save image" laid out exactly like §3.2 and index into it.
* **Simplest faithful approach:** if the port keeps the original globals as a byte arena (for example a recompiled or emulated golf.exe with the same data segment), save = concatenate the 86 ranges (+ optional ones by flag) and load = scatter them back. Otherwise, define TypeScript/C structs per block from §3.2. Most blocks are still only partly decoded, so round-trip the unknown bytes verbatim.
* **No pointers or relocation.** All references are indices: golfer → profile (`+0xB6`), building → type table, sites → static template table 0x4c1ea8 (exe data, not saved), string pool offsets. The port must ship the static tables (site templates, building types, terrain type table 0x578370, milestone names 0x4c1578, month names) from the exe or data files.
* **Derived state to rebuild after load** (mirroring FUN_0042f7a0/0042f340/0042f2c0 and the theme reload FUN_0043dbe0/FUN_00449400):
  * Terrain.dll tiles (types, variations, corner heights; see terrain.md §2.3)
  * wall bits 0x5619A0
  * path render state
  * music and ambience by course type
  * hole and course ratings
* **Flags fix-up on load** (`&~0xC | old&4 | 0x40000000`) and the **v1.02 extra block** if the flag is set. To stay compatible with both versions, accept 294,733 / 296,973 / 446,773 / 449,013-byte files, decided by the flags at 0x44E09.
* **Header garbage:** do not treat bytes after the NUL as data. Zero them on write.
* **Autosave cadence** depends only on the clock (`t&0x3FF` ∈ {0, 0x200}), so it is deterministic.
* **Storage:** names are DOS paths with `&` and spaces. In the browser, map `saved games\` to an IndexedDB/OPFS directory. `top10.sve` is read-only in the original; decide whether the port should persist it (a behaviour change).
* **RNG:** saves do not contain the RNG state (FUN_0045c1e0's seed is not in the list), so replays after a load diverge from the original **(guess: unless the seed is one of the unknown scalars)**.

## 7. Unknowns / next steps
* Field-level layout of the golfer (0x100), hole (0x208), staff (0x4C), profile (0x230) and per-profile 0x4E2 records is only partly mapped. The main user, the game loop at 0x40F5C0–0x421B60, was not decompiled when this was written. Re-run against the recovered decompile.
* Meanings of: `DAT_0056d1b8` (6000 B), `DAT_005689e8` (200 ints), `DAT_0056ae70` (32 × 0xB8), `DAT_0059ae80` (38 × 12), `DAT_00838c1c` (19×19), `DAT_004c2c94` (18 B), and several scalar counters (marked **(guess)**).
* Where the career ID `DAT_00822c78` is generated.
* The tail bytes of the career-site record (+0x24..+0x2D). The current site holds 0x10270000-like values, which look like cash at an odd offset **(guess)**.
* Whether `Tutorial Save.sve` and any `.cse` files shipped on the CD (absent from this install).
