// golf.exe's sound helpers, replaced by the Web Audio shim (platform/sound.h).
// The game reaches sound.dll only through these index-based functions and the
// 300 wrapper objects at 0x0080d840 (stride 0x6c); see docs/sound.md. Since
// LoadLibraryA(".\\sound.dll") fails in the port, the wrappers never get DLL
// objects, and these overrides do the work instead.

#include "golf_protos.h"
#include "platform/sound.h"

#define SOUND_ARRAY 0x0080d840u
#define SOUND_STRIDE 0x6cu

// @override FUN_00483e90
// init(hwnd, flags 0x8a): load sound.dll, create the devices.
int FUN_00483e90(undefined4 param_1, uint param_2) {
  (void)param_1; (void)param_2;
  snd_init();
  return 0;
}

// @override FUN_00484e30
// Wrapper method (slot +0x90): set the file and flags, load unless lazy.
undefined4 FUN_00484e30(void *this, undefined4 param_1, uint param_2) {
  uint32_t a = (uint32_t)(uintptr_t)this;
  if (a >= SOUND_ARRAY && a < SOUND_ARRAY + SND_MAX_SOUNDS * SOUND_STRIDE &&
      (a - SOUND_ARRAY) % SOUND_STRIDE == 0)
    snd_register((int)((a - SOUND_ARRAY) / SOUND_STRIDE), (const char *)(uintptr_t)param_1, param_2);
  return 0;
}

// @override FUN_004481b0
// play(index, volume 0..127, pan -64..63, pitch in cents, delay in ms)
undefined4 FUN_004481b0(int param_1, uint param_2, int param_3, int param_4, undefined4 param_5) {
  snd_play(param_1, (int)param_2, param_3, param_4, (int)param_5);
  return 0;
}

// @override FUN_00448200
// fade out over 1000 ms
undefined4 FUN_00448200(int param_1) {
  snd_fade_out(param_1, 1000);
  return 0;
}

// @override FUN_004490b0
// release every sound, then the library
undefined4 FUN_004490b0(void) {
  snd_shutdown();
  return 0;
}
