#include "png.h"

#include <stdio.h>
#include <vector>

namespace {

uint32_t crc(const uint8_t *p, size_t n, uint32_t c = 0xffffffffu) {
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
  }
  return c;
}

void put32(std::vector<uint8_t> &v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s));
}

void chunk(FILE *f, const char *type, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> c;
  put32(c, (uint32_t)data.size());
  c.insert(c.end(), type, type + 4);
  c.insert(c.end(), data.begin(), data.end());
  put32(c, crc(c.data() + 4, c.size() - 4) ^ 0xffffffffu);
  fwrite(c.data(), 1, c.size(), f);
}

}  // namespace

bool write_png_rgb555(const char *path, const uint16_t *pixels, int width, int height, int pitch) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  fwrite(kSig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  put32(ihdr, (uint32_t)width);
  put32(ihdr, (uint32_t)height);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk(f, "IHDR", ihdr);

  std::vector<uint8_t> raw;
  for (int y = 0; y < height; y++) {
    raw.push_back(0);
    for (int x = 0; x < width; x++) {
      uint16_t p = pixels[y * pitch + x];
      int r = (p >> 10) & 31, g = (p >> 5) & 31, b = p & 31;
      raw.push_back((uint8_t)(r << 3 | r >> 2));
      raw.push_back((uint8_t)(g << 3 | g >> 2));
      raw.push_back((uint8_t)(b << 3 | b >> 2));
    }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  for (size_t i = 0; i < raw.size(); i += 65535) {
    size_t n = raw.size() - i < 65535 ? raw.size() - i : 65535;
    z.push_back(i + n == raw.size() ? 1 : 0);
    z.push_back((uint8_t)n);
    z.push_back((uint8_t)(n >> 8));
    z.push_back((uint8_t)~n);
    z.push_back((uint8_t)(~n >> 8));
    z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
  }
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  put32(z, b << 16 | a);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  return fclose(f) == 0;
}
