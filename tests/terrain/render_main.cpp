// Native command-line check for the terrain renderer: loads a course, does
// golf's first-frame sequence and writes the 800x600 back buffer as a PNG.
//
//   terrain_render --game <SimGolf dir> [--save <file.sve> | --sample]
//                  [--x 25 --y 25] [--rot 0|90|180|-90] [--zoom 4|2|1] [--out terrain.png]
//
// --save paths are relative to the current directory, read before changing
// into the game directory (the renderer reads Data/Textures from the CWD).

#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>

#include "course.h"
#include "platform/terrain.h"
#include "platform/terrain_port.h"
#include "png.h"

int main(int argc, char **argv) {
  const char *game = nullptr, *save = nullptr, *out = "terrain.png";
  int cx = 25, cy = 25, zoom = 4;
  float rot = 0;
  for (int i = 1; i < argc; i++) {
    auto arg = [&](const char *name) { return strcmp(argv[i], name) == 0 && i + 1 < argc; };
    if (arg("--game")) game = argv[++i];
    else if (arg("--save")) save = argv[++i];
    else if (strcmp(argv[i], "--sample") == 0) save = nullptr;
    else if (arg("--x")) cx = atoi(argv[++i]);
    else if (arg("--y")) cy = atoi(argv[++i]);
    else if (arg("--rot")) rot = (float)atof(argv[++i]);
    else if (arg("--zoom")) zoom = atoi(argv[++i]);
    else if (arg("--out")) out = argv[++i];
    else {
      fprintf(stderr, "unknown argument %s\n", argv[i]);
      return 2;
    }
  }
  static Course course;
  if (save) {
    if (!course_load_sve(save, course)) {
      fprintf(stderr, "can't read %s\n", save);
      return 1;
    }
  } else {
    course_sample(course);
  }
  char outPath[4096];
  if (out[0] != '/' && getcwd(outPath, sizeof outPath)) {
    strncat(outPath, "/", sizeof outPath - strlen(outPath) - 1);
    strncat(outPath, out, sizeof outPath - strlen(outPath) - 1);
  } else {
    snprintf(outPath, sizeof outPath, "%s", out);
  }
  if (game && chdir(game) != 0) {
    fprintf(stderr, "can't enter %s\n", game);
    return 1;
  }

  std::vector<uint16_t> pixels(800 * 600, 0);
  TerrainSurface surface = {pixels.data(), 800, 800, 600};
  Terrain *t = Terrain::getInstance();
  auto t0 = std::chrono::steady_clock::now();
  t->initSystem(800, 600, &surface, true);
  auto t1 = std::chrono::steady_clock::now();
  course_push(t, course);
  if (zoom != 4) t->setZoomLevel(zoom);
  auto t2 = std::chrono::steady_clock::now();
  t->render(t->tileAt(cx, cy), rot);
  auto t3 = std::chrono::steady_clock::now();
  auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  printf("course type %d, init %.1f ms, sync %.1f ms, render %.1f ms\n", course.courseType, ms(t0, t1),
         ms(t1, t2), ms(t2, t3));
  int lit = 0;
  for (uint16_t p : pixels) lit += p != 0;
  printf("%d of %d pixels drawn\n", lit, 800 * 600);
  if (!write_png_rgb555(outPath, pixels.data(), 800, 600, 800)) {
    fprintf(stderr, "can't write %s\n", outPath);
    return 1;
  }
  printf("wrote %s\n", outPath);
  return 0;
}
