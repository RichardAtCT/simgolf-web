// Test-only course data for the terrain renderer: either read from a .sve
// save (docs/formats/sve.md) or generated, then pushed into Terrain the way
// golf.exe's full sync does it (FUN_00449540).

#ifndef SIMGOLF_TESTS_TERRAIN_COURSE_H
#define SIMGOLF_TESTS_TERRAIN_COURSE_H

#include <stdint.h>

class Terrain;

// Grids use golf's order: index x*50 + y (Terrain's tileAt is the transpose).
struct Course {
  int courseType = 0;        // 0 Parkland, 1 Desert, 2 Tropical, 3 Links
  uint8_t type[50 * 50];     // terrain type, 20 = off-course
  uint8_t variation[50 * 50];
  uint16_t flags[50 * 50];   // 0x20 path, 0x40 the "X" path set
  uint8_t height[51 * 51];   // corner heights, 3 = ground level
};

bool course_load_sve(const char *path, Course &c);
// A 30x30 course with every feature the renderer draws: hills with cliffs,
// tee, fairway, green, bunkers, pot bunker, mound, water of three depths,
// woods, a path, and void edges with strata skirts.
void course_sample(Course &c);

// golf.exe's first-frame order: initSystem has run; this passes the blend
// groups, picks the course textures, builds the grid and syncs the map.
void course_push(Terrain *t, const Course &c);

#endif  // SIMGOLF_TESTS_TERRAIN_COURSE_H
