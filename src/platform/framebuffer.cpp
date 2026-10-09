#include "platform/framebuffer.h"

#include <SDL.h>
#include <SDL_image.h>
#include <algorithm>

bool Framebuffer::init(SDL_Renderer *renderer) {
  renderer_ = renderer;
  texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB555, SDL_TEXTUREACCESS_STREAMING,
                               kWidth, kHeight);
  return texture_ != nullptr;
}

void Framebuffer::present() {
  SDL_UpdateTexture(texture_, nullptr, pixels_.data(), kWidth * 2);
  SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
  SDL_RenderPresent(renderer_);
}

void Framebuffer::fill(uint16_t colour) { std::fill(pixels_.begin(), pixels_.end(), colour); }

void Framebuffer::blit(const uint16_t *src, int w, int h, int x, int y) {
  int x0 = std::max(x, 0), y0 = std::max(y, 0);
  int x1 = std::min(x + w, kWidth), y1 = std::min(y + h, kHeight);
  for (int row = y0; row < y1; row++)
    std::copy_n(src + (row - y) * w + (x0 - x), std::max(x1 - x0, 0), &pixels_[row * kWidth + x0]);
}

bool Image555::load(const char *path) {
  SDL_Surface *raw = IMG_Load(path);
  if (!raw) {
    SDL_Log("IMG_Load %s: %s", path, IMG_GetError());
    return false;
  }
  SDL_Surface *conv = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGB555, 0);
  SDL_FreeSurface(raw);
  if (!conv) return false;
  w = conv->w;
  h = conv->h;
  pixels.resize((size_t)w * h);
  for (int row = 0; row < h; row++)
    SDL_memcpy(&pixels[(size_t)row * w], (uint8_t *)conv->pixels + row * conv->pitch, (size_t)w * 2);
  SDL_FreeSurface(conv);
  return true;
}
