// High-resolution course terrain for the upscaled presentation (src/port/hd.c).
//
// Whenever golf redraws any part of the course (render, stripRender,
// localRender, pathUpdateRender, lowerEdgeCorner), the bridge notes the camera
// here. Before the next present, the whole visible terrain is traversed again
// in Terrain::render's order with the same camera, but recorded instead of
// rasterized: lit, projected triangles in the 1:1 window coordinates, batched by
// texture. hd_web.js replays them on the GPU into a framebuffer at the
// display's resolution. The 800x600 software render is untouched, so the game
// still sees exactly the pixels it drew.

#include <stdint.h>

#include <vector>

#include "platform/terrain.h"
#include "platform/terrain_gl.h"
#include "platform/terrain_port.h"

namespace {

enum { kBlend = 1, kTextured = 2, kTexAlpha = 4 };

struct Batch {
  int32_t first, count;  // vertices
  int32_t tex, serial, flags;
};

bool g_dirty, g_seen;
Tile *g_centre;
float g_rot;
std::vector<tgl::RecordedVertex> g_verts;
std::vector<Batch> g_batches;
std::vector<const tgl::Texture *> g_batchTex;

void record(void *, const tgl::RecordedVertex v[3], const tgl::Texture *tex, bool blend) {
  int flags = (blend ? kBlend : 0) | (tex ? kTextured | (tex->hasAlpha ? kTexAlpha : 0) : 0);
  int serial = tex ? (int)tex->serial : 0;
  if (g_batches.empty() || g_batches.back().serial != serial || g_batches.back().flags != flags) {
    g_batches.push_back({(int32_t)g_verts.size(), 0, (int32_t)g_batchTex.size(), serial, flags});
    g_batchTex.push_back(tex);
  }
  g_verts.insert(g_verts.end(), v, v + 3);
  g_batches.back().count += 3;
}

}  // namespace

extern "C" {

// Called by terrain_bridge.cpp for every call that draws terrain.
void terrain_hd_note(void *centre, float rot) {
  g_seen = g_dirty = true;
  g_centre = (Tile *)centre;
  g_rot = rot;
}

// Any change that invalidates the recorded frame without drawing.
void terrain_hd_invalidate(void) { g_dirty = true; }

// Re-records the visible terrain if anything was drawn since the last call.
// Returns 1 when there's a new frame to replay.
int terrain_hd_build(void) {
  if (!g_seen || !g_dirty) return 0;
  g_dirty = false;
  g_verts.clear();
  g_batches.clear();
  g_batchTex.clear();
  tgl::Context &gl = terrain_gl();
  gl.setRecorder(record, nullptr, false);
  Terrain::getInstance()->render(g_centre, g_rot);
  gl.setRecorder(nullptr, nullptr, true);
  return 1;
}

// The recorded frame: 8 floats per vertex (x, y, r, g, b, a, u, v; window
// coordinates with y up) and 5 ints per batch (first, count, texture index,
// texture serial, flags).
const float *terrain_hd_vertices(void) { return (const float *)g_verts.data(); }
int terrain_hd_batch_count(void) { return (int)g_batches.size(); }
const int32_t *terrain_hd_batches(void) { return (const int32_t *)g_batches.data(); }

// Texture of batch texture index `i`: RGBA pixels, size in out[0..1].
const uint8_t *terrain_hd_texture(int i, int32_t *out) {
  const tgl::Texture *t = g_batchTex[i];
  out[0] = t->w;
  out[1] = t->h;
  return t->rgba.data();
}

// The size of the 1:1 target the window coordinates refer to.
void terrain_hd_target(int32_t *out) {
  tgl::Context &gl = terrain_gl();
  out[0] = gl.targetWidth();
  out[1] = gl.targetHeight();
}

}  // extern "C"
