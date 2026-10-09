// golf.exe's static imports from binkw32.dll and Terrain.dll.
//
// Bink: the intro/outro videos. BinkOpen fails, so the game skips them; the
// port can play the pre-converted WebM in a <video> element later.
//
// Terrain.dll's 26 imports are in src/platform/terrain_bridge.cpp.

#include <stdint.h>
#include <stdio.h>

typedef uint32_t nu;

// ------------------------------------------------------------------ Bink

nu _BinkOpen_8(nu a0, ...) { (void)a0; return 0; }
nu _BinkClose_4(nu a0, ...) { (void)a0; return 0; }
nu _BinkDoFrame_4(nu a0, ...) { (void)a0; return 0; }
nu _BinkNextFrame_4(nu a0, ...) { (void)a0; return 0; }
nu _BinkWait_4(nu a0, ...) { (void)a0; return 0; }
nu _BinkCopyToBuffer_28(nu a0, ...) { (void)a0; return 0; }
nu _BinkGetRects_8(nu a0, ...) { (void)a0; return 0; }
nu _BinkBufferOpen_16(nu a0, ...) { (void)a0; return 0; }
nu _BinkBufferClose_4(nu a0, ...) { (void)a0; return 0; }
nu _BinkBufferLock_4(nu a0, ...) { (void)a0; return 0; }
nu _BinkBufferUnlock_4(nu a0, ...) { (void)a0; return 0; }
nu _BinkBufferBlit_12(nu a0, ...) { (void)a0; return 0; }
nu _BinkBufferSetOffset_12(nu a0, ...) { (void)a0; return 0; }
nu _BinkSetSoundSystem_8(nu a0, ...) { (void)a0; return 0; }
nu _BinkSetIOSize_4(nu a0, ...) { (void)a0; return 0; }
nu _BinkOpenDirectSound_4(nu a0, ...) { (void)a0; return 0; }
