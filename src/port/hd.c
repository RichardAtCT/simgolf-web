// Upscaled presentation: the game keeps drawing its 800x600 RGB555 frame, and
// hd_web.js shows it at the display's resolution on a WebGL2 canvas laid over
// the SDL one (which stays underneath, invisible, for mouse and keyboard input,
// so click mapping is unchanged).
//
// Two layers:
//  - 2D: the game's frame, scaled by a shader (xBR, bilinear or nearest).
//  - Course terrain: terrain_hd.cpp re-records the visible terrain as triangles
//    whenever golf redraws any of it, and hd_web.js renders those at full
//    resolution. Where the frame still shows bare terrain, the high-resolution
//    render replaces it. "Bare terrain" is any pixel equal to golf's clean
//    terrain cache: the surface at *(0x4C1574) that the per-frame terrain
//    driver copies the view into before sprites and UI are drawn on top
//    (docs/terrain.md, "The golf per-frame driver").
//
// Toggles: ?hd=0 starts with all of this off, ?filter=xbr|linear|nearest picks
// the 2D filter. Ctrl+F9 toggles the high-resolution terrain, Ctrl+F10 cycles
// the 2D filter, Ctrl+F11 turns the whole thing off and on.

#include <emscripten.h>
#include <emscripten/heap.h>
#include <stdint.h>

#include "port/win32.h"

extern int terrain_hd_build(void);
extern const float *terrain_hd_vertices(void);
extern int terrain_hd_batch_count(void);
extern const int32_t *terrain_hd_batches(void);
extern const uint8_t *terrain_hd_texture(int i, int32_t *out);
extern void terrain_hd_target(int32_t *out);
extern void terrain_hd_invalidate(void);

// hd_web.js
extern int hd_js_active(void);
extern int hd_js_terrain_on(void);
extern void hd_js_texture(int serial, int w, int h, const uint8_t *rgba);
extern void hd_js_terrain(const float *verts, int nverts, const int32_t *batches, int nbatches, int tw, int th);
extern void hd_js_frame(const uint16_t *screen, int w, int h, const uint8_t *mask);

#define GOLF_TERRAIN_CACHE 0x4C1574u  // Surface wrapper or Surface*, golf.exe v1.02
#define GRAPHSY_SURFACE_VTBL 0x1011d0b0u

typedef struct {
  const uint16_t *bits;
  int pitch, w, h;
} Surf;

static int readable(uint32_t p, uint32_t n) { return p >= 0x1000 && p + n <= emscripten_get_heap_size(); }

// graphsy surface layout: docs/graphsy-surface.md.
static int surface_at(uint32_t p, Surf *s) {
  if (!readable(p, 0x4d4) || *(uint32_t *)(uintptr_t)p != GRAPHSY_SURFACE_VTBL) return 0;
  const char *o = (const char *)(uintptr_t)p;
  if (*(int32_t *)(o + 0x24) != 16) return 0;
  s->w = *(int32_t *)(o + 0x38);
  s->h = *(int32_t *)(o + 0x3c);
  s->pitch = *(int32_t *)(o + 0x40);
  uint32_t bits = *(uint32_t *)(o + 0x4c0);
  if (!readable(bits, (uint32_t)s->pitch * s->h * 2)) return 0;
  s->bits = (const uint16_t *)(uintptr_t)bits;
  return 1;
}

static int terrain_cache(Surf *s) {
  uint32_t p = *(uint32_t *)(uintptr_t)GOLF_TERRAIN_CACHE;
  if (surface_at(p, s)) return 1;
  return readable(p, 8) && surface_at(*(uint32_t *)(uintptr_t)(p + 4), s);
}

// How a frame pixel relates to the clean terrain pixel under it: 255 when
// it's the same, 128 when it's that pixel in shadow, 0 for anything else.
// The game's shadows (trees, buildings, golfers, label boxes) halve each
// channel, (t >> 1) & 0x3def, so only that exact value counts as shadow: a
// looser "uniformly darker" test also caught dark sprite pixels (trees,
// golfers, birds) and replaced them with terrain.
static int shade(uint16_t s, uint16_t t) {
  s &= 0x7fff;
  t &= 0x7fff;
  if (s == t) return 255;
  if (s == ((t >> 1) & 0x3def)) return 128;
  return 0;
}

// Returns 1 when the frame was presented here, 0 to fall back to SDL.
int hd_present(const uint16_t *screen) {
  int active = hd_js_active();
  if (!active) return 0;
  if (active == 2) terrain_hd_invalidate();
  static uint8_t mask[WIN_SCREEN_W * WIN_SCREEN_H * 2];
  int terrain = 0;
  Surf c;
  if (hd_js_terrain_on() && terrain_cache(&c) && c.w == WIN_SCREEN_W && c.h == WIN_SCREEN_H) {
    int same = 0;
    for (int y = 0; y < WIN_SCREEN_H; y++) {
      const uint16_t *a = screen + y * WIN_SCREEN_W, *b = c.bits + y * c.pitch;
      uint8_t *m = mask + y * WIN_SCREEN_W * 2;
      for (int x = 0; x < WIN_SCREEN_W; x++) {
        int k = shade(a[x], b[x]);
        m[2 * x] = k ? 255 : 0;
        m[2 * x + 1] = (uint8_t)k;
        same += k == 255;
      }
    }
    // Menus and other full-screen pages leave a stale cache behind: only use
    // it when a good part of the frame is terrain.
    terrain = same > WIN_SCREEN_W * WIN_SCREEN_H / 8;
  }
  if (terrain && terrain_hd_build()) {
    int nb = terrain_hd_batch_count();
    const int32_t *b = terrain_hd_batches();
    for (int i = 0; i < nb; i++) {
      if (!(b[i * 5 + 4] & 2)) continue;  // untextured
      int32_t size[2];
      const uint8_t *rgba = terrain_hd_texture(b[i * 5 + 2], size);
      hd_js_texture(b[i * 5 + 3], size[0], size[1], rgba);
    }
    int32_t t[2];
    terrain_hd_target(t);
    int nverts = nb ? b[(nb - 1) * 5] + b[(nb - 1) * 5 + 1] : 0;
    hd_js_terrain(terrain_hd_vertices(), nverts, b, nb, t[0], t[1]);
  }
  hd_js_frame(screen, WIN_SCREEN_W, WIN_SCREEN_H, terrain ? mask : 0);
  return 1;
}
