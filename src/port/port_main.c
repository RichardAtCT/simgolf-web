// Entry point for the translated game: replaces golf.exe's CRT startup
// (entry @ 0x004a682f). Loads the image, registers code addresses, runs the
// C++ static initializers and calls WinMain.

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/fs.h"
#include "port/runtime.h"
#include "port/win32.h"

extern const IcallEntry golf_icall_table[];
extern const unsigned golf_icall_count;
extern void golf_register_overrides(void);

// golf.exe v1.02 addresses
#define GOLF_WINMAIN 0x0045baf0u
#define GOLF_CXX_INIT_BEGIN 0x004c1000u
#define GOLF_CXX_INIT_END 0x004c1188u
#define GOLF_CODE_END 0x004a4f10u    // CRT starts after this

// _initterm over [begin, end), skipping the CRT's own entries (>= code_end).
static void run_initializers(const char *what, uint32_t begin, uint32_t end, uint32_t code_end) {
  int ran = 0, skipped = 0;
  for (uint32_t p = begin; p < end; p += 4) {
    uint32_t fn = *(uint32_t *)(uintptr_t)p;
    if (!fn) continue;
    if (fn >= code_end) {
      skipped++;
      continue;
    }
    icall_lookup(fn)(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    ran++;
  }
  fprintf(stderr, "%s static initializers: %d run, %d CRT skipped\n", what, ran, skipped);
}

// jgld.dll (v1.02 debug build) at its image base 0x10000000
#define JGLD_LIMIT 0x10200000u
#define JGLD_CODE_END 0x1007e780u
#define JGLD_DLLMAIN 0x10065300u
extern const IcallEntry jgld_icall_table[];
extern const unsigned jgld_icall_count;

// Called from LoadLibraryA("jgld.dll"): the DLL's CRT startup, minus the CRT.
void jgld_attach(void) {
  static int done;
  if (done++) return;
  if (!port_load_image("/image/jgld.bin", 0x10000000u, JGLD_LIMIT)) port_abort("jgld image missing");
  icall_register_table(jgld_icall_table, jgld_icall_count);
  run_initializers("jgld C", 0x10122320u, 0x10122638u, JGLD_CODE_END);
  run_initializers("jgld C++", 0x10122000u, 0x1012221cu, JGLD_CODE_END);
  icall_lookup(JGLD_DLLMAIN)(0x10000000u, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

int main(void) {
  fs_init();
  if (!port_load_image("/image/golf.bin", PORT_GOLF_BASE, PORT_GOLF_LIMIT)) {
    fprintf(stderr, "golf image missing\n");
    return 1;
  }
  icall_register_table(golf_icall_table, golf_icall_count);
  golf_register_overrides();
  win32_init();
  run_initializers("golf C++", GOLF_CXX_INIT_BEGIN, GOLF_CXX_INIT_END, GOLF_CODE_END);
  uint32_t r = icall_lookup(GOLF_WINMAIN)(PORT_GOLF_BASE, 0, (uint32_t)(uintptr_t)"", 10,
                                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  fprintf(stderr, "WinMain returned %u\n", r);
  port_trace_dump(200);
  return 0;
}
