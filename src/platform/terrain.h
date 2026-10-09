// Terrain: the interface golf.exe uses from Terrain.dll, the 3D course renderer.
// Recovered from the debug Terrain.dll and the v1.02 NoCD golf.exe; details,
// rendering pipeline and open questions are in docs/terrain.md.
//
// Terrain is a plain (non-virtual) C++ singleton exported by mangled name, so
// these signatures are exact. It renders a 50x50 tile grid with OpenGL 1.1
// (orthographic, one directional light, painter's order, no depth buffer)
// straight into the 16-bit graphsy back buffer through a DIB memory DC, and
// golf.exe draws sprites on top. golf.exe keeps its own logical map and
// pushes changes into Terrain every frame (setType, elevateCorner, setWall,
// layPath), then asks for an incremental or full redraw.

#ifndef SIMGOLF_PLATFORM_TERRAIN_H
#define SIMGOLF_PLATFORM_TERRAIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C++" {

enum { TERRAIN_W = 50, TERRAIN_H = 50 };

// One grid cell as Terrain.dll lays it out (0x248 bytes, 32-bit pointers).
// golf.exe reads elev, type and wall directly (it inlines tileAt, getElevation,
// getType and getWall), so a port that keeps golf's code must keep these offsets.
struct TerrainTri {                 // 0x38 bytes
  int32_t  v[3];                    // indices into the global vertex/normal arrays
  int32_t  uvSel[3][2];             // (row, col) into a rotation-aware UV table
  uint32_t tex;                     // GL texture name
  uint8_t  frame;                   // edge-transition frame 0..8
  uint8_t  pad[3];
  float    faceNormal[3];
};

struct Tile {
  int32_t    elev[8];               // +0x000 corners 1,3,5,7: height in 15-unit steps
  int32_t    unused20;              // +0x020
  int32_t    type;                  // +0x024 golf terrain type id (20 = void, never drawn)
  int32_t    variation;             // +0x028 golf subtype (bunker orientation, water depth...)
  int32_t    x, y;                  // +0x02c
  Tile      *nbr[4];                // +0x034 (x,y-1), (x,y+1), (x-1,y), (x+1,y)
  int32_t    triCount;              // +0x044 always 8
  TerrainTri tri[8];                // +0x048
  uint8_t    hasPath;               // +0x208
  uint8_t    pathVariant;           // +0x209 the "X" path texture set
  uint8_t    drawPathOverlay;       // +0x20a
  uint8_t    unused20b[5];          // +0x20b
  int32_t    wallType[9];           // +0x210 indexed by dir 0,2,4,6; 1 = cliff, else retaining wall
  uint8_t    wall[8];               // +0x234 wall present on edge dir 0,2,4,6
  uint8_t    pad23c[4];             // +0x23c
  int32_t    texVariant;            // +0x240 rand() % variants for the type
  uint8_t    normalsDirty;          // +0x244
  uint8_t    pad245[3];
};

#if UINTPTR_MAX == 0xffffffffu   // the layout only holds with 32-bit pointers (x86, wasm32)
static_assert(sizeof(TerrainTri) == 0x38, "TerrainTri layout");
static_assert(sizeof(Tile) == 0x248, "Tile layout");
static_assert(__builtin_offsetof(Tile, type) == 0x24, "Tile.type");
static_assert(__builtin_offsetof(Tile, hasPath) == 0x208, "Tile.hasPath");
static_assert(__builtin_offsetof(Tile, wallType) == 0x210, "Tile.wallType");
static_assert(__builtin_offsetof(Tile, wall) == 0x234, "Tile.wall");
static_assert(__builtin_offsetof(Tile, texVariant) == 0x240, "Tile.texVariant");
#endif

class Terrain {       // singleton, 0x164ad0 bytes; golf.exe keeps it at 0x00820ed0
public:
  static Terrain *getInstance();
  ~Terrain();

  // Lifetime. golf passes (800, 600, back-buffer HDC, Windows major version > 4).
  void initSystem(int w, int h, void *hdc, bool flip);
  void initTerrain();                                  // flat grid, links neighbours
  void closeSystem();
  void resetTerrain();                                 // new course
  void resize(int w, int h);                           // not imported by golf
  void loadNewCourseType(int courseType);              // 0 Parkland, 1 Desert, 2 Tropical, 3 Links
  void passCollarInfo(int *const groups, int n);       // per-type edge-blend group; golf passes 23
  void changeLighting(int brighter);                   // dev hotkey; not imported
  void setZoomLevel(int zoom);                         // golf zoom 4 = 1:1, 2 = 1/2, 1 = 1/4

  // Map state, mirrored from golf.exe's own map.
  Tile *tileAt(int x, int y);                          // tiles[y*50+x]; inlined by golf
  Tile *tileHit(int sx, int sy);                       // debug stub, not real picking
  int  getElevation(Tile *t, int corner);              // inlined by golf
  int  getType(Tile *t);                               // inlined by golf
  int  getVariation(Tile *t);                          // low byte of variation
  bool getWall(Tile *t, int dir);                      // inlined by golf
  bool hasPath(Tile *t);
  bool hasConnectedPath(int x, int y);                 // really returns pathVariant
  void setType(Tile *t, int type, int variation);      // picks texVariant with MSVC rand()
  void setWall(Tile *t, int dir, int wallType, bool on);
  void layPath(Tile *t, int on, int variant);
  void updatePath(int x, int y, int variant);
  void elevateCorner(Tile *t, int corner);             // corner 1,3,5,7
  void lowerCorner(Tile *t, int corner);
  void lowerEdgeCorner(Tile *t, int corner, Tile *centre, float rot); // blacks out footprint first
  void calcNormals(Tile *t);
  void calcAllNormals(Tile *centre);                   // tiles within 13 << zoomShift

  // Drawing into the back buffer. rot is the view rotation in degrees
  // (0, 90, 180, -90). Nothing is cleared; golf clears and caches.
  bool render(Tile *centre, float rot);                // full redraw of the visible window
  void localRender(Tile *dirty, Tile *centre, float rot);   // 5x5 around a changed tile
  void stripRender(Tile *centre, int dir, float rot);  // newly exposed edge after a 1-2 tile scroll
  void pathUpdateRender(Tile *centre, float rot);      // every visible tile with a path
  void renderTile(int type, int sx, int sy, int num, int den); // UI preview; not imported

  // 2D overlays drawn with GL in screen space.
  void drawLine(int x1, int y1, int x2, int y2, int rgb555, int width, int alpha10);
  void drawCircle(Tile *t, float r);                   // empty stub; not imported
  void drawBezierSpline(int, int, int, int, int, int, int, int, int);           // not imported
  void drawCardinalSpline(int, int, int, int, int, int, int, int, int, int, int); // not imported
  void setSplineHeight(float step);                    // really sets the 15-unit height step; not imported
};

}  // extern "C++"
#endif  // __cplusplus

#endif  // SIMGOLF_PLATFORM_TERRAIN_H
