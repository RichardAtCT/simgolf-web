// graphsy: the interface golf.exe uses to talk to jgl.dll / jgld.dll ("Jackal"
// graphics library, Firaxis). Recovered from the v1.02 NoCD golf.exe and the
// debug jgld.dll; see docs/graphsy.md for how and for open questions.
//
// The DLL exports one function, get_graphsy_object_ptr(), which returns a
// Graphsy object. Every other call goes through C++ vtables, so each class is
// described here as an ordered list of virtual methods, in original slot order
// (the "// N" comments), because decompiled golf.exe code names calls by vtable
// offset (slot * 4). These are reference declarations, not an ABI: under
// Emscripten's Itanium C++ ABI a virtual destructor takes two slots, so
// translated code must call methods by name, never by offset.
//
// All methods are MSVC __thiscall in the original. Types are best guesses
// unless marked otherwise; "guess" means the name or meaning isn't confirmed.
// The browser port reimplements these classes over SDL2/WebGL; it doesn't have
// to reproduce the GDI internals described in comments.

#ifndef SIMGOLF_PLATFORM_GRAPHSY_H
#define SIMGOLF_PLATFORM_GRAPHSY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C++" {

struct GsRect { int32_t left, top, right, bottom; };   // Win32 RECT layout

class GsSurface;
class GsSprite;
class GsFont;
class GsPalette;
class GsApp;
class GsScreen;

// Callbacks golf.exe implements and passes to Graphsy::createWindow().
// jgl's window procedure translates Win32 messages into these calls.
// golf.exe's implementation is the global at 0x0083a4f8 (vtable 0x004ba87c);
// slots 14+ of that vtable are golf-internal and not called by jgl.
class GsApp {
public:
  virtual void onSize(int32_t width, int32_t height) = 0;            //  0 WM_SIZE
  virtual void onPaint(const GsRect *dirty) = 0;                     //  1 WM_PAINT (BeginPaint rect)
  virtual void onHitTest(int32_t x, int32_t y) = 0;                  //  2 WM_NCHITTEST (screen coords)
  virtual void onKey(uint32_t vk, int32_t down) = 0;                 //  3 WM_(SYS)KEYDOWN/UP
  virtual void onChar(uint8_t ch) = 0;                               //  4 WM_CHAR
  virtual void onMouseMove(int32_t x, int32_t y) = 0;                //  5 WM_MOUSEMOVE
  virtual void onMouseWheel(int32_t delta, int32_t x, int32_t y) = 0;//  6 WM_MOUSEWHEEL
  virtual void onLButtonDown(int32_t dblclk, int32_t x, int32_t y) = 0; // 7 WM_LBUTTONDOWN / DBLCLK
  virtual void onLButtonUp(int32_t x, int32_t y) = 0;                //  8 WM_LBUTTONUP
  virtual void onRButtonDown(int32_t dblclk, int32_t x, int32_t y) = 0; // 9 WM_RBUTTONDOWN / DBLCLK
  virtual void onRButtonUp(int32_t x, int32_t y) = 0;                // 10 WM_RBUTTONUP
  virtual void onActivate(int32_t state, int32_t minimized) = 0;     // 11 WM_ACTIVATE
  virtual void onUser401(uint32_t wparam) = 0;                       // 12 WM_USER+1 (guess: timer tick)
  virtual void onUser402() = 0;                                      // 13 WM_USER+2, seen by pumpOne()
};

// The object golf.exe passes as the third argument of createWindow(): its
// screen back buffer (global at 0x0083a7b8). jgl calls slot 0 whenever the
// display mode changes, slot 2 on shutdown, and reads the GsSurface* at
// offset +4 to blit to the window (Graphsy::present).
class GsScreen {
public:
  virtual int32_t resize(int32_t width, int32_t height, int32_t bpp, int32_t flag) = 0; // 0
  virtual void    slot1() = 0;                                       // 1 not called by jgl
  virtual void    release() = 0;                                     // 2
  GsSurface *surface;                                                // +4
};

class GsFont {        // jgl vtable 0x1011da6c, object size 0x24
public:
  virtual ~GsFont() {}                                               // 0 scalar deleting dtor
  virtual void loadFile(const char *ttfPath, int32_t height,         // 1 AddFontResource on the TTF,
                        uint32_t style, int32_t extra) = 0;          //   then create(); args 3-4 passed through
  virtual void create(const char *faceName, int32_t height,          // 2 CreateFontIndirect; style bits:
                      uint32_t style) = 0;                           //   1 bold, 2 italic, 4 underline
  virtual void release() = 0;                                        // 3 delete font + remove resource
  virtual int32_t measure(const char *text, int32_t len) = 0;        // 4 GetTextExtentPoint32 (guess: returns cx)
  // +4 points at metrics: +8 line height, +0xc ascent-internal, +0x10 internal
  // leading, +0x14 ascent, +0x18 descent (TEXTMETRIC-derived).
};

class GsPalette {     // jgl vtable 0x1011db10, object size 0x820
public:
  virtual ~GsPalette() {}                                            // 0
  virtual void reset() = 0;                                          // 1 Windows system colours 0-9, 246-255; ramp in between
  virtual void clear() = 0;                                          // 2
  virtual void apply() = 0;                                          // 3 AnimatePalette entries 10..245
  virtual int32_t get(uint8_t *rgbOut, int32_t first, int32_t count) = 0; // 4 3 bytes per entry
  virtual void set(const uint8_t *rgbIn, int32_t first, int32_t count) = 0; // 5 then rebuilds lookup tables
  virtual void *table16() = 0;                                       // 6 +0x40c, 0x200 bytes (guess: 8->16bpp LUT)
  virtual void *table2() = 0;                                        // 7 +0x60c, 0x200 bytes (guess)
  virtual const uint8_t *entry(int32_t index) = 0;                   // 8 RGB of one entry (static buffer)
};

class Graphsy {       // jgl vtable 0x1011d640, object size 0x14c; golf global 0x0083ad50
public:
  virtual ~Graphsy() {}                                              //  0
  virtual int32_t createWindow(GsApp *app, const char *title,        //  1 "JackalClass" popup window,
                               GsScreen *screen) = 0;                //    full screen; calls screen->resize
  virtual void destroyWindow() = 0;                                  //  2 restores display mode
  virtual void shutdown() = 0;                                       //  3 deletes the global object
  virtual void show() = 0;                                           //  4 ShowWindow(SW_SHOW)
  virtual void hide() = 0;                                           //  5
  virtual void minimize() = 0;                                       //  6
  virtual void maximize() = 0;                                       //  7
  virtual void *hwnd() = 0;                                          //  8
  virtual void *hinstance() = 0;                                     //  9
  virtual void setWindowPos(int32_t x, int32_t y, int32_t w, int32_t h) = 0; // 10
  virtual void cursorPos(int32_t *x, int32_t *y) = 0;                // 11 client coords
  virtual void invalidate(const GsRect *r) = 0;                      // 12 InvalidateRect(r or all)
  virtual void screenSize(int32_t *w, int32_t *h) = 0;               // 13
  virtual void windowRect(GsRect *r) = 0;                            // 14
  virtual void *windowDC() = 0;                                      // 15
  virtual int32_t slot16() = 0;                                      // 16 returns 24 (guess: max bpp)
  virtual int32_t slot17() = 0;                                      // 17 returns 24
  virtual int32_t slot18() = 0;                                      // 18 returns 0
  virtual void    slot19() = 0;                                      // 19 no-op
  virtual void setModeBest(int32_t w, int32_t h, int32_t bpp) = 0;   // 20 highest refresh match
  virtual void setModeDesktop(int32_t bpp) = 0;                      // 21 current desktop size
  virtual int32_t setMode(int32_t w, int32_t h, int32_t bpp) = 0;    // 22 WinMain: setMode(800,600,16)
  virtual void setModeRefresh(int32_t w, int32_t h, int32_t bpp,     // 23
                              bool windowed, int32_t hz) = 0;
  virtual void setModeWindowed(int32_t w, int32_t h, int32_t bpp, bool windowed) = 0; // 24
  virtual void setModeIndex(int32_t index) = 0;                      // 25 refuses 640-wide desktops
  virtual void *enumModes() = 0;                                     // 26 list of {w,h,?,bpp,hz}
  virtual void changeResolution(uint32_t w, uint32_t h) = 0;         // 27
  virtual void restoreResolution() = 0;                              // 28
  virtual GsFont    *newFont(void *owner) = 0;                       // 29
  virtual GsPalette *newPalette(void *owner) = 0;                    // 30
  virtual GsSurface *newSurface(void *owner, int32_t bpp) = 0;       // 31
  virtual GsSprite  *newSprite(void *owner, int32_t bpp) = 0;        // 32
  virtual int32_t    slot33() = 0;                                   // 33 returns 0
  virtual void deleteFont(GsFont *) = 0;                             // 34
  virtual void deleteSurface(GsSurface *) = 0;                       // 35
  virtual void deletePalette(GsPalette *) = 0;                       // 36
  virtual void deleteSprite(GsSprite *) = 0;                         // 37
  virtual void slot38() = 0;                                         // 38 no-op
  virtual void setDrawScale(int32_t xNum, int32_t yNum, int32_t denom) = 0; // 39 global sprite scale x/denom, y/denom;
                                                                     //    (1,1,1) = unscaled fast path. Used for zoom.
  virtual void getDrawScale(int32_t *xNum, int32_t *yNum, int32_t *denom) = 0; // 40
  virtual void present(const GsRect *r) = 0;                         // 41 BitBlt screen surface to window
  virtual int32_t width() = 0;                                       // 42
  virtual int32_t height() = 0;                                      // 43
  virtual int32_t bpp() = 0;                                         // 44
  virtual int32_t slot45() = 0;                                      // 45 returns 0; golf calls it 39 times
  virtual void fontSystemInit() = 0;                                 // 46 creates the shared text DC
  virtual void fontSystemShutdown() = 0;                             // 47
  virtual void paletteSystemInit(void *a, void *b) = 0;              // 48 creates the 256-entry HPALETTE
  virtual void paletteSystemShutdown() = 0;                          // 49
  virtual int32_t slot50() = 0;                                     // 50 returns 24; unused by golf
  virtual int32_t slot51() = 0;                                     // 51 returns 24; unused by golf
  virtual int32_t slot52() = 0;                                     // 52 returns 24; unused by golf
  virtual int32_t slot53() = 0;                                     // 53 returns 24; unused by golf
  virtual int32_t slot54() = 0;                                     // 54 returns 24; unused by golf
  virtual int32_t slot55() = 0;                                     // 55 returns 24; unused by golf
  virtual int32_t slot56() = 0;                                     // 56 returns 24; unused by golf
  virtual int32_t slot57() = 0;                                     // 57 returns 24; unused by golf
  virtual int32_t slot58() = 0;                                     // 58 returns 24; unused by golf
  virtual int32_t slot59() = 0;                                     // 59 returns 24; unused by golf
  virtual int32_t slot60() = 0;                                     // 60 returns 24; unused by golf
  virtual int32_t slot61() = 0;                                     // 61 returns 24; unused by golf
  virtual int32_t slot62() = 0;                                     // 62 returns 24; unused by golf
  virtual int32_t slot63() = 0;                                     // 63 returns 24; unused by golf
  virtual int32_t slot64() = 0;                                     // 64 returns 24; unused by golf
  virtual int32_t slot65() = 0;                                     // 65 returns 24; unused by golf
  virtual int32_t slot66() = 0;                                     // 66 returns 24; unused by golf
  virtual int32_t slot67() = 0;                                     // 67 returns 24; unused by golf
  virtual int32_t slot68() = 0;                                     // 68 returns 24; unused by golf
  virtual int32_t slot69() = 0;                                     // 69 returns 24; unused by golf
  virtual int32_t slot70() = 0;                                     // 70 returns 24; unused by golf
  virtual int32_t slot71() = 0;                                     // 71 returns 24; unused by golf
  virtual void pumpUser401() = 0;                                    // 72 one WM_USER+1 message
  virtual void waitMessage() = 0;                                    // 73 pumpOne(), else WaitMessage
  virtual int32_t pumpOne() = 0;                                     // 74 handles WM_USER+2 itself
  virtual void pumpOldest() = 0;                                     // 75 oldest of input/other messages
  virtual void pumpPaint() = 0;                                      // 76 one WM_PAINT
  virtual void pumpKey() = 0;                                        // 77 one keyboard message
  virtual void pumpCharMsg() = 0;                                    // 78 one WM_CHAR
  virtual void pumpMouse() = 0;                                      // 79 one mouse message
  virtual void pumpMouseMove() = 0;                                  // 80 one WM_MOUSEMOVE
  virtual void pumpClick() = 0;                                      // 81 mouse message; pairs down+up
  virtual void flushKeys() = 0;                                      // 82 discard keyboard messages
  virtual void flushMouse() = 0;                                     // 83 discard mouse messages
  virtual void pumpAllUser401() = 0;                                 // 84
  virtual void setCopyDataHandler(void (*fn)(void *, void *)) = 0;   // 85 guess: WM_COPYDATA hook
};

// GsSurface (jgl vtable 0x1011d0b0, 60 slots) and GsSprite (0x1011d380,
// 41 slots) are declared in graphsy_surface.h.

}  // extern "C++"
#endif  // __cplusplus

#endif  // SIMGOLF_PLATFORM_GRAPHSY_H
