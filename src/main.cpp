// SimGolf browser port: skeleton. Opens the 800x600 window, keeps the game's
// RGB555 back buffer, shows the title screen art and tracks the mouse. Game
// code and the graphsy/Terrain shims plug in here later.
//
// Sound test: the course ambience (#45) loops from the first click or key.
// Left click plays the iron shot (#1) panned by the click's x position, right
// click the club drop (#144) with the game's random -0..300 cent pitch, and M
// toggles the music sting (#128) with the game's 1000 ms fade-out.

#include <SDL.h>
#include <SDL_image.h>
#include <emscripten.h>
#include <stdlib.h>

#include "platform/framebuffer.h"
#include "platform/sound.h"

namespace {

SDL_Window *g_window;
Framebuffer g_fb;
Image555 g_title;
int g_mouseX = -1, g_mouseY = -1;
bool g_ambience, g_music;

// A few of golf's registrations (FUN_00448220), with its index and flags.
void registerTestSounds() {
  snd_register(1, "sounds\\Golf sfx\\Iron.wav", SND_FLAG_LAZY);
  snd_register(45, "sounds\\GolfAmbience122.wav", SND_FLAG_LAZY);
  snd_register(128, "sounds\\Music Idea.wav", SND_FLAG_LAZY | SND_FLAG_DEDICATED);
  snd_register(144, "sounds\\effects\\club 2 drop no wtr.wav", SND_FLAG_LAZY);
}

void onInput() {
  if (!g_ambience) {
    snd_play(45, 100, 0, 0, 0);
    g_ambience = true;
  }
}

void plot(int x, int y, uint16_t c) {
  if (x >= 0 && x < Framebuffer::kWidth && y >= 0 && y < Framebuffer::kHeight)
    g_fb.pixels()[y * Framebuffer::kWidth + x] = c;
}

void drawCursor() {
  // Placeholder crosshair until the game's own cursor sprites are wired up.
  for (int d = -6; d <= 6; d++) {
    plot(g_mouseX + d, g_mouseY, 0x7fff);
    plot(g_mouseX, g_mouseY + d, 0x7fff);
  }
}

void frame() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_MOUSEMOTION) {
      g_mouseX = e.motion.x;
      g_mouseY = e.motion.y;
    } else if (e.type == SDL_MOUSEBUTTONDOWN) {
      SDL_Log("click %d,%d button %d", e.button.x, e.button.y, e.button.button);
      onInput();
      if (e.button.button == SDL_BUTTON_LEFT)
        snd_play(1, 100, e.button.x * 127 / (Framebuffer::kWidth - 1) - 64, 0, 0);
      else if (e.button.button == SDL_BUTTON_RIGHT)
        snd_play(144, 100, 0, -(rand() % 300), 0);
    } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
      onInput();
      if (e.key.keysym.sym == SDLK_m) {
        if (g_music) snd_fade_out(128, 1000);
        else snd_play(128, 100, 0, 0, 0);
        g_music = !g_music;
      }
    }
  }
  if (g_title.w) g_fb.blit(g_title.pixels.data(), g_title.w, g_title.h, 0, 0);
  else g_fb.fill(rgb555(0, 96, 0));
  if (g_mouseX >= 0) drawCursor();
  g_fb.present();
}

}  // namespace

int main() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    SDL_Log("SDL_Init: %s", SDL_GetError());
    return 1;
  }
  IMG_Init(IMG_INIT_PNG);
  g_window = SDL_CreateWindow("SimGolf", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              Framebuffer::kWidth, Framebuffer::kHeight, 0);
  SDL_Renderer *renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
  if (!g_window || !renderer || !g_fb.init(renderer)) {
    SDL_Log("video init failed: %s", SDL_GetError());
    return 1;
  }
  SDL_ShowCursor(SDL_DISABLE);
  snd_init();
  registerTestSounds();
  if (g_title.load("/assets/Interface/TitleBASE.png"))
    SDL_Log("title %dx%d", g_title.w, g_title.h);
  emscripten_set_main_loop(frame, 0, 1);
  return 0;
}
