// Minimal PNG writer for test output (stored deflate blocks, no zlib).

#ifndef SIMGOLF_TESTS_TERRAIN_PNG_H
#define SIMGOLF_TESTS_TERRAIN_PNG_H

#include <stdint.h>

// Writes an RGB555 image as a 24-bit PNG. Returns false on I/O error.
bool write_png_rgb555(const char *path, const uint16_t *pixels, int width, int height, int pitch);

#endif  // SIMGOLF_TESTS_TERRAIN_PNG_H
