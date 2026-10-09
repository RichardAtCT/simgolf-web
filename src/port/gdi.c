// GDI32 subset used by jgld.dll: DIB sections, memory DCs, BitBlt/StretchBlt,
// clip regions, palettes, and TrueType text (stb_truetype) for the game's two
// bundled fonts. The window DC draws into an 800x600 RGB555 buffer that is
// presented through an SDL streaming texture.

#define _GNU_SOURCE
#include <SDL.h>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "port/fs.h"
#include "port/runtime.h"
#include "port/win32.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#define P(x) ((void *)(uintptr_t)(x))
#define S(x) ((char *)(uintptr_t)(x))
#define U(x) ((nu)(uintptr_t)(x))
#define I32(p, off) (*(int32_t *)((char *)P(p) + (off)))
#define U32(p, off) (*(uint32_t *)((char *)P(p) + (off)))
#define U16(p, off) (*(uint16_t *)((char *)P(p) + (off)))
#define U8(p, off) (*(uint8_t *)((char *)P(p) + (off)))

typedef struct {
  int w, h, bottom_up, bpp, stride;
  uint8_t *bits;
  int owns_bits;
  uint8_t colors[256][4];   // RGBQUAD: b, g, r, reserved
  int is565;
} Dib;

typedef struct {
  int file;            // index into g_fonts
  float scale;
  int ascent, descent, height, internal_leading, avg_width, max_width;
  int bold, italic, underline;
} Font;

typedef struct {
  nu bitmap, font, palette, brush;
  uint32_t text_color;
  int bk_mode, text_align;
  int has_clip;
  int32_t clip[4];
  int is_window;
} Dc;

typedef struct { int32_t r[4]; } Rgn;
typedef struct { int n; uint8_t e[256][4]; } Pal;

// ------------------------------------------------------------------ screen

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static Dib g_screen_dib;
static nu g_screen_bitmap, g_window_dc;
static double g_last_present_yield;

uint16_t *win_screen(void) { return (uint16_t *)g_screen_dib.bits; }

unsigned g_presents;

// The original presentation; hd.c replaces it with an upscaled one unless
// that's turned off (Ctrl+F11, ?hd=0) or WebGL2 is missing.
static void present_sdl(void) {
  // RGB555 -> the GL-native RGBA byte order through a table; SDL's own
  // conversion (BlitNtoN) was the biggest cost of a menu frame
  static uint32_t lut[32768], *rgba;
  if (!rgba) {
    rgba = malloc(WIN_SCREEN_W * WIN_SCREEN_H * 4);
    for (int c = 0; c < 32768; c++) {
      uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
      r = r << 3 | r >> 2; g = g << 3 | g >> 2; b = b << 3 | b >> 2;
      lut[c] = 0xff000000u | b << 16 | g << 8 | r;
    }
  }
  const uint16_t *px = (const uint16_t *)g_screen_dib.bits;
  for (int i = 0; i < WIN_SCREEN_W * WIN_SCREEN_H; i++) rgba[i] = lut[px[i] & 0x7fff];
  SDL_UpdateTexture(g_texture, NULL, rgba, WIN_SCREEN_W * 4);
  SDL_RenderClear(g_renderer);
  SDL_RenderCopy(g_renderer, g_texture, NULL, NULL);
  SDL_RenderPresent(g_renderer);
}

int hd_present(const uint16_t *screen);  // hd.c

void win_present(void) {
  g_presents++;
  if (!hd_present((const uint16_t *)g_screen_dib.bits)) present_sdl();
  // Modal screens (reports, dialogs) redraw in a tight loop: cap presents at
  // about 30 per second so they don't keep a core busy (the course view
  // itself runs at the game's own ~11 per second).
  static double last_present;
  double now = emscripten_get_now();
  if (now - last_present < 33) {
    win_yield((int)(33 - (now - last_present)));
    g_last_present_yield = emscripten_get_now();
  } else if (now - g_last_present_yield > 10) {
    win_yield(0);
    g_last_present_yield = emscripten_get_now();
  }
  last_present = emscripten_get_now();
}

nu gdi_window_dc(void) { return g_window_dc; }

// Terrain renders straight into the DIB selected into the HDC golf passes to
// Terrain::initSystem (platform/terrain_port.h's TerrainSurface).
typedef struct { uint16_t *pixels; int pitch, width, height; } TerrainSurface;
extern void terrain_set_hdc_resolver(const TerrainSurface *(*resolve)(void *hdc));

static const TerrainSurface *terrain_surface(void *hdc) {
  static TerrainSurface s;
  Dc *dc = obj_get((nu)(uintptr_t)hdc, OBJ_DC);
  Dib *d = dc ? obj_get(dc->bitmap, OBJ_BITMAP) : NULL;
  if (!d || d->bpp != 16) {
    fprintf(stderr, "terrain: HDC %p has no 16-bit DIB\n", hdc);
    return NULL;
  }
  int pitch = d->stride / 2;
  s.pixels = d->bottom_up ? (uint16_t *)(d->bits + (size_t)(d->h - 1) * d->stride) : (uint16_t *)d->bits;
  s.pitch = d->bottom_up ? -pitch : pitch;
  s.width = d->w;
  s.height = d->h;
  return &s;
}

void gdi_init(void) {
  g_window = SDL_CreateWindow("SimGolf", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              WIN_SCREEN_W, WIN_SCREEN_H, 0);
  g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
  g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                                WIN_SCREEN_W, WIN_SCREEN_H);
  g_screen_dib.w = WIN_SCREEN_W;
  g_screen_dib.h = WIN_SCREEN_H;
  g_screen_dib.bpp = 16;
  g_screen_dib.stride = WIN_SCREEN_W * 2;
  g_screen_dib.bits = calloc(WIN_SCREEN_W * WIN_SCREEN_H, 2);
  g_screen_bitmap = obj_new(OBJ_BITMAP, &g_screen_dib);
  Dc *dc = calloc(1, sizeof *dc);
  dc->bitmap = g_screen_bitmap;
  dc->is_window = 1;
  dc->text_align = 0;
  dc->bk_mode = 2;
  g_window_dc = obj_new(OBJ_DC, dc);
  terrain_set_hdc_resolver(terrain_surface);
}

// ------------------------------------------------------------------ DCs and objects

static Dc *dc_get(nu h) { return obj_get(h, OBJ_DC); }
static Dib *dc_dib(Dc *dc) { return dc ? obj_get(dc->bitmap, OBJ_BITMAP) : NULL; }

nu CreateCompatibleDC(nu hdc) {
  API_TRACE();
  (void)hdc;
  Dc *dc = calloc(1, sizeof *dc);
  dc->bk_mode = 2;
  return obj_new(OBJ_DC, dc);
}

nu DeleteDC(nu h) {
  API_TRACE();
  Dc *dc = dc_get(h);
  if (!dc || dc->is_window) return dc != NULL;
  free(dc);
  obj_free(h);
  return 1;
}

static nu g_stock[32];

nu GetStockObject(nu i) {
  API_TRACE();
  if (i >= 32) return 0;
  if (!g_stock[i]) g_stock[i] = obj_new(OBJ_BRUSH, NULL);
  return g_stock[i];
}

nu SelectObject(nu hdc, nu h) {
  API_TRACE();
  Dc *dc = dc_get(hdc);
  if (!dc) return 0;
  nu old = 0;
  if (obj_get(h, OBJ_BITMAP)) { old = dc->bitmap; dc->bitmap = h; }
  else if (obj_get(h, OBJ_FONT)) { old = dc->font; dc->font = h; }
  else if (obj_get(h, OBJ_BRUSH) || h == 0) { old = dc->brush; dc->brush = h; }
  else if (obj_get(h, OBJ_RGN)) {
    Rgn *r = obj_get(h, OBJ_RGN);
    dc->has_clip = 1;
    memcpy(dc->clip, r->r, 16);
    return 2;   // SIMPLEREGION
  } else {
    static int warned;
    if (warned++ < 10) {
      fprintf(stderr, "SelectObject(%08x, %08x): unknown object (kind %d)\n", hdc, h, obj_kind(h));
      port_trace_dump(12);
    }
  }
  return old ? old : GetStockObject(0);
}

nu DeleteObject(nu h) {
  API_TRACE();
  Dib *d = obj_get(h, OBJ_BITMAP);
  if (d && d != &g_screen_dib) {
    if (d->owns_bits) free(d->bits);
    free(d);
    obj_free(h);
    return 1;
  }
  if (obj_get(h, OBJ_FONT) || obj_get(h, OBJ_RGN) || obj_get(h, OBJ_PALETTE)) {
    void *p = obj_get(h, OBJ_FONT);
    if (!p) p = obj_get(h, OBJ_RGN);
    if (!p) p = obj_get(h, OBJ_PALETTE);
    free(p);
    obj_free(h);
  }
  return 1;
}

nu CreateDIBSection(nu hdc, nu bmi, nu usage, nu ppbits, nu section, nu offset) {
  API_TRACE();
  (void)section; (void)offset;
  Dib *d = calloc(1, sizeof *d);
  int32_t w = I32(bmi, 4), h = I32(bmi, 8);
  d->w = w;
  d->h = h < 0 ? -h : h;
  d->bottom_up = h > 0;
  d->bpp = U16(bmi, 14);
  d->stride = ((w * d->bpp + 31) / 32) * 4;
  fprintf(stderr, "CreateDIBSection %dx%d %d bpp\n", w, h, d->bpp);
  if (w <= 0 || w > 4096 || d->h > 4096 || !d->bpp) {
    port_trace_dump(48);
    port_abort("CreateDIBSection: bad size");
  }
  d->bits = calloc((size_t)d->stride * d->h + 4, 1);
  d->owns_bits = 1;
  nu hdrsize = U32(bmi, 0);
  nu compression = U32(bmi, 16);
  if (compression == 3) {   // BI_BITFIELDS: masks follow the header
    d->is565 = U32(bmi, hdrsize + 4) == 0x7e0;
  }
  if (d->bpp <= 8) {
    nu n = U32(bmi, 32);
    if (!n) n = 1u << d->bpp;
    if (usage == 0) {
      memcpy(d->colors, (char *)P(bmi) + hdrsize, n * 4);
    } else {
      // DIB_PAL_COLORS: indices into the DC's palette
      Dc *dc = dc_get(hdc);
      Pal *pal = dc ? obj_get(dc->palette, OBJ_PALETTE) : NULL;
      for (nu i = 0; i < n; i++) {
        uint16_t idx = U16(bmi, hdrsize + i * 2);
        if (pal && idx < (nu)pal->n) {
          d->colors[i][0] = pal->e[idx][2];
          d->colors[i][1] = pal->e[idx][1];
          d->colors[i][2] = pal->e[idx][0];
        }
      }
    }
  }
  if (ppbits) U32(ppbits, 0) = U(d->bits);
  return obj_new(OBJ_BITMAP, d);
}

nu SetDIBColorTable(nu hdc, nu start, nu n, nu rgb) {
  API_TRACE();
  Dib *d = dc_dib(dc_get(hdc));
  if (!d) return 0;
  for (nu i = 0; i < n && start + i < 256; i++) memcpy(d->colors[start + i], (char *)P(rgb) + i * 4, 4);
  return n;
}

nu CreatePalette(nu lp) {
  API_TRACE();
  Pal *p = calloc(1, sizeof *p);
  p->n = U16(lp, 2);
  if (p->n > 256) p->n = 256;
  memcpy(p->e, (char *)P(lp) + 4, p->n * 4);
  return obj_new(OBJ_PALETTE, p);
}

nu SelectPalette(nu hdc, nu pal, nu bg) {
  API_TRACE();
  (void)bg;
  Dc *dc = dc_get(hdc);
  if (!dc) return 0;
  nu old = dc->palette;
  dc->palette = pal;
  return old;
}

nu RealizePalette(nu hdc) { API_TRACE(); (void)hdc; return 0; }

nu AnimatePalette(nu pal, nu start, nu n, nu entries) {
  API_TRACE();
  Pal *p = obj_get(pal, OBJ_PALETTE);
  if (!p) return 0;
  for (nu i = 0; i < n && start + i < 256; i++) memcpy(p->e[start + i], (char *)P(entries) + i * 4, 4);
  return 1;
}

nu CreateRectRgnIndirect(nu r) {
  API_TRACE();
  Rgn *g = calloc(1, sizeof *g);
  memcpy(g->r, P(r), 16);
  return obj_new(OBJ_RGN, g);
}

nu SelectClipRgn(nu hdc, nu rgn) {
  API_TRACE();
  Dc *dc = dc_get(hdc);
  if (!dc) return 0;
  Rgn *r = obj_get(rgn, OBJ_RGN);
  if (!r) {
    dc->has_clip = 0;
    return 1;   // NULLREGION
  }
  dc->has_clip = 1;
  memcpy(dc->clip, r->r, 16);
  return 2;
}

nu SetBkMode(nu hdc, nu mode) {
  API_TRACE();
  Dc *dc = dc_get(hdc);
  if (!dc) return 0;
  nu old = dc->bk_mode;
  dc->bk_mode = mode;
  return old;
}

nu SetTextColor(nu hdc, nu color) {
  API_TRACE();
  Dc *dc = dc_get(hdc);
  if (!dc) return 0;
  nu old = dc->text_color;
  dc->text_color = color;
  return old;
}

nu SetTextAlign(nu hdc, nu align) {
  API_TRACE();
  Dc *dc = dc_get(hdc);
  if (!dc) return 0;
  nu old = dc->text_align;
  dc->text_align = align;
  return old;
}

// ------------------------------------------------------------------ blits

static uint8_t *row_ptr(Dib *d, int y) {
  int ry = d->bottom_up ? d->h - 1 - y : y;
  return d->bits + (size_t)ry * d->stride;
}

static uint16_t to555(const uint8_t *bgr) {
  return (uint16_t)(((bgr[2] >> 3) << 10) | ((bgr[1] >> 3) << 5) | (bgr[0] >> 3));
}

static uint16_t to_dst16(Dib *dst, uint16_t c555) {
  if (!dst->is565) return c555;
  return (uint16_t)(((c555 & 0x7c00) << 1) | ((c555 & 0x3e0) << 1) | (c555 & 0x1f));
}

static uint16_t from_src16(Dib *src, uint16_t c) {
  if (!src->is565) return c;
  return (uint16_t)(((c >> 1) & 0x7c00) | ((c >> 1) & 0x3e0) | (c & 0x1f));
}

static void blit_pixels(Dib *dst, int dx, int dy, int w, int h, Dib *src, int sx, int sy,
                        int sw, int shh, const int32_t *clip) {
  // dst rect (dx, dy, w, h) maps to src rect (sx, sy, sw, shh); nearest neighbour.
  if (w <= 0 || h <= 0 || sw <= 0 || shh <= 0) return;
  int cl = 0, ct = 0, cr = dst->w, cb = dst->h;
  if (clip) {
    if (clip[0] > cl) cl = clip[0];
    if (clip[1] > ct) ct = clip[1];
    if (clip[2] < cr) cr = clip[2];
    if (clip[3] < cb) cb = clip[3];
  }
  int x0 = dx < cl ? cl : dx, x1 = dx + w > cr ? cr : dx + w;
  int y0 = dy < ct ? ct : dy, y1 = dy + h > cb ? cb : dy + h;
  // 8 -> 16 bit, unscaled (the menus' full-screen backgrounds): palette table
  if (src->bpp == 8 && dst->bpp == 16 && sw == w) {
    uint16_t pal[256];
    for (int i = 0; i < 256; i++) pal[i] = to_dst16(dst, to555(src->colors[i]));
    int a = x0, b = x1, sa = sx + (a - dx);
    if (sa < 0) { a -= sa; sa = 0; }
    if (sa + (b - a) > src->w) b = a + (src->w - sa);
    for (int y = y0; y < y1 && b > a; y++) {
      int syy = sy + (int)((int64_t)(y - dy) * shh / h);
      if (syy < 0 || syy >= src->h) continue;
      uint16_t *drow = (uint16_t *)row_ptr(dst, y) + a;
      const uint8_t *srow = row_ptr(src, syy) + sa;
      for (int k = 0; k < b - a; k++) drow[k] = pal[srow[k]];
    }
    return;
  }
  for (int y = y0; y < y1; y++) {
    int syy = sy + (int)((int64_t)(y - dy) * shh / h);
    if (syy < 0 || syy >= src->h) continue;
    uint8_t *drow = row_ptr(dst, y);
    uint8_t *srow = row_ptr(src, syy);
    if (src->bpp == dst->bpp && sw == w && src->is565 == dst->is565) {
      int a = x0, b = x1;
      int sa = sx + (a - dx);
      if (sa < 0) { a -= sa; sa = 0; }
      if (sa + (b - a) > src->w) b = a + (src->w - sa);
      if (b > a) memmove(drow + a * dst->bpp / 8, srow + sa * src->bpp / 8, (size_t)(b - a) * dst->bpp / 8);
      continue;
    }
    for (int x = x0; x < x1; x++) {
      int sxx = sx + (int)((int64_t)(x - dx) * sw / w);
      if (sxx < 0 || sxx >= src->w) continue;
      uint16_t c555;
      uint8_t idx = 0;
      switch (src->bpp) {
        case 8: idx = srow[sxx]; c555 = to555(src->colors[idx]); break;
        case 16: c555 = from_src16(src, ((uint16_t *)srow)[sxx]); break;
        case 24: c555 = to555(srow + sxx * 3); break;
        case 32: c555 = to555(srow + sxx * 4); break;
        default: c555 = 0;
      }
      switch (dst->bpp) {
        case 16: ((uint16_t *)drow)[x] = to_dst16(dst, c555); break;
        case 8: drow[x] = idx; break;
        case 24: case 32: {
          uint8_t *p = drow + x * (dst->bpp / 8);
          p[0] = (uint8_t)((c555 & 0x1f) << 3);
          p[1] = (uint8_t)(((c555 >> 5) & 0x1f) << 3);
          p[2] = (uint8_t)(((c555 >> 10) & 0x1f) << 3);
          break;
        }
      }
    }
  }
}

nu StretchBlt(nu hdst, nu x, nu y, nu w, nu h, nu hsrc, nu sx, nu sy, nu sw, nu sh, nu rop) {
  API_TRACE();
  (void)rop;
  Dc *ddc = dc_get(hdst), *sdc = dc_get(hsrc);
  Dib *dst = dc_dib(ddc), *src = dc_dib(sdc);
  if (!dst || !src) return 0;
  blit_pixels(dst, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, src, (int32_t)sx, (int32_t)sy,
              (int32_t)sw, (int32_t)sh, ddc->has_clip ? ddc->clip : NULL);
  if (ddc->is_window) win_present();
  return 1;
}

nu BitBlt(nu hdst, nu x, nu y, nu w, nu h, nu hsrc, nu sx, nu sy, nu rop) {
  API_TRACE();
  return StretchBlt(hdst, x, y, w, h, hsrc, sx, sy, w, h, rop);
}

// ------------------------------------------------------------------ fonts

typedef struct {
  char path[256];
  char family[64];
  unsigned char *data;
  stbtt_fontinfo info;
} FontFile;

static FontFile g_fonts[8];
static int g_nfonts;

static int load_font_file(const char *dospath) {
  char real[512];
  if (dospath[0] == '/') snprintf(real, sizeof real, "%s", dospath);
  else if (!fs_resolve(dospath, real, sizeof real, 0)) return -1;
  for (int i = 0; i < g_nfonts; i++)
    if (!strcmp(g_fonts[i].path, real)) return i;
  if (g_nfonts == 8) return -1;
  FILE *f = fopen(real, "rb");
  if (!f) return -1;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  FontFile *ff = &g_fonts[g_nfonts];
  ff->data = malloc(n);
  fread(ff->data, 1, n, f);
  fclose(f);
  if (!stbtt_InitFont(&ff->info, ff->data, stbtt_GetFontOffsetForIndex(ff->data, 0))) {
    free(ff->data);
    return -1;
  }
  snprintf(ff->path, sizeof ff->path, "%s", real);
  int len = 0;
  const char *name = stbtt_GetFontNameString(&ff->info, &len, STBTT_PLATFORM_ID_MICROSOFT,
                                             STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, 1);
  // UTF-16BE -> ASCII
  int k = 0;
  for (int i = 1; name && i < len && k < 63; i += 2) ff->family[k++] = name[i];
  ff->family[k] = 0;
  fprintf(stderr, "font %s: family \"%s\"\n", dospath, ff->family);
  return g_nfonts++;
}

// jgld registers a TTF by writing a .fot with CreateScalableFontResourceA and
// adding that; the .fot is a stub here, so add the .ttf next to it.
nu AddFontResourceA(nu path) {
  API_TRACE();
  char p[512];
  snprintf(p, sizeof p, "%s", S(path));
  size_t n = strlen(p);
  if (n > 4 && !strcasecmp(p + n - 4, ".fot")) memcpy(p + n - 4, ".ttf", 4);
  int r = load_font_file(p);
  if (r < 0) fprintf(stderr, "AddFontResourceA(%s): can't load %s\n", S(path), p);
  return r >= 0 ? 1 : 0;
}
nu RemoveFontResourceA(nu path) { API_TRACE(); (void)path; return 1; }
nu CreateScalableFontResourceA(nu hidden, nu fot, nu ttf, nu dir) {
  API_TRACE();
  (void)hidden; (void)fot; (void)dir;
  load_font_file(S(ttf));
  return 1;
}

nu CreateFontIndirectA(nu lf) {
  API_TRACE();
  int32_t height = I32(lf, 0);
  int32_t weight = I32(lf, 16);
  const char *face = S(lf) + 28;
  int file = -1;
  for (int i = 0; i < g_nfonts; i++) {
    size_t fl = strlen(g_fonts[i].family);
    // "Manual SSi Bold" is the bold face of the "Manual SSi" file
    if (!strcasecmp(g_fonts[i].family, face) ||
        (fl && !strncasecmp(g_fonts[i].family, face, fl) && face[fl] == ' ' && strncmp(g_fonts[i].path, "/fonts/", 7)))
      file = i;
    if (file == i && strncmp(g_fonts[i].path, "/fonts/", 7)) break;   // prefer the game's own font
  }
  if (file < 0) {
    // Windows system fonts: free metric-compatible stand-ins (third_party/fonts).
    const char *sub = "/fonts/LiberationSans-Regular.ttf";
    int bold = weight >= 600 || strcasestr(face, "bold");
    if (strcasestr(face, "times")) sub = "/fonts/LiberationSerif-Bold.ttf";
    else if (strcasestr(face, "comic")) sub = "/fonts/ComicNeue-Bold.ttf";
    else if (bold) sub = "/fonts/LiberationSans-Bold.ttf";
    file = load_font_file(sub);
    if (file < 0) file = 0;
  }
  if (file >= g_nfonts) return 0;
  Font *f = calloc(1, sizeof *f);
  f->file = file;
  stbtt_fontinfo *info = &g_fonts[file].info;
  if (height < 0) f->scale = stbtt_ScaleForMappingEmToPixels(info, (float)-height);
  else f->scale = stbtt_ScaleForPixelHeight(info, (float)(height ? height : 16));
  int asc, desc, gap;
  stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
  f->ascent = (int)(asc * f->scale + 0.5f);
  f->descent = (int)(-desc * f->scale + 0.5f);
  f->height = f->ascent + f->descent;
  int em = height < 0 ? -height : (int)(f->height);
  f->internal_leading = f->height - em;
  if (f->internal_leading < 0) f->internal_leading = 0;
  int adv, lsb;
  stbtt_GetCodepointHMetrics(info, 'x', &adv, &lsb);
  f->avg_width = (int)(adv * f->scale + 0.5f);
  stbtt_GetCodepointHMetrics(info, 'W', &adv, &lsb);
  f->max_width = (int)(adv * f->scale + 0.5f);
  f->bold = weight >= 600;
  f->italic = U8(lf, 20);
  f->underline = U8(lf, 21);
  nu hf = obj_new(OBJ_FONT, f);
  fprintf(stderr, "CreateFont \"%s\" %d -> %08x\n", face, (int)height, hf);
  return hf;
}

static Font *dc_font(Dc *dc) { return dc ? obj_get(dc->font, OBJ_FONT) : NULL; }

nu GetTextMetricsA(nu hdc, nu tm) {
  API_TRACE();
  Font *f = dc_font(dc_get(hdc));
  memset(P(tm), 0, 56);
  if (!f) return 0;
  I32(tm, 0) = f->height;
  I32(tm, 4) = f->ascent;
  I32(tm, 8) = f->descent;
  I32(tm, 12) = f->internal_leading;
  I32(tm, 20) = f->avg_width;
  I32(tm, 24) = f->max_width;
  I32(tm, 28) = f->bold ? 700 : 400;
  I32(tm, 36) = 96;
  I32(tm, 40) = 96;
  U8(tm, 44) = 0x20;
  U8(tm, 45) = 0xff;
  U8(tm, 46) = 0x1f;
  U8(tm, 47) = 0x20;
  U8(tm, 48) = (uint8_t)f->italic;
  U8(tm, 49) = (uint8_t)f->underline;
  return 1;
}

static int text_width(Font *f, const char *s, int n) {
  stbtt_fontinfo *info = &g_fonts[f->file].info;
  float x = 0;
  for (int i = 0; i < n; i++) {
    int adv, lsb;
    stbtt_GetCodepointHMetrics(info, (unsigned char)s[i], &adv, &lsb);
    x += adv * f->scale;
    if (i + 1 < n) x += stbtt_GetCodepointKernAdvance(info, (unsigned char)s[i], (unsigned char)s[i + 1]) * f->scale;
  }
  return (int)(x + 0.5f) + (f->bold ? 1 : 0);
}

nu GetTextExtentPoint32A(nu hdc, nu str, nu n, nu size) {
  API_TRACE();
  Font *f = dc_font(dc_get(hdc));
  if (!f) {
    I32(size, 0) = (int32_t)n * 8;
    I32(size, 4) = 16;
    return 1;
  }
  I32(size, 0) = text_width(f, S(str), (int)n);
  I32(size, 4) = f->height;
  return 1;
}

static uint8_t nearest_index(Dib *d, uint32_t colorref) {
  int r = colorref & 0xff, g = (colorref >> 8) & 0xff, b = (colorref >> 16) & 0xff;
  int best = 0, bestd = 1 << 30;
  for (int i = 0; i < 256; i++) {
    int dr = d->colors[i][2] - r, dg = d->colors[i][1] - g, db = d->colors[i][0] - b;
    int dd = dr * dr + dg * dg + db * db;
    if (dd < bestd) { bestd = dd; best = i; }
  }
  return (uint8_t)best;
}

static void put_pixel(Dib *d, const int32_t *clip, int x, int y, uint32_t c16, uint8_t c8) {
  if (x < 0 || y < 0 || x >= d->w || y >= d->h) return;
  if (clip && (x < clip[0] || y < clip[1] || x >= clip[2] || y >= clip[3])) return;
  uint8_t *row = row_ptr(d, y);
  if (d->bpp == 16) ((uint16_t *)row)[x] = (uint16_t)c16;
  else if (d->bpp == 8) row[x] = c8;
}

nu TextOutA(nu hdc, nu x, nu y, nu str, nu n) {
  API_TRACE();
  Dc *dc = dc_get(hdc);
  Dib *d = dc_dib(dc);
  Font *f = dc_font(dc);
  if (!d || !f) {
    static int warned;
    if (warned++ < 5)
      fprintf(stderr, "TextOutA \"%.*s\": DC %08x has %s%s\n", (int)n, S(str), hdc,
              d ? "" : "no bitmap ", f ? "" : "no font");
    return 0;
  }
  const char *s = S(str);
  stbtt_fontinfo *info = &g_fonts[f->file].info;
  int px = (int32_t)x, base = (int32_t)y;
  int align = dc->text_align;
  if ((align & 24) == 0) base += f->ascent;          // TA_TOP
  else if ((align & 24) == 8) base -= f->descent;    // TA_BOTTOM
  int w = text_width(f, s, (int)n);
  if ((align & 6) == 6) px -= w / 2;                 // TA_CENTER
  else if ((align & 6) == 2) px -= w;                // TA_RIGHT
  uint32_t cr = dc->text_color;
  {
    static int logged;
    if (logged++ < 40)
      fprintf(stderr, "TextOut \"%.*s\" at %d,%d color %06x align %d dib %dx%d bpp %d font h%d clip %d\n",
              (int)n, s, (int)x, (int)y, cr, dc->text_align, d->w, d->h, d->bpp, f->height, dc->has_clip);
  }
  uint16_t c555 = (uint16_t)((((cr & 0xff) >> 3) << 10) | ((((cr >> 8) & 0xff) >> 3) << 5) | (((cr >> 16) & 0xff) >> 3));
  uint16_t c16 = to_dst16(d, c555);
  uint8_t c8 = d->bpp == 8 ? nearest_index(d, cr) : 0;
  const int32_t *clip = dc->has_clip ? dc->clip : NULL;
  float fx = (float)px;
  for (int i = 0; i < (int)n; i++) {
    int ch = (unsigned char)s[i];
    int adv, lsb;
    stbtt_GetCodepointHMetrics(info, ch, &adv, &lsb);
    int gw, gh, xoff, yoff;
    unsigned char *bmp = stbtt_GetCodepointBitmapSubpixel(info, f->scale, f->scale, fx - (int)fx, 0, ch,
                                                         &gw, &gh, &xoff, &yoff);
    if (bmp) {
      for (int yy = 0; yy < gh; yy++)
        for (int xx = 0; xx < gw; xx++)
          if (bmp[yy * gw + xx] >= 128) {
            int ox = (int)fx + xoff + xx, oy = base + yoff + yy;
            put_pixel(d, clip, ox, oy, c16, c8);
            if (f->bold) put_pixel(d, clip, ox + 1, oy, c16, c8);
          }
      stbtt_FreeBitmap(bmp, NULL);
    }
    fx += adv * f->scale;
    if (i + 1 < (int)n) fx += stbtt_GetCodepointKernAdvance(info, ch, (unsigned char)s[i + 1]) * f->scale;
  }
  if (f->underline)
    for (int xx = px; xx < px + w; xx++) put_pixel(d, clip, xx, base + 1, c16, c8);
  if (dc->is_window) win_present();
  return 1;
}
