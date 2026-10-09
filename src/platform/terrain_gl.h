// A small software implementation of the OpenGL 1.1 subset Terrain.dll uses,
// drawing straight into the game's 16-bit RGB555 back buffer.
//
// The original ran on Microsoft's generic (software) GL through a 16-bit DIB
// memory DC with no depth buffer, so this is the closest match: fixed-function
// per-vertex lighting (one-sided, infinite viewer), GL_MODULATE texturing,
// back-face culling, SRC_ALPHA/ONE_MINUS_SRC_ALPHA blending, ordered dither on
// the way down to 5 bits, and painter's order decided entirely by the caller.
// Coordinates follow GL: window y points up and the buffer row is h-1-y.

#ifndef SIMGOLF_PLATFORM_TERRAIN_GL_H
#define SIMGOLF_PLATFORM_TERRAIN_GL_H

#include <stdint.h>
#include <vector>

namespace tgl {

struct Mat4 {
  float m[16];  // column-major, as GL stores it
  static Mat4 identity();
  Mat4 operator*(const Mat4 &b) const;
};

struct Light {
  float ambient[4] = {0, 0, 0, 1};
  float diffuse[4] = {0, 0, 0, 1};    // GL defaults light 0 to white; see Context()
  float specular[4] = {0, 0, 0, 1};
  float position[4] = {0, 0, 1, 0};   // eye space, after the modelview at set time
  bool enabled = false;
};

struct Texture {
  int w = 0, h = 0;
  bool hasAlpha = false;
  bool magLinear = false, minLinear = true;
  std::vector<uint8_t> rgba;  // w*h*4, row 0 is the first row uploaded (t = 0)
  unsigned serial = 0;        // unique per createTexture, so a reused id is a new texture
};

// A triangle as the rasterizer sees it: window coordinates (y up), lit colour,
// texture coordinates. terrain_hd.cpp records these to replay them on the GPU.
struct RecordedVertex {
  float x, y, r, g, b, a, u, v;
};
using Recorder = void (*)(void *user, const RecordedVertex v[3], const Texture *tex, bool blend);

struct Vertex {
  float pos[3];
  float normal[3];
  float uv[2];
};

class Context {
public:
  Context();

  // Render target: RGB555, pitch in pixels. A null target makes every draw a
  // no-op, like GL calls made without a current context.
  void setTarget(uint16_t *pixels, int pitch, int width, int height);
  bool hasTarget() const { return pixels_ != nullptr; }
  int targetWidth() const { return width_; }
  int targetHeight() const { return height_; }

  // Matrices (GL semantics: each call post-multiplies the current matrix).
  enum { MODELVIEW = 0, PROJECTION = 1 };
  void matrixMode(int mode) { mode_ = mode; }
  void loadIdentity();
  void pushMatrix();
  void popMatrix();
  void ortho(double l, double r, double b, double t, double n, double f);
  void rotate(double deg, double x, double y, double z);
  void translate(float x, float y, float z);
  void scale(float x, float y, float z);
  void viewport(int x, int y, int w, int h);

  // Fixed-function state.
  bool lighting = true;
  bool texture2D = true;
  bool blend = false;
  bool cullFace = false;
  bool frontFaceCCW = true;
  bool dither = true;
  Light lights[2];
  float globalAmbient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
  float matAmbient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
  float matDiffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f};
  float matSpecular[4] = {0, 0, 0, 1};
  float matShininess = 0;
  float color[4] = {1, 1, 1, 1};  // glColor, used when lighting is off
  float lineWidth = 1;

  // glLightfv(GL_POSITION): transformed by the current modelview.
  void setLightPosition(int light, const float p[4]);

  // Textures. Ids start at 1; 0 is GL's default texture object, which is
  // incomplete, so drawing with it is the same as texturing being off.
  unsigned createTexture(Texture &&tex);
  void deleteTexture(unsigned id);
  void bindTexture(unsigned id) { bound_ = id; }

  // Primitives. Triangles are lit per vertex when lighting is on.
  void triangle(const Vertex &a, const Vertex &b, const Vertex &c);
  // Hands every triangle that survives culling to `rec` as well; with
  // rasterize false nothing is drawn into the target.
  void setRecorder(Recorder rec, void *user, bool rasterize) {
    recorder_ = rec;
    recorderUser_ = user;
    rasterize_ = rasterize;
  }
  const Texture *texture(unsigned id) const {
    return id && id <= textures_.size() && textures_[id - 1].w ? &textures_[id - 1] : nullptr;
  }

  // An antialiased (GL_LINE_SMOOTH) line of lineWidth, colour from `color`.
  void line(float x0, float y0, float x1, float y1);

private:
  struct ScreenVertex {
    float x, y;      // window coordinates
    float c[4];      // lit colour
    float u, v;
  };
  void shade(const Vertex &v, float out[4]) const;
  void project(const Vertex &v, ScreenVertex &out) const;
  void rasterize(const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c);
  void plot(int x, int row, const float rgba[4]);
  Mat4 &current() { return stack_[mode_].back(); }

  uint16_t *pixels_ = nullptr;
  int pitch_ = 0, width_ = 0, height_ = 0;
  int vx_ = 0, vy_ = 0, vw_ = 0, vh_ = 0;
  int mode_ = MODELVIEW;
  std::vector<Mat4> stack_[2];
  std::vector<Texture> textures_;  // index = id - 1; w == 0 means free
  unsigned bound_ = 0;
  Recorder recorder_ = nullptr;
  void *recorderUser_ = nullptr;
  bool rasterize_ = true;
};

}  // namespace tgl

#endif  // SIMGOLF_PLATFORM_TERRAIN_GL_H
