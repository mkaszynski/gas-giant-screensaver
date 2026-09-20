#include "renderer.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <png.h>
#include <random>
#include <sstream>
#include <stdexcept>
namespace observatory {
namespace {
// Restore the host lock renderer's state, including on shader/asset exceptions.
struct GLState {
  GLint framebuffer = 0, viewport[4]{}, vao = 0, program = 0, buffer = 0,
        active = 0;
  GLint srcRGB = 0, dstRGB = 0, srcAlpha = 0, dstAlpha = 0, scissorBox[4]{};
  bool blend = false, depth = false, scissor = false;
  GLState() {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
    blend = glIsEnabled(GL_BLEND);
    depth = glIsEnabled(GL_DEPTH_TEST);
    scissor = glIsEnabled(GL_SCISSOR_TEST);
  }
  ~GLState() {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glUseProgram(program);
    glActiveTexture(active);
    glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
    glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
    if (blend)
      glEnable(GL_BLEND);
    else
      glDisable(GL_BLEND);
    if (depth)
      glEnable(GL_DEPTH_TEST);
    else
      glDisable(GL_DEPTH_TEST);
    if (scissor)
      glEnable(GL_SCISSOR_TEST);
    else
      glDisable(GL_SCISSOR_TEST);
  }
};

void scalar(GLuint p, const char *n, float v) {
  glUniform1f(glGetUniformLocation(p, n), v);
}
void integer(GLuint p, const char *n, int v) {
  glUniform1i(glGetUniformLocation(p, n), v);
}
void vector(GLuint p, const char *n, Vec3 v) {
  glUniform3f(glGetUniformLocation(p, n), v.x, v.y, v.z);
}
GLuint compile(GLenum type, const std::string &source) {
  GLuint s = glCreateShader(type);
  const char *p = source.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[8192];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    glDeleteShader(s);
    throw std::runtime_error(log);
  }
  return s;
}
} // namespace
std::string Renderer::defaultDataDirectory() {
  if (const char *p = std::getenv("OBSERVATORY_DATA_DIR"))
    return p;
  if (std::filesystem::exists("shaders/sky_lut.frag"))
    return ".";
  return OBSERVATORY_DATA_DIR;
}
std::string Renderer::shader(const std::string &name) {
  std::ifstream f(root + "/shaders/" + name);
  if (!f)
    throw std::runtime_error("Cannot read shader: " + name);
  std::string out, line;
  while (std::getline(f, line)) {
    if (line.starts_with("#include \"")) {
      auto end = line.find('"', 10);
      out += shader(line.substr(10, end - 10));
    } else
      out += line + "\n";
  }
  return out;
}
GLuint Renderer::program(const std::string &fragment,
                         const std::string &vertex) {
  GLuint vs = compile(GL_VERTEX_SHADER, shader(vertex)),
         fs = compile(GL_FRAGMENT_SHADER, shader(fragment));
  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[8192];
    glGetProgramInfoLog(p, sizeof(log), nullptr, log);
    glDeleteProgram(p);
    throw std::runtime_error(log);
  }
  programs.push_back(p);
  return p;
}
void Renderer::release(Target &t) {
  if (t.framebuffer)
    glDeleteFramebuffers(1, &t.framebuffer);
  if (t.texture)
    glDeleteTextures(1, &t.texture);
  t = {};
}
void Renderer::target(Target &t, int w, int h) {
  if (t.width == w && t.height == h)
    return;
  release(t);
  t.width = w;
  t.height = h;
  glGenTextures(1, &t.texture);
  glBindTexture(GL_TEXTURE_2D, t.texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &t.framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, t.framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         t.texture, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error("Floating-point framebuffer unavailable");
}
GLuint Renderer::imageTexture(const std::string &file) {
  png_image img{};
  img.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(&img, (root + "/assets/" + file).c_str()))
    throw std::runtime_error("Cannot load " + file);
  img.format = PNG_FORMAT_RGBA;
  std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(img));
  if (!png_image_finish_read(&img, nullptr, pixels.data(), 0, nullptr)) {
    auto msg = std::string(img.message);
    png_image_free(&img);
    throw std::runtime_error(msg);
  }
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                  file == "giant.png" ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  png_image_free(&img);
  textures.push_back(t);
  return t;
}
void Renderer::bind(GLuint p, int unit, const char *name, GLuint t) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, t);
  integer(p, name, unit);
}
void Renderer::quad(GLuint p) {
  glUseProgram(p);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}
Renderer::Renderer(std::string dir) : root(std::move(dir)) {
  const GLState state;
  glGenVertexArrays(1, &vao);
  transProgram = program("transmittance.frag");
  multiProgram = program("multiple.frag");
  skyProgram = program("sky_lut.frag");
  backgroundProgram = program("background.frag");
  bodyProgram = program("body.frag");
  starProgram = program("stars.frag", "stars.vert");
  postProgram = program("post.frag");
  giant = imageTexture("giant.png");
  mountains = imageTexture("mountains.png");
  std::mt19937 rng(0x194bd);
  std::uniform_real_distribution<float> unit(0, 1);
  std::vector<unsigned char> data(256 * 256);
  for (auto &v : data)
    v = static_cast<unsigned char>(unit(rng) * 255);
  glGenTextures(1, &noise);
  textures.push_back(noise);
  glBindTexture(GL_TEXTURE_2D, noise);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 256, 0, GL_RED, GL_UNSIGNED_BYTE,
               data.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  std::vector<float> catalog;
  for (int i = 0; i < stars; ++i) {
    float y = unit(rng) * 2 - 1, a = unit(rng) * 2 * pi,
          r = std::sqrt(1 - y * y);
    catalog.insert(catalog.end(), {r * std::cos(a), y, r * std::sin(a),
                                   .25f + .75f * unit(rng)});
  }
  glGenVertexArrays(1, &starVao);
  glBindVertexArray(starVao);
  glGenBuffers(1, &starBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, starBuffer);
  glBufferData(GL_ARRAY_BUFFER, catalog.size() * sizeof(float), catalog.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDisable(GL_BLEND);
  target(trans, 256, 128);
  glViewport(0, 0, 256, 128);
  quad(transProgram);
  target(multi, 32, 32);
  glViewport(0, 0, 32, 32);
  glUseProgram(multiProgram);
  bind(multiProgram, 0, "uTrans", trans.texture);
  quad(multiProgram);
  target(sky, 256, 128);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
Renderer::~Renderer() {
  release(trans);
  release(multi);
  release(sky);
  release(scene);
  for (auto p : programs)
    glDeleteProgram(p);
  for (auto t : textures)
    glDeleteTextures(1, &t);
  glDeleteBuffers(1, &starBuffer);
  glDeleteVertexArrays(1, &starVao);
  glDeleteVertexArrays(1, &vao);
}
void Renderer::common(GLuint p, const Scene &s, int w, int h, double seconds) {
  glUseProgram(p);
  glUniform2f(glGetUniformLocation(p, "uResolution"), w, h);
  vector(p, "uForward", s.forward);
  vector(p, "uRight", s.right);
  vector(p, "uUp", s.cameraUp);
  vector(p, "uSun", s.sunLocal);
  scalar(p, "uTanFov", std::tan(29 * pi / 180));
  scalar(p, "uTime", std::fmod(seconds, 100000.));
  scalar(p, "uPlanetLight", s.giantLight);
  scalar(p, "uEclipse", s.sunVisibility);
  // Stable exposure driven by daylight, not by passing bright bodies (no
  // pumping).
  double daylight = std::clamp((s.sunLocal.y + .18) / .30, 0., 1.);
  daylight = daylight * daylight * (3 - 2 * daylight);
  scalar(p, "uExposure",
         std::exp(std::log(18.) * (1 - daylight) + std::log(12.) * daylight));
  bind(p, 0, "uSky", sky.texture);
  bind(p, 1, "uTrans", trans.texture);
}
void Renderer::render(int w, int h, double seconds, double day, float opacity,
                      bool drawBodies) {
  if (w <= 0 || h <= 0)
    return;
  const GLState state;
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  auto s = sceneAt(seconds, day);
  if (std::abs(seconds - lastSky) >= day / 7200. || day != lastDay) {
    glBindFramebuffer(GL_FRAMEBUFFER, sky.framebuffer);
    glViewport(0, 0, sky.width, sky.height);
    glUseProgram(skyProgram);
    bind(skyProgram, 0, "uTrans", trans.texture);
    bind(skyProgram, 1, "uMultiple", multi.texture);
    vector(skyProgram, "uSun", s.sunLocal);
    scalar(skyProgram, "uEclipse", s.sunVisibility);
    quad(skyProgram);
    lastSky = seconds;
    lastDay = day;
  }
  target(scene, w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, scene.framebuffer);
  glViewport(0, 0, w, h);
  common(backgroundProgram, s, w, h, seconds);
  quad(backgroundProgram);
  common(starProgram, s, w, h, seconds);
  float matrix[9] = {float(s.east.x), float(s.up.x), float(s.north.x),
                     float(s.east.y), float(s.up.y), float(s.north.y),
                     float(s.east.z), float(s.up.z), float(s.north.z)};
  glUniformMatrix3fv(glGetUniformLocation(starProgram, "uStarFrame"), 1,
                     GL_FALSE, matrix);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  glBindVertexArray(starVao);
  glDrawArrays(GL_POINTS, 0, stars);
  glDisable(GL_BLEND);
  std::vector<int> order;
  for (int i = 0; i < 21; ++i)
    if (i != 1)
      order.push_back(i);
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return length(s.bodies[a].position - s.observer) >
           length(s.bodies[b].position - s.observer);
  });
  for (int idx : order) {
    if (!drawBodies)
      break;
    const auto &b = s.bodies[idx];
    const Vec3 center = s.local(b.position - s.observer);
    const double z = dot(center, s.forward), tanFov = std::tan(29 * pi / 180);
    if (z <= b.radius)
      continue;
    const double cx = dot(center, s.right) / z / tanFov * h * .5 + w * .5,
                 cy = dot(center, s.cameraUp) / z / tanFov * h * .5 + h * .5;
    const double radius =
        b.radius / (z - b.radius) / tanFov * h * .5 * 1.5 + 2.;
    const int x = std::max(0, int(std::floor(cx - radius))),
              y = std::max(0, int(std::floor(cy - radius)));
    const int x2 = std::min(w, int(std::ceil(cx + radius))),
              y2 = std::min(h, int(std::ceil(cy + radius)));
    if (x >= x2 || y >= y2)
      continue;
    common(bodyProgram, s, w, h, seconds);
    glUniform4f(glGetUniformLocation(bodyProgram, "uBody"), center.x, center.y,
                center.z, b.radius);
    integer(bodyProgram, "uMaterial", b.material);
    scalar(bodyProgram, "uSpin", std::remainder(b.spin, 2 * pi));
    vector(bodyProgram, "uAxisX", s.local({1, 0, 0}));
    vector(bodyProgram, "uAxisY", s.local({0, 1, 0}));
    vector(bodyProgram, "uAxisZ", s.local({0, 0, 1}));
    bind(bodyProgram, 2, "uGiant", giant);
    std::vector<float> occluders;
    for (int j = 0; j < 21; ++j)
      if (j != idx) {
        Vec3 delta = s.bodies[j].position - b.position;
        double along = dot(delta, s.sun);
        double perpendicular = length(delta - s.sun * along);
        if (along > 0 &&
            perpendicular < b.radius + s.bodies[j].radius + along * sunRadius) {
          Vec3 p = s.local(s.bodies[j].position - s.observer);
          occluders.insert(occluders.end(), {float(p.x), float(p.y), float(p.z),
                                             float(s.bodies[j].radius)});
        }
      }
    integer(bodyProgram, "uOccluderCount", occluders.size() / 4);
    if (!occluders.empty())
      glUniform4fv(glGetUniformLocation(bodyProgram, "uOccluders"),
                   occluders.size() / 4, occluders.data());
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, x2 - x, y2 - y);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    quad(bodyProgram);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, state.framebuffer);
  glViewport(0, 0, w, h);
  common(postProgram, s, w, h, seconds);
  bind(postProgram, 2, "uScene", scene.texture);
  bind(postProgram, 3, "uMountains", mountains);
  bind(postProgram, 4, "uNoise", noise);
  scalar(postProgram, "uOpacity", opacity);
  quad(postProgram);
}
void savePng(const std::string &path, int w, int h) {
  std::vector<unsigned char> pixels(w * h * 4), flipped(w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  for (int y = 0; y < h; ++y)
    std::copy_n(pixels.data() + y * w * 4, w * 4,
                flipped.data() + (h - y - 1) * w * 4);
  png_image img{};
  img.version = PNG_IMAGE_VERSION;
  img.width = w;
  img.height = h;
  img.format = PNG_FORMAT_RGBA;
  if (!png_image_write_to_file(&img, path.c_str(), 0, flipped.data(), 0,
                               nullptr))
    throw std::runtime_error(img.message);
}
} // namespace observatory
