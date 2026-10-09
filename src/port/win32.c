// KERNEL32, USER32 and WINMM functions used by golf.exe and jgld.dll.
// See win32.h. Argument types are plain 32-bit values; structures are read
// and written at their Win32 offsets.

#define _GNU_SOURCE
#include "port/win32.h"

#include <SDL.h>
#include <emscripten.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "port/fs.h"
#include "port/runtime.h"

#define P(x) ((void *)(uintptr_t)(x))
#define S(x) ((char *)(uintptr_t)(x))
#define U(x) ((nu)(uintptr_t)(x))
#define I32(p, off) (*(int32_t *)((char *)P(p) + (off)))
#define U32(p, off) (*(uint32_t *)((char *)P(p) + (off)))
#define U16(p, off) (*(uint16_t *)((char *)P(p) + (off)))

#define CALL4(fn, a, b, c, d) icall_lookup(fn)(a, b, c, d, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define CALL5(fn, a, b, c, d, e) icall_lookup(fn)(a, b, c, d, e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

static nu g_last_error;

EM_JS(int, js_api_trace_enabled, (void), { return Module.apiTrace ? 1 : 0; });

void api_trace(const char *name) {
  static int enabled = -1, count;
  if (enabled < 0) enabled = js_api_trace_enabled();
  if (enabled && count < 4000) {
    count++;
    fprintf(stderr, "api %s\n", name);
  }
}

// ------------------------------------------------------------------ handles

typedef struct { int kind; void *data; } Obj;
#define MAX_OBJ 4096
static Obj g_obj[MAX_OBJ];
#define OBJ_BASE 0x00020000u

nu obj_new(int kind, void *data) {
  for (int i = 1; i < MAX_OBJ; i++) {
    if (g_obj[i].kind == OBJ_FREE) {
      g_obj[i].kind = kind;
      g_obj[i].data = data;
      return OBJ_BASE + i * 4;
    }
  }
  int count[16] = {0};
  for (int i = 1; i < MAX_OBJ; i++) count[g_obj[i].kind & 15]++;
  fprintf(stderr, "out of handles; by kind (enum in win32.h):");
  for (int k = 0; k < 16; k++)
    if (count[k]) fprintf(stderr, " %d:%d", k, count[k]);
  fprintf(stderr, "\n");
  port_abort("out of handles");
}

void *obj_get(nu h, int kind) {
  if (h < OBJ_BASE || h >= OBJ_BASE + MAX_OBJ * 4 || (h & 3)) return NULL;
  Obj *o = &g_obj[(h - OBJ_BASE) / 4];
  if (o->kind != kind) return NULL;
  return o->data;
}

int obj_kind(nu h) {
  if (h < OBJ_BASE || h >= OBJ_BASE + MAX_OBJ * 4 || (h & 3)) return -1;
  return g_obj[(h - OBJ_BASE) / 4].kind;
}

void obj_free(nu h) {
  if (h < OBJ_BASE || h >= OBJ_BASE + MAX_OBJ * 4) return;
  Obj *o = &g_obj[(h - OBJ_BASE) / 4];
  o->kind = OBJ_FREE;
  o->data = NULL;
}

// ------------------------------------------------------------------ time and yielding

uint32_t win_ticks(void) { return (uint32_t)emscripten_get_now(); }

static double g_last_yield;
static void pump_sdl(void);
static void run_mm_timers(void);

// rAF would pace frames nicely, but it never fires in hidden tabs (or some
// headless setups), which would freeze the game; a timeout always returns.
EM_ASYNC_JS(void, js_wait_frame, (int ms), {
  if (ms > 0) { await new Promise(r => setTimeout(r, ms)); return; }
  // ms == 0: a macrotask without setTimeout's 4 ms clamp on nested timers
  if (!Module.yieldChannel) {
    Module.yieldChannel = new MessageChannel();
    Module.yieldQueue = [];
    Module.yieldChannel.port1.onmessage = () => { const r = Module.yieldQueue.shift(); if (r) r(); };
  }
  await new Promise(r => { Module.yieldQueue.push(r); Module.yieldChannel.port2.postMessage(0); });
});

static unsigned g_yields;

void win_yield(int ms) {
  // every 10 s: yields per second and the share of time spent in game code
  static double stat_t0, busy;
  double now = emscripten_get_now();
  ++g_yields;
  if (g_last_yield > 0) busy += now - g_last_yield;
  if (stat_t0 == 0) stat_t0 = now;
  if (now - stat_t0 >= 10000) {
    static unsigned last_n, last_p;
    extern unsigned g_presents;
    int handles = 0;
    for (int i = 1; i < MAX_OBJ; i++) handles += g_obj[i].kind != OBJ_FREE;
    fprintf(stderr, "perf: %.1f frames/s, %.1f yields/s, game code %.0f%% of the time, %d/%d handles\n",
            (g_presents - last_p) * 1000.0 / (now - stat_t0),
            (g_yields - last_n) * 1000.0 / (now - stat_t0), busy * 100.0 / (now - stat_t0), handles, MAX_OBJ);
    last_n = g_yields; last_p = g_presents; stat_t0 = now; busy = 0;
  }
  js_wait_frame(ms);
  g_last_yield = emscripten_get_now();
  pump_sdl();
  run_mm_timers();
}

nu timeGetTime(void) { return win_ticks(); }
nu GetTickCount(void) { API_TRACE(); return win_ticks(); }

nu QueryPerformanceCounter(nu out) {
  API_TRACE();
  // Browsers coarsen performance.now(); keep the counter strictly
  // increasing, because graphsy seeds palette serials from it (equal seeds
  // gave equal serials and stale 16-bit colour tables).
  static uint64_t last;
  uint64_t v = (uint64_t)(emscripten_get_now() * 1000.0);
  if (v <= last) v = last + 1;
  last = v;
  U32(out, 0) = (uint32_t)v;
  U32(out, 4) = (uint32_t)(v >> 32);
  return 1;
}

nu QueryPerformanceFrequency(nu out) {
  API_TRACE();
  U32(out, 0) = 1000000;
  U32(out, 4) = 0;
  return 1;
}

void Sleep(nu ms) { API_TRACE(); win_yield((int)ms); }

// Multimedia timers (timeSetEvent): Windows runs them on a timer thread; here
// they fire from yield points and message polling.
typedef struct { nu id, delay, fn, user, periodic; double next; int live; } MmTimer;
static MmTimer g_mm[8];

nu timeSetEvent(nu delay, nu resolution, nu fn, nu user, nu flags) {
  (void)resolution;
  for (int i = 0; i < 8; i++) {
    if (!g_mm[i].live) {
      g_mm[i] = (MmTimer){i + 1, delay ? delay : 1, fn, user, flags & 1, emscripten_get_now() + delay, 1};
      return i + 1;
    }
  }
  return 0;
}

nu timeKillEvent(nu id) {
  if (id >= 1 && id <= 8) g_mm[id - 1].live = 0;
  return 0;
}

static void run_mm_timers(void) {
  double now = emscripten_get_now();
  for (int i = 0; i < 8; i++) {
    MmTimer *t = &g_mm[i];
    if (!t->live || now < t->next) continue;
    if (t->periodic) t->next = now + t->delay;
    else t->live = 0;
    CALL5(t->fn, t->id, 0, t->user, 0, 0);
  }
}

// ------------------------------------------------------------------ window and messages

typedef struct { nu hwnd, message, wparam, lparam, time; int32_t x, y; } Msg;
#define QCAP 512
static Msg g_q[QCAP];
static int g_qhead, g_qlen;
static nu g_wndproc, g_class_style;
static int g_invalid;
static int g_mouse_x, g_mouse_y;
static uint8_t g_keys[256];          // bit 7 = down
static int g_cursor_count;

typedef struct { nu id, ms, proc; double next; int live; } WinTimer;
static WinTimer g_timers[16];

void win_post(nu msg, nu wparam, nu lparam) {
  if (g_qlen == QCAP) return;
  Msg *m = &g_q[(g_qhead + g_qlen++) % QCAP];
  m->hwnd = WIN_HWND;
  m->message = msg;
  m->wparam = wparam;
  m->lparam = lparam;
  m->time = win_ticks();
  m->x = g_mouse_x;
  m->y = g_mouse_y;
}

void win_invalidate(void) { g_invalid = 1; }

static nu send_message(nu msg, nu wparam, nu lparam) {
  if (!g_wndproc) return 0;
  return CALL4(g_wndproc, WIN_HWND, msg, wparam, lparam);
}

static nu vk_from_sdl(SDL_Keycode k) {
  if (k >= 'a' && k <= 'z') return k - 'a' + 'A';
  if (k >= '0' && k <= '9') return k;
  if (k >= SDLK_F1 && k <= SDLK_F12) return 0x70 + (k - SDLK_F1);
  switch (k) {
    case SDLK_BACKSPACE: return 0x08;
    case SDLK_TAB: return 0x09;
    case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0d;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return 0x10;
    case SDLK_LCTRL: case SDLK_RCTRL: return 0x11;
    case SDLK_LALT: case SDLK_RALT: return 0x12;
    case SDLK_PAUSE: return 0x13;
    case SDLK_CAPSLOCK: return 0x14;
    case SDLK_ESCAPE: return 0x1b;
    case SDLK_SPACE: return 0x20;
    case SDLK_PAGEUP: return 0x21;
    case SDLK_PAGEDOWN: return 0x22;
    case SDLK_END: return 0x23;
    case SDLK_HOME: return 0x24;
    case SDLK_LEFT: return 0x25;
    case SDLK_UP: return 0x26;
    case SDLK_RIGHT: return 0x27;
    case SDLK_DOWN: return 0x28;
    case SDLK_INSERT: return 0x2d;
    case SDLK_DELETE: return 0x2e;
    case SDLK_KP_0: case SDLK_KP_1: case SDLK_KP_2: case SDLK_KP_3: case SDLK_KP_4:
    case SDLK_KP_5: case SDLK_KP_6: case SDLK_KP_7: case SDLK_KP_8: case SDLK_KP_9:
      return 0x60 + (k == SDLK_KP_0 ? 0 : k - SDLK_KP_1 + 1);
    case SDLK_KP_MULTIPLY: return 0x6a;
    case SDLK_KP_PLUS: return 0x6b;
    case SDLK_KP_MINUS: return 0x6d;
    case SDLK_KP_PERIOD: return 0x6e;
    case SDLK_KP_DIVIDE: return 0x6f;
    case SDLK_SEMICOLON: return 0xba;
    case SDLK_EQUALS: return 0xbb;
    case SDLK_COMMA: return 0xbc;
    case SDLK_MINUS: return 0xbd;
    case SDLK_PERIOD: return 0xbe;
    case SDLK_SLASH: return 0xbf;
    case SDLK_BACKQUOTE: return 0xc0;
    case SDLK_LEFTBRACKET: return 0xdb;
    case SDLK_BACKSLASH: return 0xdc;
    case SDLK_RIGHTBRACKET: return 0xdd;
    case SDLK_QUOTE: return 0xde;
  }
  return 0;
}

static nu mouse_keys(void) {
  nu k = 0;
  if (g_keys[0x01] & 0x80) k |= 1;
  if (g_keys[0x02] & 0x80) k |= 2;
  if (g_keys[0x10] & 0x80) k |= 4;
  if (g_keys[0x11] & 0x80) k |= 8;
  return k;
}

static nu xy(int x, int y) { return ((nu)(uint16_t)y << 16) | (uint16_t)x; }

#define EDGE_SCROLL_RELEASE 24  // px from the edge where the course view stops scrolling

static void pump_sdl(void) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_MOUSEMOTION:
        g_mouse_x = e.motion.x;
        g_mouse_y = e.motion.y;
        win_post(0x200, mouse_keys(), xy(g_mouse_x, g_mouse_y));
        break;
      case SDL_WINDOWEVENT:
        // In the original the pointer couldn't leave the full-screen window;
        // here the last position would stay on the edge and the course view
        // would edge-scroll forever. Leaving the canvas (or the tab losing
        // focus) pulls the position in from the edges instead.
        if (e.window.event == SDL_WINDOWEVENT_LEAVE || e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
          const int margin = EDGE_SCROLL_RELEASE;
          int x = g_mouse_x < margin ? margin : g_mouse_x > WIN_SCREEN_W - 1 - margin ? WIN_SCREEN_W - 1 - margin : g_mouse_x;
          int y = g_mouse_y < margin ? margin : g_mouse_y > WIN_SCREEN_H - 1 - margin ? WIN_SCREEN_H - 1 - margin : g_mouse_y;
          if (x != g_mouse_x || y != g_mouse_y) {
            g_mouse_x = x;
            g_mouse_y = y;
            win_post(0x200, mouse_keys(), xy(g_mouse_x, g_mouse_y));
          }
        }
        break;
      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP: {
        int down = e.type == SDL_MOUSEBUTTONDOWN;
        g_mouse_x = e.button.x;
        g_mouse_y = e.button.y;
        int left = e.button.button == SDL_BUTTON_LEFT;
        if (!left && e.button.button != SDL_BUTTON_RIGHT) break;
        g_keys[left ? 1 : 2] = down ? 0x80 : 0;
        nu msg = left ? (down ? 0x201 : 0x202) : (down ? 0x204 : 0x205);
        if (down && e.button.clicks >= 2 && (g_class_style & 8)) msg = left ? 0x203 : 0x206;
        win_post(msg, mouse_keys(), xy(g_mouse_x, g_mouse_y));
        break;
      }
      case SDL_MOUSEWHEEL:
        win_post(0x20a, ((nu)(uint16_t)(int16_t)(e.wheel.y * 120) << 16) | mouse_keys(),
                 xy(g_mouse_x, g_mouse_y));
        break;
      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        // Ctrl+F12: debugging aid, dump the recent function trace
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F12 && (e.key.keysym.mod & KMOD_CTRL)) {
          port_trace_dump(400);
          break;
        }
        nu vk = vk_from_sdl(e.key.keysym.sym);
        if (!vk) break;
        int down = e.type == SDL_KEYDOWN;
        g_keys[vk] = down ? 0x80 : 0;
        nu lp = ((nu)e.key.keysym.scancode << 16) | 1;
        if (!down) lp |= 0xc0000000u;
        else if (e.key.repeat) lp |= 0x40000000u;
        win_post(down ? 0x100 : 0x101, vk, lp);
        // Control characters that SDL_TEXTINPUT doesn't produce.
        if (down && (vk == 0x08 || vk == 0x0d || vk == 0x1b || vk == 0x09)) win_post(0x102, vk, lp);
        break;
      }
      case SDL_TEXTINPUT:
        for (const unsigned char *p = (const unsigned char *)e.text.text; *p; p++)
          if (*p < 0x80) win_post(0x102, *p, 1);
        break;
    }
  }
}

static void run_win_timers(void) {
  double now = emscripten_get_now();
  for (int i = 0; i < 16; i++) {
    WinTimer *t = &g_timers[i];
    if (!t->live || now < t->next) continue;
    t->next = now + t->ms;
    int queued = 0;
    for (int k = 0; k < g_qlen; k++) {
      Msg *m = &g_q[(g_qhead + k) % QCAP];
      if (m->message == 0x113 && m->wparam == t->id) queued = 1;
    }
    if (!queued) win_post(0x113, t->id, t->proc);
  }
}

nu SetTimer(nu hwnd, nu id, nu ms, nu proc) {
  API_TRACE();
  (void)hwnd;
  for (int i = 0; i < 16; i++)
    if (g_timers[i].live && g_timers[i].id == id) g_timers[i].live = 0;
  for (int i = 0; i < 16; i++) {
    if (!g_timers[i].live) {
      g_timers[i] = (WinTimer){id ? id : (nu)(i + 100), ms ? ms : 1, proc, emscripten_get_now() + ms, 1};
      return g_timers[i].id;
    }
  }
  return 0;
}

nu KillTimer(nu hwnd, nu id) {
  API_TRACE();
  (void)hwnd;
  for (int i = 0; i < 16; i++)
    if (g_timers[i].live && g_timers[i].id == id) g_timers[i].live = 0;
  return 1;
}

static int msg_match(nu m, nu lo, nu hi) { return (!lo && !hi) || (m >= lo && m <= hi); }

nu PeekMessageA(nu lpmsg, nu hwnd, nu lo, nu hi, nu remove) {
  API_TRACE();
  (void)hwnd;
  double now = emscripten_get_now();
  pump_sdl();
  run_win_timers();
  run_mm_timers();
  for (int k = 0; k < g_qlen; k++) {
    Msg *m = &g_q[(g_qhead + k) % QCAP];
    if (!msg_match(m->message, lo, hi)) continue;
    memcpy(P(lpmsg), m, 28);
    if (remove & 1) {
      // remove entry k, keeping order
      for (int j = k; j < g_qlen - 1; j++) g_q[(g_qhead + j) % QCAP] = g_q[(g_qhead + j + 1) % QCAP];
      g_qlen--;
    }
    return 1;
  }
  if (g_invalid && msg_match(0x0f, lo, hi)) {
    Msg m = {WIN_HWND, 0x0f, 0, 0, win_ticks(), g_mouse_x, g_mouse_y};
    memcpy(P(lpmsg), &m, 28);
    return 1;
  }
  // Nothing to do: the game polls PeekMessage and the clock between frames,
  // so sleep a little instead of spinning (and let the browser run).
  if (now - g_last_yield > 1) win_yield(1);
  return 0;
}

nu GetMessageA(nu lpmsg, nu hwnd, nu lo, nu hi) {
  API_TRACE();
  while (!PeekMessageA(lpmsg, hwnd, lo, hi, 1)) win_yield(0);
  return U32(lpmsg, 4) != 0x12;
}

nu WaitMessage(void) {
  API_TRACE();
  for (;;) {
    pump_sdl();
    run_win_timers();
    if (g_qlen || g_invalid) return 1;
    win_yield(0);
  }
}

nu TranslateMessage(nu lpmsg) { API_TRACE(); (void)lpmsg; return 0; }

nu DispatchMessageA(nu lpmsg) {
  API_TRACE();
  nu msg = U32(lpmsg, 4);
  if (msg == 0x113 && U32(lpmsg, 12)) {
    return CALL4(U32(lpmsg, 12), WIN_HWND, 0x113, U32(lpmsg, 8), win_ticks());
  }
  return send_message(msg, U32(lpmsg, 8), U32(lpmsg, 12));
}

nu PostMessageA(nu hwnd, nu msg, nu wparam, nu lparam) {
  API_TRACE();
  (void)hwnd;
  win_post(msg, wparam, lparam);
  return 1;
}

nu SendMessageA(nu hwnd, nu msg, nu wparam, nu lparam) {
  API_TRACE();
  (void)hwnd;
  return send_message(msg, wparam, lparam);
}

nu DefWindowProcA(nu hwnd, nu msg, nu wparam, nu lparam) {
  API_TRACE();
  (void)hwnd; (void)wparam; (void)lparam;
  if (msg == 0x0f) g_invalid = 0;   // WM_PAINT handled by default: validate
  return 0;
}

nu RegisterClassA(nu wc) {
  API_TRACE();
  g_class_style = U32(wc, 0);
  g_wndproc = U32(wc, 4);
  fprintf(stderr, "RegisterClassA %s wndproc %08x style %x\n", S(U32(wc, 36)), g_wndproc, g_class_style);
  return 0xc001;
}

nu UnregisterClassA(nu name, nu inst) { API_TRACE(); (void)name; (void)inst; return 1; }



nu CreateWindowExA(nu exstyle, nu cls, nu title, nu style, nu x, nu y, nu w, nu h,
                   nu parent, nu menu, nu inst, nu param) {
  API_TRACE();
  (void)exstyle; (void)cls; (void)style; (void)x; (void)y; (void)w; (void)h;
  (void)parent; (void)menu;
  fprintf(stderr, "CreateWindowExA \"%s\" %dx%d\n", title ? S(title) : "", (int)w, (int)h);

  // CREATESTRUCTA: lpCreateParams, hInstance, hMenu, hwndParent, cy, cx, y, x, style, name, class, exstyle
  static uint32_t cs[12];
  cs[0] = param; cs[1] = inst; cs[4] = h; cs[5] = w; cs[6] = y; cs[7] = x; cs[8] = style;
  cs[9] = title; cs[10] = cls; cs[11] = exstyle;
  send_message(0x81, 0, U(cs));   // WM_NCCREATE
  send_message(0x01, 0, U(cs));   // WM_CREATE
  return WIN_HWND;
}

nu DestroyWindow(nu hwnd) { API_TRACE(); (void)hwnd; return 1; }

nu ShowWindow(nu hwnd, nu cmd) {
  API_TRACE();
  (void)hwnd;
  if (cmd) {
    send_message(0x05, 0, xy(WIN_SCREEN_W, WIN_SCREEN_H));   // WM_SIZE
    send_message(0x06, 1, 0);                                 // WM_ACTIVATE
    g_invalid = 1;
  }
  return 1;
}

nu UpdateWindow(nu hwnd) { API_TRACE(); (void)hwnd; return 1; }
nu SetWindowPos(nu a, nu b, nu c, nu d, nu e, nu f, nu g) {
  API_TRACE();
  (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g;
  return 1;
}
nu SetForegroundWindow(nu h) { API_TRACE(); (void)h; return 1; }
nu GetForegroundWindow(void) { API_TRACE(); return WIN_HWND; }
nu SetFocus(nu h) { API_TRACE(); (void)h; return WIN_HWND; }

nu GetWindowRect(nu hwnd, nu r) {
  API_TRACE();
  (void)hwnd;
  I32(r, 0) = 0; I32(r, 4) = 0; I32(r, 8) = WIN_SCREEN_W; I32(r, 12) = WIN_SCREEN_H;
  return 1;
}
nu GetClientRect(nu hwnd, nu r) { API_TRACE(); return GetWindowRect(hwnd, r); }
nu ScreenToClient(nu hwnd, nu pt) { API_TRACE(); (void)hwnd; (void)pt; return 1; }
nu ClientToScreen(nu hwnd, nu pt) { API_TRACE(); (void)hwnd; (void)pt; return 1; }

nu InvalidateRect(nu hwnd, nu r, nu erase) {
  API_TRACE();
  (void)hwnd; (void)r; (void)erase;
  g_invalid = 1;
  return 1;
}

nu BeginPaint(nu hwnd, nu ps) {
  API_TRACE();
  (void)hwnd;
  memset(P(ps), 0, 64);
  U32(ps, 0) = gdi_window_dc();
  I32(ps, 16) = WIN_SCREEN_W;
  I32(ps, 20) = WIN_SCREEN_H;
  g_invalid = 0;
  return U32(ps, 0);
}
nu EndPaint(nu hwnd, nu ps) { API_TRACE(); (void)hwnd; (void)ps; return 1; }

nu GetDC(nu hwnd) { API_TRACE(); (void)hwnd; return gdi_window_dc(); }
nu ReleaseDC(nu hwnd, nu dc) { API_TRACE(); (void)hwnd; (void)dc; return 1; }

nu GetSystemMetrics(nu i) {
  API_TRACE();
  switch (i) {
    case 0: return WIN_SCREEN_W;
    case 1: return WIN_SCREEN_H;
    case 36: case 37: return 4;   // double-click rectangle
    default: return 0;
  }
}

nu ChangeDisplaySettingsA(nu dm, nu flags) { API_TRACE(); (void)dm; (void)flags; return 0; }

nu EnumDisplaySettingsA(nu dev, nu i, nu dm) {
  API_TRACE();
  (void)dev;
  if (i != 0 && i != 0xffffffffu && i != 0xfffffffeu) return 0;
  U32(dm, 104) = 16;
  U32(dm, 108) = WIN_SCREEN_W;
  U32(dm, 112) = WIN_SCREEN_H;
  U32(dm, 116) = 0;
  U32(dm, 120) = 60;
  return 1;
}

nu GetCursorPos(nu pt) {
  API_TRACE();
  I32(pt, 0) = g_mouse_x;
  I32(pt, 4) = g_mouse_y;
  return 1;
}
nu SetCursorPos(nu x, nu y) { API_TRACE(); (void)x; (void)y; return 1; }
nu ClipCursor(nu r) { API_TRACE(); (void)r; return 1; }
nu SetCapture(nu h) { API_TRACE(); (void)h; return 0; }
nu ReleaseCapture(void) { API_TRACE(); return 1; }

nu ShowCursor(nu show) {
  API_TRACE();
  g_cursor_count += show ? 1 : -1;
  SDL_ShowCursor(g_cursor_count >= 0 ? SDL_ENABLE : SDL_DISABLE);
  return (nu)g_cursor_count;
}

static nu g_cursor;
nu SetCursor(nu c) { API_TRACE(); nu old = g_cursor; g_cursor = c; return old; }
// Cursors and icons from LoadCursor/LoadIcon are shared resources in Win32:
// the same name gives the same handle and nothing frees it. golf calls
// LoadCursorA on every cursor update, so a fresh handle each time used up the
// handle table after a while in the course view.
static struct { nu inst, name, h; } g_shared_icons[64];
static int g_nshared_icons;
static nu shared_icon(nu inst, nu name) {
  typeof(g_shared_icons[0]) *cache = g_shared_icons;
  int n = g_nshared_icons;
  for (int i = 0; i < n; i++)
    if (cache[i].inst == inst && cache[i].name == name) return cache[i].h;
  nu h = obj_new(OBJ_ICON, NULL);
  if (n < 64) {
    cache[n].inst = inst;
    cache[n].name = name;
    cache[n].h = h;
    g_nshared_icons++;
  }
  return h;
}
nu LoadCursorA(nu inst, nu name) { API_TRACE(); return shared_icon(inst, name); }
nu LoadIconA(nu inst, nu name) { API_TRACE(); return shared_icon(inst, name); }
nu LoadImageA(nu inst, nu name, nu type, nu cx, nu cy, nu flags) {
  API_TRACE();
  (void)inst; (void)type; (void)cx; (void)cy; (void)flags;
  fprintf(stderr, "LoadImageA %s\n", (name >> 16) ? S(name) : "(resource)");
  return obj_new(OBJ_ICON, NULL);
}
nu DestroyCursor(nu c) {
  API_TRACE();
  for (int i = 0; i < g_nshared_icons; i++)
    if (g_shared_icons[i].h == c) return 1;  // shared: never freed
  obj_free(c);
  return 1;
}

nu GetKeyState(nu vk) { API_TRACE(); return (g_keys[vk & 0xff] & 0x80) ? 0xff80 : 0; }
nu GetAsyncKeyState(nu vk) { API_TRACE(); return (g_keys[vk & 0xff] & 0x80) ? 0x8000 : 0; }
nu GetKeyboardState(nu buf) { API_TRACE(); memcpy(P(buf), g_keys, 256); return 1; }
nu MapVirtualKeyA(nu code, nu type) { API_TRACE(); (void)type; return code; }
void keybd_event(nu vk, nu scan, nu flags, nu extra) { (void)vk; (void)scan; (void)flags; (void)extra; }
nu MessageBeep(nu t) { API_TRACE(); (void)t; return 1; }

nu MessageBoxA(nu hwnd, nu text, nu caption, nu type) {
  API_TRACE();
  (void)hwnd; (void)type;
  fprintf(stderr, "MessageBox [%s]: %s\n", caption ? S(caption) : "", text ? S(text) : "");
  return 1;   // IDOK
}

// ------------------------------------------------------------------ rects

nu IntersectRect(nu dst, nu a, nu b) {
  API_TRACE();
  int32_t l = I32(a, 0) > I32(b, 0) ? I32(a, 0) : I32(b, 0);
  int32_t t = I32(a, 4) > I32(b, 4) ? I32(a, 4) : I32(b, 4);
  int32_t r = I32(a, 8) < I32(b, 8) ? I32(a, 8) : I32(b, 8);
  int32_t bo = I32(a, 12) < I32(b, 12) ? I32(a, 12) : I32(b, 12);
  if (l >= r || t >= bo) {
    I32(dst, 0) = I32(dst, 4) = I32(dst, 8) = I32(dst, 12) = 0;
    return 0;
  }
  I32(dst, 0) = l; I32(dst, 4) = t; I32(dst, 8) = r; I32(dst, 12) = bo;
  return 1;
}

static int rect_empty(nu r) { return I32(r, 0) >= I32(r, 8) || I32(r, 4) >= I32(r, 12); }

nu UnionRect(nu dst, nu a, nu b) {
  API_TRACE();
  int32_t v[4];
  if (rect_empty(a)) memcpy(v, P(b), 16);
  else if (rect_empty(b)) memcpy(v, P(a), 16);
  else {
    v[0] = I32(a, 0) < I32(b, 0) ? I32(a, 0) : I32(b, 0);
    v[1] = I32(a, 4) < I32(b, 4) ? I32(a, 4) : I32(b, 4);
    v[2] = I32(a, 8) > I32(b, 8) ? I32(a, 8) : I32(b, 8);
    v[3] = I32(a, 12) > I32(b, 12) ? I32(a, 12) : I32(b, 12);
  }
  memcpy(P(dst), v, 16);
  return !rect_empty(dst);
}

nu EqualRect(nu a, nu b) { API_TRACE(); return memcmp(P(a), P(b), 16) == 0; }

// ------------------------------------------------------------------ kernel misc

nu GetLastError(void) { API_TRACE(); return g_last_error; }
void SetLastError(nu e) { API_TRACE(); g_last_error = e; }
void InitializeCriticalSection(nu cs) { API_TRACE(); memset(P(cs), 0, 24); }
void DeleteCriticalSection(nu cs) { API_TRACE(); (void)cs; }
void EnterCriticalSection(nu cs) { API_TRACE(); (void)cs; }
void LeaveCriticalSection(nu cs) { API_TRACE(); (void)cs; }
nu InterlockedIncrement(nu p) { API_TRACE(); return ++*(int32_t *)P(p); }
nu InterlockedDecrement(nu p) { API_TRACE(); return --*(int32_t *)P(p); }
void OutputDebugStringA(nu s) { API_TRACE(); fprintf(stderr, "debug: %s", S(s)); }
nu GetCurrentThreadId(void) { API_TRACE(); return 1; }
nu GetCurrentProcess(void) { API_TRACE(); return 0xffffffffu; }
nu CreateEventA(nu a, nu b, nu c, nu d) { API_TRACE(); (void)a; (void)b; (void)c; (void)d; return obj_new(OBJ_EVENT, NULL); }
nu SetEvent(nu e) { API_TRACE(); (void)e; return 1; }
nu ResetEvent(nu e) { API_TRACE(); (void)e; return 1; }
nu WaitForMultipleObjects(nu n, nu h, nu all, nu ms) { API_TRACE(); (void)n; (void)h; (void)all; (void)ms; return 0; }
nu WaitForSingleObject(nu h, nu ms) { API_TRACE(); (void)h; (void)ms; return 0; }

nu FormatMessageA(nu flags, nu src, nu id, nu lang, nu buf, nu size, nu args) {
  API_TRACE();
  (void)flags; (void)src; (void)lang; (void)args;
  return snprintf(S(buf), size, "error %u", id);
}

// Windows 2000 (5.0, build 2195), the era's NT.
nu GetVersion(void) { API_TRACE(); return 0x08930005; }

nu GetVersionExA(nu info) {
  API_TRACE();
  U32(info, 4) = 5;       // dwMajorVersion
  U32(info, 8) = 0;       // dwMinorVersion
  U32(info, 12) = 2195;   // dwBuildNumber
  U32(info, 16) = 2;      // VER_PLATFORM_WIN32_NT
  S(info)[20] = 0;
  return 1;
}

nu GetModuleHandleA(nu name) {
  API_TRACE();
  if (!name) return WIN_HINSTANCE;
  if (strcasestr(S(name), "jgl")) return WIN_JGLD_BASE;
  return 0;
}

nu GetModuleFileNameA(nu mod, nu buf, nu size) {
  API_TRACE();
  const char *p = mod == WIN_JGLD_BASE ? "C:\\SimGolf\\jgld.dll" : "C:\\SimGolf\\golf.exe";
  snprintf(S(buf), size, "%s", p);
  return (nu)strlen(S(buf));
}

nu GetCurrentDirectoryA(nu size, nu buf) {
  API_TRACE();
  snprintf(S(buf), size, "C:\\SimGolf");
  return (nu)strlen(S(buf));
}

nu SetCurrentDirectoryA(nu path) { API_TRACE(); (void)path; return 1; }

// Modules. jgld.dll is translated and lives at its image base; sound.dll is
// replaced by native code at the game's helper level, so loading it fails and
// golf runs its no-sound path (see docs/porting.md).
extern void jgld_attach(void);

nu LoadLibraryA(nu name) {
  API_TRACE();
  const char *n = S(name);
  fprintf(stderr, "LoadLibraryA %s\n", n);
  if (strcasestr(n, "jgl")) {
    jgld_attach();
    return WIN_JGLD_BASE;
  }
  return 0;
}

nu FreeLibrary(nu h) { API_TRACE(); (void)h; return 1; }

nu GetProcAddress(nu mod, nu name) {
  API_TRACE();
  if (mod == WIN_JGLD_BASE && name > 0xffff && !strcmp(S(name), "get_graphsy_object_ptr"))
    return 0x10001780u;
  fprintf(stderr, "GetProcAddress %08x %s: not found\n", mod, name > 0xffff ? S(name) : "(ordinal)");
  return 0;
}

// ------------------------------------------------------------------ files

typedef struct { int fd; int write; char path[256]; } FileObj;

nu CreateFileA(nu name, nu access, nu share, nu sec, nu disp, nu flags, nu tmpl) {
  API_TRACE();
  (void)share; (void)sec; (void)flags; (void)tmpl;
  int write = (access & 0x40000000u) != 0;
  char real[512];
  if (!fs_resolve(S(name), real, sizeof real, write || disp == 1 || disp == 2 || disp == 4)) {
    g_last_error = 2;
    return 0xffffffffu;
  }
  int f = write ? ((access & 0x80000000u) ? O_RDWR : O_WRONLY) : O_RDONLY;
  switch (disp) {
    case 1: f |= O_CREAT | O_EXCL; break;    // CREATE_NEW
    case 2: f |= O_CREAT | O_TRUNC; break;   // CREATE_ALWAYS
    case 4: f |= O_CREAT; break;             // OPEN_ALWAYS
    case 5: f |= O_TRUNC; break;             // TRUNCATE_EXISTING
  }
  int fd = open(real, f, 0666);
  if (fd < 0) {
    g_last_error = 2;
    return 0xffffffffu;
  }
  FileObj *o = calloc(1, sizeof *o);
  o->fd = fd;
  o->write = write;
  snprintf(o->path, sizeof o->path, "%s", real);
  return obj_new(OBJ_FILE, o);
}

nu ReadFile(nu h, nu buf, nu n, nu nread, nu ov) {
  API_TRACE();
  (void)ov;
  FileObj *o = obj_get(h, OBJ_FILE);
  if (!o) return 0;
  ssize_t r = read(o->fd, P(buf), n);
  if (nread) U32(nread, 0) = r < 0 ? 0 : (nu)r;
  return r >= 0;
}

nu WriteFile(nu h, nu buf, nu n, nu nwritten, nu ov) {
  API_TRACE();
  (void)ov;
  FileObj *o = obj_get(h, OBJ_FILE);
  if (!o) {
    // stdout/stderr handles from GetStdHandle
    if (h == 0xfff4 || h == 0xfff5) {
      fwrite(P(buf), 1, n, stderr);
      if (nwritten) U32(nwritten, 0) = n;
      return 1;
    }
    return 0;
  }
  ssize_t r = write(o->fd, P(buf), n);
  if (nwritten) U32(nwritten, 0) = r < 0 ? 0 : (nu)r;
  return r >= 0;
}

nu GetStdHandle(nu which) { API_TRACE(); return which == 0xfffffff5u ? 0xfff5 : 0xfff4; }

nu SetFilePointer(nu h, nu dist, nu high, nu method) {
  API_TRACE();
  FileObj *o = obj_get(h, OBJ_FILE);
  if (!o) return 0xffffffffu;
  if (high) U32(high, 0) = 0;
  off_t r = lseek(o->fd, (int32_t)dist, (int)method);
  return r < 0 ? 0xffffffffu : (nu)r;
}

nu GetFileSize(nu h, nu high) {
  API_TRACE();
  FileObj *o = obj_get(h, OBJ_FILE);
  if (high) U32(high, 0) = 0;
  if (!o) return 0xffffffffu;
  struct stat st;
  fstat(o->fd, &st);
  return (nu)st.st_size;
}

nu SetEndOfFile(nu h) {
  API_TRACE();
  FileObj *o = obj_get(h, OBJ_FILE);
  if (!o) return 0;
  off_t pos = lseek(o->fd, 0, SEEK_CUR);
  return ftruncate(o->fd, pos) == 0;
}

nu FlushFileBuffers(nu h) { API_TRACE(); (void)h; return 1; }
nu GetFileType(nu h) { API_TRACE(); (void)h; return 1; }

nu DeleteFileA(nu name) {
  API_TRACE();
  char real[512];
  if (!fs_resolve(S(name), real, sizeof real, 0)) return 0;
  if (unlink(real) != 0) return 0;
  fs_deleted(real);
  return 1;
}

nu CreateDirectoryA(nu name, nu sec) {
  API_TRACE();
  (void)sec;
  char real[512];
  if (!fs_resolve(S(name), real, sizeof real, 1)) return 0;
  return mkdir(real, 0777) == 0;
}

typedef struct { void *data; nu size; } Mapping;

nu CreateFileMappingA(nu h, nu sec, nu prot, nu hi, nu lo, nu name) {
  API_TRACE();
  (void)sec; (void)prot; (void)hi; (void)name;
  FileObj *o = obj_get(h, OBJ_FILE);
  if (!o) return 0;
  struct stat st;
  fstat(o->fd, &st);
  Mapping *m = calloc(1, sizeof *m);
  m->size = lo ? lo : (nu)st.st_size;
  m->data = calloc(1, m->size + 1);
  pread(o->fd, m->data, st.st_size < m->size ? st.st_size : m->size, 0);
  return obj_new(OBJ_MAPPING, m);
}

nu MapViewOfFile(nu h, nu access, nu offhi, nu offlo, nu n) {
  API_TRACE();
  (void)access; (void)offhi; (void)n;
  Mapping *m = obj_get(h, OBJ_MAPPING);
  return m ? U((char *)m->data + offlo) : 0;
}

nu UnmapViewOfFile(nu p) { API_TRACE(); (void)p; return 1; }

nu CloseHandle(nu h) {
  API_TRACE();
  FileObj *f = obj_get(h, OBJ_FILE);
  if (f) {
    close(f->fd);
    if (f->write) fs_written(f->path);
    free(f);
  }
  Mapping *m = obj_get(h, OBJ_MAPPING);
  if (m) {
    free(m->data);
    free(m);
  }
  obj_free(h);
  return 1;
}

// FindFirstFileA: the whole listing is gathered up front.
typedef struct { int n, pos; struct { char name[260]; int dir; unsigned size; } e[512]; } FindObj;

static void find_add(void *ctx, const char *name, int is_dir, unsigned size) {
  FindObj *f = ctx;
  if (f->n >= 512) return;
  snprintf(f->e[f->n].name, 260, "%s", name);
  f->e[f->n].dir = is_dir;
  f->e[f->n].size = size;
  f->n++;
}

static void find_fill(FindObj *f, nu data) {
  memset(P(data), 0, 320);
  U32(data, 0) = f->e[f->pos].dir ? 0x10 : 0x20;
  U32(data, 32) = f->e[f->pos].size;
  snprintf(S(data) + 44, 260, "%s", f->e[f->pos].name);
  f->pos++;
}

nu FindFirstFileA(nu pattern, nu data) {
  API_TRACE();
  FindObj *f = calloc(1, sizeof *f);
  fs_list(S(pattern), find_add, f);
  if (!f->n) {
    free(f);
    g_last_error = 2;
    return 0xffffffffu;
  }
  find_fill(f, data);
  return obj_new(OBJ_FIND, f);
}

nu FindNextFileA(nu h, nu data) {
  API_TRACE();
  FindObj *f = obj_get(h, OBJ_FIND);
  if (!f || f->pos >= f->n) {
    g_last_error = 18;   // ERROR_NO_MORE_FILES
    return 0;
  }
  find_fill(f, data);
  return 1;
}

nu FindClose(nu h) {
  API_TRACE();
  free(obj_get(h, OBJ_FIND));
  obj_free(h);
  return 1;
}

// ------------------------------------------------------------------ WINMM mmio (only via sound.dll paths)

nu mmioDescend(nu a, nu b, nu c, nu d) { (void)a; (void)b; (void)c; (void)d; return 1; }
nu mmioAscend(nu a, nu b, nu c) { (void)a; (void)b; (void)c; return 1; }
nu mmioRead(nu a, nu b, nu c) { (void)a; (void)b; (void)c; return (nu)-1; }

// ------------------------------------------------------------------ init

void win32_init(void) {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_StopTextInput();
  SDL_StartTextInput();
  gdi_init();
  g_last_yield = emscripten_get_now();
}
