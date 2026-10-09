# Terrain.dll — the SimGolf 3D course renderer

Sources (raw decompiles kept outside the repo, never commit them): Ghidra headless decompile of `Terrain.dll` (project SimGolfDeps, `-noanalysis -readOnly`) →
`~/ghidra_work/terrain/out/decomp_all.c`. The DLL's own code is split into `out/user.c` (0x10001000–0x10015780) and
`out/user2.c` (0x10037930–0x10038f60). golf.exe's side is in `~/ghidra_work/out_v102/decomp_all.c`, and the wrapper
region is in `out/golf_terrain_wrap.c`. Disassembly is in `out/golf_disasm*.txt` and xrefs in `out/golf_refs*.txt`.
Constants were read straight from the PE using `scripts/peread.py`.

**Build facts.** Terrain.dll is an MSVC6 **Debug** build: it has `0xCCCCCCCC` stack fill, `__chkesp`, the debug heap,
asserts that point at `C:\Projects\3DTerrainLowPoly\Terrain.cpp`, and incremental-link thunks (each export is a
`jmp`; Ghidra decompiled through it). All code is plain C++ with no virtuals. There are 38 exports in total. golf.exe
imports 26 of them. golf.exe *inlines* `tileAt`, `getElevation`, `getWall` and `getType` from the header, so those
methods read Terrain.dll's memory directly.

Confidence: statements are read from the code unless they are marked **(guess)**.

---

## 0. Object layout and globals

### `Terrain` object (0x164AD0 bytes; a singleton allocated by `getInstance`)
| off | type | meaning |
|---|---|---|
| +0x000 | HGLRC | GL context (0 means not initialised) |
| +0x004 | float[4] | light-0 ambient used by `changeLighting` (initial value 0.15,0.15,0.2,1) |
| +0x014 | int | grid width W = 50 |
| +0x018 | int | grid height H = 50 |
| +0x01C | int | zoom shift: 0 = 1:1, 1 = ½×, 2 = ¼× (set by `setZoomLevel`) |
| +0x020 / +0x024 | int | screen w / h (from `initSystem`) |
| +0x028 | bool | `flip` = the 4th argument of `initSystem` (golf passes "Windows major version > 4") |
| +0x029 | bool | textures loaded |
| +0x02C | `{char name[20]; int numVariants;}[37]` | texture-set table, indexed by terrain type (see §2.4) |
| +0x3A4 | `Tile[2500]` | the grid, stride 0x248 |
| +0x164AC4 | `std::list<Tile*>` | the sorted render queue that `localRender`, `stripRender` and `pathUpdateRender` use |

### Module globals
| addr | meaning |
|---|---|
| 0x100B28C8 | `float pos[22500][3]`: vertex positions (glVertexPointer). 150×150 = 3×3 vertices per tile, **not shared** between tiles. |
| 0x10070A18 | `float nrm[22500][3]`: per-vertex normals (glNormalPointer) |
| 0x100687F8 | `GLuint tex[37][25][9]`: texture names indexed [type][variant][frame] |
| 0x10070A0C | course type (0 Parkland, 1 Desert, 2 Tropical, 3 Links) |
| 0x10070A10 | camera pitch in degrees (set by `initSystem` from the resolution) |
| 0x10070A14 | rotation index 0..3 for the current draw (0°, 90°, 180°, else = 270/−90) |
| 0x10063E54 | height step in world units = **15.0** (this is what `setSplineHeight` overwrites) |
| 0x10063E4C/50 | tile size = 100.0 world units |
| 0x10106B48 | `int collar[n]` copied by `passCollarInfo` |
| 0x10106B4C | the singleton pointer |

---

## 1. Per-method reference

"Imp" means golf.exe imports the method. Golf call sites are counted in golf_nocd. Every golf call goes through the
global `DAT_00820ed0` (Terrain*), which a static initializer sets from `getInstance()` at 0x449170. Rotation in golf is
`[0x5685F4]` ∈ {0,2,4,6}. golf converts it to the float angle {0, 90, 180, −90}.

| Method (params named) | Imp | What it does | GL used | How golf calls it |
|---|---|---|---|---|
| `static Terrain* getInstance()` | ✔ | Allocates 0x164AD0 bytes once. The constructor (FUN_10002ae0, W=H=50) builds 2500 Tiles, sets ambient to (0.15,0.15,0.2,1), fills the texture-name table and calls the texture loader (FUN_100076e0). **(guess)** The load fails harmlessly because there is no context yet. It does not reset the 0x29 flag. | none directly | 1 call, from a static ctor (0x449170) at program start |
| `~Terrain()` | ✔ | Frees the collar array, deletes textures (glDeleteTextures, 9 at a time), destroys the list and the tile array | glDeleteTextures | 1 call, at shutdown (FUN_00449860, after closeSystem) |
| `void initSystem(int w, int h, HDC hdc, bool flip)` | ✔ | If there is no context yet: ChoosePixelFormat/SetPixelFormat with the PFD in §3.7, then wglCreateContext and wglMakeCurrent on `hdc`. Stores `flip`. Sets the fixed state (FUN_100033e0, §4), loads `<course>Lighting.txt` (§3.4), loads all textures (§3.5) and calls `resize(w,h)`. Stores w and h. Sets pitch to 38.682° (800×600), 40.5416° (1024×768) or 40.8322° (1280×1024). Any other size leaves pitch at 0. | wgl*, glHint, glEnable, glFrontFace, glEnableClientState, glLight*, glMaterial*, textures, glViewport, glOrtho | 1 site (FUN_00449790), lazily on the first terrain frame (flag 0x4D2044 starts at 1). Arguments: (screenW, screenH from 0x822C8C/90 = 800,600 by default, **GetDC of the view back-buffer surface** `*(0x519CD8)` vtbl+0x28, `GetVersionEx().dwMajorVersion > 4`). |
| `void initTerrain()` | ✔ | Builds the vertex grid (FUN_1000c560: flat, y = 0, normals (0,1,0)). Initialises every tile (FUN_1000c2c0, then FUN_10002060 builds the 8 triangles) and links the 4 neighbours. Then glVertexPointer/glNormalPointer on the global arrays. | glVertexPointer, glNormalPointer | 1 call, right after initSystem; FUN_00449540 then pushes the whole golf map in |
| `void closeSystem()` | ✔ | wglMakeCurrent(0,0), wglDeleteContext, glDeleteTextures | wgl, glDeleteTextures | 1 call, at shutdown |
| `void resetTerrain()` | ✔ | Re-initialises every Tile (x, y, type 4, elevation 0, walls and path cleared), then does the same as initTerrain | arrays | 1 call (FUN_00449520): on course load / new course (FUN_0040b9b0 and the main loop 0x41050e), followed by a full map sync |
| `void resize(int w, int h)` | ✗ | glViewport(0,0,w,h). Projection = **glOrtho**(−X/2, X/2, −Y/2, Y/2, near = 5000, far = −5000), with bottom and top swapped when `flip` is set. X×Y = 1767×1325 (800×600), 1810×1303 (1024×768), 1740×1392 (1280×1024); any other size uses X×Y = w×h. Then MODELVIEW identity. | glViewport, glMatrixMode, glLoadIdentity, glOrtho | only internal (initSystem, setZoomLevel(4)) |
| `void loadNewCourseType(int courseType)` | ✔ | If the course type changed: reload the lighting file, delete all textures, reload textures from `Data/Textures/<Parkland\|Desert\|Tropical\|Links>/` | lighting and textures | 1 site (FUN_00449400). Called on course load and on course-type change, always right after `passCollarInfo` |
| `void changeLighting(int brighter)` | ✗ | Dev hotkey: ambient (+0x04) ±0.1, then sets light 0 to hard-coded values (diffuse 0.9,0.9,0.9; specular 1,1,0.9) | glLightfv, glEnable(LIGHT0) | unused |
| `void setZoomLevel(int zoom)` | ✔ | Re-does the projection from the 1:1 extents. zoom=1 means extents ×4 (shift 2). zoom=2 means ×2 (shift 1). zoom=4 calls resize() (shift 0). Other values keep the current shift. Uses the same flip rule and the same near/far as resize. | glMatrixMode, glOrtho | 1 site, from the per-frame driver when golf's zoom `[0x4C2844]` ∈ {1,2,4} changes. Always followed by a full `render`. |
| `void passCollarInfo(int* const groups, int n)` | ✔ | Copies `n` ints into a new array (0x10106B48). This is the per-terrain-type "collar" (blend-group) id. Two neighbouring tiles whose types share a group get no edge transition (FUN_10015650). | none | 2 sites. golf passes **23** values: byte +4 of its 0x30-byte terrain-type records at 0x578372. On Desert some values are forced to 4. Called at init and on course-type change. |
| `Tile* tileAt(int x, int y)` | ✗ (inlined) | Returns `&tiles[y*W + x]`, or NULL when out of range | none | inlined about 20× in golf |
| `Tile* tileHit(int sx, int sy)` | ✗ | **Debug stub.** Prints coordinates with OutputDebugString and uses a hard-coded 800×600 / 64×40 diamond formula. Ghidra shows no proper return value. This is not real picking. | none | unused (golf does its own picking) |
| `int getElevation(Tile*, int corner)` | ✗ (inlined) | Returns `((int*)tile)[corner]` | none | golf inlines it as `*(int*)(tile+4+8k)` (corners 1,3,5,7) |
| `int getType(Tile*)` | ✗ (inlined) | Returns tile+0x24 | none | inlined |
| `int getVariation(Tile*)` | ✔ | Returns the **low byte** of tile+0x28 (golf's "variation"/subtype) | none | 2 sites, in the type sync (FUN_00449470 full resync, FUN_00449f00 per tile) |
| `bool getWall(Tile*, int dir)` | ✗ (inlined) | Returns the byte at tile+0x234+dir | none | inlined |
| `bool hasPath(Tile*)` | ✔ | Returns the byte at tile+0x208 | none | 2 sites (path sync FUN_0044a410) |
| `bool hasConnectedPath(int x, int y)` | ✔ | Returns the byte at tileAt(x,y)+0x209, the path "variant" flag. The name is misleading. | none | 1 site (path sync) |
| `void setType(Tile*, int type, int variation)` | ✔ | Picks the texture variant `+0x240 = rand() % numVariants[type]` (**it uses MSVC rand()**). Sets +0x28 = variation. Then FUN_10014020: undoes the geometry of the old type (bunker 7 → raise centre by 13; mound 6 → lower by 20), stores the type, applies the new geometry (type 7: centre −13; type 6: centre +20, also bending matching neighbours) and recomputes the per-triangle edge-transition textures for itself and its 8 neighbours (FUN_10013500/13670) | none (data only) | 3 sites: full sync (2500 calls on load), full resync FUN_00449470 (only tiles that differ), and per-tile incremental sync FUN_00449f00 |
| `void setWall(Tile*, int dir, int wallType, bool on)` | ✔ | +0x234+dir = on, +0x210+4·dir = wallType (dir ∈ {0,2,4,6}) | none | 2 sites (full sync, incremental FUN_0044a380). golf's wallType comes from byte 0 of its terrain-type record. |
| `void layPath(Tile*, int on, int variant)` | ✔ | +0x208 = on, +0x209 = variant != 0. +0x20A ("draw overlay") = on && type ∉ {0,1,2,3,7,9,22} | none | 3 sites. Golf: on = path bit 0x20 of `0x53CAF0[x*50+y]`, variant = bit 0x40 |
| `void updatePath(int x, int y, int variant)` | ✔ | tileAt(x,y)+0x209 = variant | none | 2 sites. When one path tile changes variant, golf loops over all 2500 tiles and then sets the "path redraw" flag. |
| `void elevateCorner(Tile*, int corner)` | ✔ | corner ∈ {1,3,5,7}. ++elev[corner]. Writes vertex heights: corner vertex = (n+1)·15. The two adjacent edge-midpoint vertices = average with their other corner. Centre = max(centre, average with the opposite corner). Marks the tile and its 8 neighbours "normals dirty" (+0x244). This affects only this tile's own 9 vertices. | none | 3 sites. Golf sync: `while (tile.elev[c] < golfHeight(x,y,c) − 3) elevateCorner`. Golf height 3 equals Terrain level 0. |
| `void lowerCorner(Tile*, int corner)` | ✔ | The reverse of elevateCorner. It also re-applies the −13 bunker depression to matching type-7 neighbours. | none | 2 sites (used when none of the 8 neighbours is "special" per golf FUN_0040bf60) |
| `void lowerEdgeCorner(Tile*, int corner, Tile* centre, float rot)` | ✔ | When the 3 relevant neighbours (which depend on rotation) are void (type 20) or off-grid, it **paints over the tile's current screen footprint**: light 0 off, light 1 with all-zero colours, then the tile is drawn 5× at ±10-unit offsets (giving a near-black colour), then light 0 is restored. After that it calls lowerCorner. This exists because nothing redraws behind an edge tile once it is lowered. | full tile draw, glLight(1), glEnable/Disable(LIGHT0/1), glTranslatef | 2 sites (used instead of lowerCorner when a neighbour is "special") |
| `void calcNormals(Tile*)` | ✔ | If the tile is dirty: recomputes the face normals of its triangles and its neighbours' triangles, averages them into its 9 vertex normals and normalises | none | 1 site: full sync (all 2500 tiles) |
| `void calcAllNormals(Tile* centre)` | ✔ | Runs calcNormals over tiles within ±(13 << zoomShift) of `centre` | none | 1 site. Per frame, but only after an elevation, bunker or mound change (flag 0x820F2C). |
| `bool render(Tile* centre, float rot)` | ✔ | **Full redraw** of the visible window: LoadIdentity, Push, Rotate(pitch about X), Rotate(45 + rot about Y), Translate((25−x)·100, 0, (25−y)·100). Tiles within ±(16 << shift) of the centre are drawn in **back-to-front order for the rotation**. Void tiles (type 20) and off-screen tiles (FUN_10006850 cull) are skipped. Then Pop and glFlush. With `centre == NULL` it draws tiles 10..39 instead. The return value is garbage. It **does not clear** anything. | everything in §3.6 | 4 sites, all in the per-frame driver FUN_004498a0: on rotation change, on zoom change, on a scroll of ≥ 3 tiles, and on the first frame after init. Golf first clears the view surface (FUN_004808c0 on view 0x519A60). |
| `void localRender(Tile* dirty, Tile* centre, float rot)` | ✔ | Same camera setup. Puts the 5×5 tiles around `dirty` into the sorted queue (FUN_100381a0), recursively adds tiles in front of them that have a wall facing the camera (FUN_10007380), then draws the queue and empties it. It overdraws on top of the existing image. | as render | 1 site. Per frame, once for each tile in golf's dirty list (0x820F1C). Golf builds that list in FUN_0044a5b0 by syncing type, elevation, walls and paths for the tiles around the view. |
| `void stripRender(Tile* centre, int dir, float rot)` | ✔ | After a small scroll: draws only the newly exposed strip of tiles. dir 2/4/6/8 are the 4 screen edges. dir 1/3/5/7 recurse into two edges. Strip widths scale with `<< zoomShift`. The output is queued, then drawn. | as render | 1 site (FUN_0044a6e0), for scrolls of fewer than 3 tiles. Golf then blits the cached old terrain, shifted, around the strip. |
| `void pathUpdateRender(Tile* centre, float rot)` | ✔ | Redraws (through the queue, together with wall dependants) every visible tile that has a path, within radius 15 << shift | as render | 1 site. Per frame, only after the path variant flipped (flag 0x820F2B). |
| `void renderTile(int type, int sx, int sy, int num, int den)` | ✗ | UI preview: one flat 100×100 quad (2 triangles) showing frame 0 of variant 0 of `type`, at screen position ((sx−432)·2.2097, (sy−300)·2.2097), scaled num/den, with lighting off. Asserts type ≤ 30. | immediate mode, glScalef | unused |
| `void drawLine(int x1,int y1,int x2,int y2,int rgb555,int width,int alpha10)` | ✔ | 2D overlay line: Push projection, glOrtho(0,w,h,0,−1,1) (y-down; (0,w,0,h) when `flip`), Push modelview. Lighting and texture off, blend on with SRC_ALPHA/ONE_MINUS_SRC_ALPHA. glLineWidth(width). Colour = RGB555 expanded to 8 bits, alpha = alpha10/10. GL_LINES. Then restore and glFlush. | glMatrixMode, glOrtho, glBlendFunc, glLineWidth, glColor4f, glBegin(GL_LINES), glVertex2i | 1 wrapper (FUN_004493d0) with **15 call sites** (8 in the main loop FUN_0040f5c0). Typical arguments: colour 0x80000000 (black), width 2 or 10, alpha 5 or 9. These are probably the shot/aim and measurement lines. **(guess)** |
| `void drawCircle(Tile*, float)` | ✗ | Empty stub | none | unused |
| `void drawBezierSpline(9 ints)` | ✗ | 2D quadratic Bezier (3 control points) drawn as GL_LINE_STRIP. Same 2D setup as drawLine. | as drawLine, glVertex2fv | unused |
| `void drawCardinalSpline(11 ints)` | ✗ | 2D Hermite/cardinal curve (p0, p1 plus tangents), step 0.01, as a line strip. Ignores `flip` (a bug). | as drawLine | unused |
| `void setSplineHeight(float)` | ✗ | Sets the global at 0x10063E54. That global is really the **elevation step** (15.0) used by elevate/lowerCorner. The name is misleading. | none | unused |

**Not imported by golf (12):** tileAt, tileHit, getElevation, getType, getWall, changeLighting, resize, renderTile,
drawCircle, drawBezierSpline, drawCardinalSpline, setSplineHeight. golf has its own *inline* copies of tileAt,
getElevation, getWall and getType at 0x4490D0–0x449150. The other 8 are never used.

### The golf per-frame driver: FUN_004498a0(cx, cy, zoom, rot)
It is called every frame from the main loop FUN_0040f5c0 (0x4104D3) as `(cx=[0x4C2BA0], cy=[0x4C2BA4], zoom=[0x4C2844], rot=[0x5685F4])`. It is called again right after a course reload (0x41052D).
1. On the first call it runs initSystem, initTerrain and a full sync, then does a full `render`.
2. If rotation changed, it clears the view and does a full `render`. If zoom changed, it calls `setZoomLevel` and then does a full `render`. If the camera moved 1–2 tiles, it calls `stripRender` and a shifted blit. A bigger move gets a full `render`.
3. Otherwise ("idle" frame) it does the following:
   - It restores the clean terrain from the cache surface into the view back buffer.
   - If golf's map-dirty bit (`0x5619A0 & 2`) is clear, it syncs the map (FUN_0044a5b0) and sets the bit.
   - It runs `calcAllNormals` if elevation changed.
   - It runs `localRender` once for each dirty tile, and `pathUpdateRender` if the path variant changed.
4. Finally it copies the view back buffer into the cache surface.

Surfaces involved (from the disassembly):
- `0x519CD4` is the view's back-buffer wrapper (= view object 0x519A60 + 0x274). Its inner surface `*(0x519CD8)` provides the HDC passed to initSystem.
- `*(0x4C1574)` is a second surface that acts as the terrain cache.
- The blit helper FUN_00475c60/FUN_00475d00 is thiscall(ECX = src) with arg1 = dest. **(guess at direction, from usage)**

So **GL renders straight into the view back buffer, and golf keeps a separate clean copy.** Sprites are then drawn over the
back buffer each frame by golf/graphsy.

---

## 2. Tile struct and grid

### 2.1 `Tile` (0x248 bytes)
| off | type | meaning |
|---|---|---|
| +0x000 | int elev[8] | Indices 1,3,5,7 are the 4 **corner height counters** (units of 15 world units). Even indices are unused, but the ctor zeroes 1,3,5,7. Golf reads `+4+8k`. FUN_10015500 = max(elev[1,3,5,7]) is used for culling. **(guess)** Which corner is N, E, S or W is not established. |
| +0x020 | int | unused **(guess)** |
| +0x024 | int type | Golf terrain type id (it is the same id space: golf passes its type grid 0x5722E8 straight through). The ctor default is 4 (Rough). |
| +0x028 | int variation | Golf subtype. Uses: &3 = bunker orientation (type 7); 0x80 = tricky green (type 1); 1/2 = mid/deep water (type 17); 0x40..0x47 on type 22 = rough-textured building lot. `getVariation` returns the low byte. |
| +0x02C / +0x030 | int x, y | grid position |
| +0x034 | Tile* nbr[4] | [0]=(x,y−1), [1]=(x,y+1), [2]=(x−1,y), [3]=(x+1,y) |
| +0x044 | int triCount | always 8 |
| +0x048 | Tri tri[8] (0x38 each) | `{int v[3]; int uvSel[3][2]; GLuint tex; uint8 frame; pad; float faceN[3]}`. `v` indexes the global arrays. `uvSel` = (row, col) into a 3×3×4-rotation UV table (0x10063CA0). Tri 0's v[2] (+0x50) is the **centre vertex**. **(guess)** The 8 triangles fan around the centre, with 2 per quadrant. |
| +0x208 | u8 hasPath | |
| +0x209 | u8 pathVariant | selects the "X" path texture set (golf path bit 0x40) |
| +0x20A | u8 drawPathOverlay | |
| +0x20B..0x20E | u8[4] | cleared, unused |
| +0x210 | int wallType[8] | index = dir 0,2,4,6. Value 1 = cliff texture (CliffTest.bmp). Anything else = retaining wall (RetainWallA.bmp). |
| +0x234 | u8 wall[8] | wall present on edge dir 0,2,4,6 |
| +0x240 | int texVariant | random choice of texture set A..E (or 0..24 for Tee) |
| +0x244 | u8 normalsDirty | |

### 2.2 Grid and geometry
- 50×50 tiles. `tileAt(x,y) = tiles[y*50 + x]`. The world is 5000×5000 units with the y axis up.
- Tile (i,j) spans X ∈ [100i − 2500, 100i − 2400] and Z ∈ [100j − 2500, …]. It has a 3×3 vertex lattice at 50-unit spacing.
  Vertex index = `subX + subZ*150 + j*450 + i*3`.
- Vertices are duplicated at tile borders. That allows hard discontinuities, so cliffs and walls are separate quads.
- Heights: a corner at counter n sits at n·15 units. Bunkers (type 7) drop the centre by 13 and mounds (type 6) raise it by 20.
- Rendering translates so that tile (cx,cy)'s min corner is at the origin. **(guess)** That point is the screen centre.

### 2.3 Picking
`tileHit` is a non-functional debug stub (see §1). golf.exe does its own picking. golf also keeps its own **logical** map:
- types `0x5722E8[50][50]`
- variations `0x56988C`
- wall bits `0x5619A0`
- path flags `0x53CAF0`
- corner heights via FUN_0040bfe0/FUN_00449310

Terrain's tiles are a **render mirror** of that map. Golf reads back Terrain's fields only to diff them during the sync.
The only Terrain layout golf depends on is: +0x14/+0x18/+0x3A4/stride 0x248, tile +0x00..0x1C, +0x24, +0x234.

### 2.4 Type → texture set (name table in the ctor; index = type id)
| id | name | id | name |
|---|---|---|---|
| 0 | Tee (25 variants, `TeeA0001..A0025`, frame 0 only) | 13 | Woods (types 13–16 all use the Woods set) |
| 1 | PuttingGreen (variation flag 0x80 → set 26 TrickyGreen) | 17 | WaterShallow (variation 1 → set 23 WaterMiddle, 2 → set 24 WaterDeep; Desert with a grassy neighbour → set 25 WaterShallowDesert) |
| 2 | Fairway | 18 | Marsh |
| 3 | FirmFairway | 19 | Overgrowth |
| 4 | Rough | 20 | — **void / off-course; never drawn.** Neighbours draw a strata skirt down to y = −75. |
| 5 | DeepRough | 22 | Building |
| 6 | — (mound: uses the Rough set; also 21) | 27–30 | SandBunker1–4: the bunker (type 7) texture per screen orientation when rot ≠ 0 |
| 7 | — (sand bunker: the per-orientation SandBunker1–4 set) | 32/33 | Path / PathX (frames 0 Path, 1 Cap, 2 Inside, 3 Curve) |
| 8 | GrassySand | 34 | RetainWallA.bmp |
| 9 | PotSandBunker | 35 | CliffTest.bmp |
| 10 | Overgrowth | 36 | strata.bmp (void-edge skirts) |
| 11 | Brush, 12 Rock | | |

Some texture folders on disk are not referenced by this DLL's name table: Cliff, Ravine, GrassBunker, ZenSand, FlowerBed.
See §5.

---

## 3. Rendering pipeline

### 3.1 Projection and camera
- **Orthographic.** `glOrtho(±X/2, ±Y/2, near = 5000, far = −5000)`. The extents are chosen so that 1 pixel equals **2.2097 units at
  800×600** (1.7678 at 1024×768, 1.3598 at 1280×1024).
- Modelview: rotate pitch about X, then rotate (45° + view rotation) about Y. Pitch is 38.68° / 40.54° / 40.83° per resolution.
- Result: a tile is a **64×40 px diamond at 800×600** (80×52 at 1024, 104×68 at 1280). These are exactly the
  constants in the cull routine FUN_10006850 and golf's scroll code. One height step (15 units) is ≈ 5.3 px; the cull routine
  uses 5 px.
- View rotation is one of 0/90/180/−90. The UV table rotates with the view, so ground textures **stay screen-aligned**:
  they are pre-lit, pre-oriented images.
- `flip` (NT5+) swaps bottom/top in the ortho call and changes front-face from GL_CCW to GL_CW. **(guess)** graphsy's
  DIB section is top-down on Win2000+, so GL's bottom-up output had to be flipped.

### 3.2 Zoom
golf zoom 4 means 1:1 (shift 0), 2 means ½× (shift 1, ortho ×2) and 1 means ¼× (shift 2, ortho ×4). Draw and cull radii scale with
`<< shift`. At shift 2 the radius is 64, which covers the whole map, and culling is skipped.

### 3.3 Draw order and visibility
- **No depth buffer and no depth test anywhere.** The PFD has 0 depth bits, there is no glEnable(GL_DEPTH_TEST), no glClear and no
  glDepthMask. Visibility is a **painter's algorithm**: the tile loops run back-to-front for each of the 4 rotations.
- Each tile draws, in this order:
  1. its 8 triangles (each one is its own glBegin(GL_TRIANGLES) with a texture bind when it changes; glTexCoord2fv + glArrayElement)
  2. the path overlay (alpha-blended GL_TRIANGLE_FAN quads toward connected neighbours plus corner pieces chosen by a 4-bit neighbour mask)
  3. walls: vertical textured quads between this tile's edge vertices and the neighbour's edge vertices, with hard-coded normals
  4. strata skirts toward void neighbours, down to y = −75, recursing up to 3 tiles
- Back-face culling is on (GL_CULL_FACE, default GL_BACK).

### 3.4 Lighting
- Lighting comes from `<Parkland|Desert|Tropical|Links>Lighting.txt` in the game root (course type 0..3; any other value uses
  `lighting.txt`). The file is read relative to the CWD.
- Parsed sections: `#AMBIENT r g b`, `#DIFFUSE r g b`, `#SPECULAR r g b`, each 0..255 and divided by 255.
  `#HIGHLIGHT` (a hex blob) is **ignored**.
- Settings applied to **GL_LIGHT0**: ambient, diffuse and specular from the file. Position is the directional vector (−0.5, 0.1, −1, 0)
  rotated 40° about X, then 45° about Y, then normalised.
  - The helper FUN_10003a50 reuses the already-rotated component (a bug), which gives about (−0.61, 0.77, 0.20).
    **(guess: assumes the two CRT calls are cos and sin)**
  - It is set while MODELVIEW is the identity, so the light is fixed in eye space and the terrain turns under it.
- Material: `glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, 1,1,1,1)`, `glMateriali(…, GL_SHININESS, 13)`.
  - Ambient and diffuse stay at the GL defaults (0.2 / 0.8).
  - No GL_COLOR_MATERIAL and no glLightModel (global ambient 0.2, infinite viewer, one-sided).
- GL_LIGHTING is on by default. It is disabled only for 2D lines and renderTile.
- Shading is Gouraud: per-vertex normals averaged across tiles (calcNormals).
- GL_LIGHT1 (all colours 0) is used only by lowerEdgeCorner's "black-out" pass.
- **Texture env is effectively GL_MODULATE.** The loader calls `glTexEnvi(GL_TEXTURE_2D, GL_TEXTURE_ENV_MODE, GL_REPLACE)`, which has
  the wrong *target* and is therefore GL_INVALID_ENUM and ignored. So lit colour × texture is what you see. Because GL 1.1 has no
  separate specular, the specular highlight is also modulated by the texture.

### 3.5 Textures
- Base path `./Data/Textures/<Parkland|Desert|Tropical|Links>/`. Course type 0 or unknown uses Parkland.
- Ground sets:
  - `<Name><A..E>000<1..9>.bmp`: up to 5 variants × 9 frames. Loading stops at the first missing `<Name><letter>0001.bmp`.
  - Tee uses `TeeA0001..A0025` as 25 variants with frame 0 only.
  - The 9 frames are **edge-transition pieces**. Per triangle, FUN_10013670/13f00/13dd0 pick frame 0..8 from the 3 neighbours
    that touch that triangle ("is the neighbour in the same collar group, or lower/higher priority").
- Ground BMPs: 24-bit, 64×64 BMP. The loader swaps BGR→RGB and uploads with `glTexImage2D(GL_TEXTURE_2D, 0, 3, w, h, 0, GL_RGB, UNSIGNED_BYTE)`.
  - **No mipmaps.** The gluBuild*Mipmaps branch is dead, because every call passes GL_LINEAR.
  - MAG = GL_NEAREST, MIN = GL_LINEAR, wrap = GL_REPEAT.
  - A BMP with height 1 would go to GL_TEXTURE_1D, but that never happens.
- Paths: uncompressed type-2 TGA, 24/32-bit (Path.tga is 64×64×32 RGBA). Uploaded as GL_RGBA (or GL_RGB), MIN/MAG = GL_LINEAR.
  Drawn with GL_BLEND (SRC_ALPHA, ONE_MINUS_SRC_ALPHA). 8 TGAs: Path, PathCap, PathInside, PathCurve, plus their "X" versions.
- Walls and skirts: CliffTest.bmp, RetainWallA.bmp and strata.bmp (64×64 BMP).
- The 1-to-4 Data/*.bmp water files in the Data root are **not** loaded by this DLL (the path is `./Data/Textures/…`).
  **(guess)** They are leftovers.
- About 600 textures are loaded per course: roughly 7 MB as RGB, or 10 MB as RGBA in WebGL.

### 3.6 What each render entry point redraws (dirty-region rendering into the 2D buffer)
| entry | redraws | when |
|---|---|---|
| `render` | whole visible window (radius 16 << shift) over a **pre-cleared** surface (golf clears it) | rotation, zoom, big scroll, first frame |
| `stripRender` | only the newly exposed edge strip; golf blits the old cache, shifted, around it | scroll by 1–2 tiles |
| `localRender` | a 5×5 block around one changed tile plus the wall tiles in front of it, overdrawn onto the existing image | per changed tile (editing: terraform, type paint, walls, paths) |
| `pathUpdateRender` | every visible tile that has a path, plus the tiles that depend on it | path variant changed |
| `lowerEdgeCorner` | paints a near-black footprint before lowering an edge tile | lowering next to void |

Because nothing is ever cleared, all incremental paths rely on the new geometry fully covering the old pixels.

### 3.7 Pixel format and GL target
The PFD at 0x10063E10 is:
- `nSize = 40, nVersion = 1, dwFlags = PFD_DRAW_TO_BITMAP | PFD_SUPPORT_OPENGL` (0x28)
- `iPixelType = RGBA`, **`cColorBits = 16`**, every other field 0. That means **no depth, stencil, alpha or accumulation buffer, and single-buffered**.

The DC is graphsy's DIB-section memory DC, so this is Microsoft's generic software GL drawing into the 16-bit back buffer. GL_DITHER
is left at its default (on). drawLine decodes its colour argument as **RGB555**, which suggests the graphsy surface format.
**(guess)**

### 3.8 Depth/height buffer for sprites
**Terrain.dll writes none.** There is no depth buffer, no glReadPixels and no extra output. The 16-bit "depth" surface used by
graphsy sprite slots 39/40 must be produced by golf.exe itself, e.g. from tile row/elevation. **(open question, §5)**

---

## 4. Every GL / WGL call and the state it uses, with WebGL2 notes

| Call | Arguments actually used | WebGL2 / port note |
|---|---|---|
| ChoosePixelFormat, SetPixelFormat, wglCreateContext, wglMakeCurrent, wglDeleteContext | see §3.7 | Replace with the SDL/Emscripten context, or an FBO (recommended: render terrain into an FBO, see below) |
| glViewport | (0,0,w,h) | native |
| glMatrixMode / glLoadIdentity / glPushMatrix / glPopMatrix | PROJECTION and MODELVIEW stacks, depth ≤ 2 | needs a small CPU matrix stack |
| glOrtho | 3D: (±X/2, ±Y/2, 5000, −5000). 2D: (0,w,h,0,−1,1) | CPU matrix |
| glRotated / glRotatef / glTranslatef / glScalef | pitch about X, yaw about Y, tile translate, renderTile scale | CPU matrix |
| glHint | PERSPECTIVE_CORRECTION = FASTEST, LINE_SMOOTH = NICEST | drop (ortho, so perspective correction is irrelevant) |
| glEnable/glDisable | GL_LIGHTING, GL_LIGHT0, GL_LIGHT1, GL_TEXTURE_2D, GL_BLEND, GL_CULL_FACE, GL_LINE_SMOOTH | lighting must be done in a shader. LINE_SMOOTH is not in WebGL. |
| glFrontFace | CW (flip) or CCW | native (or drop if you never flip) |
| glEnableClientState | VERTEX_ARRAY, NORMAL_ARRAY | VBO / attributes |
| glVertexPointer / glNormalPointer | 3 floats, stride 0, the global arrays | upload to a VBO (22500 verts), or rebuild per draw |
| glArrayElement | one vertex at a time inside glBegin(GL_TRIANGLES) | **not in WebGL**. Emscripten LEGACY_GL_EMULATION does not implement it as far as I know **(verify)**. Rewrite as indexed triangles. |
| glBegin/glEnd | GL_TRIANGLES (most), GL_TRIANGLE_FAN (path), GL_LINES, GL_LINE_STRIP (2D) | immediate mode: emulation-able, or batch on the CPU |
| glVertex2i / glVertex2fv / glVertex3f / glVertex3fv | | |
| glNormal3f / glNormal3fv | | |
| glTexCoord2f / glTexCoord2fv | | |
| glColor4f | 2D lines only | |
| glLineWidth | 2, 10, or a spline width | **WebGL is effectively limited to 1 px.** Draw thick lines as quads. |
| glBlendFunc | SRC_ALPHA, ONE_MINUS_SRC_ALPHA | native |
| glLightfv | LIGHT0/1: POSITION (directional), AMBIENT, DIFFUSE, SPECULAR | **fixed-function lighting must be written in GLSL.** LEGACY_GL_EMULATION has no lighting **(verify)**. Per-vertex Blinn-Phong as in GL 1.1, with infinite viewer, global ambient 0.2 and shininess 13. |
| glMaterialfv / glMateriali | FRONT_AND_BACK specular (1,1,1,1), shininess 13 | shader uniforms |
| glGenTextures / glBindTexture / glDeleteTextures | | native |
| glTexParameteri/f | MAG NEAREST, MIN LINEAR, wrap REPEAT (BMP); LINEAR/LINEAR (TGA) | native. 64×64 is power-of-two, so REPEAT is fine. |
| glTexEnvi | invalid target, so a no-op; effective mode is MODULATE | shader: `texel * litColour` |
| glTexImage2D | internal format 3 or GL_RGB, or GL_RGBA, UNSIGNED_BYTE | use RGB8/RGBA8 |
| gluBuild1DMipmaps / gluBuild2DMipmaps | dead code | drop |
| glFlush | after every entry point | no-op / drop |
| *Never used:* glClear, glDepthFunc, glDepthMask, glReadPixels, glShadeModel (default SMOOTH), glColorMaterial, glLightModel, glDrawArrays/Elements, fog, stencil | | |

### Port assessment
**LEGACY_GL_EMULATION is a poor fit.** It would still leave you to hand-write:
- lighting (glLight and glMaterial)
- glArrayElement
- wide and smooth lines
- the 16-bit DIB interop

The renderer is small: about 8 triangles per tile, roughly 1–8 K triangles per full redraw, 3 primitive types and one light. Two
hand-port options look better:

- **(a) Hand-port to WebGL2.** Use one shader with per-vertex lighting, modulated texture, and an optional alpha-blended pass. Keep
  painter's order, or simply add a depth buffer. Render the whole visible terrain into an RGBA FBO ("terrain cache") whenever golf
  would have called render, strip, local or path update. Read it back with glReadPixels into the 16-bit 2D back buffer **only when
  it changes**, or composite the FBO texture under the sprites on the GPU if the 2D layer moves to GL. A full redraw each time is
  cheap and avoids the overdraw tricks (lowerEdgeCorner black-out, strip blits).
- **(b) Software GL 1.1 rasteriser** (e.g. a TinyGL-style subset) compiled to wasm, writing directly into the 16-bit buffer. This
  stays closest to the original generic-GL output, including dithering and painter's overdraw, and needs no GPU interop.

---

## 5. Open questions
1. What generates graphsy's 16-bit depth surface (sprite slots 39/40)? It is not Terrain.dll. Look in golf.exe for code that fills
   it per tile from row and elevation.
2. Blit direction in golf's FUN_00475d00 (vtbl+0x40). The usage only makes sense as "ECX surface → arg surface"
   (view → cache after a draw, cache → view at idle-frame start). This should be confirmed in graphsy.
3. What does golf's FUN_0040bf60(x,y) mean? It decides lowerCorner vs lowerEdgeCorner and skips syncing a tile.
   **(guess)** "Tile is void/off-course."
4. ~~Corner index geometry~~ Answered in §6.2: corners 1/3/5/7 are lattice points (2,0)/(2,2)/(0,2)/(0,0).
5. ~~FUN_10003a50's sin/cos order~~ Confirmed from the disassembly: FUN_10019214 is `fcos`, FUN_10019164 is `fsin`.
6. The texture folders contain sets this DLL never names (Cliff, Ravine, GrassBunker, ZenSand, FlowerBed, RedRingThin.tga), and
   the name table has empty slots 6, 7, 14–16, 20, 21 and 31. Is the shipped Terrain.dll (Debug build) older than the art? How
   does the game render ravines and flowerbeds (as golf-side sprites)?
7. ~~`rand()` in setType~~ The port reproduces it (§6.3): the DLL has its own CRT, seeded 1, never reseeded.
8. ~~Lost return values~~ Read from the disassembly: FUN_10015650 returns 0/1/2 (§6.3), FUN_10006850 returns "culled"
   and always false at ¼ zoom. render's return value is still garbage.
9. At 1024×768, `resize` uses a vertical extent of 1303 (0x517), but `setZoomLevel` uses 1357.645. The latter is the value that
   is consistent with the 1.7678 units/px aspect. So at 1:1 zoom on 1024×768, after `resize` the image is about 4% vertically
   stretched compared with zoom levels 1/2. This looks like a typo in the original; check it in game.
10. Non-standard resolutions: initSystem/setZoomLevel use uninitialised extents and a pitch of 0 for anything other than
   800×600, 1024×768 or 1280×1024. Golf presumably only offers those three.

---

## 6. The port (`src/platform/terrain_dll.cpp`, `terrain_gl.cpp`, `terrain_bridge.cpp`)

### 6.1 Approach
Option (b) from §4: Terrain.dll ported function by function onto a small software GL 1.1 (`terrain_gl.cpp`) that writes
straight into the RGB555 back buffer. That is what generic GL did, so there is no GPU interop, the incremental redraw
paths (`localRender`, `stripRender`, `lowerEdgeCorner`'s black-out) work unchanged on top of the existing image, and it
runs natively for tests. A full 1:1 redraw is ~10 ms native and ~18 ms in Chrome.

* `Terrain*` points at a struct with the DLL's layout (0x164AD0 bytes on wasm32; `static_assert`ed), so golf's inlined
  accessors read real tiles. The render list at +0x164AC4 is unused padding; the port keeps a vector instead.
* Vertex/normal arrays, the texture table `[37][25][9]`, course type, pitch, rotation index and height step are module
  globals, as in the DLL.
* `initSystem`'s HDC is a `TerrainSurface*` (`terrain_port.h`); `terrain_bridge.cpp` lets the GDI layer map its own HDCs.
  The `flip` flag is stored only: the port always produces the upright image that `flip` produced on Windows 2000+.
* Files are opened relative to the CWD like the DLL (`./Data/Textures/<Course>/…`, `<Course>Lighting.txt`) with a
  per-component case-insensitive fallback (`terrain_fopen`), because e.g. the table says `Woods` and the files are `woods…`.

### 6.2 Tile geometry (from FUN_10002060 and the fix-ups)
Lattice point (sx, sz) of tile (x, y) is vertex `x*3 + sx + (y*3 + sz)*150`, at X = 100x − 2500 + 50sx, Z = 100y − 2500 + 50sz.
Triangles (as (sx,sz)): 0 = (0,0)(0,1)(1,1), 1 = (0,0)(1,1)(1,0), 2 = (1,0)(1,1)(2,0), 3 = (1,1)(2,1)(2,0),
4 = (0,1)(0,2)(1,1), 5 = (0,2)(1,2)(1,1), 6 = (1,1)(1,2)(2,2), 7 = (1,1)(2,2)(2,1): a fan round the centre.
uvSel holds (col, row) indices {0 → 0, 1 → 1, 2 → 0.5} of (sx/2, 1 − sz/2) into the 3×3×4 table at 0x10063CA0.
Corners: 1 = (2,0), 3 = (2,2), 5 = (0,2), 7 = (0,0). golf's corner c of tile (x, y) reads its 51×51 height grid at
c1 → (x+1, y−1), c3 → (x+1, y), c5 → (x, y), c7 → (x, y−1), i.e. golf grid (a, b) is lattice point (a, b+1).

### 6.3 Behaviour worth knowing (all reproduced)
* **Texture variants**: `setType` draws `rand() % numVariants[type]` from the DLL's own CRT (MSVC LCG, seed 1, never
  reseeded) and only for types whose set has variants, so the picture depends on every `setType` call since start-up.
  `terrain_srand` exists for tests. Tees never count their 25 variants (numVariants stays 0); the tee texture is picked
  by golf's variation instead. Pot bunkers (9) always use frame 3.
* **Edge transitions**: FUN_10015650 returns 0 (same), 1 (different collar group, or a water edge) or 2 (same group,
  other type; or water of another depth). Each triangle's frame comes from the code `a·(4|40) + b·(1|10) + c·(2|20)` of
  its two edge neighbours and the diagonal, mapped by FUN_10013dd0. Rough, Fairway and bunkers clamp frames > 4 to 0.
* **Bunker and mound geometry**: applying a bunker drops the centre (and shared vertices of bunker neighbours) by 13,
  a mound raises by 20. Undoing a mound subtracts 20 everywhere *except* corner (0,0) of a three-mound corner, which
  gets +20 (a sign slip in FUN_10014190). `lowerCorner` on a bunker resets its dip to *the lowered corner's height*
  − 13, not the old centre − 13.
* **Lighting**: light 0 is enabled, and material specular/shininess set, only if the lighting file loads; without it the
  course is lit by the global ambient alone. Light direction is (−0.5, 0.1, −1) rotated with the reused-component bug
  (≈ (−0.61, 0.77, 0.20) normalised), fixed in eye space. Wall/skirt normals (1,1,0) and (−1,1,0) are not unit length
  and GL_NORMALIZE is off, so those faces light brighter.
* `pathUpdateRender` never sets the rotation index, so it uses the previous render's. `setZoomLevel(4)` applies its
  ortho to the modelview (reset by the next render). Culling is in screen pixels with margins (w/2)·shift, and off at ¼.

### 6.4 Guesses and differences
* **Dither**: 4×4 ordered (Bayer) dither when quantising to 5 bits, standing in for generic GL's GL_DITHER. Pattern and
  phase are a guess.
* **Lines**: GL_LINE_SMOOTH wide lines are drawn as coverage-antialiased rectangles; generic GL's exact AA is unknown.
* **Filtering**: per triangle (the mapping is affine), MIN linear / MAG nearest for BMPs, linear for TGAs, as the DLL sets.
* No near/far clipping (nothing reaches |z| = 5000); no 1D textures (none are 1 px high).
* The constructor's texture pass (made before any GL context, then thrown away by `initSystem`) is skipped.
* `tileHit` (a debug stub) returns NULL. The Bezier/cardinal splines (unused by golf) use textbook formulas.
* Unknown resolutions fall back to the 800×600 extents and cull constants instead of the DLL's uninitialised values.

### 6.5 Tests
`tests/terrain` is a standalone CMake project (no change to the game's CMakeLists):
* `terrain_render --game game [--save "game/Saved Games/x.sve" | --sample] [--x --y --rot --zoom] --out t.png` renders
  natively. The sample course has hills, every ground type, three water depths, bunkers in all orientations, a path
  and a void notch.
* `terrain_test.html` (Emscripten) preloads `Data/Textures`, the lighting files and `Saved Games/` from the user's install
  and drives Terrain like golf's per-frame driver: full render + cache, idle frames that restore the cache and draw an aim
  line with `drawLine`, and edits redrawn with `localRender`.
* The `.sve` loader (`course.cpp`) is test-only: it lacks golf's terrain-type table (0x578370, filled at run time) and
  per-tile height overrides (0x51B770), so blend groups are the identity, walls are always cliffs, and water levels
  aren't special-cased.
