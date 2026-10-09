// Terrain: a port of Terrain.dll, the SimGolf 3D course renderer, onto the
// software GL in terrain_gl.cpp. It implements src/platform/terrain.h.
//
// The structure follows the DLL function by function (addresses in the
// comments are Terrain.dll's), including its quirks, because golf.exe reads
// the tile data directly and the look of the course depends on the exact
// draw order and texture picks. docs/terrain.md describes the original; the
// "Port" section there lists where this file knowingly differs.

#ifdef SIMGOLF_PORT_FS
#include "port/fs.h"
#endif
#include "platform/terrain.h"
#include "platform/terrain_gl.h"
#include "platform/terrain_port.h"

#include <dirent.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>

namespace {

// --- The DLL's object layout and module globals ----------------------------

struct TextureSet {
  char name[20];
  int32_t numVariants;
};

// Terrain.dll's Terrain object. Terrain* points at one of these, so code
// translated from golf.exe that reads +0x14/+0x18/+0x3a4 finds what it expects.
struct TerrainState {
  void *glrc;                         // +0x000 non-null once initSystem had a target
  float ambient[4];                   // +0x004 only used by changeLighting
  int32_t w, h;                       // +0x014 50 x 50
  int32_t zoomShift;                  // +0x01c 0 = 1:1, 1 = 1/2, 2 = 1/4
  int32_t screenW, screenH;           // +0x020
  uint8_t flip;                       // +0x028
  uint8_t texturesLoaded;             // +0x029
  uint8_t pad2a[2];
  TextureSet sets[37];                // +0x02c indexed by terrain type
  Tile tiles[TERRAIN_W * TERRAIN_H];  // +0x3a4 tiles[y*50 + x]
  uint8_t renderList[12];             // +0x164ac4 a std::list<Tile*> in the DLL
};
#if UINTPTR_MAX == 0xffffffffu
static_assert(offsetof(TerrainState, sets) == 0x2c, "Terrain.sets");
static_assert(offsetof(TerrainState, tiles) == 0x3a4, "Terrain.tiles");
static_assert(sizeof(TerrainState) == 0x164ad0, "Terrain size");
#endif

enum { kVoid = 20, kTypeBunker = 7, kTypeMound = 6, kTypeWater = 17 };
enum { kStride = TERRAIN_W * 3 };  // vertices per row of the global arrays

TerrainState *g_inst;                             // 0x10106b4c
float g_pos[TERRAIN_W * TERRAIN_H * 9][3];        // 0x100b28c8, 3x3 vertices per tile
float g_nrm[TERRAIN_W * TERRAIN_H * 9][3];        // 0x10070a18
unsigned g_tex[37][25][9];                        // 0x100687f8 [set][variant][frame]
int g_courseType;                                 // 0x10070a0c
float g_pitch;                                    // 0x10070a10
int g_rot;                                        // 0x10070a14, 0..3
float g_heightStep = 15.0f;                       // 0x10063e54
std::vector<int> g_collar;                        // 0x10106b48
std::vector<Tile *> g_queue;                      // the render list at +0x164ac4
uint32_t g_holdrand = 1;                          // the DLL's own CRT rand state
tgl::Context g_gl;
TerrainSurface g_surface;

int msvcRand() {
  g_holdrand = g_holdrand * 214013u + 2531011u;
  return (int)((g_holdrand >> 16) & 0x7fff);
}

// Texture coordinates by (uvSel row, uvSel col, rotation index), 0x10063ca0.
// Ground textures are pre-lit images, so they rotate with the view.
const float kUV[3][3][4][2] = {
  {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}, {{0, 1}, {0, 0}, {1, 0}, {1, 1}}, {{0, .5f}, {.5f, 0}, {1, .5f}, {.5f, 1}}},
  {{{1, 0}, {1, 1}, {0, 1}, {0, 0}}, {{1, 1}, {0, 1}, {0, 0}, {1, 0}}, {{1, .5f}, {.5f, 1}, {0, .5f}, {.5f, 0}}},
  {{{.5f, 0}, {1, .5f}, {.5f, 1}, {0, .5f}}, {{.5f, 1}, {0, .5f}, {.5f, 0}, {1, .5f}}, {{.5f, .5f}, {.5f, .5f}, {.5f, .5f}, {.5f, .5f}}},
};

// Hard-coded normals for walls and strata skirts, [rot 0/2, rot 1/3]. Two of
// them aren't unit length; GL_NORMALIZE is off, so they light brighter.
const float kNormalYMinus[2][3] = {{-0.707f, 0, -0.707f}, {-1, 1, 0}};  // 0x10063c40
const float kNormalYPlus[2][3] = {{0.707f, 0, 0.707f}, {1, 1, 0}};      // 0x10063c58
const float kNormalXMinus[2][3] = {{1, 1, 0}, {-1, 0, 0}};             // 0x10063c70
const float kNormalXPlus[2][3] = {{-1, 1, 0}, {1, 0, 0}};              // 0x10063c88

// --- Small helpers ----------------------------------------------------------

Tile *tileAt(int x, int y) {
  if (!g_inst || x < 0 || y < 0 || x >= g_inst->w || y >= g_inst->h) return nullptr;
  return &g_inst->tiles[y * g_inst->w + x];
}

// Index of lattice point (sx, sz) of a tile in the global vertex arrays. The
// DLL addresses these through the triangle table; every slot it touches is one
// of these nine points.
int vidx(const Tile *t, int sx, int sz) { return t->x * 3 + sx + (t->y * 3 + sz) * kStride; }
float &Y(const Tile *t, int sx, int sz) { return g_pos[vidx(t, sx, sz)][1]; }
int typeOf(const Tile *t) { return t->type; }
bool isVoid(const Tile *t) { return t->type == kVoid; }  // FUN_10015460

int maxCorner(const Tile *t) {  // FUN_10015500
  int a = t->elev[7] < t->elev[1] ? t->elev[1] : t->elev[7];
  int b = t->elev[5] < t->elev[3] ? t->elev[3] : t->elev[5];
  return b < a ? a : b;
}

void markDirtyAround(Tile *t) {
  Tile *n0 = t->nbr[0], *n1 = t->nbr[1], *n2 = t->nbr[2], *n3 = t->nbr[3];
  t->normalsDirty = 1;
  if (n0) n0->normalsDirty = 1;
  if (n0 && n2) n0->nbr[2]->normalsDirty = 1;
  if (n0 && n3) n0->nbr[3]->normalsDirty = 1;
  if (n2) n2->normalsDirty = 1;
  if (n3) n3->normalsDirty = 1;
  if (n1) n1->normalsDirty = 1;
  if (n1 && n2) n1->nbr[2]->normalsDirty = 1;
  if (n1 && n3) n1->nbr[3]->normalsDirty = 1;
}

void setRotation(float rot) {  // FUN_1000adc0
  if (rot == 0.0f) g_rot = 0;
  else if (rot == 90.0f) g_rot = 1;
  else if (rot == 180.0f) g_rot = 2;
  else g_rot = 3;
}

// --- Tile geometry ------------------------------------------------------------

int uvSelOf(float f) { return f == 0.5f ? 2 : (int)f; }  // FUN_10002010

// FUN_10002060: two triangles per quadrant, then four vertex swaps that turn
// the diagonals of the off-axis quadrants so all eight fan around the centre.
void buildTriangles(Tile *t) {
  t->triCount = 8;
  int k = 0;
  for (int qz = 0; qz < 2; qz++)
    for (int qx = 0; qx < 2; qx++) {
      int a[3][2] = {{qx, qz}, {qx, qz + 1}, {qx + 1, qz + 1}};
      int b[3][2] = {{qx, qz}, {qx + 1, qz + 1}, {qx + 1, qz}};
      for (int (*v)[2] : {a, b}) {
        TerrainTri &tri = t->tri[k++];
        for (int i = 0; i < 3; i++) {
          tri.v[i] = vidx(t, v[i][0], v[i][1]);
          tri.uvSel[i][0] = uvSelOf(v[i][0] / 2.0f);
          tri.uvSel[i][1] = uvSelOf(1.0f - v[i][1] / 2.0f);
        }
        tri.faceNormal[0] = 0;
        tri.faceNormal[1] = 1;
        tri.faceNormal[2] = 0;
      }
    }
  TerrainTri *tr = t->tri;
  tr[2].v[2] = tr[3].v[2];
  tr[2].uvSel[2][0] = tr[3].uvSel[2][0];
  tr[2].uvSel[2][1] = tr[3].uvSel[2][1];
  tr[3].v[0] = tr[2].v[1];
  tr[3].uvSel[0][0] = tr[2].uvSel[1][0];
  tr[3].uvSel[0][1] = tr[2].uvSel[1][1];
  tr[4].v[2] = tr[5].v[2];
  tr[4].uvSel[2][0] = tr[5].uvSel[2][0];
  tr[4].uvSel[2][1] = tr[5].uvSel[2][1];
  tr[5].v[0] = tr[4].v[1];
  tr[5].uvSel[0][0] = tr[4].uvSel[1][0];
  tr[5].uvSel[0][1] = tr[4].uvSel[1][1];
}

// FUN_1000c2c0: back to flat Rough, no walls, no path. Leaves normalsDirty,
// wallType and the triangles alone.
void resetTile(Tile *t, int x, int y) {
  t->x = x;
  t->y = y;
  t->texVariant = 0;
  t->elev[1] = t->elev[7] = t->elev[3] = t->elev[5] = 0;
  for (Tile *&n : t->nbr) n = nullptr;
  t->type = 4;
  t->variation = 0;
  t->hasPath = t->pathVariant = t->drawPathOverlay = 0;
  memset(t->unused20b, 0, 4);
  t->wall[0] = t->wall[2] = t->wall[6] = t->wall[4] = 0;
}

// FUN_1000a130: flat vertex lattice (y = 0, normals up), tiles, neighbours.
void buildGrid(TerrainState *s) {
  float tile = 100.0f;  // 0x10063e4c / 0x10063e50
  for (int j = 0; j < s->h; j++)
    for (int i = 0; i < s->w; i++)
      for (int sz = 0; sz < 3; sz++)
        for (int sx = 0; sx < 3; sx++) {
          int v = i * 3 + j * kStride * 3 + sx + sz * kStride;
          g_pos[v][0] = tile / 2 * sx + (i * tile - s->w * tile / 2);
          g_pos[v][1] = 0;
          g_pos[v][2] = tile / 2 * sz + (j * tile - s->h * tile / 2);
          g_nrm[v][0] = 0;
          g_nrm[v][1] = 1;
          g_nrm[v][2] = 0;
        }
  for (int x = 0; x < s->w; x++)
    for (int y = 0; y < s->h; y++) {
      Tile *t = &s->tiles[y * s->w + x];
      resetTile(t, x, y);
      buildTriangles(t);
    }
  for (int x = 0; x < s->w; x++)
    for (int y = 0; y < s->h; y++) {
      Tile *t = &s->tiles[y * s->w + x];
      t->nbr[2] = tileAt(x - 1, y);
      t->nbr[0] = tileAt(x, y - 1);
      t->nbr[3] = tileAt(x + 1, y);
      t->nbr[1] = tileAt(x, y + 1);
    }
}

// Raises (d > 0) or lowers a bunker or mound tile's centre and the matching
// shared vertices of neighbours of the same type. One body for FUN_1000d540
// (bunker -13), FUN_1000de60 (mound +20) and their undo FUN_100149e0 (+13)
// and FUN_10014190 (-20). The mound undo adds instead of subtracting at the
// (0,0) corner, which the original does too: `cornerD` carries that.
void shiftHollow(Tile *t, int type, float d, float cornerD) {
  Tile *n0 = t->nbr[0], *n1 = t->nbr[1], *n2 = t->nbr[2], *n3 = t->nbr[3];
  Y(t, 1, 1) += d;
  if (n0 && typeOf(n0) == type) {
    Y(t, 1, 0) += d;
    Y(n0, 1, 2) += d;
    if (n2 && typeOf(n2) == type && typeOf(n0->nbr[2]) == type) {
      Y(t, 0, 0) += cornerD;
      Y(n0, 0, 2) += d;
      Y(n2, 2, 0) += d;
      Y(n0->nbr[2], 2, 2) += d;
    }
    if (n3 && typeOf(n3) == type && typeOf(n0->nbr[3]) == type) {
      Y(t, 2, 0) += d;
      Y(n0, 2, 2) += d;
      Y(n3, 0, 0) += d;
      Y(n0->nbr[3], 0, 2) += d;
    }
  }
  if (n1 && typeOf(n1) == type) {
    Y(t, 1, 2) += d;
    Y(n1, 1, 0) += d;
    if (n2 && typeOf(n2) == type && typeOf(n1->nbr[2]) == type) {
      Y(t, 0, 2) += d;
      Y(n1, 0, 0) += d;
      Y(n2, 2, 2) += d;
      Y(n1->nbr[2], 2, 0) += d;
    }
    if (n3 && typeOf(n3) == type && typeOf(n1->nbr[3]) == type) {
      Y(t, 2, 2) += d;
      Y(n1, 2, 0) += d;
      Y(n3, 0, 2) += d;
      Y(n1->nbr[3], 0, 0) += d;
    }
  }
  if (n2 && typeOf(n2) == type) {
    Y(t, 0, 1) += d;
    Y(n2, 2, 1) += d;
  }
  if (n3 && typeOf(n3) == type) {
    Y(t, 2, 1) += d;
    Y(n3, 0, 1) += d;
  }
}

// Corner c in {1,3,5,7}: its lattice point, the two corners sharing an edge
// with it (and the midpoints between), and the opposite corner.
struct CornerGeom {
  int self[2], nbrA[2], midA[2], nbrB[2], midB[2], opp[2];
};
const CornerGeom *cornerGeom(int c) {
  static const CornerGeom k1 = {{2, 0}, {2, 2}, {2, 1}, {0, 0}, {1, 0}, {0, 2}};
  static const CornerGeom k3 = {{2, 2}, {0, 2}, {1, 2}, {2, 0}, {2, 1}, {0, 0}};
  static const CornerGeom k5 = {{0, 2}, {0, 0}, {0, 1}, {2, 2}, {1, 2}, {2, 0}};
  static const CornerGeom k7 = {{0, 0}, {2, 0}, {1, 0}, {0, 2}, {0, 1}, {2, 2}};
  switch (c) {
    case 1: return &k1;
    case 3: return &k3;
    case 5: return &k5;
    case 7: return &k7;
  }
  return nullptr;
}

void raiseCorner(Tile *t, int c) {  // FUN_1000c7b0
  const CornerGeom *g = cornerGeom(c);
  if (!g) return;
  float h = (t->elev[c] + 1) * g_heightStep;
  float a = Y(t, g->nbrA[0], g->nbrA[1]), b = Y(t, g->nbrB[0], g->nbrB[1]);
  float centre = (h + Y(t, g->opp[0], g->opp[1])) * 0.5f;
  if (centre <= Y(t, 1, 1)) centre = Y(t, 1, 1);
  Y(t, 1, 1) = centre;
  Y(t, g->midA[0], g->midA[1]) = (h + a) * 0.5f;
  Y(t, g->midB[0], g->midB[1]) = (h + b) * 0.5f;
  t->elev[c]++;
  Y(t, g->self[0], g->self[1]) = h;
  markDirtyAround(t);
}

void lowerCornerImpl(Tile *t, int c) {  // FUN_1000ccc0
  const CornerGeom *g = cornerGeom(c);
  if (!g) return;
  float h = (t->elev[c] - 1) * g_heightStep;
  float a = Y(t, g->nbrA[0], g->nbrA[1]), b = Y(t, g->nbrB[0], g->nbrB[1]);
  float centre = (h + Y(t, g->opp[0], g->opp[1])) * 0.5f;
  if (Y(t, 1, 1) <= centre) centre = Y(t, 1, 1);
  Y(t, 1, 1) = centre;
  Y(t, g->midA[0], g->midA[1]) = (h + a) * 0.5f;
  Y(t, g->midB[0], g->midB[1]) = (h + b) * 0.5f;
  t->elev[c]--;
  Y(t, g->self[0], g->self[1]) = h;
  if (t->type == kTypeBunker) {
    // Re-applies the bunker dip, but relative to the lowered corner's height
    // rather than the old centre, as the DLL does.
    float dip = h - 13.0f;
    Tile *n0 = t->nbr[0], *n1 = t->nbr[1], *n2 = t->nbr[2], *n3 = t->nbr[3];
    Y(t, 1, 1) = dip;
    if (n0 && typeOf(n0) == kTypeBunker) {
      Y(t, 1, 0) = dip;
      if (n2 && typeOf(n2) == kTypeBunker && typeOf(n0->nbr[2]) == kTypeBunker) Y(t, 0, 0) = dip;
      if (n3 && typeOf(n3) == kTypeBunker && typeOf(n0->nbr[3]) == kTypeBunker) Y(t, 2, 0) = dip;
    }
    if (n1 && typeOf(n1) == kTypeBunker) {
      Y(t, 1, 2) = dip;
      if (n2 && typeOf(n2) == kTypeBunker && typeOf(n1->nbr[2]) == kTypeBunker) Y(t, 0, 2) = dip;
      if (n3 && typeOf(n3) == kTypeBunker && typeOf(n1->nbr[3]) == kTypeBunker) Y(t, 2, 2) = dip;
    }
    if (n2 && typeOf(n2) == kTypeBunker) Y(t, 0, 1) = dip;
    if (n3 && typeOf(n3) == kTypeBunker) Y(t, 2, 1) = dip;
  }
  markDirtyAround(t);
}

// --- Normals ----------------------------------------------------------------

void faceNormal(TerrainTri &tri) {  // FUN_10011d60
  const float *p0 = g_pos[tri.v[0]], *p1 = g_pos[tri.v[1]], *p2 = g_pos[tri.v[2]];
  float a[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
  float b[3] = {p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]};
  float n[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  for (int i = 0; i < 3; i++) tri.faceNormal[i] = n[i] / l;
}

// FUN_10012cf0 and FUN_10011ef0: each of the tile's nine vertex normals is the
// normalised sum of the face normals of every triangle touching that lattice
// point, in this tile and in the (up to three) neighbours that share it.
// Vertices are duplicated per tile, so a cliff edge still averages across.
void calcTileNormals(Tile *t) {
  if (!t->normalsDirty) return;
  for (int sz = 0; sz < 3; sz++)
    for (int sx = 0; sx < 3; sx++) {
      int gx = t->x * 2 + sx, gz = t->y * 2 + sz;  // in half-tile units
      float sum[3] = {0, 0, 0};
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          bool centre = sx == 1 && sz == 1;
          if (centre && (dx || dy)) continue;
          if (sx == 1 && dx) continue;
          if (sz == 1 && dy) continue;
          if ((sx == 0 && dx > 0) || (sx == 2 && dx < 0)) continue;
          if ((sz == 0 && dy > 0) || (sz == 2 && dy < 0)) continue;
          Tile *n = tileAt(t->x + dx, t->y + dy);
          if (!n) continue;
          for (int i = 0; i < n->triCount; i++) {
            TerrainTri &tri = n->tri[i];
            bool touches = false;
            for (int k = 0; k < 3 && !touches; k++) {
              int v = tri.v[k];
              int vx = v % kStride, vz = v / kStride;  // global lattice coords
              int px = vx / 3 * 2 + vx % 3, pz = vz / 3 * 2 + vz % 3;
              touches = px == gx && pz == gz;
            }
            if (!touches) continue;
            faceNormal(tri);
            for (int c = 0; c < 3; c++) sum[c] += tri.faceNormal[c];
          }
        }
      float l = sqrtf(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
      float *n = g_nrm[vidx(t, sx, sz)];
      for (int c = 0; c < 3; c++) n[c] = sum[c] / l;
    }
  t->normalsDirty = 0;
}

// --- Texture choice ---------------------------------------------------------

// FUN_10015650: how a tile's edge meets a neighbour. 0 = same, 1 = a different
// blend group (hard edge), 2 = same group but a different type (or water of a
// different depth).
int edgeKind(const Tile *t, const Tile *n) {
  if (!n) return 0;
  if (t->type == kTypeWater) {
    if (t->variation == 0 && n->type != t->type) return 1;
    int nv = (int8_t)(n->variation & 0xff);
    if (t->variation == nv) return 0;
    if (t->variation == 0) return 2;
    if (t->variation == 1 && nv == 2) return 2;
    return 1;
  }
  auto group = [](int type) {
    return type >= 0 && type < (int)g_collar.size() ? g_collar[type] : 1000 + type;
  };
  if (group(t->type) != group(n->type)) return 1;
  return n->type != t->type ? 2 : 0;
}

int edgeCode(int a, int b, int c) {  // FUN_10013f00
  int code = a == 1 ? 4 : a == 2 ? 40 : 0;
  code += b == 1 ? 1 : b == 2 ? 10 : 0;
  code += c == 1 ? 2 : c == 2 ? 20 : 0;
  return code;
}

int edgeFrame(int code) {  // FUN_10013dd0
  switch (code) {
    case 3: case 43: return 2;
    case 4: case 14: case 24: case 34: return 4;
    case 5: case 7: case 25: return 3;
    case 6: case 16: return 1;
    case 30: return 6;
    case 40: case 41: case 42: case 61: return 8;
    case 50: case 52: case 70: return 7;
    case 60: return 5;
    default: return 0;
  }
}

// Desert water next to grass-like ground uses the WaterShallowDesert set.
bool grassy(const Tile *t) {  // FUN_100155b0
  int ty = t->type;
  return ty == 2 || ty == 7 || ty == 1 || ty == 0 || ty == 9 || ty == 8 || ty == 3;
}

// FUN_10012ec0. Slots go round the centre: 0 = tri2, 1 = tri3, ... 7 = tri1.
void setTriTexture(Tile *t, int set, int slot, int frame) {
  static const int kSlotTri[8] = {2, 3, 7, 6, 5, 4, 0, 1};
  switch (t->type) {
    case 1:
      if (t->variation & 0x80) set = 26;  // TrickyGreen
      break;
    case 6: case 21: set = 4; break;
    case 7: set = 27 + t->variation % 4; break;
    case 13: case 14: case 15: case 16: set = 13; break;
    case 17:
      if (t->variation == 1) set = 23;
      else if (t->variation == 2) set = 24;
      break;
    case 22:
      if (t->variation > 0x3f && t->variation < 0x48) set = 4;
      break;
  }
  if ((set == 4 || t->type == kTypeBunker || set == 2) && frame > 4) frame = 0;
  TerrainTri &tri = t->tri[kSlotTri[slot]];
  tri.tex = g_tex[set][t->texVariant][frame];
  tri.frame = (uint8_t)frame;
}

// FUN_10013670: picks the edge-transition frame of each triangle from the
// three neighbours touching it. Tees and pot bunkers keep fixed textures.
void updateTransitions(Tile *t, int set) {
  if (set == -1) set = t->type;
  if (set == kVoid || set == 9 || set == 0) return;
  Tile *n0 = t->nbr[0], *n1 = t->nbr[1], *n2 = t->nbr[2], *n3 = t->nbr[3];
  int e0 = edgeKind(t, n0), e2 = edgeKind(t, n2), e3 = edgeKind(t, n3), e1 = edgeKind(t, n1);
  int d02 = 0, d03 = 0, d12 = 0, d13 = 0;
  bool desert = g_courseType == 1 && set == kTypeWater;
  bool g0 = false, g1 = false, g2 = false, g3 = false;
  if (n0) {
    if (desert && grassy(n0)) g0 = true;
    if (n2) d02 = edgeKind(t, n0->nbr[2]);
    if (n3) d03 = edgeKind(t, n0->nbr[3]);
  }
  if (n1) {
    if (desert && grassy(n1)) g1 = true;
    if (n2) d12 = edgeKind(t, n1->nbr[2]);
    if (n3) d13 = edgeKind(t, n1->nbr[3]);
  }
  if (n2 && desert && grassy(n2)) g2 = true;
  if (n3 && desert && grassy(n3)) g3 = true;
  auto put = [&](bool desertSet, int slot, int a, int b, int c) {
    setTriTexture(t, desertSet ? 25 : set, slot, edgeFrame(edgeCode(a, b, c)));
  };
  put(g0, 7, e0, e2, d02);
  put(g0, 0, e0, e3, d03);
  put(g3, 1, e3, e0, d03);
  put(g3, 2, e3, e1, d13);
  put(g1, 3, e1, e3, d13);
  put(g1, 4, e1, e2, d12);
  put(g2, 6, e2, e0, d02);
  put(g2, 5, e2, e1, d12);
}

void updateTransitionsAround(Tile *t) {  // FUN_10013500
  Tile *n0 = t->nbr[0], *n1 = t->nbr[1], *n2 = t->nbr[2], *n3 = t->nbr[3];
  updateTransitions(t, t->type);
  if (n0) updateTransitions(n0, -1);
  if (n0 && n2) updateTransitions(n0->nbr[2], -1);
  if (n2) updateTransitions(n2, -1);
  if (n1 && n2) updateTransitions(n1->nbr[2], -1);
  if (n1) updateTransitions(n1, -1);
  if (n1 && n3) updateTransitions(n1->nbr[3], -1);
  if (n3) updateTransitions(n3, -1);
  if (n0 && n3) updateTransitions(n0->nbr[3], -1);
}

void setPathOverlayFor(Tile *t, int type) {  // FUN_1000d480
  if (t->hasPath) {
    bool hidden = type == kVoid || type == kTypeWater || type == 22 || type == 0 || type == 2 ||
                  type == 1 || type == 3 || type == kTypeBunker || type == 9;
    t->drawPathOverlay = !hidden;
  }
  updateTransitionsAround(t);
}

void setFixedTexture(Tile *t) {  // FUN_10015230: tees by variation, pot bunkers frame 3
  if (t->hasPath) t->drawPathOverlay = 0;
  for (int i = 0; i < t->triCount; i++)
    t->tri[i].tex = t->type != 0                        ? g_tex[t->type][t->texVariant][3]
                    : (unsigned)t->variation < 25 ? g_tex[0][t->variation][0]
                                                  : 0;
  updateTransitionsAround(t);
}

void changeType(Tile *t, int type) {  // FUN_10014020
  if (t->hasPath) t->drawPathOverlay = 1;
  if (t->type == kTypeBunker) shiftHollow(t, kTypeBunker, 13, 13);
  if (t->type == kTypeMound) shiftHollow(t, kTypeMound, -20, 20);
  else if (t->type == kTypeWater) t->normalsDirty = 1;
  if (t->type == kTypeBunker || t->type == kTypeMound) markDirtyAround(t);
  t->type = type;
  switch (type) {
    case 0: case 9: setFixedTexture(t); break;
    case 4: case kVoid: updateTransitionsAround(t); break;
    case kTypeMound:
      shiftHollow(t, kTypeMound, 20, 20);
      markDirtyAround(t);
      updateTransitionsAround(t);
      break;
    case kTypeBunker:  // FUN_1000d540
      t->drawPathOverlay = 0;
      shiftHollow(t, kTypeBunker, -13, -13);
      t->normalsDirty = 1;
      for (Tile *n : {t->nbr[0], t->nbr[1], t->nbr[2], t->nbr[3]})
        if (n && n->type == kTypeBunker) n->normalsDirty = 1;
      for (int i : {0, 1})
        if (Tile *n = t->nbr[i])
          for (int j : {2, 3})
            if (t->nbr[j] && n->nbr[j]->type == kTypeBunker) n->nbr[j]->normalsDirty = 1;
      updateTransitionsAround(t);
      break;
    case 13: case 14: case 15: case 16: setPathOverlayFor(t, 13); break;
    default: setPathOverlayFor(t, type); break;
  }
}

// --- Drawing ----------------------------------------------------------------

tgl::Vertex arrayVertex(int v, float u, float t) {  // glTexCoord + glArrayElement
  tgl::Vertex r;
  memcpy(r.pos, g_pos[v], sizeof r.pos);
  memcpy(r.normal, g_nrm[v], sizeof r.normal);
  r.uv[0] = u;
  r.uv[1] = t;
  return r;
}

tgl::Vertex vertex(float x, float y, float z, const float *n, float u, float t) {
  tgl::Vertex r = {{x, y, z}, {n[0], n[1], n[2]}, {u, t}};
  return r;
}

// FUN_100116d0: corner pieces joining path segments, by neighbour mask
// (1 = y-1, 2 = y+1, 4 = x+1, 8 = x-1).
void drawPathCorners(Tile *t, int mask, int set) {
  int a = 0, b = 0, c = 0, d = 0;  // quadrants (-x,-z), (+x,-z), (-x,+z), (+x,+z)
  switch (mask) {
    case 0: a = b = c = d = 3; break;
    case 1: c = d = 3; break;
    case 2: a = b = 3; break;
    case 4: c = a = 3; break;
    case 5: c = 3; b = 2; break;
    case 6: a = 3; d = 2; break;
    case 7: b = d = 2; break;
    case 8: d = b = 3; break;
    case 9: d = 3; a = 2; break;
    case 10: b = 3; c = 2; break;
    case 11: a = c = 2; break;
    case 13: a = b = 2; break;
    case 14: c = d = 2; break;
    case 15: a = b = c = d = 2; break;
    default: return;
  }
  int ctr = vidx(t, 1, 1);
  if (a) {
    g_gl.bindTexture(g_tex[set][0][a]);
    g_gl.triangle(arrayVertex(vidx(t, 0, 1), 0, .5f), arrayVertex(ctr, .5f, .5f), arrayVertex(vidx(t, 1, 0), .5f, 1));
  }
  if (b) {
    g_gl.bindTexture(g_tex[set][0][b]);
    g_gl.triangle(arrayVertex(vidx(t, 1, 0), .5f, 1), arrayVertex(ctr, .5f, .5f), arrayVertex(vidx(t, 2, 1), 1, .5f));
  }
  if (c) {
    g_gl.bindTexture(g_tex[set][0][c]);
    g_gl.triangle(arrayVertex(vidx(t, 1, 2), .5f, 0), arrayVertex(ctr, .5f, .5f), arrayVertex(vidx(t, 0, 1), 0, .5f));
  }
  if (d) {
    g_gl.bindTexture(g_tex[set][0][d]);
    g_gl.triangle(arrayVertex(vidx(t, 2, 1), 1, .5f), arrayVertex(ctr, .5f, .5f), arrayVertex(vidx(t, 1, 2), .5f, 0));
  }
}

void quad(const tgl::Vertex &a, const tgl::Vertex &b, const tgl::Vertex &c, const tgl::Vertex &d) {
  g_gl.triangle(a, b, c);  // GL_TRIANGLE_FAN
  g_gl.triangle(a, c, d);
}

// FUN_100108f0: alpha-blended path strips from the centre toward each
// neighbour that also has a path, then the corner pieces.
void drawPath(Tile *t) {
  if (!t->drawPathOverlay) return;
  const float w = 16.67f, lo = 0.333f, hi = 0.667f;  // 0x3eaa7efa, 0x3f2ac083
  bool p0 = t->nbr[0] && t->nbr[0]->hasPath, p2 = t->nbr[2] && t->nbr[2]->hasPath;
  bool p3 = t->nbr[3] && t->nbr[3]->hasPath, p1 = t->nbr[1] && t->nbr[1]->hasPath;
  int set = t->pathVariant ? 33 : 32;
  int mask = 0;
  int ci = vidx(t, 1, 1);
  const float *c = g_pos[ci], *cn = g_nrm[ci];
  g_gl.blend = true;
  g_gl.bindTexture(g_tex[set][0][0]);
  if (p0) {
    mask += 1;
    int e = vidx(t, 1, 0);
    quad(vertex(c[0] - w, c[1], c[2], cn, lo, hi), vertex(c[0] + w, c[1], c[2], cn, hi, hi),
         vertex(c[0] + w, g_pos[e][1], g_pos[e][2], g_nrm[e], hi, 1),
         vertex(c[0] - w, g_pos[e][1], g_pos[e][2], g_nrm[e], lo, 1));
  }
  if (p1) {
    mask += 2;
    int e = vidx(t, 1, 2);
    quad(vertex(c[0] - w, c[1], c[2], cn, lo, lo),
         vertex(g_pos[e][0] - w, g_pos[e][1], g_pos[e][2], g_nrm[e], lo, 0),
         vertex(g_pos[e][0] + w, g_pos[e][1], g_pos[e][2], g_nrm[vidx(t, 2, 2)], hi, 0),
         vertex(c[0] + w, c[1], c[2], cn, hi, lo));
  }
  if (p3) {
    mask += 4;
    int e = vidx(t, 2, 1);
    quad(vertex(c[0], c[1], c[2] - w, cn, hi, hi), vertex(c[0], c[1], c[2] + w, cn, hi, lo),
         vertex(g_pos[e][0], g_pos[e][1], g_pos[e][2] + w, g_nrm[e], 1, lo),
         vertex(g_pos[e][0], g_pos[e][1], g_pos[e][2] - w, g_nrm[e], 1, hi));
  }
  if (p2) {
    mask += 8;
    int e = vidx(t, 0, 1);
    quad(vertex(c[0], c[1], c[2] - w, cn, lo, hi),
         vertex(g_pos[e][0], g_pos[e][1], g_pos[e][2] - w, g_nrm[e], 0, hi),
         vertex(g_pos[e][0], g_pos[e][1], g_pos[e][2] + w, g_nrm[e], 0, lo),
         vertex(c[0], c[1], c[2] + w, cn, lo, lo));
  }
  drawPathCorners(t, mask, set);
  g_gl.blend = false;
}

// One wall: two pairs of triangles between this tile's edge (top) and the
// neighbour's facing edge, with a fixed normal. `self`/`other` are the three
// lattice points along each edge, in the DLL's vertex order.
void drawWall(Tile *t, Tile *n, int type, const float normal[3], const int self[3][2], const int other[3][2]) {
  g_gl.bindTexture(type == 1 ? g_tex[35][0][0] : g_tex[34][0][0]);
  auto o = [&](int i, float u, float v) {
    const float *p = g_pos[vidx(n, other[i][0], other[i][1])];
    return vertex(p[0], p[1], p[2], normal, u, v);
  };
  auto s = [&](int i, float u, float v) {
    const float *p = g_pos[vidx(t, self[i][0], self[i][1])];
    return vertex(p[0], p[1], p[2], normal, u, v);
  };
  g_gl.triangle(o(0, 0, 1), s(0, 0, 0), s(1, .5f, 0));
  g_gl.triangle(o(0, 0, 1), s(1, .5f, 0), o(1, .5f, 1));
  g_gl.triangle(o(1, .5f, 1), s(1, .5f, 0), s(2, 1, 0));
  g_gl.triangle(o(1, .5f, 1), s(2, 1, 0), o(2, 1, 1));
}

void drawWalls(Tile *t) {  // FUN_1000f7f0
  int k = (g_rot == 0 || g_rot == 2) ? 0 : 1;
  if (t->wall[0] && t->nbr[0]) {
    static const int s[3][2] = {{0, 0}, {1, 0}, {2, 0}}, o[3][2] = {{0, 2}, {1, 2}, {2, 2}};
    drawWall(t, t->nbr[0], t->wallType[0], kNormalYMinus[k], s, o);
  }
  if (t->wall[6] && t->nbr[2]) {
    static const int s[3][2] = {{0, 2}, {0, 1}, {0, 0}}, o[3][2] = {{2, 2}, {2, 1}, {2, 0}};
    drawWall(t, t->nbr[2], t->wallType[6], kNormalXMinus[k], s, o);
  }
  if (t->wall[2] && t->nbr[3]) {
    static const int s[3][2] = {{2, 0}, {2, 1}, {2, 2}}, o[3][2] = {{0, 0}, {0, 1}, {0, 2}};
    drawWall(t, t->nbr[3], t->wallType[2], kNormalXPlus[k], s, o);
  }
  if (t->wall[4] && t->nbr[1]) {
    static const int s[3][2] = {{2, 2}, {1, 2}, {0, 2}}, o[3][2] = {{2, 0}, {1, 0}, {0, 0}};
    drawWall(t, t->nbr[1], t->wallType[4], kNormalYPlus[k], s, o);
  }
}

// FUN_1000ea30: earth "strata" skirts down to y = -75 along edges that face a
// void tile and the camera, recursing along the edge for up to `depth` tiles.
void drawStrata(Tile *t, int depth) {
  if (t->type == kVoid) return;
  int k = (g_rot == 0 || g_rot == 2) ? 0 : 1;
  const float bottom = -75.0f;
  g_gl.bindTexture(g_tex[36][0][0]);
  auto skirt = [&](const float normal[3], int a[2], int b[2]) {
    const float *p = g_pos[vidx(t, a[0], a[1])], *q = g_pos[vidx(t, b[0], b[1])];
    tgl::Vertex ta = vertex(p[0], p[1], p[2], normal, 0, 1), ba = vertex(p[0], bottom, p[2], normal, 0, 0);
    tgl::Vertex bb = vertex(q[0], bottom, q[2], normal, 1, 0), tb = vertex(q[0], q[1], q[2], normal, 1, 1);
    g_gl.triangle(ta, ba, bb);
    g_gl.triangle(ta, bb, tb);
  };
  Tile *n0 = t->nbr[0], *n1 = t->nbr[1], *n2 = t->nbr[2], *n3 = t->nbr[3];
  if (n1 && isVoid(n1) && (g_rot == 0 || g_rot == 3)) {
    int a[2] = {0, 2}, b[2] = {2, 2};
    skirt(kNormalYMinus[k], a, b);
    if (g_rot == 0) { if (n3 && depth > 0) drawStrata(n3, depth - 1); }
    else if (n2 && depth > 0) drawStrata(n2, depth - 1);
  }
  if (n0 && isVoid(n0) && (g_rot == 1 || g_rot == 2)) {
    int a[2] = {2, 0}, b[2] = {0, 0};
    skirt(kNormalYPlus[k], a, b);
    if (g_rot == 1) { if (n3 && depth > 0) drawStrata(n3, depth - 1); }
    else if (n2 && depth > 0) drawStrata(n2, depth - 1);
  }
  if (n2 && isVoid(n2) && (g_rot == 0 || g_rot == 1)) {
    int a[2] = {0, 0}, b[2] = {0, 2};
    skirt(kNormalXPlus[k], a, b);
    if (g_rot == 0) { if (n0 && depth > 0) drawStrata(n0, depth - 1); }
    else if (n1 && depth > 0) drawStrata(n1, depth - 1);
  }
  if (n3 && isVoid(n3) && (g_rot == 2 || g_rot == 3)) {
    int a[2] = {2, 2}, b[2] = {2, 0};
    skirt(kNormalXMinus[k], a, b);
    if (g_rot == 2) { if (n1 && depth > 0) drawStrata(n1, depth - 1); }
    else if (n0 && depth > 0) drawStrata(n0, depth - 1);
  }
}

void drawTile(Tile *t) {  // FUN_1000e6c0
  if (!t) return;
  for (int i = 0; i < t->triCount; i++) {
    TerrainTri &tri = t->tri[i];
    unsigned tex = tri.tex;
    if (g_rot != 0 && t->type == kTypeBunker) {
      // Bunker art is drawn per screen orientation, so turn the set with the view.
      int o = t->variation % 4 - g_rot;
      if (o < 0) o += 4;
      tex = g_tex[27 + o][t->texVariant][tri.frame];
    }
    g_gl.bindTexture(tex);
    tgl::Vertex v[3];
    for (int k = 0; k < 3; k++) {
      const float *uv = kUV[tri.uvSel[k][0]][tri.uvSel[k][1]][g_rot];
      v[k] = arrayVertex(tri.v[k], uv[0], uv[1]);
    }
    g_gl.triangle(v[0], v[1], v[2]);
  }
  if (t->hasPath && t->drawPathOverlay) drawPath(t);
  drawWalls(t);
  drawStrata(t, 3);
}

// FUN_10006850: true when a tile is certainly off screen. Works in screen
// pixels from the tile diamond size, and never culls at 1/4 zoom.
bool culled(const Tile *t, const Tile *centre, float rot) {
  const TerrainState *s = g_inst;
  if (s->zoomShift == 2) return false;
  int mx = (s->screenW >> 1) * s->zoomShift, my = (s->screenH >> 1) * s->zoomShift;
  int dy = t->y - centre->y, dx = t->x - centre->x;
  int tw = 64, th = 40, hw = 32, hh = 20;  // 800x600 (also the fallback)
  if (s->screenW == 1024 && s->screenH == 768) { tw = 80; th = 52; hw = 40; hh = 26; }
  else if (s->screenW == 1280 && s->screenH == 1024) { tw = 104; th = 68; hw = 52; hh = 34; }
  int W = s->screenW, H = s->screenH, lift = maxCorner(t) * -5;
  if (rot == 0.0f) {
    int x = (W >> 1) + dy * hw + dx * hw, y = (H >> 1) + dy * hh - dx * hh + lift;
    return W + mx < x || x < -mx - tw || H + th + my < y || y < -my - th;
  }
  if (rot == 90.0f) {
    int x = (W >> 1) - dy * hw + dx * hw, y = (H >> 1) - dy * hh - dx * hh + lift;
    return W + hw + mx < x || x < -mx - tw || H + th + my < y || y < -my - th;
  }
  if (rot == 180.0f) {
    int x = (W >> 1) - dy * hw - dx * hw, y = (H >> 1) - dy * hh + dx * hh + lift;
    return W + tw + mx < x || x < -mx - hw || H + th + my < y || y < -my - th;
  }
  int x = (W >> 1) + dy * hw - dx * hw, y = (H >> 1) + dy * hh + dx * hh + lift;
  return W + hw + mx < x || x < -mx - hw || H + hh + my < y || y < (-hh - th) - my;
}

// FUN_100381a0: insert into the render list in back-to-front order for the
// rotation, skipping duplicates.
void enqueue(Tile *t, float rot) {
  auto before = [rot](const Tile *a, const Tile *b) {  // a is drawn before b
    if (rot == 0.0f) return a->x != b->x ? a->x > b->x : a->y < b->y;
    if (rot == 90.0f) return a->y != b->y ? a->y > b->y : a->x > b->x;
    if (rot == 180.0f) return a->x != b->x ? a->x < b->x : a->y > b->y;
    return a->y != b->y ? a->y < b->y : a->x < b->x;
  };
  auto it = g_queue.begin();
  while (it != g_queue.end() && before(*it, t)) ++it;
  if (it != g_queue.end() && (*it)->x == t->x && (*it)->y == t->y) return;
  g_queue.insert(it, t);
}

// FUN_10007380: a tile with a wall facing the camera hides part of the tiles
// in front of it, so those get redrawn too (recursively).
void enqueueWallDependants(Tile *t, Tile *centre, float rot) {
  int x = t->x, y = t->y;
  Tile *dep[3] = {nullptr, nullptr, nullptr};
  if (rot == 0.0f) {
    if (t->wall[6]) { dep[0] = tileAt(x - 1, y); dep[2] = tileAt(x - 1, y + 1); }
    if (t->wall[4]) { dep[1] = tileAt(x, y + 1); dep[2] = tileAt(x - 1, y + 1); }
  } else if (rot == 90.0f) {
    if (t->wall[6]) { dep[0] = tileAt(x - 1, y); dep[2] = tileAt(x - 1, y - 1); }
    if (t->wall[0]) { dep[1] = tileAt(x, y - 1); dep[2] = tileAt(x - 1, y - 1); }
  } else if (rot == 180.0f) {
    if (t->wall[2]) { dep[0] = tileAt(x + 1, y); dep[2] = tileAt(x + 1, y - 1); }
    if (t->wall[0]) { dep[1] = tileAt(x, y - 1); dep[2] = tileAt(x + 1, y - 1); }
  } else {
    if (t->wall[2]) { dep[0] = tileAt(x + 1, y); dep[2] = tileAt(x + 1, y + 1); }
    if (t->wall[4]) { dep[1] = tileAt(x, y + 1); dep[2] = tileAt(x + 1, y + 1); }
  }
  for (Tile *d : dep)
    if (d && !isVoid(d) && !culled(d, centre, rot)) {
      enqueue(d, rot);
      enqueueWallDependants(d, centre, rot);
    }
}

void flushQueue() {
  for (Tile *t : g_queue) drawTile(t);
  g_queue.clear();
}

// The camera every 3D entry point sets up: pitch, then 45 degrees plus the
// view rotation about y, then the centre tile's corner to the origin.
void beginView(const Tile *anchor, const Tile *centre, float rot) {
  g_gl.loadIdentity();
  g_gl.pushMatrix();
  g_gl.rotate(g_pitch, 1, 0, 0);
  g_gl.rotate(45.0f + rot, 0, 1, 0);
  if (anchor && centre) g_gl.translate((float)(25 - centre->x) * 100.0f, 0, (float)(25 - centre->y) * 100.0f);
}

void endView() { g_gl.popMatrix(); }

// --- Files ------------------------------------------------------------------

bool readFile(const char *path, std::vector<uint8_t> &out) {
  FILE *f = terrain_fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? (size_t)n : 0);
  bool ok = n > 0 && fread(out.data(), 1, out.size(), f) == out.size();
  fclose(f);
  return ok;
}

uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

// FUN_1000bbd0: a 24-bit BMP as an RGB texture, MAG nearest, MIN linear,
// REPEAT. Rows stay bottom-up, as GL wants them. Returns 0 if missing.
unsigned loadBmp(const char *path) {
  std::vector<uint8_t> d;
  if (!readFile(path, d) || d.size() < 54 || d[0] != 'B' || d[1] != 'M') return 0;
  uint32_t off = le32(&d[10]);
  int32_t w = (int32_t)le32(&d[18]), h = (int32_t)le32(&d[22]);
  int bpp = le16(&d[28]);
  if (w <= 0 || h == 0 || (bpp != 24 && bpp != 32) || le32(&d[30]) != 0) return 0;
  bool topDown = h < 0;
  if (topDown) h = -h;
  size_t stride = ((size_t)w * (bpp / 8) + 3) & ~(size_t)3;
  if (off + stride * h > d.size()) return 0;
  tgl::Texture tex;
  tex.w = w;
  tex.h = h;
  tex.magLinear = false;
  tex.minLinear = true;
  tex.rgba.resize((size_t)w * h * 4);
  for (int y = 0; y < h; y++) {
    const uint8_t *src = &d[off + stride * (topDown ? h - 1 - y : y)];
    for (int x = 0; x < w; x++) {
      uint8_t *p = &tex.rgba[((size_t)y * w + x) * 4];
      p[0] = src[x * (bpp / 8) + 2];
      p[1] = src[x * (bpp / 8) + 1];
      p[2] = src[x * (bpp / 8)];
      p[3] = 255;
    }
  }
  return g_gl.createTexture(std::move(tex));
}

// FUN_1000be30: an uncompressed true-colour TGA (paths), linear filtering.
// The header must be exactly {0,0,2,0...}; rows are used as stored.
unsigned loadTga(const char *path) {
  std::vector<uint8_t> d;
  static const uint8_t kHeader[12] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  if (!readFile(path, d) || d.size() < 18 || memcmp(d.data(), kHeader, 12) != 0) return 0;
  int w = le16(&d[12]), h = le16(&d[14]), bpp = d[16];
  if (!w || !h || (bpp != 24 && bpp != 32)) return 0;
  size_t n = (size_t)w * h * (bpp / 8);
  if (18 + n > d.size()) return 0;
  tgl::Texture tex;
  tex.w = w;
  tex.h = h;
  tex.hasAlpha = bpp == 32;
  tex.magLinear = tex.minLinear = true;
  tex.rgba.resize((size_t)w * h * 4);
  for (size_t i = 0; i < (size_t)w * h; i++) {
    const uint8_t *s = &d[18 + i * (bpp / 8)];
    uint8_t *p = &tex.rgba[i * 4];
    p[0] = s[2];
    p[1] = s[1];
    p[2] = s[0];
    p[3] = bpp == 32 ? s[3] : 255;
  }
  return g_gl.createTexture(std::move(tex));
}

void freeTextures(TerrainState *s) {  // FUN_100380a0
  if (!s->texturesLoaded) return;
  for (int set = 0; set < 37; set++) {
    for (int v = 0; v < 25; v++)
      for (int f = 0; f < 9; f++) {
        g_gl.deleteTexture(g_tex[set][v][f]);
        g_tex[set][v][f] = 0;
      }
    s->sets[set].numVariants = 0;
  }
  s->texturesLoaded = 0;
}

// FUN_100076e0: every ground set as <Name><A..E>000<1..9>.bmp (variants stop
// at the first missing frame-1 file), 25 tee variants, then paths, walls and
// strata. Missing files leave texture 0.
void loadTextures(TerrainState *s) {
  freeTextures(s);
  static const char *const kCourse[] = {"Parkland/", "Desert/", "Tropical/", "Links/"};
  std::string base = std::string("./Data/Textures/") +
                     (g_courseType >= 0 && g_courseType < 4 ? kCourse[g_courseType] : "Parkland/");
  int tees = 0;
  {
    std::string p = base + s->sets[0].name + "A0001.bmp";
    size_t units = p.size() - 5, tens = p.size() - 6;
    for (char c = '1'; c <= '9'; c++) {
      p[units] = c;
      unsigned id = loadBmp(p.c_str());
      g_tex[0][tees++][0] = id;
    }
    for (char t = '1'; t <= '2'; t++) {
      p[tens] = t;
      for (char c = '0'; c <= '9'; c++)
        if (tees < 25) {
          p[units] = c;
          g_tex[0][tees++][0] = loadBmp(p.c_str());
        }
    }
  }
  for (int set = 1; set <= 30; set++) {
    for (char letter = 'A'; letter < 'F'; letter++) {
      std::string p = base + s->sets[set].name + letter + "0001.bmp";
      unsigned id = loadBmp(p.c_str());
      if (!id) break;
      int v = s->sets[set].numVariants;
      g_tex[set][v][0] = id;
      for (char f = '2'; f <= '9'; f++) {
        p[p.size() - 5] = f;
        g_tex[set][v][f - '1'] = loadBmp(p.c_str());
      }
      s->sets[set].numVariants++;
    }
  }
  static const char *const kPath[] = {"/Path.tga", "/PathCap.tga", "/PathInside.tga", "/PathCurve.tga"};
  static const char *const kPathX[] = {"/PathX.tga", "/PathCapX.tga", "/PathInsideX.tga", "/PathCurveX.tga"};
  for (int f = 0; f < 4; f++) g_tex[32][0][f] = loadTga((base + kPath[f]).c_str());
  s->sets[32].numVariants++;
  for (int f = 0; f < 4; f++) g_tex[33][0][f] = loadTga((base + kPathX[f]).c_str());
  s->sets[33].numVariants++;
  g_tex[35][0][0] = loadBmp((base + "/CliffTest.bmp").c_str());
  s->sets[35].numVariants++;
  g_tex[34][0][0] = loadBmp((base + "/RetainWallA.bmp").c_str());
  s->sets[34].numVariants++;
  g_tex[36][0][0] = loadBmp((base + "/strata.bmp").c_str());
  s->sets[36].numVariants++;
  s->texturesLoaded = 1;
}

// FUN_10006dd0: #AMBIENT, #DIFFUSE and #SPECULAR (each "r g b" on the next
// line, 0..255) for light 0, at a fixed direction. Without the file, light 0
// stays off and the course is lit by the global ambient alone, as in the DLL.
void loadLighting() {
  static const char *const kFiles[] = {"ParklandLighting.txt", "DesertLighting.txt", "TropicalLighting.txt",
                                       "LinksLighting.txt"};
  const char *name = g_courseType >= 0 && g_courseType < 4 ? kFiles[g_courseType] : "lighting.txt";
  FILE *f = terrain_fopen(name, "rb");
  if (!f) return;
  char line[256];
  auto next = [&]() -> bool {
    if (!fgets(line, sizeof line, f)) return false;
    line[strcspn(line, "\r\n")] = 0;
    return true;
  };
  auto find = [&](const char *tag) {
    while (next()) {
      char copy[256];
      memcpy(copy, line, sizeof copy);
      char *tok = strtok(copy, " ");
      if (tok && strcmp(tok, tag) == 0) return true;
    }
    return false;
  };
  auto rgb = [&](float out[4]) {
    if (!next()) return false;
    char *tok = strtok(line, " ");
    for (int i = 0; i < 3; i++) {
      out[i] = (tok ? atoi(tok) : 0) / 255.0f;
      tok = strtok(nullptr, " ");
    }
    out[3] = 1;
    return true;
  };
  float amb[4], dif[4], spe[4];
  bool ok = find("#AMBIENT") && rgb(amb) && find("#DIFFUSE") && rgb(dif) && find("#SPECULAR") && rgb(spe);
  fclose(f);
  if (!ok) return;
  // FUN_10003a50 rotates (-0.5, 0.1, -1) by 40 degrees about x and then 45
  // about y, reusing the already-updated component in the second line of
  // each rotation (a bug the light direction depends on).
  float p[4] = {-0.5f, 0.1f, -1.0f, 0.0f};
  float c = cosf(40.0f * (float)M_PI / 180), s = sinf(40.0f * (float)M_PI / 180);
  p[1] = -s * p[2] + c * p[1];
  p[2] = c * p[2] + s * p[1];
  c = cosf(45.0f * (float)M_PI / 180);
  s = sinf(45.0f * (float)M_PI / 180);
  p[0] = s * p[2] + c * p[0];
  p[2] = c * p[2] + -s * p[0];
  float l = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
  for (int i = 0; i < 3; i++) p[i] /= l;
  tgl::Light &l0 = g_gl.lights[0];
  g_gl.setLightPosition(0, p);
  memcpy(l0.ambient, amb, sizeof amb);
  memcpy(l0.diffuse, dif, sizeof dif);
  memcpy(l0.specular, spe, sizeof spe);
  l0.enabled = true;
  for (int i = 0; i < 4; i++) g_gl.matSpecular[i] = 1;
  g_gl.matShininess = 13;
}

// The DLL's resize() extents: 1 pixel = 2.2097 units at 800x600.
void extentsFor(int w, int h, float &x, float &y) {
  if (w == 1024 && h == 768) { x = 1810.1934f; y = 1357.645f; }
  else if (w == 1280 && h == 1024) { x = 1740.5706f; y = 1392.4564f; }
  else { x = 1767.767f; y = 1325.8252f; }
}

}  // namespace

// --- Port glue ----------------------------------------------------------------

void terrain_set_surface(const TerrainSurface *surface) {
  if (surface) g_surface = *surface;
  else g_surface = TerrainSurface{};
  g_gl.setTarget(g_surface.pixels, g_surface.pitch, g_surface.width, g_surface.height);
}

void terrain_srand(unsigned seed) { g_holdrand = seed; }

tgl::Context &terrain_gl() { return g_gl; }

FILE *terrain_fopen(const char *path, const char *mode) {
#ifdef SIMGOLF_PORT_FS
  // In the game build, files are fetched on demand through the port's FS.
  char real[512];
  return fs_resolve(path, real, sizeof real, 0) ? fopen(real, mode) : nullptr;
#endif
  if (FILE *f = fopen(path, mode)) return f;
  // Resolve each component case-insensitively, accepting '\' as a separator.
  std::string p(path);
  for (char &c : p)
    if (c == '\\') c = '/';
  std::string dir = p[0] == '/' ? "/" : ".";
  size_t i = p[0] == '/' ? 1 : 0;
  while (i <= p.size()) {
    size_t j = p.find('/', i);
    if (j == std::string::npos) j = p.size();
    std::string part = p.substr(i, j - i);
    i = j + 1;
    if (part.empty() || part == ".") continue;
    std::string found;
    if (DIR *d = opendir(dir.c_str())) {
      while (dirent *e = readdir(d))
        if (strcasecmp(e->d_name, part.c_str()) == 0) {
          found = e->d_name;
          break;
        }
      closedir(d);
    }
    if (found.empty()) return nullptr;
    dir = dir == "/" ? "/" + found : dir + "/" + found;
  }
  return fopen(dir.c_str(), mode);
}

// --- Terrain ------------------------------------------------------------------

Terrain *Terrain::getInstance() {
  if (!g_inst) {
    // FUN_10002ae0. The DLL also tries to load textures here, before any GL
    // context exists; initSystem reloads them, so the port skips that pass.
    TerrainState *s = (TerrainState *)calloc(1, sizeof(TerrainState));
    s->w = TERRAIN_W;
    s->h = TERRAIN_H;
    s->ambient[0] = s->ambient[1] = 0.15f;
    s->ambient[2] = 0.2f;
    s->ambient[3] = 1;
    for (int x = 0; x < s->w; x++)
      for (int y = 0; y < s->h; y++) resetTile(&s->tiles[y * s->w + x], x, y);
    static const struct { int set; const char *name; } kNames[] = {
      {0, "Tee"}, {1, "PuttingGreen"}, {2, "Fairway"}, {3, "FirmFairway"}, {4, "Rough"}, {5, "DeepRough"},
      {8, "GrassySand"}, {9, "PotSandBunker"}, {10, "Overgrowth"}, {11, "Brush"}, {12, "Rock"},
      {13, "Woods"}, {17, "WaterShallow"}, {18, "Marsh"}, {19, "Overgrowth"}, {22, "Building"},
      {24, "WaterDeep"}, {23, "WaterMiddle"}, {25, "WaterShallowDesert"}, {26, "TrickyGreen"},
      {27, "SandBunker1"}, {28, "SandBunker2"}, {29, "SandBunker3"}, {30, "SandBunker4"},
    };
    for (auto &n : kNames) strcpy(s->sets[n.set].name, n.name);
    g_courseType = 0;
    g_inst = s;
  }
  return (Terrain *)g_inst;
}

Terrain::~Terrain() {
  TerrainState *s = (TerrainState *)this;
  s->w = s->h = -1;
  g_collar.clear();
  freeTextures(s);
  g_queue.clear();
  // Like the DLL, the destructor only forgets the singleton; the memory is
  // the caller's (golf calls ~Terrain at shutdown and never deletes it).
  if (g_inst == s) g_inst = nullptr;
}

void Terrain::initSystem(int w, int h, void *hdc, bool flip) {
  TerrainState *s = (TerrainState *)this;
  if (s->glrc) return;
  if (hdc) {
    terrain_set_surface((const TerrainSurface *)hdc);
    s->glrc = this;
  }
  // The port always renders upright into a top-down buffer, which is what the
  // DLL's `flip` (bottom/top swapped, clockwise front faces) achieved on
  // Windows 2000+, so the flag is only stored.
  s->flip = flip;
  g_gl.texture2D = true;  // FUN_100033e0
  g_gl.lighting = true;
  g_gl.cullFace = true;
  g_gl.frontFaceCCW = true;
  loadLighting();
  loadTextures(s);
  resize(w, h);
  s->screenW = w;
  s->screenH = h;
  if (w == 800 && h == 600) g_pitch = 38.68219f;          // 0x421aba8f
  else if (w == 1024 && h == 768) g_pitch = 40.54160f;    // 0x42222a9a
  else if (w == 1280 && h == 1024) g_pitch = 40.83222f;   // 0x42235431
}

void Terrain::initTerrain() { buildGrid((TerrainState *)this); }

void Terrain::closeSystem() {
  TerrainState *s = (TerrainState *)this;
  if (!s->glrc) return;
  s->glrc = nullptr;
  freeTextures(s);
}

void Terrain::resetTerrain() {
  TerrainState *s = (TerrainState *)this;
  for (int x = 0; x < s->w; x++)
    for (int y = 0; y < s->h; y++) resetTile(&s->tiles[y * s->w + x], x, y);
  buildGrid(s);
}

void Terrain::resize(int w, int h) {
  if (h == 0) h = 1;
  if (w == 0) w = 1;
  g_gl.viewport(0, 0, w, h);
  g_gl.matrixMode(tgl::Context::PROJECTION);
  g_gl.loadIdentity();
  if (w == 800 && h == 600) { w = 1767; h = 1325; }
  else if (w == 1024 && h == 768) { w = 1810; h = 1303; }
  else if (w == 1280 && h == 1024) { w = 1740; h = 1392; }
  // Integer halves, so the window is one unit off-centre (-884..883).
  g_gl.ortho(-w >> 1, w >> 1, -h >> 1, h >> 1, 5000, -5000);
  g_gl.matrixMode(tgl::Context::MODELVIEW);
  g_gl.loadIdentity();
}

void Terrain::loadNewCourseType(int courseType) {
  if (g_courseType == courseType) return;
  g_courseType = courseType;
  loadLighting();
  freeTextures((TerrainState *)this);
  loadTextures((TerrainState *)this);
}

void Terrain::passCollarInfo(int *const groups, int n) { g_collar.assign(groups, groups + (n > 0 ? n : 0)); }

void Terrain::changeLighting(int brighter) {
  TerrainState *s = (TerrainState *)this;
  if (!brighter) {
    if (0.1f < s->ambient[0])
      for (int i = 0; i < 3; i++) s->ambient[i] -= 0.1f;
  } else if (s->ambient[0] < 1.0f) {
    for (int i = 0; i < 3; i++) s->ambient[i] += 0.1f;
  }
  float p[4] = {-0.5f, 0.1f, -1.0f, 0.0f};
  float c = cosf(40.0f * (float)M_PI / 180), sn = sinf(40.0f * (float)M_PI / 180);
  p[1] = -sn * p[2] + c * p[1];
  p[2] = c * p[2] + sn * p[1];
  c = cosf(45.0f * (float)M_PI / 180);
  sn = sinf(45.0f * (float)M_PI / 180);
  p[0] = sn * p[2] + c * p[0];
  p[2] = c * p[2] + -sn * p[0];
  float l = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
  for (int i = 0; i < 3; i++) p[i] /= l;
  tgl::Light &l0 = g_gl.lights[0];
  g_gl.setLightPosition(0, p);
  memcpy(l0.ambient, s->ambient, sizeof l0.ambient);
  for (int i = 0; i < 3; i++) {
    l0.diffuse[i] = 0.9f;
    l0.specular[i] = i == 2 ? 0.9f : 1.0f;
  }
  l0.enabled = true;
}

void Terrain::setZoomLevel(int zoom) {
  TerrainState *s = (TerrainState *)this;
  g_gl.matrixMode(tgl::Context::PROJECTION);
  g_gl.loadIdentity();
  float x, y;
  extentsFor(s->screenW, s->screenH, x, y);
  if (zoom == 1) {
    x *= 4;
    y *= 4;
    s->zoomShift = 2;
  } else if (zoom == 2) {
    x += x;
    y += y;
    s->zoomShift = 1;
  } else if (zoom == 4) {
    // resize() leaves MODELVIEW current, so the ortho below lands on the
    // modelview matrix, which the next render resets. Harmless, kept as is.
    resize(s->screenW, s->screenH);
    s->zoomShift = 0;
  }
  g_gl.ortho(-x * 0.5f, x * 0.5f, -y * 0.5f, y * 0.5f, 5000, -5000);
  g_gl.matrixMode(tgl::Context::MODELVIEW);
}

Tile *Terrain::tileAt(int x, int y) {
  TerrainState *s = (TerrainState *)this;
  if (x < 0 || y < 0 || x >= s->w || y >= s->h) return nullptr;
  return &s->tiles[y * s->w + x];
}

// A debug stub in the DLL (it prints the coordinates and returns whatever is
// left in a register); golf.exe never calls it.
Tile *Terrain::tileHit(int, int) { return nullptr; }

int Terrain::getElevation(Tile *t, int corner) { return t ? t->elev[corner] : 0; }
int Terrain::getType(Tile *t) { return t->type; }
int Terrain::getVariation(Tile *t) { return (int8_t)(t->variation & 0xff); }
bool Terrain::getWall(Tile *t, int dir) { return t->wall[dir] != 0; }
bool Terrain::hasPath(Tile *t) { return t->hasPath != 0; }
bool Terrain::hasConnectedPath(int x, int y) { return tileAt(x, y)->pathVariant != 0; }

void Terrain::setType(Tile *t, int type, int variation) {
  TerrainState *s = (TerrainState *)this;
  t->texVariant = s->sets[type].numVariants < 1 ? 0 : msvcRand() % s->sets[type].numVariants;
  t->variation = variation;
  changeType(t, type);
}

void Terrain::setWall(Tile *t, int dir, int wallType, bool on) {  // FUN_10015400
  t->wall[dir] = on;
  t->wallType[dir] = wallType;
}

void Terrain::layPath(Tile *t, int on, int variant) {  // FUN_10013400
  if (!on) {
    t->hasPath = t->pathVariant = t->drawPathOverlay = 0;
    return;
  }
  t->hasPath = 1;
  if (variant) t->pathVariant = 1;
  int ty = t->type;
  t->drawPathOverlay = !(ty == 22 || ty == 0 || ty == 2 || ty == 1 || ty == 3 || ty == 7 || ty == 9);
}

void Terrain::updatePath(int x, int y, int variant) { tileAt(x, y)->pathVariant = variant != 0; }

void Terrain::elevateCorner(Tile *t, int corner) {
  if (t) raiseCorner(t, corner);
}

void Terrain::lowerCorner(Tile *t, int corner) {
  if (t) lowerCornerImpl(t, corner);
}

// FUN_10038900 then lowerCorner. When the tiles beyond the lowered corner
// (for this rotation) are void or off the map, nothing would redraw the strip
// that the tile uncovers, so the tile's current footprint is painted almost
// black first: light 0 off, an all-zero light 1, the tile drawn at five offsets.
void Terrain::lowerEdgeCorner(Tile *t, int corner, Tile *centre, float rot) {
  if (!t) return;
  int x = t->x, y = t->y;
  int dx = (rot == 0.0f || rot == 90.0f) ? 1 : -1;
  int dy = (rot == 90.0f || rot == 180.0f) ? 1 : -1;
  auto solid = [&](int tx, int ty) {
    Tile *n = tileAt(tx, ty);
    return n && !isVoid(n);
  };
  bool blackout = !(solid(x + dx, y) && solid(x, y + dy) && solid(x + dx, y + dy));
  beginView(t, centre, rot);
  setRotation(rot);
  if (blackout) {
    tgl::Light &l1 = g_gl.lights[1];
    g_gl.lights[0].enabled = false;
    for (int i = 0; i < 4; i++) l1.ambient[i] = l1.diffuse[i] = l1.specular[i] = 0;
    l1.enabled = true;
    static const float kSteps[5][3] = {{10, 0, -10}, {-20, 0, 0}, {0, 0, 20}, {20, 0, 0}, {-10, 0, -10}};
    for (auto &st : kSteps) {
      g_gl.translate(st[0], st[1], st[2]);
      drawTile(t);
    }
    l1.enabled = false;
    g_gl.lights[0].enabled = true;
  }
  endView();
  lowerCornerImpl(t, corner);
}

void Terrain::calcNormals(Tile *t) {
  if (t) calcTileNormals(t);
}

void Terrain::calcAllNormals(Tile *centre) {
  TerrainState *s = (TerrainState *)this;
  int r = 13 << s->zoomShift;
  for (int y = centre->y - r; y < centre->y + r; y++)
    for (int x = centre->x + r; x > centre->x - r; x--)
      if (Tile *t = tileAt(x, y)) calcTileNormals(t);
}

bool Terrain::render(Tile *centre, float rot) {
  TerrainState *s = (TerrainState *)this;
  int r = 16 << s->zoomShift;
  int right = r, left = r, down = r, up = r;  // x+, x-, y+, y-
  beginView(centre, centre, rot);
  setRotation(rot);
  if (!centre) {
    for (int y = 10; y < 40; y++)
      for (int x = 40; x > 10; x--) drawTile(tileAt(x, y));
    endView();
    return true;
  }
  int cx = centre->x, cy = centre->y;
  if (cy < up) up = cy;
  if (50 - cy < down) down = 50 - cy;
  if (cx < left) left = cx;
  if (50 - cx < right) right = 50 - cx;
  auto visit = [&](int x, int y) {
    Tile *t = tileAt(x, y);
    if (t && !isVoid(t) && !culled(t, centre, rot)) drawTile(t);
  };
  if (rot == 0.0f) {
    for (int y = cy - up; y < cy + down; y++)
      for (int x = cx + right; cx - left <= x; x--) visit(x, y);
  } else if (rot == 90.0f) {
    for (int x = cx + right; cx - left <= x; x--)
      for (int y = cy + down; cy - up <= y; y--) visit(x, y);
  } else if (rot == 180.0f) {
    for (int y = cy + down; cy - up <= y; y--)
      for (int x = cx - left; x < cx + right; x++) visit(x, y);
  } else {
    for (int y = cy - up; y < cy + down; y++)
      for (int x = cx - left; x < cx + right; x++) visit(x, y);
  }
  endView();
  return true;
}

void Terrain::localRender(Tile *dirty, Tile *centre, float rot) {
  if (!dirty) return;
  beginView(dirty, centre, rot);
  setRotation(rot);
  int x0 = dirty->x, y0 = dirty->y;
  auto visit = [&](int x, int y) {
    Tile *t = tileAt(x, y);
    if (t && !isVoid(t)) {
      enqueue(t, rot);
      enqueueWallDependants(t, centre, rot);
    }
  };
  if (rot == 0.0f) {
    for (int y = y0 - 2; y <= y0 + 2; y++)
      for (int x = x0 + 2; x0 - 2 <= x; x--) visit(x, y);
  } else if (rot == 90.0f) {
    for (int x = x0 + 2; x0 - 2 <= x; x--)
      for (int y = y0 + 2; y0 - 2 <= y; y--) visit(x, y);
  } else if (rot == 180.0f) {
    for (int y = y0 + 2; y0 - 2 <= y; y--)
      for (int x = x0 - 2; x <= x0 + 2; x++) visit(x, y);
  } else {
    for (int y = y0 - 2; y <= y0 + 2; y++)
      for (int x = x0 - 2; x <= x0 + 2; x++) visit(x, y);
  }
  flushQueue();
  endView();
}

void Terrain::stripRender(Tile *centre, int dir, float rot) {
  TerrainState *s = (TerrainState *)this;
  int sh = s->zoomShift;
  beginView(centre, centre, rot);
  setRotation(rot);
  int cx = centre->x, cy = centre->y;
  auto visit = [&](int x, int y) {
    Tile *t = tileAt(x, y);
    if (x > -1 && y > -1 && t && !isVoid(t) && !culled(t, centre, rot)) {
      enqueue(t, rot);
      enqueueWallDependants(t, centre, rot);
    }
  };
  switch (dir) {
    case 1: stripRender(centre, 8, rot); stripRender(centre, 2, rot); break;
    case 2: {
      int back = 15 << sh, fwd = 3 << sh, left = (sh << sh) + 5, right = 4;
      for (int x0 = cx - left; x0 < cx + right; x0++)
        for (int y = cy - back, x = x0; y < cy + fwd && y < 50 && x < 50; x++, y++) visit(x, y);
      break;
    }
    case 3: stripRender(centre, 2, rot); stripRender(centre, 4, rot); break;
    case 4: {
      int a = 4 << sh, b = 16 << sh, fwd = 2, back = (sh << sh) + 9;
      for (int y0 = cy - back; y0 < cy + fwd; y0++)
        for (int x = cx + b, y = y0; cx - a < x && x > -1 && y < 50; x--, y++)
          if (y > -1 && x < 50) visit(x, y);
      break;
    }
    case 5: stripRender(centre, 4, rot); stripRender(centre, 6, rot); break;
    case 6: {
      int a = 3 << sh, b = 16 << sh, fwd = 4, back = (sh << sh) + 8;
      for (int y0 = cy - back; y0 < cy + fwd; y0++)
        for (int x = cx - b, y = y0; x < cx + a && y < 50 && x < 50; x++, y++) visit(x, y);
      break;
    }
    case 7: stripRender(centre, 8, rot); stripRender(centre, 6, rot); break;
    case 8: {
      int a = 4 << sh, b = 17 << sh, fwd = (sh << sh) + 12, back = -2;
      for (int y0 = cy + back; y0 < cy + fwd; y0++)
        for (int x = cx - b, y = y0; x < cx + a && y > -1 && x < 50; x++, y--)
          if (x > -1 && y < 50) visit(x, y);
      break;
    }
  }
  flushQueue();
  endView();
}

void Terrain::pathUpdateRender(Tile *centre, float rot) {
  TerrainState *s = (TerrainState *)this;
  int r = 15 << s->zoomShift;
  int right = r, left = r, down = r, up = r;
  // No setRotation here in the DLL: the rotation index of the last render stays.
  beginView(centre, centre, rot);
  int cx = centre->x, cy = centre->y;
  if (cy < up) up = cy;
  if (50 - cy < down) down = 50 - cy;
  if (cx < left) left = cx + 1;
  if (49 - cx < right) right = 49 - cx;
  for (int y = cy - up; y < cy + down; y++)
    for (int x = cx + right; cx - left < x; x--) {
      Tile *t = tileAt(x, y);
      if (t && t->hasPath && !isVoid(t) && !culled(t, centre, rot)) {
        enqueue(t, rot);
        enqueueWallDependants(t, centre, rot);
      }
    }
  flushQueue();
  endView();
}

void Terrain::renderTile(int type, int sx, int sy, int num, int den) {
  float k = (float)num / (float)den;
  g_gl.loadIdentity();
  g_gl.pushMatrix();
  g_gl.translate((float)(sx - 432) * 2.2097087f, (float)(sy - 300) * 2.2097087f, 0);
  g_gl.rotate(g_pitch, 1, 0, 0);
  g_gl.rotate(45, 0, 1, 0);
  g_gl.scale(k, k, k);
  if (type > 30) type = 30;  // an assert in the DLL
  g_gl.lighting = false;
  g_gl.bindTexture(g_tex[type][0][0]);
  static const float up[3] = {0, 1, 0};
  tgl::Vertex a = vertex(-50, 0, -50, up, 0, 1), b = vertex(-50, 0, 50, up, 0, 0);
  tgl::Vertex c = vertex(50, 0, 50, up, 1, 0), d = vertex(50, 0, -50, up, 1, 1);
  g_gl.triangle(a, b, c);
  g_gl.triangle(a, c, d);
  g_gl.lighting = true;
  g_gl.popMatrix();
}

namespace {

// The 2D overlay state the line functions share: screen-space ortho with y
// down, no lighting or texture, blending on, colour from an RGB555 value.
void begin2D(int rgb555, int width, float alpha) {
  TerrainState *s = g_inst;
  g_gl.matrixMode(tgl::Context::PROJECTION);
  g_gl.pushMatrix();
  g_gl.loadIdentity();
  g_gl.ortho(0, s->screenW, s->screenH, 0, -1, 1);
  g_gl.matrixMode(tgl::Context::MODELVIEW);
  g_gl.pushMatrix();
  g_gl.loadIdentity();
  g_gl.lighting = false;
  g_gl.texture2D = false;
  g_gl.blend = true;
  g_gl.lineWidth = (float)width;
  g_gl.color[0] = (float)((rgb555 >> 7) & 0xf8) / 255.0f;
  g_gl.color[1] = (float)((rgb555 >> 2) & 0xf8) / 255.0f;
  g_gl.color[2] = (float)((rgb555 & 0x1f) << 3) / 255.0f;
  g_gl.color[3] = alpha;
}

void end2D() {
  g_gl.lighting = true;
  g_gl.texture2D = true;
  g_gl.blend = false;
  g_gl.popMatrix();
  g_gl.matrixMode(tgl::Context::PROJECTION);
  g_gl.popMatrix();
  g_gl.matrixMode(tgl::Context::MODELVIEW);
}

}  // namespace

void Terrain::drawLine(int x1, int y1, int x2, int y2, int rgb555, int width, int alpha10) {
  begin2D(rgb555, width, (float)alpha10 / 10.0f);
  g_gl.line((float)x1, (float)y1, (float)x2, (float)y2);
  end2D();
}

void Terrain::drawCircle(Tile *, float) {}

// Quadratic Bezier through three points, stepped by 10 / (manhattan length).
void Terrain::drawBezierSpline(int x0, int y0, int x1, int y1, int x2, int y2, int rgb555, int width,
                               int alpha10) {
  int len = abs(x0 - x1) + abs(y0 - y1) + abs(x1 - x2) + abs(y1 - y2);
  if (len <= 0) return;
  float step = 1.0f / ((float)len / 10.0f);
  begin2D(rgb555, width, (float)alpha10 / 10.0f);
  float px = (float)x0, py = (float)y0;
  for (float t = step; t < 1.0f; t += step) {
    float a = (1 - t) * (1 - t), b = 2 * t * (1 - t), c = t * t;
    float x = a * x0 + b * x1 + c * x2, y = a * y0 + b * y1 + c * y2;
    g_gl.line(px, py, x, y);
    px = x;
    py = y;
  }
  g_gl.line(px, py, (float)x2, (float)y2);
  end2D();
}

// Hermite curve from (x0,y0) to (x1,y1), tangents (x0-x2, y0-y2) and
// (x3-x1, y3-y1), step 0.01. The DLL ignores `flip` here; so does the port.
void Terrain::drawCardinalSpline(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, int rgb555,
                                 int width, int alpha10) {
  float m0x = (float)(x0 - x2), m0y = (float)(y0 - y2), m1x = (float)(x3 - x1), m1y = (float)(y3 - y1);
  begin2D(rgb555, width, (float)alpha10 / 10.0f);
  float px = (float)x0, py = (float)y0;
  for (float t = 0.01f; t < 1.0f; t += 0.01f) {
    float t2 = t * t, t3 = t2 * t;
    float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
    float x = h00 * x0 + h10 * m0x + h01 * x1 + h11 * m1x, y = h00 * y0 + h10 * m0y + h01 * y1 + h11 * m1y;
    g_gl.line(px, py, x, y);
    px = x;
    py = y;
  }
  g_gl.line(px, py, (float)x1, (float)y1);
  end2D();
}

void Terrain::setSplineHeight(float step) { g_heightStep = step; }
