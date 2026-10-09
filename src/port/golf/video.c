// The intro and closing videos. golf.exe plays them through binkw32.dll
// (FUN_00487090 and friends); the port plays a WebM converted from the .bik
// (tools/video/convert-bik.sh) instead, see src/port/video.c.

#include "golf_protos.h"

void port_play_game_video(const char *dospath, int background);

// @override FUN_0043cce0
// Plays a video by its game path (".\\flics\\SMSG_introfinal.bik"); param_2
// is 0 for the intro, 1 for the closing video. Returns 0 like the original
// after a normal play. The intro plays while the game loads behind it.
undefined4 FUN_0043cce0(LPCSTR param_1, int param_2) {
  port_play_game_video((const char *)param_1, param_2 == 0);
  return 0;
}
