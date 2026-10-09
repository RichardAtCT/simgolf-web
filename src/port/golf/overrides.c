// Hand-written replacements for golf.exe functions, marked with
// `// @override <name>` so tools/translate skips the generated version.
// Signatures must match build/gen/golf/golf_protos.h.

#include "golf_protos.h"
#include "port/runtime.h"

// @override FUN_004ae020
// Writes a JPEG snapshot with the statically linked IJG libjpeg 6b
// (jpeg_CreateCompress etc. at 0x4ae020-0x4b6400). Not ported yet: report
// failure, which the snapshot code handles.
undefined4 FUN_004ae020(int param_1, int param_2, int param_3, code *param_4) {
  (void)param_1; (void)param_2; (void)param_3; (void)param_4;
  return 1;
}

// 0x004321a0 copies a string into the global at 0x587da0 (strcpy inlined as
// repne scasb / rep movsd). Ghidra merged it into FUN_00432170 because of
// junk bytes before it, so callers see an undefined func_0x004321a0; the
// return type is int because those call sites have no prototype.
int func_0x004321a0(char *s) {
  char *d = (char *)0x587da0;
  while ((*d++ = *s++) != 0) {}
  return 0;
}

// 0x00474a80 is a C++ static initializer (in the CRT's list at 0x4c1000)
// that Ghidra never made a function: it calls 0x474a90 and tail-jumps to
// 0x474aa0. Registered by golf_register_overrides().
static uint32_t init_00474a80(ICALL_PARAMS) {
  FUN_00474a90();
  FUN_00474aa0();
  return 0;
}

void golf_register_overrides(void) {
  icall_register(0x00474a80, init_00474a80);
}
