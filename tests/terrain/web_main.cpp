// Browser test page for the terrain renderer. Renders the sample course or one
// of the user's saved games into the 800x600 RGB555 back buffer, driven the
// way golf.exe's per-frame driver (FUN_004498a0) uses Terrain:
//
//   full redraw  clear the view, render(), copy the result to a cache
//   idle frame   restore the cache, draw overlays (drawLine), present
//   edits        localRender() over the existing image, then re-cache
//
// Keys: 1-5 course, arrows scroll, R rotate, Z zoom, B bunker at the centre,
// H raise the centre tile, L lower it. The mouse draws an aim line.

#include <SDL.h>
#include <dirent.h>
#include <emscripten.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <vector>

#include "course.h"
#include "platform/framebuffer.h"
#include "platform/terrain.h"
#include "platform/terrain_port.h"

namespace {

Framebuffer g_fb;
std::vector<uint16_t> g_cache(Framebuffer::kWidth * Framebuffer::kHeight);
TerrainSurface g_surface;
Terrain *g_terrain;
std::vector<std::string> g_courses;  // "" = the sample course
Course g_course;
int g_cx = 25, g_cy = 25, g_rot = 0, g_zoomIndex = 0, g_mouseX = -1, g_mouseY = -1;
const float kRot[4] = {0, 90, 180, -90};  // golf's rotation 0,2,4,6
const int kZoom[3] = {4, 2, 1};

void status(const char *what, double ms) {
  EM_ASM({ document.getElementById('status').textContent = UTF8ToString($0); }, (std::string(what) + " in " +
          std::to_string((int)ms) + " ms. Centre " + std::to_string(g_cx) + "," + std::to_string(g_cy) +
          ", rotation " + std::to_string((int)kRot[g_rot]) + ", zoom " + std::to_string(kZoom[g_zoomIndex]))
                                                                  .c_str());
}

void fullRedraw(const char *why) {
  double t0 = emscripten_get_now();
  g_fb.fill(0);
  g_terrain->render(g_terrain->tileAt(g_cx, g_cy), kRot[g_rot]);
  memcpy(g_cache.data(), g_fb.pixels(), g_cache.size() * 2);
  status(why, emscripten_get_now() - t0);
}

void loadCourse(size_t index) {
  if (index >= g_courses.size()) return;
  double t0 = emscripten_get_now();
  if (g_courses[index].empty()) course_sample(g_course);
  else if (!course_load_sve(g_courses[index].c_str(), g_course)) return;
  g_terrain->resetTerrain();
  course_push(g_terrain, g_course);
  SDL_Log("course %s: type %d, sync %.1f ms", g_courses[index].empty() ? "sample" : g_courses[index].c_str(),
          g_course.courseType, emscripten_get_now() - t0);
  fullRedraw("Course loaded and rendered");
}

// An edit around the centre tile, redrawn the way golf does it: recompute
// normals near the change, then localRender each dirty tile over the image.
void edit(char key) {
  double t0 = emscripten_get_now();
  Tile *t = g_terrain->tileAt(g_cx, g_cy);
  if (!t) return;
  if (key == 'b') {
    g_terrain->setType(t, 7, rand() & 3);
  } else {
    for (int c = 1; c < 8; c += 2) {
      if (key == 'h') g_terrain->elevateCorner(t, c);
      else if (t->elev[c] > 0) g_terrain->lowerCorner(t, c);
    }
    g_terrain->calcAllNormals(t);
  }
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if (Tile *d = g_terrain->tileAt(g_cx + dx, g_cy + dy))
        g_terrain->localRender(d, t, kRot[g_rot]);
  memcpy(g_cache.data(), g_fb.pixels(), g_cache.size() * 2);
  status(key == 'b' ? "Bunker placed, localRender" : "Corners moved, localRender", emscripten_get_now() - t0);
}

void frame() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_MOUSEMOTION) {
      g_mouseX = e.motion.x;
      g_mouseY = e.motion.y;
    } else if (e.type == SDL_KEYDOWN) {
      SDL_Keycode k = e.key.keysym.sym;
      if (k >= SDLK_1 && k <= SDLK_9) loadCourse((size_t)(k - SDLK_1));
      else if (k == SDLK_r) { g_rot = (g_rot + 1) & 3; fullRedraw("Rotated, full render"); }
      else if (k == SDLK_z) {
        g_zoomIndex = (g_zoomIndex + 1) % 3;
        g_terrain->setZoomLevel(kZoom[g_zoomIndex]);
        fullRedraw("Zoomed, full render");
      } else if (k == SDLK_b || k == SDLK_h || k == SDLK_l) edit((char)k);
      else {
        int dx = k == SDLK_RIGHT ? 1 : k == SDLK_LEFT ? -1 : 0, dy = k == SDLK_DOWN ? 1 : k == SDLK_UP ? -1 : 0;
        if (dx || dy) {
          g_cx = std::min(49, std::max(0, g_cx + dx));
          g_cy = std::min(49, std::max(0, g_cy + dy));
          fullRedraw("Scrolled, full render");
        }
      }
    }
  }
  memcpy(g_fb.pixels(), g_cache.data(), g_cache.size() * 2);
  if (g_mouseX >= 0)
    g_terrain->drawLine(Framebuffer::kWidth / 2, Framebuffer::kHeight / 2, g_mouseX, g_mouseY,
                        (int)0x80000000, 2, 5);
  g_fb.present();
}

}  // namespace

int main() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
  SDL_Window *window = SDL_CreateWindow("Terrain test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                        Framebuffer::kWidth, Framebuffer::kHeight, 0);
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!window || !renderer || !g_fb.init(renderer)) return 1;

  // Saved games preloaded next to the game files; the sample comes first.
  g_courses.push_back("");
  if (DIR *d = opendir("/game/Saved Games")) {
    while (dirent *e = readdir(d))
      if (strstr(e->d_name, ".sve")) g_courses.push_back(std::string("/game/Saved Games/") + e->d_name);
    closedir(d);
  }
  std::string list;
  for (size_t i = 0; i < g_courses.size(); i++)
    list += std::to_string(i + 1) + " " + (g_courses[i].empty() ? "sample" : g_courses[i].substr(18)) + "   ";
  EM_ASM({ document.getElementById('courses').textContent = UTF8ToString($0); }, list.c_str());

  chdir("/game");  // Terrain reads ./Data/Textures and <Course>Lighting.txt
  g_surface = {g_fb.pixels(), g_fb.pitch(), Framebuffer::kWidth, Framebuffer::kHeight};
  g_terrain = Terrain::getInstance();
  double t0 = emscripten_get_now();
  g_terrain->initSystem(Framebuffer::kWidth, Framebuffer::kHeight, &g_surface, true);
  SDL_Log("initSystem (lighting, textures) %.1f ms", emscripten_get_now() - t0);
  loadCourse(0);
  emscripten_set_main_loop(frame, 0, 1);
  return 0;
}
