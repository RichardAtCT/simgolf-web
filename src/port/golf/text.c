// golf.exe's styled text drawing, ported by hand from the disassembly
// because the decompile of it is wrong: the palette lookups push the colour
// bytes for setTextColor between calls, and Ghidra hands those pushes to the
// wrong calls.

#include "golf_protos.h"
#include "port/runtime.h"

#define U32(p, off) (*(uint32_t *)((char *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((char *)(p) + (off)))
#define VCALL(obj, off) icall_lookup(*(uint32_t *)((uintptr_t)*(uint32_t *)(uintptr_t)(obj) + (off)))
#define A(x) ((uint32_t)(uintptr_t)(x))
#define Z 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
#define GRAPHSY (*(uint32_t *)0x0083ad50)

// jgld GsSurface methods used here
static void surf_select_font(uint32_t s, uint32_t f) { VCALL(s, 0xa8)(s, f, 0, 0, 0, Z); }
static void surf_unselect_font(uint32_t s) { VCALL(s, 0xac)(s, 0, 0, 0, 0, Z); }
static void surf_text_index(uint32_t s, uint32_t i) { VCALL(s, 0xb0)(s, i, 0, 0, 0, Z); }
static void surf_text_rgb(uint32_t s, int r, int g, int b) { VCALL(s, 0xb4)(s, r & 0xff, g & 0xff, b & 0xff, 0, Z); }
static void surf_text_out(uint32_t s, int x, int y, const char *t, int n) { VCALL(s, 0xb8)(s, x, y, A(t), n, Z); }

// Sets the surface's text colour from a style colour: bit 31 = raw 555/565
// pixel, else a palette index (through `pal` if there is one).
static void set_colour(void *style, uint32_t surface, uint32_t pal, uint32_t c, int index_ok) {
  if (c & 0x80000000u) {
    int mode565 = VCALL(GRAPHSY, 0xb4)(GRAPHSY, 0, 0, 0, 0, Z) == 1;
    int g = mode565 ? ((int)c >> 3) & 0xfc : ((int)c >> 2) & 0xf8;
    int r = mode565 ? ((int)c >> 8) & 0xf8 : ((int)c >> 7) & 0xf8;
    surf_text_rgb(surface, r, g, (c << 3) & 0xff);
  } else if (pal) {
    uint8_t *b = (uint8_t *)(uintptr_t)VCALL(pal, 0x20)(pal, c, 0, 0, 0, Z);
    int blue = b[2];
    uint8_t *g = (uint8_t *)(uintptr_t)VCALL(pal, 0x20)(pal, c, 0, 0, 0, Z);
    int green = g[1];
    uint8_t *r = (uint8_t *)(uintptr_t)VCALL(pal, 0x20)(pal, c, 0, 0, 0, Z);
    surf_text_rgb(surface, r[0], green, blue);
  } else if (index_ok || !(U32(style, 0x20) & 4)) {
    surf_text_index(surface, c);
  }
}

// @override FUN_004767a0
// Draws `len` bytes of `text` at (x, y) with this text style; '_' prints as
// a space and tabs advance to the next 30-pixel stop. Returns x plus the
// width of the text.
int FUN_004767a0(void *this, char *param_1, int param_2, int param_3, uint param_4) {
  char *style = this;
  char *text = param_1;
  int x = param_2, y = param_3, len = (int)param_4;
  uint32_t hook = U32(style, 0xc);
  if (hook) {
    int r = (int)icall_lookup(hook)(A(text), x, y, len, Z, 0);
    if (r) return r;
  }
  uint32_t surface = U32(style, 4);
  if (len == 0 || !surface) return x;

  char *copy = (char *)(uintptr_t)crt_malloc(len);
  if (copy) {
    for (int i = 0; i < len; i++) copy[i] = text[i] == '_' ? ' ' : text[i];
    text = copy;
  }

  // the palette that index colours go through (16-bit surfaces only)
  uint32_t pal = 0;
  int shared_pal = 0;
  uint32_t fmt = VCALL(surface, 0xe4)(surface, 0, 0, 0, 0, Z);
  if (*(int32_t *)(uintptr_t)fmt == 0x10) {
    uint32_t p83ad18 = *(uint32_t *)0x0083ad18;
    if (p83ad18) {
      pal = U32((uintptr_t)p83ad18, 4);
      shared_pal = 1;
    } else if (VCALL(surface, 0xe8)(surface, 0, 0, 0, 0, Z)) {
      pal = VCALL(surface, 0xe8)(surface, 0, 0, 0, 0, Z);
    } else {
      pal = U32((uintptr_t)*(uint32_t *)0x0083ad0c, 4);
    }
  }

  int font_idx, col_idx = 0;
  if (I32(style, 0x40) == 0) {
    font_idx = 0;
    y += I32((uintptr_t)U32(style, 0x5c), 0x10);
  } else {
    int st = I32(style, 0x30);
    font_idx = U32(style, 0x5c + st * 4) ? st : 0;
    col_idx = I32(style, 0x6c + st * 4) != -1 ? st : 0;
    int asc = 0;
    for (int k = 0; k < 3; k++) {
      uint32_t f = U32(style, 0x5c + k * 4);
      if (f && I32((uintptr_t)f, 0x10) > asc) asc = I32((uintptr_t)f, 0x10);
    }
    y += asc;
  }
  uint32_t font = U32(style, 0x5c + font_idx * 4);
  surf_select_font(surface, font ? font + 4 : 0);

  // shadow pass, offset by (+0x8c, +0x9c)
  uint32_t shadow = U32(style, 0x7c + col_idx * 4);
  if (shadow != 0xffffffffu && !(shared_pal && shadow == 0xff)) {
    set_colour(style, surface, pal, shadow, 0);
    surf_text_out(surface, x + I32(style, 0x8c + col_idx * 4), y + I32(style, 0x9c + col_idx * 4), text, len);
  }

  set_colour(style, surface, pal, U32(style, 0x6c + col_idx * 4), 0);
  char seg[256];
  int start = 0, pos = x;
  for (int i = 0; i <= len; i++) {
    if (i < len && text[i] != '\t') continue;
    int n = i - start;
    if (n > (int)sizeof seg) n = sizeof seg;
    for (int k = 0; k < n; k++) seg[k] = text[start + k];
    surf_text_out(surface, pos, y, seg, n);
    pos += (int)FUN_00483930((void *)(uintptr_t)font, seg, n);
    if (i < len) pos += 30 - pos % 30;   // tab stop
    start = i + 1;
  }
  surf_unselect_font(surface);
  int width = (int)FUN_00483930((void *)(uintptr_t)font, text, len);
  if (copy) crt_free_((nu)(uintptr_t)copy);
  return width + param_2;
}
