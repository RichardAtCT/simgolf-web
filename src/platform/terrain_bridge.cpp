// C entry points for the translated golf.exe: Terrain.dll's 26 imports as
// Terrain_<method>(this, ...), the names and signatures src/port/imports.c
// stubs today. Link this file instead of those stubs to get the real renderer.
//
// golf passes initSystem the HDC of graphsy's back-buffer surface. The port's
// GDI layer decides what an HDC is, so it registers a resolver that turns that
// HDC into the TerrainSurface (RGB555 pixels) to draw into. Without one, the
// HDC is taken to be a TerrainSurface* already.

#include <stdint.h>

#include "platform/terrain.h"
#include "platform/terrain_port.h"

// terrain_hd.cpp: re-record the high-resolution frame after any terrain draw.
extern "C" void terrain_hd_note(void *centre, float rot);
extern "C" void terrain_hd_invalidate(void);

namespace {
const TerrainSurface *(*g_resolveHdc)(void *hdc);
Terrain *T(void *t) { return (Terrain *)t; }
Tile *L(void *tile) { return (Tile *)tile; }
}  // namespace

extern "C" {

void terrain_set_hdc_resolver(const TerrainSurface *(*resolve)(void *hdc)) { g_resolveHdc = resolve; }

void *Terrain_getInstance(void) { return Terrain::getInstance(); }
void Terrain_dtor(void *t) { T(t)->~Terrain(); }
void Terrain_initSystem(void *t, int w, int h, void *hdc, uint8_t flip) {
  const TerrainSurface *s = g_resolveHdc && hdc ? g_resolveHdc(hdc) : (const TerrainSurface *)hdc;
  T(t)->initSystem(w, h, (void *)s, flip != 0);
}
void Terrain_closeSystem(void *t) { T(t)->closeSystem(); }
void Terrain_initTerrain(void *t) { T(t)->initTerrain(); }
void Terrain_resetTerrain(void *t) { T(t)->resetTerrain(); }
void Terrain_loadNewCourseType(void *t, int type) {
  T(t)->loadNewCourseType(type);
  terrain_hd_invalidate();
}
void Terrain_setZoomLevel(void *t, int zoom) {
  T(t)->setZoomLevel(zoom);
  terrain_hd_invalidate();
}
void Terrain_passCollarInfo(void *t, int *groups, int n) { T(t)->passCollarInfo(groups, n); }

uint8_t Terrain_render(void *t, void *centre, float rot) {
  terrain_hd_note(centre, rot);
  return T(t)->render(L(centre), rot);
}
void Terrain_localRender(void *t, void *dirty, void *centre, float rot) {
  terrain_hd_note(centre, rot);
  T(t)->localRender(L(dirty), L(centre), rot);
}
void Terrain_stripRender(void *t, void *centre, int dir, float rot) {
  terrain_hd_note(centre, rot);
  T(t)->stripRender(L(centre), dir, rot);
}
void Terrain_pathUpdateRender(void *t, void *centre, float rot) {
  terrain_hd_note(centre, rot);
  T(t)->pathUpdateRender(L(centre), rot);
}
void Terrain_drawLine(void *t, int x1, int y1, int x2, int y2, int rgb555, int width, int alpha10) {
  T(t)->drawLine(x1, y1, x2, y2, rgb555, width, alpha10);
}

uint8_t Terrain_hasPath(void *t, void *tile) { return T(t)->hasPath(L(tile)); }
void Terrain_updatePath(void *t, int x, int y, int variant) { T(t)->updatePath(x, y, variant); }
uint8_t Terrain_hasConnectedPath(void *t, int x, int y) { return T(t)->hasConnectedPath(x, y); }
void Terrain_layPath(void *t, void *tile, int on, int variant) { T(t)->layPath(L(tile), on, variant); }
int Terrain_getVariation(void *t, void *tile) { return T(t)->getVariation(L(tile)); }
void Terrain_setType(void *t, void *tile, int type, int variation) { T(t)->setType(L(tile), type, variation); }
void Terrain_setWall(void *t, void *tile, int dir, int wallType, uint8_t on) {
  T(t)->setWall(L(tile), dir, wallType, on != 0);
}

void Terrain_elevateCorner(void *t, void *tile, int corner) { T(t)->elevateCorner(L(tile), corner); }
void Terrain_lowerCorner(void *t, void *tile, int corner) { T(t)->lowerCorner(L(tile), corner); }
void Terrain_lowerEdgeCorner(void *t, void *tile, int corner, void *centre, float rot) {
  terrain_hd_note(centre, rot);
  T(t)->lowerEdgeCorner(L(tile), corner, L(centre), rot);
}
void Terrain_calcNormals(void *t, void *tile) { T(t)->calcNormals(L(tile)); }
void Terrain_calcAllNormals(void *t, void *centre) { T(t)->calcAllNormals(L(centre)); }

}  // extern "C"
