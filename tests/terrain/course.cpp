// Test-only course loading and golf's map sync. See course.h.
//
// The sync follows golf.exe's FUN_00449540 and the helpers it calls, with two
// simplifications because the data they need isn't in a save: per-tile
// height overrides (0x51b770) and the terrain-type table (0x578370) are
// treated as empty, so water-level heights aren't special-cased, every blend
// group is the type itself, and every wall is drawn as a cliff.

#include "course.h"

#include <stdio.h>
#include <string.h>
#include <vector>

#include "platform/terrain.h"

namespace {

bool offCourse(const Course &c, int x, int y) {  // FUN_0040bf60
  return x < 0 || x >= 50 || y < 0 || y >= 50 || c.type[x * 50 + y] == 20;
}

int gridHeight(const Course &c, int a, int b) {  // FUN_0040c170, stored-grid case
  if (a < 0 || a >= 50 || b < 0 || b >= 50) return 3;
  if (offCourse(c, a, b) && offCourse(c, a, b + 1)) return 3;
  return c.height[a * 51 + b];
}

int cornerHeight(const Course &c, int x, int y, int corner) {  // FUN_0040bfe0
  if (x < 0 || x >= 50 || y < 0 || y >= 50 || !(corner & 1)) return 3;
  switch (corner & 7) {
    case 1: return gridHeight(c, x + 1, y - 1);
    case 3: return gridHeight(c, x + 1, y);
    case 5: return gridHeight(c, x, y);
    default: return gridHeight(c, x, y - 1);
  }
}

// FUN_0042f530: a wall on a tile's edge where the neighbour is higher at
// either shared corner. Directions 0,2,4,6 = y-1, x+1, y+1, x-1.
bool wallOn(const Course &c, int x, int y, int dir) {
  static const int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1}, kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
  int nx = x + kDx[dir], ny = y + kDy[dir];
  if (offCourse(c, nx, ny)) return false;
  int k = dir - 1;  // own corners k, k+2; neighbour corners k+6, k+4 (mod 8)
  auto h = [&](int tx, int ty, int corner) { return cornerHeight(c, tx, ty, (corner + 8) & 7); };
  return h(x, y, k) < h(nx, ny, k + 6) || h(x, y, k + 2) < h(nx, ny, k + 4);
}

}  // namespace

bool course_load_sve(const char *path, Course &c) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  std::vector<uint8_t> d(0x7000);
  size_t n = fread(d.data(), 1, d.size(), f);
  fclose(f);
  if (n < 0x5A15 + 51 * 51) return false;
  c.courseType = d[0x74] & 3;
  memcpy(c.type, &d[0x2941], 2500);
  memcpy(c.variation, &d[0x3305], 2500);
  for (int i = 0; i < 2500; i++) c.flags[i] = (uint16_t)(d[0x3CC9 + i * 2] | d[0x3CC9 + i * 2 + 1] << 8);
  memcpy(c.height, &d[0x5A15], 51 * 51);
  return true;
}

void course_sample(Course &c) {
  c.courseType = 0;
  memset(c.variation, 0, sizeof c.variation);
  memset(c.flags, 0, sizeof c.flags);
  memset(c.height, 3, sizeof c.height);
  auto set = [&](int x, int y, int type, int var = 0) {
    c.type[x * 50 + y] = (uint8_t)type;
    c.variation[x * 50 + y] = (uint8_t)var;
  };
  for (int x = 0; x < 50; x++)
    for (int y = 0; y < 50; y++) set(x, y, x >= 10 && x < 40 && y >= 10 && y < 40 ? 4 : 20);
  // A raised plateau with cliffs on two sides and a slope on the others.
  for (int a = 22; a <= 33; a++)
    for (int b = 12; b <= 22; b++) {
      int h = 6;
      if (a == 22 || b == 22) h = 5;
      c.height[a * 51 + b] = (uint8_t)h;
    }
  for (int a = 26; a <= 30; a++)
    for (int b = 15; b <= 19; b++) c.height[a * 51 + b] = 7;
  // Tee, fairway, green on the plateau and down the slope.
  for (int x = 27; x <= 28; x++)
    for (int y = 14; y <= 15; y++) set(x, y, 0, (x - 27) * 2 + (y - 14));
  for (int y = 16; y <= 30; y++)
    for (int x = 25 + (y > 24); x <= 30 - (y > 27); x++) set(x, y, y < 20 || y > 26 ? 2 : 3);
  for (int x = 24; x <= 29; x++)
    for (int y = 31; y <= 34; y++) set(x, y, 1, x == 24 ? 0x80 : 0);
  // Bunkers by the green (each orientation), a pot bunker and grassy sand.
  for (int i = 0; i < 4; i++) set(30 + (i & 1), 31 + (i >> 1), 7, i);
  set(23, 30, 7, 1);
  set(23, 33, 9);
  set(30, 34, 8);
  set(31, 34, 8);
  // Pond with shallow, middle and deep water.
  for (int x = 13; x <= 20; x++)
    for (int y = 24; y <= 31; y++) {
      int dx = x - 16, dy = y - 27;
      int r = dx * dx + dy * dy;
      if (r <= 12) set(x, y, 17, r <= 2 ? 2 : r <= 6 ? 1 : 0);
    }
  // Woods, brush, rocks, deep rough, marsh, a mound.
  for (int x = 33; x <= 38; x++)
    for (int y = 24; y <= 30; y++) set(x, y, (x + y) % 3 ? 13 : 5);
  for (int x = 12; x <= 15; x++)
    for (int y = 12; y <= 16; y++) set(x, y, (x + y) & 1 ? 11 : 12);
  for (int y = 34; y <= 37; y++) set(14, y, 18);
  set(34, 35, 6);
  set(35, 35, 6);
  // A cart path down the left of the fairway, turning toward the green.
  for (int y = 16; y <= 32; y++) c.flags[23 * 50 + y] = 0x20;
  for (int x = 19; x <= 23; x++) c.flags[x * 50 + 33] = 0x20;
  c.flags[23 * 50 + 33] = 0x20;
  // A notch of unbought land, so the strata skirts show.
  for (int x = 36; x < 40; x++)
    for (int y = 10; y < 14; y++) set(x, y, 20);
}

void course_push(Terrain *t, const Course &c) {
  int groups[23];
  for (int i = 0; i < 23; i++) groups[i] = i;
  t->passCollarInfo(groups, 23);
  t->loadNewCourseType(c.courseType);
  t->initTerrain();
  for (int x = 49; x >= 0; x--)
    for (int y = 49; y >= 0; y--) t->setType(t->tileAt(x, y), c.type[x * 50 + y], c.variation[x * 50 + y]);
  for (int x = 49; x >= 0; x--)
    for (int y = 49; y >= 0; y--) {
      if (c.type[x * 50 + y] == 17) continue;
      for (int corner = 1; corner < 8; corner += 2) {
        int h = cornerHeight(c, x, y, corner);
        for (int i = 0; i < h - 3; i++) t->elevateCorner(t->tileAt(x, y), corner);
      }
    }
  for (int x = 49; x >= 0; x--)
    for (int y = 49; y >= 0; y--) t->calcNormals(t->tileAt(x, y));
  for (int x = 49; x >= 0; x--)
    for (int y = 49; y >= 0; y--)
      for (int dir = 0; dir < 8; dir += 2)
        if (wallOn(c, x, y, dir)) t->setWall(t->tileAt(x, y), dir, 1, true);
  for (int x = 49; x >= 0; x--)
    for (int y = 49; y >= 0; y--) {
      uint16_t f = c.flags[x * 50 + y];
      if (f & 0x20) t->layPath(t->tileAt(x, y), 0x20, f & 0x40);
    }
}
