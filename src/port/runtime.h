// Runtime support shared by translated code and the platform layer.
//
// Memory layout (wasm32 linear memory):
//   0x00400000-0x00845000  golf.exe image at its original addresses
//   0x10000000-0x1013a000  jgld.dll (graphsy) at its image base
//   0x11000000             Emscripten static data (GLOBAL_BASE = 272 MB),
//                          then the stack and the malloc heap. Untouched
//                          pages below it cost address space, not memory.
//   0xF0000000+            fake code addresses for native functions that
//                          translated code reaches through icall

#ifndef SIMGOLF_PORT_RUNTIME_H
#define SIMGOLF_PORT_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_GOLF_BASE 0x00400000u
#define PORT_GOLF_LIMIT 0x00900000u
#define PORT_FAKE_CODE_BASE 0xF0000000u

#ifndef ICALL_PARAMS
#define ICALL_PARAMS uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12, uint32_t a13, uint32_t a14, uint32_t a15
typedef uint32_t (*IcallFn)(ICALL_PARAMS);
typedef struct { uint32_t addr; IcallFn fn; uint32_t flags; } IcallEntry;
IcallFn icall_lookup(uint32_t addr);
extern double g_fret;
extern uint32_t g_edx;
#endif

void icall_register(uint32_t addr, IcallFn fn);
void icall_register_flags(uint32_t addr, IcallFn fn, uint32_t flags);
void icall_register_table(const IcallEntry *t, unsigned n);
// Gives a native function a fake code address that translated code can store
// and call through icall.
uint32_t icall_register_native(IcallFn fn);

int port_load_image(const char *path, uint32_t base, uint32_t limit);
void port_backtrace(void);
void port_abort(const char *why) __attribute__((noreturn));
void port_halt(const char *fn, const char *why);
void port_unimplemented(const char *name);
void port_trace_dump(int n);

#ifdef __cplusplus
}
#endif
#endif  // SIMGOLF_PORT_RUNTIME_H
