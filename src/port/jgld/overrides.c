// Hand-written replacements for jgld.dll functions (see src/port/golf/overrides.c).

// No libc headers here: they clash with Ghidra's types (wchar_t, size_t).
#include "jgld_protos.h"

// jgld's growable array: +4 data, +8 capacity, +0xc count, +0x10 growth step.
// push_back(T) takes T by value on the stack, which the decompile can't
// express (the callee walks ESP), so the callers are patched to pass a
// pointer to the copy they built (tools/translate/jgld.json).
static int array_push(void *self, const void *value, int size) {
  char *a = self;
  int *data = (int *)(a + 4), *cap = (int *)(a + 8), *count = (int *)(a + 0xc), *step = (int *)(a + 0x10);
  if (*count >= *cap) {
    int ncap = *cap + *step;
    char *buf = (char *)(uintptr_t)crt_malloc((nu)(ncap * size));
    if (*data) __builtin_memcpy(buf, (void *)(uintptr_t)*data, (unsigned)(*count * size));
    crt_free((nu)*data);
    *data = (int)(uintptr_t)buf;
    *cap = ncap;
  }
  __builtin_memcpy((char *)(uintptr_t)*data + *count * size, value, (unsigned)size);
  return (*count)++;
}

// @override FUN_100681a0
int FUN_100681a0(void *this, void *value) { return array_push(this, value, 0x14); }

// @override FUN_10068400
int FUN_10068400(void *this, void *value) { return array_push(this, value, 0x94); }
