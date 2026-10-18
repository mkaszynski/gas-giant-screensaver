#include "renderer.hpp"
#include "sky_catalog.hpp"
#include "sky_texture.hpp"
#include <GLES2/gl2ext.h>
#ifndef __EMSCRIPTEN__
#include <GLES3/gl31.h>
#endif
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <png.h>
#include <random>
#include <sstream>
#include <stdexcept>
#ifdef OBSERVATORY_WEATHER_PROFILE
#include "weather_profile.hpp"
#endif
namespace observatory {
#ifdef OBSERVATORY_WEATHER_PROFILE
static void weatherProfileStage(int index) {
  if (index)
    glEndQuery(GL_TIME_ELAPSED_EXT);
  glBeginQuery(GL_TIME_ELAPSED_EXT, weatherProfileQueries[index]);
}
#define WEATHER_PROFILE_STAGE(index) weatherProfileStage(index)
#else
#define WEATHER_PROFILE_STAGE(index) ((void)0)
#endif
#ifdef OBSERVATORY_SKY_PROFILE
extern GLuint skyProfileQueries[2];
#endif
namespace {
// Keep faint sky/emission radiance out of half-float's coarse subnormal range.
// The post pass reverses this scale before physical display exposure.
constexpr float sceneStorageScale = 4096;
// Restore the host lock renderer's state, including on shader/asset exceptions.
struct GLState {
  GLint framebuffer = 0, viewport[4]{}, vao = 0, program = 0, buffer = 0,
        active = 0, renderbuffer = 0, depthFunction = 0;
  GLboolean depthWrite = GL_TRUE;
  GLfloat clearDepth = 1, clearColor[4]{};
  GLint readFramebuffer = 0;
  GLint srcRGB = 0, dstRGB = 0, srcAlpha = 0, dstAlpha = 0, scissorBox[4]{};
  bool blend = false, depth = false, scissor = false;
  GLState() {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
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
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
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
GLuint linkProgram(std::initializer_list<GLuint> shaders) {
  GLuint p = glCreateProgram();
  for (auto shader : shaders)
    glAttachShader(p, shader);
  glLinkProgram(p);
  for (auto shader : shaders)
    glDeleteShader(shader);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[8192];
    glGetProgramInfoLog(p, sizeof(log), nullptr, log);
    glDeleteProgram(p);
    throw std::runtime_error(log);
  }
  return p;
}
#ifndef __EMSCRIPTEN__
struct ImageBindings {
  struct Binding {
    GLint texture, level, layered, layer, access, format;
  };
  std::array<Binding, 2> saved{};
  ImageBindings() {
    for (GLuint i = 0; i < saved.size(); ++i) {
      auto &s = saved[i];
      glGetIntegeri_v(GL_IMAGE_BINDING_NAME, i, &s.texture);
      glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL, i, &s.level);
      glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED, i, &s.layered);
      glGetIntegeri_v(GL_IMAGE_BINDING_LAYER, i, &s.layer);
      glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS, i, &s.access);
      glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, i, &s.format);
    }
  }
  ~ImageBindings() {
    for (GLuint i = 0; i < saved.size(); ++i) {
      auto &s = saved[i];
      glBindImageTexture(i, s.texture, s.level, s.layered, s.layer, s.access,
                         s.format);
    }
  }
};
#endif
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
  // Weather is a construction-time option. Remove its unreachable branches
  // from the large composite shader instead of retaining a uniform branch.
  if (fragment == "post.frag")
    fragmentSource.insert(
        fragmentSource.find('\n') + 1,
        std::string("#define OBSERVATORY_WEATHER_MODE ") +
            (surfaceOptions.enabled && surfaceOptions.weather ? "1\n" : "0\n"));
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
      fragment == "emission.frag" || fragment == "meteors.frag") {
    fragmentSource.insert(fragmentSource.find('\n') + 1,
                          "uniform highp float uSceneScale;\n");
    const auto main = fragmentSource.find("void main()");
    if (main == std::string::npos)
      throw std::runtime_error("Missing scene shader entry point");
    fragmentSource.replace(main, 11, "void scenePixel()");
    fragmentSource += "\nvoid "
                      "main(){scenePixel();color.rgb=min(color.rgb*uSceneScale,"
                      "vec3(60000.));}\n";
  }
  GLuint vs = compile(GL_VERTEX_SHADER, shader(vertex)),
         fs = compile(GL_FRAGMENT_SHADER, fragmentSource);
  GLuint p = linkProgram({vs, fs});
  programs.push_back(p);
  return p;
}
GLuint Renderer::createWeatherComputeProgram(bool calm) {
#ifndef __EMSCRIPTEN__
  GLint major = 0, minor = 0, images = 0, samplers = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);
  const std::string version(
      reinterpret_cast<const char *>(glGetString(GL_VERSION)));
  const bool es = version.starts_with("OpenGL ES");
  if (es ? (major < 3 || (major == 3 && minor < 1))
         : (major < 4 || (major == 4 && minor < 3)))
    return 0;
  // The coordinate cache needs full float render targets. Half-float-only
  // implementations keep the original fragment renderer.
  if (es) {
    bool floatingColor = false;
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i)
      floatingColor |= std::string(reinterpret_cast<const char *>(glGetStringi(
                           GL_EXTENSIONS, i))) == "GL_EXT_color_buffer_float";
    if (!floatingColor)
      return 0;
  }
  glGetIntegerv(GL_MAX_COMPUTE_IMAGE_UNIFORMS, &images);
  glGetIntegerv(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS, &samplers);
  if (images < 2 || samplers < 16)
    return 0;
  auto source = shader("weather.frag");
  const std::string versionLine = "#version 300 es\n";
  if (!source.starts_with(versionLine))
    throw std::runtime_error(
        "Weather shader requires a GLSL ES 300 entry point");
  // Compile the same solver and entry point. Keeping this transformation at
  // the entry-point boundary avoids maintaining a second cloud algorithm.
  source.replace(0, versionLine.size(),
                 "#version 310 es\n#define OBSERVATORY_WEATHER_COMPUTE\n"
                 "#define main weatherPixel\n");
  if (calm)
    source.insert(source.find('\n') + 1, "#define OBSERVATORY_WEATHER_CALM\n");
  source += R"(
#undef main
layout(local_size_x=16,local_size_y=4)in;
uniform highp sampler2D uWeatherPixels;
layout(rgba16f,binding=0)writeonly uniform highp image2D uWeatherImage;
layout(rgba16f,binding=1)writeonly uniform highp image2D uWeatherSkyImage;
void main(){
 ivec2 p=ivec2(gl_GlobalInvocationID.xy);
 if(any(greaterThanEqual(p,ivec2(uWeatherResolution))))return;
 vec4 pixel=texelFetch(uWeatherPixels,p,0);
 uv=pixel.rg;
 weatherCachePixel=pixel.ba;
 weatherPixel();
 imageStore(uWeatherImage,p,color);
 // Only mixed MSAA pixels consume this attachment, including manual meteors.
 // Newly exposed mixed pixels are written in this dispatch before either use.
 if(weatherNeedsSkyOutput)imageStore(uWeatherSkyImage,p,skyWeather);
}
)";
  const GLuint p = linkProgram({compile(GL_COMPUTE_SHADER, source)});
  programs.push_back(p);
  return p;
#else
  (void)calm;
  return 0;
#endif
}
void Renderer::release(Target &t) {
  if (t.depth)
    glDeleteRenderbuffers(1, &t.depth);
  if (t.framebuffer)
    glDeleteFramebuffers(1, &t.framebuffer);
  if (t.texture)
    glDeleteTextures(1, &t.texture);
  if (t.skyTexture)
    glDeleteTextures(1, &t.skyTexture);
  t = {};
}
void Renderer::target(Target &t, int w, int h, bool depth, bool skyLayer,
                      GLenum format, bool immutable) {
  if (t.width == w && t.height == h && t.format == format &&
      t.immutable == immutable)
    return;
  release(t);
  t.width = w;
  t.height = h;
  t.format = format;
  t.immutable = immutable;
  glGenTextures(1, &t.texture);
  glBindTexture(GL_TEXTURE_2D, t.texture);
  if (immutable)
    glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
  else
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, GL_RGBA, GL_HALF_FLOAT,
                 nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &t.framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, t.framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         t.texture, 0);
  if (skyLayer) {
    glGenTextures(1, &t.skyTexture);
    glBindTexture(GL_TEXTURE_2D, t.skyTexture);
    if (immutable)
      glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, w, h);
    else
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA,
                   GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                           t.skyTexture, 0);
    const GLenum buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, buffers);
  }
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
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  if (file == "terrain/geometry.png") {
    std::vector<float> geometry(pixels.size());
    for (size_t i = 0; i < pixels.size(); i += 4) {
      geometry[i] = (pixels[i] * 256. + pixels[i + 1]) / 1000.;
      geometry[i + 1] = 0.;
      geometry[i + 2] = pixels[i + 2] / 255.f;
      geometry[i + 3] = pixels[i + 3] / 255.f;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, img.width, img.height, 0,
                 GL_RGBA, GL_FLOAT, geometry.data());
  } else {
    glTexImage2D(GL_TEXTURE_2D, 0,
                 (file == "terrain/albedo.png" ? GL_SRGB8_ALPHA8 : GL_RGBA8),
                 img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels.data());
  }
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                  file == "giant.png" ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (file.starts_with("terrain/")) {
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
GLuint Renderer::galaxyTexture() {
  GLuint texture;
  glGenTextures(1, &texture);
  textures.push_back(texture);
  glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
  const char *faces[] = {"px", "nx", "py", "ny", "pz", "nz"};
  unsigned size = 0;
  bool cubeCompressed = false;
  GLint unpackAlignment = 0;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
  GLint extensions = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
  bool compressed = false;
  for (GLint i = 0; i < extensions; ++i)
    if (std::string(reinterpret_cast<const char *>(glGetStringi(
            GL_EXTENSIONS, i))) == "GL_EXT_texture_compression_bptc")
      compressed = starfield.compressed;
  for (int face = 0; face < 6; ++face) {
    const auto file = root + "/assets/sky/galaxy-" + faces[face] + ".ggsky";
    const auto mips = loadSkyTexture(file);
    if (size && (size != mips.front().size ||
                 cubeCompressed != mips.front().compressed))
      throw std::runtime_error("Inconsistent galaxy cube format: " + file);
    size = mips.front().size;
    cubeCompressed = mips.front().compressed;
    for (unsigned level = 0; level < mips.size(); ++level) {
      const auto &mip = mips[level];
      if (compressed && mip.compressed) {
        glCompressedTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level,
                               GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT,
                               mip.size, mip.size, 0, mip.blocks.size(),
                               mip.blocks.data());
      } else {
        const auto pixels =
            mip.compressed ? decodeSkyMip(mip) : std::vector<std::uint16_t>{};
        // RGB half-float rows at the 1x1 mip contain six bytes.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGB16F,
                     mip.size, mip.size, 0, GL_RGB, GL_HALF_FLOAT,
                     mip.compressed
                         ? static_cast<const void *>(pixels.data())
                         : static_cast<const void *>(mip.blocks.data()));
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
      }
    }
  }
  // Model radiance and all mipmaps remain HDR and linear, including the
  // portable decoder path. No display exposure or 8-bit quantization is baked.
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  for (GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
    glTexParameteri(GL_TEXTURE_CUBE_MAP, wrap, GL_CLAMP_TO_EDGE);
  return texture;
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
bool Renderer::needsFloatingWeatherNoise() {
#ifdef __EMSCRIPTEN__
  // WebGPU has no synchronous readback. Its browser backend uses the precise
  // floating-point noise field without running the WebGL interpolation probe.
  if (std::string(reinterpret_cast<const char *>(glGetString(GL_RENDERER))) ==
      "Observatory WebGPU")
    return true;
#endif
  GLuint input = 0, output = 0, fbo = 0;
  const unsigned char ramp[] = {64, 64, 64, 255, 96, 96, 96, 255};
  glGenTextures(1, &input);
  glActiveTexture(GL_TEXTURE15);
  glBindTexture(GL_TEXTURE_3D, input);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 2, 1, 1, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, ramp);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  for (auto axis : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
    glTexParameteri(GL_TEXTURE_3D, axis, GL_CLAMP_TO_EDGE);
  glGenTextures(1, &output);
  glBindTexture(GL_TEXTURE_2D, output);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         output, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error("Weather filter probe framebuffer unavailable");
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glViewport(0, 0, 64, 1);
  GLuint probe = program("weather-filter-probe.frag");
  glUseProgram(probe);
  integer(probe, "uProbe", 15);
  quad(probe);
  std::array<unsigned char, 64 * 4> pixels{};
  GLint packed = 0;
  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packed);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glReadPixels(0, 0, 64, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glBindBuffer(GL_PIXEL_PACK_BUFFER, packed);
  int error = 0;
  for (size_t i = 0; i < pixels.size(); i += 4)
    error = std::max(error, int(pixels[i]));
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &input);
  glDeleteTextures(1, &output);
  return error > 40;
}
Renderer::Renderer(std::string dir, StarfieldOptions options,
                   SurfaceOptions travel)
    : root(std::move(dir)), starfield(options), surfaceOptions(travel) {
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
  if (surfaceOptions.enabled) {
    surface = std::make_unique<SurfaceRenderer>(
        root, surfaceOptions.reflections, surfaceOptions.culling,
        surfaceOptions.waterLod, surfaceOptions.weather);
    if (surfaceOptions.weather) {
      weatherProgram = program("weather.frag");
      if (surfaceOptions.weatherCompute) {
        try {
          const GLuint compute = createWeatherComputeProgram();
          if (compute) {
            weatherPixelProgram = program("weather-pixels.frag");
            weatherComputeProgram = compute;
          }
        } catch (const std::exception &error) {
          std::cerr << "Weather compute unavailable; using fragment shader: "
                    << error.what() << '\n';
        }
      }
      if (weatherComputeProgram) {
        try {
          weatherCalmComputeProgram = createWeatherComputeProgram(true);
        } catch (const std::exception &error) {
          std::cerr << "Calm weather specialization unavailable; using full "
                       "compute shader: "
                    << error.what() << '\n';
        }
      }
      weatherLightProgram = program("weather-light.frag");
      glGenTextures(1, &weatherLightTexture);
      textures.push_back(weatherLightTexture);
      glBindTexture(GL_TEXTURE_3D, weatherLightTexture);
      glTexImage3D(GL_TEXTURE_3D, 0, GL_RG16F, 128, 128, 32, 0, GL_RG,
                   GL_HALF_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      for (auto axis :
           {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
        glTexParameteri(GL_TEXTURE_3D, axis, GL_CLAMP_TO_EDGE);
      glGenFramebuffers(1, &weatherLightFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, weatherLightFbo);
      glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                weatherLightTexture, 0, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Cloud light cache framebuffer unavailable");
      floatingNoise = needsFloatingWeatherNoise();
      auto volume = weatherNoise();
      glGenTextures(1, &weatherNoiseTexture);
      textures.push_back(weatherNoiseTexture);
      glBindTexture(GL_TEXTURE_3D, weatherNoiseTexture);
      // Keep the compact cache when measured interpolation is sufficiently
      // precise. Quantized software filtering needs a floating-point cache.
      if (floatingNoise) {
        std::vector<float> filtered(volume.size());
        std::transform(volume.begin(), volume.end(), filtered.begin(),
                       [](uint8_t value) { return value / 255.f; });
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, weatherNoiseSize,
                     weatherNoiseSize, weatherNoiseSize, 0, GL_RGBA, GL_FLOAT,
                     filtered.data());
      } else {
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, weatherNoiseSize,
                     weatherNoiseSize, weatherNoiseSize, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, volume.data());
      }
      glGenerateMipmap(GL_TEXTURE_3D);
      glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER,
                      GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      for (auto axis :
           {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
        glTexParameteri(GL_TEXTURE_3D, axis, GL_REPEAT);
    }
    unsigned char empty[4]{};
    glGenTextures(1, &mountains);
    textures.push_back(mountains);
    glBindTexture(GL_TEXTURE_2D, mountains);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 empty);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    terrainNormal = terrainGeometry = horizonA = horizonB = mountains;
  } else {
    mountains = imageTexture("terrain/albedo.png");
    terrainNormal = imageTexture("terrain/normal.png");
    terrainGeometry = imageTexture("terrain/geometry.png");
    horizonA = imageTexture("terrain/horizon-a.png");
    horizonB = imageTexture("terrain/horizon-b.png");
  }
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
  // Float filtering retains subframe cloud interpolation at grazing angles.
  std::vector<float> cloudNoise(data.size());
  for (size_t i = 0; i < data.size(); ++i)
    cloudNoise[i] = data[i] / 255.f;
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, 256, 256, 0, GL_RED, GL_FLOAT,
               cloudNoise.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  const auto catalog = loadStarCatalog(root + "/assets/sky/stars.bin");
  stars = static_cast<int>(catalog.size());
  if (starfield.galaxy)
    galaxy = galaxyTexture();
  glGenVertexArrays(1, &starVao);
  glBindVertexArray(starVao);
  glGenBuffers(1, &starBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, starBuffer);
  glBufferData(GL_ARRAY_BUFFER, catalog.size() * sizeof(CatalogStar),
               catalog.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(CatalogStar), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(CatalogStar),
                        reinterpret_cast<void *>(4 * sizeof(float)));
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
  release(weather);
  release(skyWeatherCache);
  release(weatherPixels);
  glDeleteFramebuffers(1, &weatherLightFbo);
  for (auto p : programs)
    glDeleteProgram(p);
  for (auto t : textures)
    glDeleteTextures(1, &t);
  glDeleteBuffers(1, &meteorBuffer);
  glDeleteVertexArrays(1, &meteorVao);
  glDeleteBuffers(1, &starBuffer);
  glDeleteBuffers(1, &emissionBuffer);
  glDeleteVertexArrays(1, &emissionVao);
  glDeleteVertexArrays(1, &starVao);
  glDeleteVertexArrays(1, &vao);
}
void Renderer::weatherPixelCoordinates(int w, int h) {
  if (weatherPixels.width == w && weatherPixels.height == h)
    return;
  // Preserve fragment interpolation and cache-cell arithmetic exactly. Mesa
  // varies their last bits across rows, so separable strips are insufficient.
  // Generate this immutable coordinate cache only at construction/resize.
  target(weatherPixels, w, h, false, false, GL_RGBA32F, true);
  glBindTexture(GL_TEXTURE_2D, weatherPixels.texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glViewport(0, 0, w, h);
  glUseProgram(weatherPixelProgram);
  glUniform2f(glGetUniformLocation(weatherPixelProgram, "uWeatherCacheSize"),
              (w + 2) / 3, (h + 1) / 2);
  glBindFramebuffer(GL_FRAMEBUFFER, weatherPixels.framebuffer);
  quad(weatherPixelProgram);
}
void Renderer::common(GLuint p, const Scene &s, int w, int h) {
  glUseProgram(p);
  glUniform2f(glGetUniformLocation(p, "uResolution"), w, h);
  vector(p, "uForward", s.forward);
  vector(p, "uRight", s.right);
  vector(p, "uUp", s.cameraUp);
  vector(p, "uSun", s.sunLocal);
  scalar(p, "uTanFov", std::tan(29 * pi / 180));
  scalar(p, "uObserverHeight", s.altitudeMeters / 1000.);
  scalar(p, "uPlanetLight", s.giantLight);
  scalar(p, "uEclipse", s.sunVisibility);
  // Stable exposure driven by daylight, not by passing bright bodies (no
  // pumping).
  double daylight = std::clamp((s.sunLocal.y + .18) / .30, 0., 1.);
  daylight = daylight * daylight * (3 - 2 * daylight);
  // Adapt only when both the local sky and the visible giant are dim. The
  // Lambert phase integral suppresses adaptation beside a sunlit giant even
  // when the observer is at midnight. This bounded display model is not an
  // emission-energy multiplier or a claim to simulate retinal adaptation.
  const double phaseAngle = std::acos(std::clamp(
      dot(normalized(s.observer - s.bodies[0].position), s.sun), -1., 1.));
  const double phase =
      (std::sin(phaseAngle) + (pi - phaseAngle) * std::cos(phaseAngle)) / pi;
  const double planet =
      .45 * phase *
      std::pow(s.bodies[0].radius / length(s.observer - s.bodies[0].position),
               2);
  const double dark = 1 - daylight * s.sunVisibility;
  const double dimPlanet = std::clamp(1 - planet / .002, 0., 1.);
  const double gain = 1 + 255 * std::pow(dark, 4) * dimPlanet * dimPlanet /
                              (1 + planet * 50000);
  scalar(p, "uDarkGain", gain);
  scalar(p, "uSceneScale", sceneStorageScale);
  // The enormous nearby giant lights the observer far more than Earth's Moon.
  // Its phase controls contrast adaptation: the galaxy emerges when it is dim.
  // This is a bounded display adaptation model, not absolute eye photometry.
  scalar(p, "uCelestialGain", 1. / (1. + planet * 2500.));
  scalar(p, "uExposure",
         gain * std::exp(std::log(18.) * (1 - daylight) +
                         std::log(12.) * daylight));
  bind(p, 0, "uSky", sky.texture);
  bind(p, 1, "uTrans", trans.texture);
}
void Renderer::weatherUniforms(GLuint p, const Scene &s) {
  integer(p, "uWeatherEnabled", bool(weatherProgram));
  glActiveTexture(GL_TEXTURE15);
  glBindTexture(GL_TEXTURE_3D, weatherNoiseTexture);
  integer(p, "uWeatherNoise", 15);
  if (!weatherProgram)
    return;
  auto &v = weatherState;
  vector(p, "uWeatherOffset", v.noiseOffset);
  vector(p, "uWeatherRegionalOffset", v.regionalOffset);
  vector(p, "uWeatherCirrusOffset", v.cirrusOffset);
  vector(p, "uWeatherPlanetDirection", normalized(s.local(-s.observer)));
  glUniform4f(glGetUniformLocation(p, "uWeatherKind"), v.cover, v.storm,
              v.stratus, v.cirrus);
  glUniform4f(glGetUniformLocation(p, "uWeatherLayer"), v.base, v.thickness,
              v.fog, v.fogTop);
  glUniform4f(glGetUniformLocation(p, "uWeatherFlash"), v.flash.x, v.flash.y,
              v.flash.z, v.flashRadiance);
  scalar(p, "uWeatherRain", v.rain);
  auto east = geographicEast({s.latitude, s.longitude});
  auto up = geographicUp({s.latitude, s.longitude});
  auto north = geographicNorth({s.latitude, s.longitude});
  float frame[9] = {float(east.x),  float(east.y),  float(east.z),
                    float(up.x),    float(up.y),    float(up.z),
                    float(north.x), float(north.y), float(north.z)};
  glUniformMatrix3fv(glGetUniformLocation(p, "uWeatherFrame"), 1, GL_FALSE,
                     frame);
}
void Renderer::cloudUniforms(GLuint p, const Scene &s, double seconds) {
  weatherUniforms(p, s);
  // Reduce each octave separately at the repeating noise texture's period.
  // A single wrapped clock cannot preserve noninteger octave frequencies.
  // Upload small offsets so Unix-scale time never loses subframe precision
  // in a float shader. Same five texture samples and no new render pass.
  const double frequencies[] = {1., 2.01, 4.03, 8.07, .045};
  float offsets[10];
  double cloudEast = surface ? surfaceRadius * s.longitude / 1000. * .85 : 0.;
  double cloudNorth = surface ? surfaceRadius * s.latitude / 1000. * 1.9 : 0.;
  scalar(p, "uCloudLongitudeScale", surface ? 1. / std::cos(s.latitude) : 1.);
  for (int i = 0; i < 5; ++i) {
    offsets[2 * i] =
        std::remainder((seconds * .007 + cloudEast) * frequencies[i], 256.);
    offsets[2 * i + 1] =
        std::remainder((seconds * .003 + cloudNorth) * frequencies[i], 256.);
  }
  glUniform2fv(glGetUniformLocation(p, "uCloudOffset[0]"), 5, offsets);
  bind(p, 8, "uNoise", noise);
}
void Renderer::rings(GLuint p, const Scene &s, bool enabled) {
  integer(p, "uRingsEnabled", enabled);
  vector(p, "uRingCenter", s.local(-s.observer));
  vector(p, "uRingNormal", s.local(giantPole()));
  glUniform2f(glGetUniformLocation(p, "uRingBounds"), ringInner, ringOuter);
  bind(p, 5, "uRingProfile", ringProfile);
}
void Renderer::render(int w, int h, double seconds, double day, float opacity,
                      bool drawBodies, bool drawRings, EmissionFrame emission,
                      double weatherSeconds) {
  if (surface) {
    auto s = travelScene(surface->world(), seconds,
                         seconds * surfaceOptions.speed + surfaceOptions.offset,
                         day, drawRings);
    renderScene(w, h, s, seconds, day, opacity, drawBodies, drawRings, emission,
                weatherSeconds);
  } else {
    renderScene(w, h, sceneAt(seconds, day, drawRings), seconds, day, opacity,
                drawBodies, drawRings, emission, weatherSeconds);
  }
}
void Renderer::renderScene(int w, int h, const Scene &s, double seconds,
                           double day, float opacity, bool drawBodies,
                           bool drawRings, EmissionFrame emission,
                           double weatherSeconds) {
  if (w <= 0 || h <= 0)
    return;
  if (surface)
    surfaceLocation = {s.latitude, s.longitude};
  if (weatherProgram) {
    double clock = weatherSeconds >= 0
                       ? weatherSeconds
                       : (emission.seconds >= 0 ? emission.seconds : seconds);
    weatherState =
        weatherAt(weatherClimate(surface->world(), surfaceLocation),
                  surfaceLocation, clock, emission.exposure, weatherPreset);
  }
  const GLState state;
  WEATHER_PROFILE_STAGE(0);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  if (std::abs(seconds - lastSky) >= day / 7200. || day != lastDay ||
      std::abs(s.sunVisibility - lastVisibility) > .01 ||
      length(s.sunLocal - lastSun) > .01 || drawRings != lastRings ||
      std::abs(s.altitudeMeters - lastAltitude) > 2.) {
    glBindFramebuffer(GL_FRAMEBUFFER, sky.framebuffer);
    glViewport(0, 0, sky.width, sky.height);
    glUseProgram(skyProgram);
    bind(skyProgram, 0, "uTrans", trans.texture);
    bind(skyProgram, 1, "uMultiple", multi.texture);
    vector(skyProgram, "uSun", s.sunLocal);
    scalar(skyProgram, "uEclipse", s.sunVisibility);
    scalar(skyProgram, "uObserverHeight", s.altitudeMeters / 1000.);
    lastAltitude = s.altitudeMeters;
    quad(skyProgram);
    lastSky = seconds;
    lastDay = day;
    lastVisibility = s.sunVisibility;
    lastSun = s.sunLocal;
    lastRings = drawRings;
  }
  WEATHER_PROFILE_STAGE(1);
  target(scene, w, h, true);
  glBindFramebuffer(GL_FRAMEBUFFER, scene.framebuffer);
  glViewport(0, 0, w, h);
  glDepthMask(GL_TRUE);
  glClearDepthf(1);
  glClear(GL_DEPTH_BUFFER_BIT);
  float matrix[9] = {float(s.east.x), float(s.up.x), float(s.north.x),
                     float(s.east.y), float(s.up.y), float(s.north.y),
                     float(s.east.z), float(s.up.z), float(s.north.z)};
#ifdef OBSERVATORY_SKY_PROFILE
  glBeginQuery(GL_TIME_ELAPSED_EXT, skyProfileQueries[0]);
#endif
  common(backgroundProgram, s, w, h);
  glUniformMatrix3fv(glGetUniformLocation(backgroundProgram, "uStarFrame"), 1,
                     GL_FALSE, matrix);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_CUBE_MAP, galaxy);
  integer(backgroundProgram, "uGalaxy", 2);
  integer(backgroundProgram, "uGalaxyEnabled", starfield.galaxy);
  quad(backgroundProgram);
#ifdef OBSERVATORY_SKY_PROFILE
  glEndQuery(GL_TIME_ELAPSED_EXT);
  glBeginQuery(GL_TIME_ELAPSED_EXT, skyProfileQueries[1]);
#endif
  if (starfield.stars) {
    common(starProgram, s, w, h);
    glUniformMatrix3fv(glGetUniformLocation(starProgram, "uStarFrame"), 1,
                       GL_FALSE, matrix);
    const double sigma = std::max(.4, .55 * h / 1080.);
    scalar(starProgram, "uStarSigma", sigma);
    scalar(starProgram, "uStarFluxScale", .8 * std::pow(h / 1080., 2));
    scalar(starProgram, "uStarSize", 2 * std::ceil(4 * sigma));
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBindVertexArray(starVao);
    glDrawArrays(GL_POINTS, 0, stars);
    glDisable(GL_BLEND);
  }
#ifdef OBSERVATORY_SKY_PROFILE
  glEndQuery(GL_TIME_ELAPSED_EXT);
#endif
  std::vector<float> ringOccluders;
  if (drawBodies && drawRings) {
    common(ringProgram, s, w, h);
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
      common(bodyShader, s, w, h);
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
  drawMeteors(s, w, h, day);
  GLuint surfaceTexture = 0;
  WEATHER_PROFILE_STAGE(2);
  if (surface)
    surfaceTexture = surface->render(s, surfaceLocation, w, h, seconds, day,
                                     scene.texture, [&](GLuint p) {
                                       common(p, s, w, h);
                                       cloudUniforms(p, s, seconds);
                                     });
  WEATHER_PROFILE_STAGE(3);
  if (weatherProgram) {
    // Cache broad solar attenuation in a small geographic volume. Fine view
    // extinction stays at display resolution; repeated up-sun probes do not.
    if (s.sunLocal.y > -.08 && s.sunVisibility > 0.) {
      glBindFramebuffer(GL_FRAMEBUFFER, weatherLightFbo);
      glViewport(0, 0, 128, 128);
      common(weatherLightProgram, s, w, h);
      weatherUniforms(weatherLightProgram, s);
      for (int layer = 0; layer < 32; ++layer) {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  weatherLightTexture, 0, layer);
        scalar(weatherLightProgram, "uWeatherCacheSlice", (layer + .5) / 32.);
        quad(weatherLightProgram);
      }
    }
    WEATHER_PROFILE_STAGE(4);
    // Preserve vertical sampling of long grazing columns. Wider horizontal
    // cells remove redundant smooth-sky rays without coarsening the horizon.
    if (surfaceOptions.weatherCache)
      target(skyWeatherCache, (w + 2) / 3, (h + 1) / 2);
    // Ground and partial MSAA pixels retain exact, display-resolution paths.
    target(weather, w, h, false, true, GL_RGBA16F, weatherComputeProgram != 0);
    if (weatherComputeProgram)
      weatherPixelCoordinates(w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, weather.framebuffer);
    glViewport(0, 0, weather.width, weather.height);
    auto prepareWeather = [&](GLuint p) {
      common(p, s, w, h);
      weatherUniforms(p, s);
      glActiveTexture(GL_TEXTURE12);
      glBindTexture(GL_TEXTURE_3D, weatherLightTexture);
      integer(p, "uWeatherLightCache", 12);
      glUniform2f(glGetUniformLocation(p, "uWeatherResolution"), weather.width,
                  weather.height);
      scalar(p, "uSurfaceFraction", surface->coverageFraction());
      bind(p, 11, "uSurfaceDistance", surface->distanceTexture());
      bind(p, 9, "uSurface", surfaceTexture);
      bind(p, 14, "uWeatherScene", scene.texture);
      bind(p, 13, "uWeatherSkyCache", mountains);
      integer(p, "uWeatherCacheEnabled", surfaceOptions.weatherCache);
    };
    prepareWeather(weatherProgram);
    if (surfaceOptions.weatherCache) {
      integer(weatherProgram, "uWeatherSkyPass", true);
      glBindFramebuffer(GL_FRAMEBUFFER, skyWeatherCache.framebuffer);
      glViewport(0, 0, skyWeatherCache.width, skyWeatherCache.height);
      quad(weatherProgram);
      bind(weatherProgram, 13, "uWeatherSkyCache", skyWeatherCache.texture);
    }
    integer(weatherProgram, "uWeatherSkyPass", false);
    WEATHER_PROFILE_STAGE(5);
    glBindFramebuffer(GL_FRAMEBUFFER, weather.framebuffer);
    glViewport(0, 0, weather.width, weather.height);
    if (weatherComputeProgram) {
#ifndef __EMSCRIPTEN__
      // Exact zeros only: a growing storm or even a subframe flash must use
      // the full solver immediately. No weather parameter is quantized.
      const GLuint compute = computingCalmWeather() ? weatherCalmComputeProgram
                                                    : weatherComputeProgram;
      prepareWeather(compute);
      bind(compute, 13, "uWeatherSkyCache",
           surfaceOptions.weatherCache ? skyWeatherCache.texture : mountains);
      bind(compute, 2, "uWeatherPixels", weatherPixels.texture);
      const ImageBindings images;
      glBindImageTexture(0, weather.texture, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                         GL_RGBA16F);
      glBindImageTexture(1, weather.skyTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                         GL_RGBA16F);
      glDispatchCompute((w + 15) / 16, (h + 3) / 4, 1);
      // The post pass samples these images; manual meteors also blend into
      // their framebuffer attachments. Both consumers need visibility.
      glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT |
                      GL_FRAMEBUFFER_BARRIER_BIT |
                      GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
#endif
    } else
      quad(weatherProgram);
    revealMeteors(s, w, h, surfaceTexture);
  }
  WEATHER_PROFILE_STAGE(6);
  glBindFramebuffer(GL_FRAMEBUFFER, state.framebuffer);
  glViewport(0, 0, w, h);
  common(postProgram, s, w, h);
  integer(postProgram, "uMovingSurface", bool(surface));
  scalar(postProgram, "uSurfaceFraction",
         surface ? surface->coverageFraction() : 1.);
  bind(postProgram, 9, "uSurface", surfaceTexture ? surfaceTexture : mountains);
  cloudUniforms(postProgram, s, seconds);
  bind(postProgram, 10, "uWeather",
       weather.texture ? weather.texture : mountains);
  bind(postProgram, 11, "uSkyWeather",
       weather.skyTexture ? weather.skyTexture : mountains);
  bind(postProgram, 2, "uScene", scene.texture);
  bind(postProgram, 3, "uMountains", mountains);
  bind(postProgram, 4, "uNoise", noise);
  bind(postProgram, 5, "uTerrainNormal", terrainNormal);
  bind(postProgram, 6, "uTerrainGeometry", terrainGeometry);
  bind(postProgram, 7, "uHorizonA", horizonA);
  bind(postProgram, 8, "uHorizonB", horizonB);
  glUniform2f(glGetUniformLocation(postProgram, "uWaterOffset"),
              std::remainder(seconds * .496, 2 * pi),
              std::remainder(seconds * .157, 2 * pi));
  scalar(postProgram, "uOpacity", opacity);
  quad(postProgram);
#ifdef OBSERVATORY_WEATHER_PROFILE
  glEndQuery(GL_TIME_ELAPSED_EXT);
#endif
}
void Renderer::emissions(const Scene &s, int w, int h, EmissionFrame frame,
                         bool ringsEnabled, double daySeconds) {
  const auto &body = s.bodies[0];
  auto brightness = [](double value, double fallback) {
    return std::isfinite(value) ? std::clamp(value, 0., 1.e6) : fallback;
  };
  const double lightningGain = brightness(frame.lightningBrightness, 5);
  const double auroraGain = brightness(frame.auroraBrightness, 1000);
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
       (frame.lightning && lightningGain > 0
            ? lightningAt(acceleratedLightning(frame, daySeconds))
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
    const Vec3 light = Vec3{1.04, .98, .98} * (peak * lightningGain);
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
                   sigmaAngle = 2800. / 2.354820045 / 71492.;
  constexpr double photonEnergy = 6.62607015e-34 * 299792458. / 650e-9;
  const double baseRadiance = auroraRayleighs(frame.seconds) * 1.e10 /
                              (4 * pi) * photonEnergy / visibleSolarIrradiance;
  if (frame.aurora && auroraGain > 0)
    for (int hemisphere : {-1, 1}) {
      struct Sample {
        Pixel screen, offset;
        Vec3 point, light;
        double extent;
      };
      std::array<Sample, segments + 1> samples{};
      for (int i = 0; i <= segments; ++i) {
        const double longitude = 2 * pi * i / segments;
        // Traveling waves deform the oval slowly in the weather clock,
        // independently of body rotation. No random per-frame displacement.
        const double drift = hemisphere * frame.seconds / 45.;
        const double colat =
            (20 + 2 * std::sin(2 * longitude) + 3 * std::sin(longitude) +
             1.2 * std::sin(5 * longitude - drift) +
             .6 * std::sin(9 * longitude + drift * .73)) *
            pi / 180;
        const double ribbonWidth =
            sigmaAngle * (1 + .18 * std::sin(4 * longitude - drift * .8));
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
                    normalAt(colat + ribbonWidth) * (body.radius + altitude) -
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
                      Vec3{.92, .80, 1.12} *
                          (baseRadiance * path * structure * auroraGain),
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

  common(emissionProgram, s, w, h);
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
