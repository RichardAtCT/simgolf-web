// Port-side glue for the Terrain renderer (terrain_dll.cpp), the parts of the
// interface that have no counterpart in Terrain.dll itself.
//
// golf.exe hands Terrain::initSystem the HDC of graphsy's back-buffer DIB and
// the DLL renders into it with generic GL. The port passes a TerrainSurface*
// in that HDC slot instead: the same 16-bit RGB555 buffer the rest of the game
// draws into (see framebuffer.h). Files are read relative to the current
// directory, as the DLL does (./Data/Textures/..., <Course>Lighting.txt), with
// a case-insensitive fallback because the game's names don't match the case of
// the files on disc (e.g. "Woods" vs woodsA0001.bmp).

#ifndef SIMGOLF_PLATFORM_TERRAIN_PORT_H
#define SIMGOLF_PLATFORM_TERRAIN_PORT_H

#include <stdint.h>
#include <stdio.h>

struct TerrainSurface {
  uint16_t *pixels;  // RGB555
  int pitch;         // in pixels
  int width, height;
};

// Retargets rendering after initSystem (e.g. when the back buffer moves).
void terrain_set_surface(const TerrainSurface *surface);

// Reseeds the DLL's private rand() (MSVC's LCG, seed 1 at load). Terrain.dll
// links its own CRT and never calls srand, so texture variants depend only on
// the order of setType calls since the game started.
void terrain_srand(unsigned seed);

// The software GL context Terrain draws through (terrain_hd.cpp records from it).
namespace tgl { class Context; }
tgl::Context &terrain_gl();

// fopen with a case-insensitive fallback for each path component.
FILE *terrain_fopen(const char *path, const char *mode);

#endif  // SIMGOLF_PLATFORM_TERRAIN_PORT_H
