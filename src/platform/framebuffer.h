// The game's 800x600 16-bit RGB555 back buffer and its presentation through SDL.
// The original renders everything (terrain via software GL, sprites via jgl)
// into one RGB555 DIB and BitBlts it to the window; the port keeps that model
// and uploads the buffer to an SDL streaming texture once per frame.

#ifndef SIMGOLF_PLATFORM_FRAMEBUFFER_H
#define SIMGOLF_PLATFORM_FRAMEBUFFER_H

#include <stdint.h>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

class Framebuffer {
public:
  static constexpr int kWidth = 800;
  static constexpr int kHeight = 600;

  bool init(SDL_Renderer *renderer);
  void present();

  uint16_t *pixels() { return pixels_.data(); }
  int pitch() const { return kWidth; }      // in pixels, as in GsSurface

  void fill(uint16_t colour);
  // Copies an RGB555 image to (x, y), clipped to the buffer.
  void blit(const uint16_t *src, int w, int h, int x, int y);

private:
  SDL_Renderer *renderer_ = nullptr;
  SDL_Texture *texture_ = nullptr;
  std::vector<uint16_t> pixels_ = std::vector<uint16_t>(kWidth * kHeight);
};

// An RGB555 image loaded from a converted PNG.
struct Image555 {
  int w = 0, h = 0;
  std::vector<uint16_t> pixels;
  bool load(const char *path);
};

static inline uint16_t rgb555(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

#endif  // SIMGOLF_PLATFORM_FRAMEBUFFER_H
