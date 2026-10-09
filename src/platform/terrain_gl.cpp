// Software OpenGL 1.1 subset for the Terrain.dll port. See terrain_gl.h.

#include "platform/terrain_gl.h"

#include <math.h>
#include <string.h>
#include <algorithm>

namespace tgl {

Mat4 Mat4::identity() {
  Mat4 r;
  memset(r.m, 0, sizeof r.m);
  r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
  return r;
}

Mat4 Mat4::operator*(const Mat4 &b) const {
  Mat4 r;
  for (int c = 0; c < 4; c++)
    for (int row = 0; row < 4; row++) {
      float s = 0;
      for (int k = 0; k < 4; k++) s += m[k * 4 + row] * b.m[c * 4 + k];
      r.m[c * 4 + row] = s;
    }
  return r;
}

namespace {

void xform(const Mat4 &a, const float in[4], float out[4]) {
  for (int row = 0; row < 4; row++)
    out[row] = a.m[row] * in[0] + a.m[4 + row] * in[1] + a.m[8 + row] * in[2] + a.m[12 + row] * in[3];
}

// Upper 3x3 inverse transpose, which GL uses for normals.
void normalMatrix(const Mat4 &a, float n[9]) {
  float m00 = a.m[0], m01 = a.m[4], m02 = a.m[8];
  float m10 = a.m[1], m11 = a.m[5], m12 = a.m[9];
  float m20 = a.m[2], m21 = a.m[6], m22 = a.m[10];
  float c00 = m11 * m22 - m12 * m21, c01 = m12 * m20 - m10 * m22, c02 = m10 * m21 - m11 * m20;
  float c10 = m02 * m21 - m01 * m22, c11 = m00 * m22 - m02 * m20, c12 = m01 * m20 - m00 * m21;
  float c20 = m01 * m12 - m02 * m11, c21 = m02 * m10 - m00 * m12, c22 = m00 * m11 - m01 * m10;
  float det = m00 * c00 + m01 * c01 + m02 * c02;
  float k = det != 0 ? 1 / det : 0;
  // (M^-1)^T = cofactor matrix / det, rows in row-major order.
  n[0] = c00 * k; n[1] = c01 * k; n[2] = c02 * k;
  n[3] = c10 * k; n[4] = c11 * k; n[5] = c12 * k;
  n[6] = c20 * k; n[7] = c21 * k; n[8] = c22 * k;
}

void normalize3(float v[3]) {
  float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

int wrap(int i, int n) {
  i %= n;
  return i < 0 ? i + n : i;
}

void sampleNearest(const Texture &t, float u, float v, float out[4]) {
  int x = wrap((int)floorf(u * t.w), t.w), y = wrap((int)floorf(v * t.h), t.h);
  const uint8_t *p = &t.rgba[(y * t.w + x) * 4];
  for (int i = 0; i < 4; i++) out[i] = p[i] / 255.0f;
}

void sampleLinear(const Texture &t, float u, float v, float out[4]) {
  float fu = u * t.w - 0.5f, fv = v * t.h - 0.5f;
  float x0f = floorf(fu), y0f = floorf(fv);
  float a = fu - x0f, b = fv - y0f;
  int x0 = wrap((int)x0f, t.w), y0 = wrap((int)y0f, t.h);
  int x1 = wrap(x0 + 1, t.w), y1 = wrap(y0 + 1, t.h);
  const uint8_t *p00 = &t.rgba[(y0 * t.w + x0) * 4], *p10 = &t.rgba[(y0 * t.w + x1) * 4];
  const uint8_t *p01 = &t.rgba[(y1 * t.w + x0) * 4], *p11 = &t.rgba[(y1 * t.w + x1) * 4];
  for (int i = 0; i < 4; i++)
    out[i] = ((1 - a) * (1 - b) * p00[i] + a * (1 - b) * p10[i] + (1 - a) * b * p01[i] + a * b * p11[i]) / 255.0f;
}

}  // namespace

Context::Context() {
  stack_[0].push_back(Mat4::identity());
  stack_[1].push_back(Mat4::identity());
  for (int i = 0; i < 3; i++) lights[0].diffuse[i] = lights[0].specular[i] = 1;  // GL's LIGHT0 defaults
}

void Context::setTarget(uint16_t *pixels, int pitch, int width, int height) {
  pixels_ = pixels;
  pitch_ = pitch;
  width_ = width;
  height_ = height;
}

void Context::loadIdentity() { current() = Mat4::identity(); }
void Context::pushMatrix() { stack_[mode_].push_back(current()); }
void Context::popMatrix() {
  if (stack_[mode_].size() > 1) stack_[mode_].pop_back();
}

void Context::ortho(double l, double r, double b, double t, double n, double f) {
  Mat4 o = Mat4::identity();
  o.m[0] = (float)(2 / (r - l));
  o.m[5] = (float)(2 / (t - b));
  o.m[10] = (float)(-2 / (f - n));
  o.m[12] = (float)(-(r + l) / (r - l));
  o.m[13] = (float)(-(t + b) / (t - b));
  o.m[14] = (float)(-(f + n) / (f - n));
  current() = current() * o;
}

void Context::rotate(double deg, double x, double y, double z) {
  double len = sqrt(x * x + y * y + z * z);
  if (len == 0) return;
  x /= len; y /= len; z /= len;
  double a = deg * M_PI / 180, c = cos(a), s = sin(a), k = 1 - c;
  Mat4 r = Mat4::identity();
  r.m[0] = (float)(x * x * k + c);     r.m[4] = (float)(x * y * k - z * s); r.m[8] = (float)(x * z * k + y * s);
  r.m[1] = (float)(y * x * k + z * s); r.m[5] = (float)(y * y * k + c);     r.m[9] = (float)(y * z * k - x * s);
  r.m[2] = (float)(x * z * k - y * s); r.m[6] = (float)(y * z * k + x * s); r.m[10] = (float)(z * z * k + c);
  current() = current() * r;
}

void Context::translate(float x, float y, float z) {
  Mat4 t = Mat4::identity();
  t.m[12] = x; t.m[13] = y; t.m[14] = z;
  current() = current() * t;
}

void Context::scale(float x, float y, float z) {
  Mat4 s = Mat4::identity();
  s.m[0] = x; s.m[5] = y; s.m[10] = z;
  current() = current() * s;
}

void Context::viewport(int x, int y, int w, int h) {
  vx_ = x; vy_ = y; vw_ = w; vh_ = h;
}

void Context::setLightPosition(int light, const float p[4]) {
  xform(stack_[MODELVIEW].back(), p, lights[light].position);
}

unsigned Context::createTexture(Texture &&tex) {
  static unsigned serial;
  tex.serial = ++serial;
  for (size_t i = 0; i < textures_.size(); i++)
    if (textures_[i].w == 0) {
      textures_[i] = std::move(tex);
      return (unsigned)i + 1;
    }
  textures_.push_back(std::move(tex));
  return (unsigned)textures_.size();
}

void Context::deleteTexture(unsigned id) {
  if (id == 0 || id > textures_.size()) return;
  textures_[id - 1] = Texture();
  if (bound_ == id) bound_ = 0;
}

// GL 1.1 lighting (section 2.13): one-sided, local viewer off, no colour
// material, no separate specular, so the highlight is modulated by the
// texture like everything else.
void Context::shade(const Vertex &v, float out[4]) const {
  if (!lighting) {
    memcpy(out, color, sizeof color);
    return;
  }
  float nm[9];
  normalMatrix(stack_[MODELVIEW].back(), nm);
  float n[3] = {nm[0] * v.normal[0] + nm[1] * v.normal[1] + nm[2] * v.normal[2],
                nm[3] * v.normal[0] + nm[4] * v.normal[1] + nm[5] * v.normal[2],
                nm[6] * v.normal[0] + nm[7] * v.normal[1] + nm[8] * v.normal[2]};
  // GL_NORMALIZE is off: non-unit normals (Terrain's wall normals) stay as given.
  float c[3];
  for (int i = 0; i < 3; i++) c[i] = matAmbient[i] * globalAmbient[i];
  for (const Light &l : lights) {
    if (!l.enabled) continue;
    float vp[3] = {l.position[0], l.position[1], l.position[2]};  // directional only
    normalize3(vp);
    float ndotl = n[0] * vp[0] + n[1] * vp[1] + n[2] * vp[2];
    float diff = std::max(ndotl, 0.0f);
    float spec = 0;
    if (ndotl != 0) {  // f_i in the spec: 1 whenever n.VP is non-zero
      float h[3] = {vp[0], vp[1], vp[2] + 1};
      normalize3(h);
      float ndoth = std::max(n[0] * h[0] + n[1] * h[1] + n[2] * h[2], 0.0f);
      spec = matShininess == 0 ? 1 : powf(ndoth, matShininess);
    }
    for (int i = 0; i < 3; i++)
      c[i] += matAmbient[i] * l.ambient[i] + diff * matDiffuse[i] * l.diffuse[i] +
              spec * matSpecular[i] * l.specular[i];
  }
  for (int i = 0; i < 3; i++) out[i] = clamp01(c[i]);
  out[3] = clamp01(matDiffuse[3]);
}

void Context::project(const Vertex &v, ScreenVertex &out) const {
  float p[4] = {v.pos[0], v.pos[1], v.pos[2], 1}, e[4], c[4];
  xform(stack_[MODELVIEW].back(), p, e);
  xform(stack_[PROJECTION].back(), e, c);
  float w = c[3] != 0 ? c[3] : 1;
  out.x = vx_ + (c[0] / w + 1) * vw_ * 0.5f;
  out.y = vy_ + (c[1] / w + 1) * vh_ * 0.5f;
  shade(v, out.c);
  out.u = v.uv[0];
  out.v = v.uv[1];
}

void Context::triangle(const Vertex &a, const Vertex &b, const Vertex &c) {
  if (!pixels_ && !recorder_) return;
  ScreenVertex s[3];
  project(a, s[0]);
  project(b, s[1]);
  project(c, s[2]);
  float area = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[2].x - s[0].x) * (s[1].y - s[0].y);
  if (area == 0) return;
  bool front = (area > 0) == frontFaceCCW;
  if (cullFace && !front) return;
  if (recorder_) {
    RecordedVertex r[3];
    for (int i = 0; i < 3; i++)
      r[i] = {s[i].x, s[i].y, s[i].c[0], s[i].c[1], s[i].c[2], s[i].c[3], s[i].u, s[i].v};
    recorder_(recorderUser_, r, texture2D ? texture(bound_) : nullptr, blend);
  }
  if (!rasterize_ || !pixels_) return;
  if (area > 0) rasterize(s[0], s[1], s[2]);
  else rasterize(s[0], s[2], s[1]);
}

// Counter-clockwise triangle in window space (y up). Pixel centres are at
// +0.5; pixels exactly on an edge belong to the top or left edge (GL's usual
// rule) so that neighbouring triangles neither overlap nor leave gaps.
void Context::rasterize(const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c) {
  float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
  int x0 = std::max(0, (int)floorf(std::min({a.x, b.x, c.x})));
  int x1 = std::min(width_ - 1, (int)ceilf(std::max({a.x, b.x, c.x})));
  int y0 = std::max(0, (int)floorf(std::min({a.y, b.y, c.y})));
  int y1 = std::min(height_ - 1, (int)ceilf(std::max({a.y, b.y, c.y})));
  if (x0 > x1 || y0 > y1) return;

  const ScreenVertex *v[3] = {&a, &b, &c};
  // Edge i is opposite vertex i: from v[i+1] to v[i+2].
  float ex[3], ey[3];
  bool topLeft[3];
  for (int i = 0; i < 3; i++) {
    const ScreenVertex &p = *v[(i + 1) % 3], &q = *v[(i + 2) % 3];
    ex[i] = q.x - p.x;
    ey[i] = q.y - p.y;
    topLeft[i] = ey[i] < 0 || (ey[i] == 0 && ex[i] < 0);
  }

  const Texture *tex = nullptr;
  if (texture2D && bound_ && bound_ <= textures_.size() && textures_[bound_ - 1].w) tex = &textures_[bound_ - 1];
  bool linear = false;
  if (tex) {
    // The mapping is affine (orthographic), so lambda is constant per triangle.
    auto ddx = [&](float fa, float fb, float fc) {
      return ((fb - fa) * (c.y - a.y) - (fc - fa) * (b.y - a.y)) / area;
    };
    auto ddy = [&](float fa, float fb, float fc) {
      return ((fc - fa) * (b.x - a.x) - (fb - fa) * (c.x - a.x)) / area;
    };
    float dudx = ddx(a.u, b.u, c.u) * tex->w, dvdx = ddx(a.v, b.v, c.v) * tex->h;
    float dudy = ddy(a.u, b.u, c.u) * tex->w, dvdy = ddy(a.v, b.v, c.v) * tex->h;
    float rho = std::max(sqrtf(dudx * dudx + dvdx * dvdx), sqrtf(dudy * dudy + dvdy * dvdy));
    linear = rho > 1 ? tex->minLinear : tex->magLinear;
  }

  for (int y = y0; y <= y1; y++) {
    float py = y + 0.5f;
    for (int x = x0; x <= x1; x++) {
      float px = x + 0.5f;
      float w[3];
      bool inside = true;
      for (int i = 0; i < 3; i++) {
        const ScreenVertex &p = *v[(i + 1) % 3];
        w[i] = ex[i] * (py - p.y) - ey[i] * (px - p.x);
        if (w[i] < 0 || (w[i] == 0 && !topLeft[i])) { inside = false; break; }
      }
      if (!inside) continue;
      float l0 = w[0] / area, l1 = w[1] / area, l2 = 1 - l0 - l1;
      float col[4];
      for (int i = 0; i < 4; i++) col[i] = l0 * a.c[i] + l1 * b.c[i] + l2 * c.c[i];
      if (tex) {
        float u = l0 * a.u + l1 * b.u + l2 * c.u, vv = l0 * a.v + l1 * b.v + l2 * c.v;
        float t[4];
        if (linear) sampleLinear(*tex, u, vv, t);
        else sampleNearest(*tex, u, vv, t);
        for (int i = 0; i < 3; i++) col[i] *= t[i];
        if (tex->hasAlpha) col[3] *= t[3];
      }
      plot(x, height_ - 1 - y, col);
    }
  }
}

void Context::plot(int x, int row, const float rgba[4]) {
  uint16_t &dst = pixels_[row * pitch_ + x];
  float r = rgba[0], g = rgba[1], b = rgba[2];
  if (blend) {
    float a = clamp01(rgba[3]);
    float dr = ((dst >> 10) & 31) / 31.0f, dg = ((dst >> 5) & 31) / 31.0f, db = (dst & 31) / 31.0f;
    r = r * a + dr * (1 - a);
    g = g * a + dg * (1 - a);
    b = b * a + db * (1 - a);
  }
  float d = dither ? (kBayer[row & 3][x & 3] + 0.5f) / 16.0f : 0.5f;
  auto q = [&](float v) {
    int i = (int)floorf(clamp01(v) * 31 + d);
    return i > 31 ? 31 : i;
  };
  dst = (uint16_t)((q(r) << 10) | (q(g) << 5) | q(b));
}

void Context::line(float x0, float y0, float x1, float y1) {
  if (!pixels_) return;
  Vertex va = {{x0, y0, 0}, {0, 0, 1}, {0, 0}}, vb = {{x1, y1, 0}, {0, 0, 1}, {0, 0}};
  ScreenVertex a, b;
  project(va, a);
  project(vb, b);
  float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
  if (len == 0) return;
  float ux = dx / len, uy = dy / len, half = lineWidth * 0.5f;
  int xa = std::max(0, (int)floorf(std::min(a.x, b.x) - half - 1));
  int xb = std::min(width_ - 1, (int)ceilf(std::max(a.x, b.x) + half + 1));
  int ya = std::max(0, (int)floorf(std::min(a.y, b.y) - half - 1));
  int yb = std::min(height_ - 1, (int)ceilf(std::max(a.y, b.y) + half + 1));
  bool oldBlend = blend;
  blend = true;  // coverage goes through alpha, as GL_LINE_SMOOTH does
  for (int y = ya; y <= yb; y++)
    for (int x = xa; x <= xb; x++) {
      float px = x + 0.5f - a.x, py = y + 0.5f - a.y;
      float along = px * ux + py * uy, across = fabsf(px * uy - py * ux);
      float cov = clamp01(half + 0.5f - across) * clamp01(std::min(along + 0.5f, len - along + 0.5f));
      if (cov <= 0) continue;
      float col[4] = {color[0], color[1], color[2], color[3] * cov};
      plot(x, height_ - 1 - y, col);
    }
  blend = oldBlend;
}

}  // namespace tgl
