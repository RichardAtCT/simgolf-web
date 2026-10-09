// graphsy drawing classes: GsSurface (a DIB-backed render target) and GsSprite
// (an 8- or 16-bit image drawn onto surfaces). Slot-by-slot notes, including
// pixel rules and known bugs in the original, are in docs/graphsy-surface.md
// and docs/graphsy-sprite.md.
//
// Conventions shared by both classes:
// - Pixels are top-down, ptr = bits + (x + y*pitch) * bytesPerPixel, and
//   pitch is in pixels. The game draws at 8 and 16 bpp; 16 bpp is RGB555.
// - A "colour" argument with bit 31 set is a literal 16-bit pixel; otherwise
//   its low byte is a palette index (through the surface's palette, or the
//   global default palette if it has none).
// - 8-bit sprite indices: 0xFF is always transparent, plain Draw also skips
//   0xFE, and 0xF8-0xFE are shadow/light pixels in the effect draws.
// - Graphsy::setDrawScale() scales and (with a negative numerator) mirrors
//   every sprite draw. Destination writes are clipped to the surface clip rect.
// - Blit-style surface methods use `this` as the source and the argument as
//   the destination.

#ifndef SIMGOLF_PLATFORM_GRAPHSY_SURFACE_H
#define SIMGOLF_PLATFORM_GRAPHSY_SURFACE_H

#include "graphsy.h"

#ifdef __cplusplus
extern "C++" {

struct GsPixelFormat {          // GsSurface::pixelFormat(), surface +0x24
  int32_t  bpp;
  int32_t  is565;               // always 0 (RGB555) in practice
  int16_t  shiftR, shiftG, shiftB;
  uint16_t maskR, maskG, maskB;
};

class GsSurface {     // jgl vtable 0x1011d0b0, object size 0x4d4
public:
  virtual ~GsSurface() {}                                            //  0 clears owner->+4
  virtual void create(int32_t w, int32_t h, int32_t bpp) = 0;        //  1 bpp 0 = display default; exit(4) on failure
  virtual void release() = 0;                                        //  2 frees GDI objects
  virtual uint8_t *pixelPtr(int32_t x, int32_t y) = 0;               //  3 locks; caller unlocks
  virtual uint8_t *lock() = 0;                                       //  4
  virtual uint8_t *pixelPtr8(int32_t x, int32_t y) = 0;              //  5 = 3
  virtual uint8_t *lock8() = 0;                                      //  6 = 4
  virtual uint16_t *pixelPtr16(int32_t x, int32_t y) = 0;            //  7 = 3
  virtual uint16_t *lock16() = 0;                                    //  8 = 4
  virtual void unlock(int32_t n) = 0;                                //  9
  virtual void *getDC() = 0;                                         // 10 refcounted
  virtual void releaseDC(int32_t n) = 0;                             // 11
  virtual void nop12(int32_t) = 0;                                   // 12
  virtual void setClipRect(const GsRect *r) = 0;                     // 13 clipped to bounds
  virtual void blitBehindTo(GsSurface *dst, int32_t sx, int32_t sy,  // 14 8bpp src; writes only where
                            int32_t dx, int32_t dy, int32_t w,       //    dst pixel == key
                            int32_t h, uint8_t key) = 0;
  virtual void blitTo(GsSurface *dst, int32_t sx, int32_t sy,        // 15 opaque copy, overlap-safe;
                      int32_t dx, int32_t dy, int32_t w, int32_t h) = 0; // 8->8, 8->16, 16->16
  virtual void stretchTo(GsSurface *dst, const GsRect *src,          // 16 GDI StretchBlt
                         const GsRect *dstRect) = 0;
  virtual void fillRect(const GsRect *r, uint32_t colour) = 0;       // 17 NULL = whole clip
  virtual void blendFillRect(const GsRect *r, uint32_t colour,       // 18 16bpp; keepPct = % of old pixel kept
                             int32_t keepPct) = 0;
  virtual void fillRectChecker(const GsRect *r, uint32_t colour) = 0;// 19 50% stipple
  virtual void blendFillRectChecker(const GsRect *r, uint32_t colour,// 20 16bpp
                                    int32_t keepPct) = 0;
  virtual void shadeFillHoles(GsRect *r, GsSurface *src, float level,// 21 16bpp; 16x32768 LUT, magenta
                              const uint16_t *lut) = 0;              //    holes take src (partly guessed)
  virtual void replaceColor(const GsRect *r, uint32_t from, uint32_t to) = 0; // 22 raw compare
  virtual void remapRect(const GsRect *r, const uint8_t *lut256) = 0;// 23 8bpp
  virtual void drawDashedLine(int32_t x1, int32_t y1, int32_t x2,    // 24 inclusive; -1 skips (8bpp only)
                              int32_t y2, uint32_t colourA, uint32_t colourB,
                              int32_t lenA, int32_t lenB, int32_t phase) = 0;
  virtual void drawLine(int32_t x1, int32_t y1, int32_t x2, int32_t y2, // 25 inclusive Bresenham
                        uint32_t colour) = 0;
  virtual int32_t unsupported26(int32_t, int32_t, int32_t, int32_t) = 0; // 26 returns 0x18
  virtual int32_t unsupported27(int32_t, int32_t, int32_t, int32_t) = 0; // 27 returns 0x18
  virtual int32_t unsupported28(int32_t) = 0;                        // 28 returns 0x18
  virtual int32_t loadPNG(const char *path, GsPalette *pal,          // 29 24-bit RGB PNG only; recreates
                          int32_t first, int32_t count) = 0;         //    via owner. 0 ok, 6 no file, 0x17 type
  virtual int32_t savePCX(const char *path) = 0;                     // 30 8bpp, RLE + 256 palette
  virtual int32_t unsupported31(int32_t) = 0;                        // 31 returns 0x18
  virtual int32_t unsupported32(int32_t, int32_t, int32_t, int32_t) = 0; // 32 returns 0x18
  virtual int32_t drawTransparentTo(GsSurface *dst, int32_t x, int32_t y) = 0; // 33 via GsSprite::draw
  virtual int32_t unsupported34(int32_t, int32_t, int32_t, int32_t) = 0; // 34 returns 0x18
  virtual int32_t drawShadowedTo(GsSurface *dst, int32_t x, int32_t y,   // 35 via GsSprite::drawShadow
                                 const uint16_t *shadowLut) = 0;
  virtual int32_t unsupported36(int32_t, int32_t, int32_t, int32_t) = 0; // 36-41 return 0x18
  virtual int32_t unsupported37(int32_t, int32_t, int32_t, int32_t) = 0;
  virtual int32_t unsupported38(int32_t, int32_t, int32_t, int32_t) = 0;
  virtual int32_t unsupported39(int32_t, int32_t, int32_t, int32_t) = 0;
  virtual int32_t unsupported40(int32_t, int32_t, int32_t, int32_t) = 0;
  virtual int32_t unsupported41(int32_t, int32_t, int32_t, int32_t) = 0;
  virtual void selectFont(GsFont **font) = 0;                        // 42
  virtual void selectDefaultFont() = 0;                              // 43 SYSTEM_FONT
  virtual void setTextColorIndex(uint32_t index) = 0;                // 44
  virtual void setTextColorRGB(uint8_t r, uint8_t g, uint8_t b) = 0; // 45
  virtual int32_t textOut(int32_t x, int32_t yBaseline,              // 46 transparent background
                          const char *s, int32_t len) = 0;
  virtual int32_t setColorKey(uint32_t key) = 0;                     // 47 default 0x80007c1f (magenta)
  virtual uint32_t colorKey() = 0;                                   // 48
  virtual void resetClip() = 0;                                      // 49
  virtual void clipRect(GsRect *out) = 0;                            // 50
  virtual GsRect *clipRectPtr() = 0;                                 // 51
  virtual void bounds(GsRect *out) = 0;                              // 52 {0,0,w,h}
  virtual GsRect *boundsPtr() = 0;                                   // 53
  virtual int32_t width() = 0;                                       // 54
  virtual int32_t height() = 0;                                      // 55
  virtual int32_t pitch() = 0;                                       // 56 in pixels
  virtual GsPixelFormat *pixelFormat() = 0;                          // 57
  virtual GsPalette *palette() = 0;                                  // 58 may be NULL
  virtual void setPalette(GsPalette *pal) = 0;                       // 59 uploads DIB colour table
};

class GsSprite {      // jgl vtable 0x1011d380, object size 0x50 (jglsprite.cpp)
public:
  virtual ~GsSprite() {}                                             //  0 clears *owner
  virtual void create(const void *pixels, uint32_t w, int32_t h,     //  1 copies pixels; may trim rows
                      int32_t bpp, int32_t unused, GsPalette *pal) = 0; //   (compression is off by default)
  virtual void free() = 0;                                           //  2
  virtual void copyFrom(const GsSprite *src) = 0;                    //  3 deep copy, incl. colour key
  virtual void setPalette(GsPalette *pal) = 0;                       //  4 NULL = global default
  virtual GsPalette *palette() = 0;                                  //  5
  virtual void setColorKey(uint32_t key) = 0;                        //  6 default 0xFF
  virtual uint32_t colorKey() = 0;                                   //  7
  virtual void *bits() = 0;                                          //  8 non-NULL = loaded
  virtual void unlock(int32_t) = 0;                                  //  9 no-op
  virtual int32_t bpp() = 0;                                         // 10 8 or 16
  virtual int32_t pitch() = 0;                                       // 11 pixels
  virtual int32_t width() = 0;                                       // 12
  virtual int32_t height() = 0;                                      // 13
  virtual void replaceIndex(uint8_t from, uint8_t to) = 0;           // 14 8-bit only
  // Draws. palOverride, when non-NULL, replaces the sprite's palette for the
  // call (slots 18-22 and 34-35 leave it in place afterwards: original bug).
  virtual void drawColorLUT(GsSurface *dst, int32_t x, int32_t y,    // 15 dst = lut[pal16[i]]
                            const uint16_t *lut, GsPalette *palOverride) = 0;
  virtual void drawRemapped(GsSurface *dst, int32_t x, int32_t y,    // 16 dst = pal16[remap[i]]
                            const uint8_t *remap, GsPalette *palOverride) = 0;
  virtual void draw(GsSurface *dst, int32_t x, int32_t y,            // 17 main blit; skips 0xFE/0xFF
                    GsPalette *palOverride) = 0;                     //    (16bpp: skips colour key)
  virtual void drawTintHalf(GsSurface *dst, int32_t x, int32_t y,    // 18 50/50 with colour
                            uint16_t colour555, GsPalette *palOverride) = 0;
  virtual void drawColorize(GsSurface *dst, int32_t x, int32_t y,    // 19 luminance * colour
                            uint16_t colour555, GsPalette *palOverride) = 0;
  virtual void drawMaskedOver(GsSprite *mask8, GsSurface *bg,        // 20 anti-aliased edge blend (guess)
                              GsSurface *dst, int32_t x, int32_t y,
                              GsPalette *palOverride) = 0;
  virtual void drawMasked(GsSprite *mask8, GsSurface *dst,           // 21 as 20 with bg = dst
                          int32_t x, int32_t y, GsPalette *palOverride) = 0;
  virtual void drawMaskedGeneric(GsSprite *mask8, GsSurface *dst,    // 22 slow per-pixel version of 21
                                 int32_t x, int32_t y, GsPalette *palOverride) = 0;
  virtual void drawWithFE(GsSurface *dst, int32_t x, int32_t y,      // 23 like draw, but 0xFE drawn
                          GsPalette *palOverride) = 0;
  virtual void unimplemented24(GsSurface *dst, int32_t, int32_t,     // 24 asserts
                               int32_t, int32_t, GsPalette *) = 0;
  virtual void drawRemappedShadow(GsSurface *dst, int32_t x, int32_t y, // 25 0xF8: dst = shadowLut[dst]
                                  const uint8_t *remap, const uint16_t *shadowLut,
                                  GsPalette *palOverride) = 0;
  virtual void drawRemappedShadow8(GsSurface *dst, int32_t x, int32_t y, // 26 8->8 (guess)
                                   const uint8_t *remap, const uint8_t *shadowLut,
                                   GsPalette *palOverride) = 0;
  virtual void drawShadow(GsSurface *dst, int32_t x, int32_t y,      // 27 0xF8 darkens dst
                          const uint16_t *shadowLut, GsPalette *palOverride) = 0;
  virtual void drawShadow8(GsSurface *dst, int32_t x, int32_t y,     // 28 8->8 (guess)
                           const uint8_t *lut, GsPalette *palOverride) = 0;
  virtual void drawSilhouette(GsSurface *dst, int32_t x, int32_t y,  // 29 solid colour where i < 0xFE
                              uint32_t colour, GsPalette *palOverride) = 0;
  virtual void drawWithLightLevels(GsSurface *dst, int32_t x, int32_t y, // 30 0xF8-0xFE = 7 LUT planes
                                   const uint16_t *lut, GsPalette *palOverride) = 0;
  virtual void drawLightMap(GsSurface *dst, int32_t x, int32_t y,    // 31 sprite is a light map
                            const uint16_t *lut, GsPalette *palOverride) = 0;
  virtual void drawLightMap8(GsSurface *dst, int32_t x, int32_t y,   // 32 8->8 (guess)
                             const void *lut, GsPalette *palOverride) = 0;
  virtual void drawShadeMap(GsSurface *dst, int32_t x, int32_t y,    // 33 0 = black, 1-15 shade levels
                            const uint16_t *lut, GsPalette *palOverride) = 0;
  virtual void drawEffectScaled(GsSurface *dst, GsSurface *bg,       // 34 explicit scale, sx == sy only
                                int32_t x, int32_t y, int32_t sx, int32_t sy,
                                int32_t den, const uint16_t *lut, GsPalette *palOverride) = 0;
  virtual void drawEffect(GsSurface *dst, GsSurface *bg, int32_t x,  // 35 0xE0-0xFE LUT planes; magenta
                          int32_t y, const uint16_t *lut,            //    dst pixels read from bg
                          GsPalette *palOverride) = 0;
  virtual void drawEffectNoBg(GsSurface *dst, int32_t x, int32_t y,  // 36
                              const uint16_t *lut, GsPalette *palOverride) = 0;
  virtual void drawTranslucent(GsSurface *dst, int32_t x, int32_t y, // 37 opacity quantised to 1/16
                               float opacity, GsPalette *palOverride,
                               bool skipSpecial) = 0;
  virtual void drawDithered(GsSurface *dst, int32_t x, int32_t y,    // 38 percent: 25, 50 or 100
                            int32_t percent, GsPalette *palOverride) = 0;
  virtual void drawDepthOutline(GsSurface *dst, int32_t x, int32_t y,// 39 hidden parts drawn as outline
                                GsSurface *depth, uint8_t threshold,
                                uint16_t outlineColour, GsPalette *palOverride) = 0;
  virtual void drawDepthTested(GsSurface *dst, int32_t x, int32_t y, // 40 drawn where depth >= threshold
                               GsSurface *depth, uint8_t threshold,
                               GsPalette *palOverride) = 0;
};

}  // extern "C++"
#endif  // __cplusplus

#endif  // SIMGOLF_PLATFORM_GRAPHSY_SURFACE_H
