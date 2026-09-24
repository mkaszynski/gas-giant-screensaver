#include "renderer.hpp"
#include <GLES2/gl2ext.h>
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
        active = 0, renderbuffer = 0, depthFunction = 0;
  GLboolean depthWrite = GL_TRUE;
  GLfloat clearDepth = 1;
  GLint srcRGB = 0, dstRGB = 0, srcAlpha = 0, dstAlpha = 0, scissorBox[4]{};
  bool blend = false, depth = false, scissor = false;
  GLState() {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clearDepth);
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
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glDepthFunc(depthFunction);
    glDepthMask(depthWrite);
    glClearDepthf(clearDepth);
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
GLuint Renderer::program(const std::string &fragment, const std::string &vertex,
                         int giantAtmosphere) {
  auto fragmentSource = shader(fragment);
  if (giantAtmosphere)
    fragmentSource.insert(
        fragmentSource.find('\n') + 1,
        std::string("#define GIANT_ATMOSPHERE\n#define GIANT_SLAB_LUT\n") +
            (giantAtmosphere == 1 ? "#define GIANT_INTERIOR\n"
                                  : "#define GIANT_LIMB\n"));
  // Fixed pre-exposure preserves dim visible auroras in the existing RGBA16F
  // target. No larger framebuffer or extra pass. The solar disk is already
  // far above display white; clamp it before half-float storage can overflow.
  if (fragment == "background.frag" || fragment == "body.frag" ||
      fragment == "stars.frag" || fragment == "rings.frag" ||
      fragment == "emission.frag") {
    const auto main = fragmentSource.find("void main()");
    if (main == std::string::npos)
      throw std::runtime_error("Missing scene shader entry point");
    fragmentSource.replace(main, 11, "void scenePixel()");
    fragmentSource +=
        "\nvoid "
        "main(){scenePixel();color.rgb=min(color.rgb*128.,vec3(60000.));}\n";
  }
  GLuint vs = compile(GL_VERTEX_SHADER, shader(vertex)),
         fs = compile(GL_FRAGMENT_SHADER, fragmentSource);
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
  if (t.depth)
    glDeleteRenderbuffers(1, &t.depth);
  if (t.framebuffer)
    glDeleteFramebuffers(1, &t.framebuffer);
  if (t.texture)
    glDeleteTextures(1, &t.texture);
  t = {};
}
void Renderer::target(Target &t, int w, int h, bool depth) {
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
  if (depth) {
    glGenRenderbuffers(1, &t.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, t.depth);
  }
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
  if (file == "mountains.png") {
    // Make the ridge opaque BEFORE minification. Thresholding a filtered
    // alpha value in the shader destroys its subpixel coverage. Premultiply
    // color as well so transparent asset pixels cannot darken the silhouette.
    for (size_t i = 0; i < pixels.size(); i += 4) {
      double coverage = std::clamp((pixels[i + 3] / 255. - .01) / .11, 0., 1.);
      coverage = coverage * coverage * (3 - 2 * coverage);
      for (int channel = 0; channel < 3; ++channel)
        pixels[i + channel] = std::lround(pixels[i + channel] * coverage);
      pixels[i + 3] = std::lround(255 * coverage);
    }
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
  if (file == "mountains.png") {
    const auto *extensions =
        reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    if (extensions &&
        std::string(extensions).find("GL_EXT_texture_filter_anisotropic") !=
            std::string::npos) {
      GLfloat maximum = 1;
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                      std::min(4.f, maximum));
    }
  }
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
  giantProgram = program("body.frag", "fullscreen.vert", 1);
  giantLimbProgram = program("body.frag", "fullscreen.vert", 2);
  ringProgram = program("rings.frag");
  starProgram = program("stars.frag", "stars.vert");
  postProgram = program("post.frag");
  emissionProgram = program("emission.frag", "emission.vert");
  glGenVertexArrays(1, &emissionVao);
  glGenBuffers(1, &emissionBuffer);
  glBindVertexArray(emissionVao);
  glBindBuffer(GL_ARRAY_BUFFER, emissionBuffer);
  for (int i = 0, offset = 0; i < 4; ++i) {
    const int size = i == 0 ? 2 : (i == 3 ? 4 : 3);
    glEnableVertexAttribArray(i);
    glVertexAttribPointer(i, size, GL_FLOAT, GL_FALSE, 12 * sizeof(float),
                          reinterpret_cast<void *>(offset * sizeof(float)));
    offset += size;
  }
  giant = imageTexture("giant.png");
  mountains = imageTexture("mountains.png");
  constexpr int profileWidth = 4096;
  std::vector<float> profile(profileWidth * 2);
  for (int i = 0; i < profileWidth; ++i) {
    const double r =
        ringInner + (ringOuter - ringInner) * (i + .5) / profileWidth;
    profile[i * 2] = ringOpticalDepth(r);
    // Slight particle-albedo variation remains visible where dense bands
    // saturate.
    profile[i * 2 + 1] = .69 +
                         .11 * std::sin(r * 173. + 2. * std::sin(r * 31.)) +
                         .09 * std::sin(r * 1103. + 3. * std::sin(r * 119.)) +
                         .04 * std::sin(r * 3779.);
  }
  glGenTextures(1, &ringProfile);
  textures.push_back(ringProfile);
  glBindTexture(GL_TEXTURE_2D, ringProfile);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, profileWidth, 1, 0, GL_RG, GL_FLOAT,
               profile.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  std::mt19937 rng(0x194bd);
  // Static thin-atmosphere solution: RGB single-scattering integral and
  // its view/total column ratio. The same sample reconstructs extinction.
  // Quadratic coordinates concentrate samples near grazing sunlight.
  constexpr int slabSize = 128;
  auto column = [](double mu) {
    constexpr double h = .0001905;
    const double x = mu / std::sqrt(2 * h);
    const double erfcx = x < 20 ? std::exp(x * x) * std::erfc(x)
                                : (1 - .5 / (x * x) + .75 / std::pow(x, 4)) /
                                      (std::sqrt(pi) * x);
    return std::sqrt(pi / (2 * h)) * erfcx;
  };
  std::vector<float> slab(slabSize * slabSize * 4);
  constexpr double beta[3] = {.00135, .00285, .00675};
  for (int y = 0; y < slabSize; ++y)
    for (int x = 0; x < slabSize; ++x) {
      const double sunColumn = column(std::pow(double(x) / (slabSize - 1), 2));
      const double viewColumn = column(std::pow(double(y) / (slabSize - 1), 2));
      const double sum = sunColumn + viewColumn, weight = viewColumn / sum;
      const int at = (y * slabSize + x) * 4;
      for (int c = 0; c < 3; ++c)
        slab[at + c] = weight * -std::expm1(-beta[c] * sum);
      slab[at + 3] = weight;
    }
  glGenTextures(1, &giantSlab);
  textures.push_back(giantSlab);
  glBindTexture(GL_TEXTURE_2D, giantSlab);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, slabSize, slabSize, 0, GL_RGBA,
               GL_FLOAT, slab.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
  glDeleteBuffers(1, &emissionBuffer);
  glDeleteVertexArrays(1, &emissionVao);
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
void Renderer::rings(GLuint p, const Scene &s, bool enabled) {
  integer(p, "uRingsEnabled", enabled);
  vector(p, "uRingCenter", s.local(-s.observer));
  vector(p, "uRingNormal", s.local(giantPole()));
  glUniform2f(glGetUniformLocation(p, "uRingBounds"), ringInner, ringOuter);
  bind(p, 5, "uRingProfile", ringProfile);
}
void Renderer::render(int w, int h, double seconds, double day, float opacity,
                      bool drawBodies, bool drawRings, EmissionFrame emission) {
  renderScene(w, h, sceneAt(seconds, day, drawRings), seconds, day, opacity,
              drawBodies, drawRings, emission);
}
void Renderer::renderScene(int w, int h, const Scene &s, double seconds,
                           double day, float opacity, bool drawBodies,
                           bool drawRings, EmissionFrame emission) {
  if (w <= 0 || h <= 0)
    return;
  const GLState state;
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  if (std::abs(seconds - lastSky) >= day / 7200. || day != lastDay ||
      std::abs(s.sunVisibility - lastVisibility) > .01 ||
      length(s.sunLocal - lastSun) > .01 || drawRings != lastRings) {
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
    lastVisibility = s.sunVisibility;
    lastSun = s.sunLocal;
    lastRings = drawRings;
  }
  target(scene, w, h, true);
  glBindFramebuffer(GL_FRAMEBUFFER, scene.framebuffer);
  glViewport(0, 0, w, h);
  glDepthMask(GL_TRUE);
  glClearDepthf(1);
  glClear(GL_DEPTH_BUFFER_BIT);
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
  std::vector<float> ringOccluders;
  if (drawBodies && drawRings) {
    common(ringProgram, s, w, h, seconds);
    rings(ringProgram, s, true);
    for (const auto &body : s.bodies) {
      const double along = dot(body.position, s.sun);
      const double perpendicular = length(body.position - s.sun * along);
      if (along + body.radius < -ringOuter ||
          perpendicular >
              ringOuter + body.radius + std::max(0., along) * sunRadius)
        continue;
      const auto p = s.local(body.position - s.observer);
      ringOccluders.insert(
          ringOccluders.end(),
          {float(p.x), float(p.y), float(p.z), float(body.radius)});
    }
    integer(ringProgram, "uRingOccluderCount", ringOccluders.size() / 4);
    glUniform4fv(glGetUniformLocation(ringProgram, "uRingOccluders"),
                 ringOccluders.size() / 4, ringOccluders.data());
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    quad(ringProgram);
    glDisable(GL_BLEND);
  }
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
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
    // Separate programs keep the narrow limb integrator's register pressure
    // out of the much larger cloud-disk draw. Their pixel domains are disjoint.
    for (int pass = 0; pass < (b.material == 0 ? 2 : 1); ++pass) {
      const GLuint bodyShader =
          b.material == 0 ? (pass == 0 ? giantProgram : giantLimbProgram)
                          : bodyProgram;
      common(bodyShader, s, w, h, seconds);
      rings(bodyShader, s, drawRings);
      integer(bodyShader, "uRingOccluderCount", ringOccluders.size() / 4);
      if (!ringOccluders.empty())
        glUniform4fv(glGetUniformLocation(bodyShader, "uRingOccluders"),
                     ringOccluders.size() / 4, ringOccluders.data());
      glUniform4f(glGetUniformLocation(bodyShader, "uBody"), center.x, center.y,
                  center.z, b.radius);
      integer(bodyShader, "uMaterial", b.material);
      scalar(bodyShader, "uSpin", std::remainder(b.spin, 2 * pi));
      const Vec3 axisY = b.pole;
      const Vec3 axisX = normalized(cross(axisY, {0, 0, 1}));
      vector(bodyShader, "uAxisX", s.local(axisX));
      vector(bodyShader, "uAxisY", s.local(axisY));
      vector(bodyShader, "uAxisZ", s.local(cross(axisX, axisY)));
      bind(bodyShader, 2, "uGiant", giant);
      if (b.material == 0)
        bind(bodyShader, 6, "uGiantSlab", giantSlab);
      std::vector<float> occluders;
      for (int j = 0; j < 21; ++j)
        if (j != idx) {
          Vec3 delta = s.bodies[j].position - b.position;
          double along = dot(delta, s.sun);
          double perpendicular = length(delta - s.sun * along);
          if (along > 0 &&
              perpendicular < b.radius * (b.material == 0 ? 1.0023 : 1.) +
                                  s.bodies[j].radius + along * sunRadius) {
            Vec3 p = s.local(s.bodies[j].position - s.observer);
            occluders.insert(occluders.end(),
                             {float(p.x), float(p.y), float(p.z),
                              float(s.bodies[j].radius)});
          }
        }
      integer(bodyShader, "uOccluderCount", occluders.size() / 4);
      if (!occluders.empty())
        glUniform4fv(glGetUniformLocation(bodyShader, "uOccluders"),
                     occluders.size() / 4, occluders.data());
      glEnable(GL_SCISSOR_TEST);
      glScissor(x, y, x2 - x, y2 - y);
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      quad(bodyShader);
      glDisable(GL_BLEND);
      glDisable(GL_SCISSOR_TEST);
    }
  }
  glDisable(GL_DEPTH_TEST);
  if (drawBodies && emission.seconds >= 0)
    emissions(s, w, h, emission, drawRings, day);
  glBindFramebuffer(GL_FRAMEBUFFER, state.framebuffer);
  glViewport(0, 0, w, h);
  common(postProgram, s, w, h, seconds);
  bind(postProgram, 2, "uScene", scene.texture);
  bind(postProgram, 3, "uMountains", mountains);
  bind(postProgram, 4, "uNoise", noise);
  scalar(postProgram, "uOpacity", opacity);
  quad(postProgram);
}
void Renderer::emissions(const Scene &s, int w, int h, EmissionFrame frame,
                         bool ringsEnabled, double daySeconds) {
  const auto &body = s.bodies[0];
  const double focal = h / (2 * std::tan(29 * pi / 180));
  struct Pixel {
    double x, y;
  };
  auto project = [&](Vec3 p) {
    const double z = dot(p, s.forward);
    return Pixel{w * .5 + focal * dot(p, s.right) / z,
                 h * .5 + focal * dot(p, s.cameraUp) / z};
  };
  std::vector<float> vertices;
  vertices.reserve(2 * 256 * 6 * 12 + stormCount * 6 * 12);
  auto vertex = [&](Pixel pixel, Vec3 point, Vec3 light, Vec3 profile,
                    double halfLength = 0.) {
    for (double value : {2 * pixel.x / w - 1, 2 * pixel.y / h - 1, point.x,
                         point.y, point.z, light.x, light.y, light.z, profile.x,
                         profile.y, profile.z, halfLength})
      vertices.push_back(float(value));
  };
  for (const auto &flash :
       (frame.lightning ? lightningAt(acceleratedLightning(frame, daySeconds))
                        : std::vector<LightningFlash>{})) {
    const Vec3 normal = bodyDirection(flash.normal, body);
    const Vec3 world = body.position + normal * body.radius;
    const Vec3 point = s.local(world - s.observer);
    const double mu = dot(normal, normalized(s.observer - world));
    if (mu <= 0 || dot(point, s.forward) <= 0)
      continue;
    const auto center = project(point);
    const double sigma =
        focal * flash.sigmaKm / (71492 * dot(point, s.forward));
    const double extent = 4 * sigma + 1.5;
    if (center.x + extent < 0 || center.x - extent > w ||
        center.y + extent < 0 || center.y - extent > h)
      continue;
    // Gaussian cloud-screen footprint, normalized to upward Lambertian power.
    // mu preserves projected flux without trying to resolve a subpixel bolt.
    const double area = 2 * pi * std::pow(flash.sigmaKm * 1000, 2);
    const double peak =
        flash.powerWatts / (pi * area * visibleSolarIrradiance) * mu;
    const Vec3 light = Vec3{1.04, .98, .98} * peak;
    for (int corner : {0, 1, 2, 2, 1, 3}) {
      const double x = corner & 1 ? extent : -extent;
      const double y = corner & 2 ? extent : -extent;
      vertex({center.x + x, center.y + y}, point, light,
             {x / sigma, y / sigma, 1});
    }
  }
  // A Jupiter-like magnetic geometry is an explicit assumption for this
  // fictional planet. It is attached to the rotating body, not the camera.
  constexpr int segments = 256;
  constexpr double altitude = 120. / 71492.,
                   sigmaAngle = 350. / 2.354820045 / 71492.;
  constexpr double photonEnergy = 6.62607015e-34 * 299792458. / 650e-9;
  const double baseRadiance = auroraRayleighs(frame.seconds) * 1.e10 /
                              (4 * pi) * photonEnergy / visibleSolarIrradiance;
  if (frame.aurora)
    for (int hemisphere : {-1, 1}) {
      struct Sample {
        Pixel screen, offset;
        Vec3 point, light;
        double extent;
      };
      std::array<Sample, segments + 1> samples{};
      for (int i = 0; i <= segments; ++i) {
        const double longitude = 2 * pi * i / segments;
        const double colat =
            (20 + 2 * std::sin(2 * longitude) + 3 * std::sin(longitude)) * pi /
            180;
        const double tilt = (hemisphere > 0 ? 10. : -3.) * pi / 180;
        auto normalAt = [&](double angle) {
          Vec3 n{std::sin(angle) * std::cos(longitude),
                 hemisphere * std::cos(angle),
                 std::sin(angle) * std::sin(longitude)};
          n = {n.x * std::cos(tilt) + n.y * std::sin(tilt),
               -n.x * std::sin(tilt) + n.y * std::cos(tilt), n.z};
          return bodyDirection(n, body);
        };
        const Vec3 normal = normalAt(colat);
        const Vec3 point = s.local(
            body.position + normal * (body.radius + altitude) - s.observer);
        if (dot(point, s.forward) <= .01)
          return;
        const auto center = project(point);
        const auto across = project(
            s.local(body.position +
                    normalAt(colat + sigmaAngle) * (body.radius + altitude) -
                    s.observer));
        const double mu = std::abs(dot(s.local(normal), normalized(-point)));
        // Finite emitting-layer thickness bounds tangent-path enhancement.
        const double path = 1 / std::sqrt(mu * mu + 2 * 35. / 71492.);
        const double structure =
            .75 + .15 * std::sin(5 * longitude + frame.seconds / 43.) +
            .10 * std::sin(13 * longitude - frame.seconds / 29.);
        samples[i] = {center,
                      {across.x - center.x, across.y - center.y},
                      point,
                      Vec3{1.6, .65, .75} * (baseRadiance * path * structure),
                      0};
      }
      // Filter segment endpoints too: short bright tangent sections otherwise
      // disappear between pixels even with perfect cross-arc filtering.
      for (int i = 0; i < segments; ++i) {
        const auto &a = samples[i], &b = samples[i + 1];
        Pixel tangent{b.screen.x - a.screen.x, b.screen.y - a.screen.y};
        const double length = std::hypot(tangent.x, tangent.y);
        if (length < 1.e-6)
          continue;
        tangent.x /= length;
        tangent.y /= length;
        const Pixel side{-tangent.y, tangent.x};
        const double sigma = std::max(
            .0001, .5 * (std::abs(side.x * a.offset.x + side.y * a.offset.y) +
                         std::abs(side.x * b.offset.x + side.y * b.offset.y)));
        const Pixel center{.5 * (a.screen.x + b.screen.x),
                           .5 * (a.screen.y + b.screen.y)};
        for (int corner : {0, 1, 2, 2, 1, 3}) {
          const double across = (corner & 1 ? 1 : -1) * (4 * sigma + 1.5);
          const double along = (corner & 2 ? 1 : -1) * (length * .5 + 1.5);
          vertex({center.x + side.x * across + tangent.x * along,
                  center.y + side.y * across + tangent.y * along},
                 (a.point + b.point) * .5, (a.light + b.light) * .5,
                 {across / sigma, along, 0}, length * .5);
        }
      }
    }

  common(emissionProgram, s, w, h, frame.seconds);
  rings(emissionProgram, s, ringsEnabled);
  std::vector<float> blockers;
  for (const auto &b : s.bodies) {
    const auto point = s.local(b.position - s.observer);
    blockers.insert(blockers.end(), {float(point.x), float(point.y),
                                     float(point.z), float(b.radius)});
  }
  integer(emissionProgram, "uEmissionOccluderCount", blockers.size() / 4);
  glUniform4fv(glGetUniformLocation(emissionProgram, "uEmissionOccluders"),
               blockers.size() / 4, blockers.data());
  glBindVertexArray(emissionVao);
  glBindBuffer(GL_ARRAY_BUFFER, emissionBuffer);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float),
               vertices.data(), GL_STREAM_DRAW);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  glDrawArrays(GL_TRIANGLES, 0, vertices.size() / 12);
  glDisable(GL_BLEND);
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
