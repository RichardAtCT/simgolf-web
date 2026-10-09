// Runtime for translated code: loads module images at their original
// addresses and maps original code addresses to callable wrappers.

#include "port/runtime.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *ExceptionList;
uint32_t port_trace_buf[1024];
uint32_t port_trace_pos;

void port_debug4(const char *what, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  fprintf(stderr, "debug %s: %08x %08x %08x %08x\n", what, a, b, c, d);
}

void port_ret_log(uint32_t fn, int where, uint32_t value) {
  fprintf(stderr, "return %06x @%d: %u (0x%x)\n", fn, where, value, value);
}

void port_trace_dump(int n) {
  if (n > 1024) n = 1024;
  fprintf(stderr, "last %d functions entered (oldest first):", n);
  for (int i = n; i > 0; i--) {
    if (i % 8 == 0) fprintf(stderr, "\n ");
    fprintf(stderr, " %06x", port_trace_buf[(port_trace_pos - i) & 1023]);
  }
  fprintf(stderr, "\n");
}
double g_fret;
uint32_t g_edx;

typedef struct { uint32_t addr; IcallFn fn; uint32_t flags; } Entry;

static Entry *g_table;
static unsigned g_count, g_cap;
static int g_sorted;

static int cmp_entry(const void *a, const void *b) {
  uint32_t x = ((const Entry *)a)->addr, y = ((const Entry *)b)->addr;
  return x < y ? -1 : x > y;
}

void icall_register(uint32_t addr, IcallFn fn) { icall_register_flags(addr, fn, 0); }

void icall_register_flags(uint32_t addr, IcallFn fn, uint32_t flags) {
  if (g_count == g_cap) {
    g_cap = g_cap ? g_cap * 2 : 4096;
    g_table = realloc(g_table, g_cap * sizeof(Entry));
  }
  g_table[g_count].addr = addr;
  g_table[g_count].fn = fn;
  g_table[g_count].flags = flags;
  g_count++;
  g_sorted = 0;
}

void icall_register_table(const IcallEntry *t, unsigned n) {
  for (unsigned i = 0; i < n; i++) icall_register_flags(t[i].addr, t[i].fn, t[i].flags);
}

static uint32_t g_next_fake = PORT_FAKE_CODE_BASE;

uint32_t icall_register_native(IcallFn fn) {
  uint32_t a = g_next_fake;
  g_next_fake += 16;
  icall_register(a, fn);
  return a;
}

static uint32_t unresolved(ICALL_PARAMS) {
  (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
  (void)a8; (void)a9; (void)a10; (void)a11; (void)a12; (void)a13; (void)a14; (void)a15;
  return 0;
}

static const Entry *find(uint32_t addr) {
  if (!g_sorted) {
    qsort(g_table, g_count, sizeof(Entry), cmp_entry);
    g_sorted = 1;
  }
  unsigned lo = 0, hi = g_count;
  while (lo < hi) {
    unsigned mid = (lo + hi) / 2;
    if (g_table[mid].addr < addr) lo = mid + 1;
    else hi = mid;
  }
  return lo < g_count && g_table[lo].addr == addr ? &g_table[lo] : NULL;
}

// A virtual call whose `this` Ghidra dropped: pass it only to __thiscall targets.
uint32_t icall_this(uint32_t fn, uint32_t obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                    uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9,
                    uint32_t a10, uint32_t a11, uint32_t a12, uint32_t a13, uint32_t a14) {
  const Entry *e = find(fn);
  if (!e) {
    fprintf(stderr, "icall: no function at 0x%08x (this=0x%08x)\n", fn, obj);
    port_abort("virtual call to unknown address");
  }
  if (e->flags & 1)
    return e->fn(obj, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
  return e->fn(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, 0);
}

IcallFn icall_lookup(uint32_t addr) {
  if (!g_sorted) {
    qsort(g_table, g_count, sizeof(Entry), cmp_entry);
    g_sorted = 1;
  }
  unsigned lo = 0, hi = g_count;
  while (lo < hi) {
    unsigned mid = (lo + hi) / 2;
    if (g_table[mid].addr < addr) lo = mid + 1;
    else hi = mid;
  }
  if (lo < g_count && g_table[lo].addr == addr) return g_table[lo].fn;
  fprintf(stderr, "icall: no function at 0x%08x\n", addr);
  port_backtrace();
  port_abort("indirect call to unknown address");
  return unresolved;
}

void port_backtrace(void) {
  EM_ASM({ console.log(new Error().stack); });
}

void port_halt(const char *fn, const char *why) {
  fprintf(stderr, "halt in %s: %s\n", fn, why);
  port_abort("halt");
}

void port_abort(const char *why) {
  fprintf(stderr, "port_abort: %s\n", why);
  port_trace_dump(64);
  port_backtrace();
  abort();
}

int port_load_image(const char *path, uint32_t base, uint32_t limit) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "image %s: not found\n", path);
    return 0;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0 || base + (uint32_t)n > limit) {
    fprintf(stderr, "image %s: bad size %ld\n", path, n);
    fclose(f);
    return 0;
  }
  size_t got = fread((void *)(uintptr_t)base, 1, (size_t)n, f);
  fclose(f);
  return got == (size_t)n;
}

void port_unimplemented(const char *name) {
  fprintf(stderr, "unimplemented: %s\n", name);
}
