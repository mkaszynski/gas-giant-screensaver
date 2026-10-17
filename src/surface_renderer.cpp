#include "surface_renderer.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <png.h>
#include <sstream>
#include <stdexcept>
#include <thread>
namespace observatory {
namespace {
std::string shader(const std::string &root, const std::string &file) {
  std::ifstream f(root + "/shaders/" + file);
  if (!f)
    throw std::runtime_error("Missing surface shader: " + file);
  std::string line, out;
  while (std::getline(f, line)) {
    if (line.starts_with("#include \"")) {
      auto end = line.find('"', 10);
      out += shader(root, line.substr(10, end - 10));
    } else
      out += line + "\n";
  }
  return out;
}
GLuint compile(GLenum type, const std::string &source) {
  GLuint s = glCreateShader(type);
  const char *p = source.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char b[8192];
    glGetShaderInfoLog(s, sizeof(b), nullptr, b);
    glDeleteShader(s);
    throw std::runtime_error(b);
  }
  return s;
}
enum class SurfaceMaterial { general, deepWater, pureLand };
GLuint link(const std::string &root, const char *vert, const char *frag,
            bool reflection = false, bool weather = true,
            SurfaceMaterial material = SurfaceMaterial::general) {
  auto fragment = shader(root, frag);
  fragment.insert(fragment.find('\n') + 1,
                  std::string("#define OBSERVATORY_WEATHER_MODE ") +
                      (weather ? "1\n" : "0\n"));
  if (reflection)
    fragment.insert(fragment.find('\n') + 1, "#define SURFACE_REFLECTION\n");
  if (material == SurfaceMaterial::deepWater)
    fragment.insert(fragment.find('\n') + 1, "#define SURFACE_DEEP_WATER\n");
  if (material == SurfaceMaterial::pureLand)
    fragment.insert(fragment.find('\n') + 1, "#define SURFACE_PURE_LAND\n");
  GLuint v = compile(GL_VERTEX_SHADER, shader(root, vert)),
         f = compile(GL_FRAGMENT_SHADER, fragment), p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char b[8192];
    glGetProgramInfoLog(p, sizeof(b), nullptr, b);
    glDeleteProgram(p);
    throw std::runtime_error(b);
  }
  return p;
}
void vec(GLuint p, const char *n, Vec3 v) {
  glUniform3f(glGetUniformLocation(p, n), v.x, v.y, v.z);
}
void scalar(GLuint p, const char *n, float v) {
  glUniform1f(glGetUniformLocation(p, n), v);
}
void bind(GLuint p, int unit, const char *n, GLuint t) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, t);
  glUniform1i(glGetUniformLocation(p, n), unit);
}
GLuint arrayTexture(const std::vector<std::string> &files, int size) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
  int levels = 1;
  for (int n = size; n > 1; n /= 2)
    ++levels;
  // These are linear numerical fields. Allocate a complete bounded mip chain.
  glTexStorage3D(GL_TEXTURE_2D_ARRAY, levels, GL_RGBA8, size, size,
                 files.size());
  try {
    for (size_t i = 0; i < files.size(); ++i) {
      png_image image{};
      image.version = PNG_IMAGE_VERSION;
      if (!png_image_begin_read_from_file(&image, files[i].c_str()))
        throw std::runtime_error("Missing surface field: " + files[i]);
      if (image.width != unsigned(size) || image.height != unsigned(size)) {
        png_image_free(&image);
        throw std::runtime_error("Invalid surface field dimensions: " +
                                 files[i]);
      }
      image.format = PNG_FORMAT_RGBA;
      std::vector<unsigned char> data(PNG_IMAGE_SIZE(image));
      if (!png_image_finish_read(&image, nullptr, data.data(), 0, nullptr)) {
        const std::string error = image.message;
        png_image_free(&image);
        throw std::runtime_error(error);
      }
      glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, size, size, 1, GL_RGBA,
                      GL_UNSIGNED_BYTE, data.data());
      png_image_free(&image);
    }
  } catch (...) {
    glDeleteTextures(1, &texture);
    throw;
  }
  glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
  return texture;
}
GLuint materialArray(const std::string &root, const char *suffix,
                     bool crowns = false) {
  constexpr const char *names[] = {"rock",   "grass", "sand",
                                   "tundra", "snow",  "soil"};
  std::vector<std::string> files;
  for (int i = 0; i < (crowns ? 8 : 6); ++i)
    files.push_back(crowns ? root + "/assets/world/crown-shadows/" +
                                 std::to_string(i) + ".png"
                           : root + "/assets/world/materials/" + names[i] +
                                 "-" + suffix + ".png");
  return arrayTexture(files, 512);
}
void check() {
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error("Surface framebuffer unavailable");
}
} // namespace
SurfaceRenderer::SurfaceRenderer(const std::string &root, bool reflections,
                                 bool culling, bool waterLod, bool weather)
    : terrain(root + "/assets/world/moon.ggworld"),
      reflectionsEnabled(reflections), cullingEnabled(culling),
      waterLodEnabled(waterLod), weatherEnabled(weather) {
  program = link(root, "surface.vert", "surface.frag", false, weather);
  if (waterLodEnabled) {
    waterProgram = link(root, "surface.vert", "surface.frag", false, weather,
                        SurfaceMaterial::deepWater);
    landProgram = link(root, "surface.vert", "surface.frag", false, weather,
                       SurfaceMaterial::pureLand);
  }
  if (reflectionsEnabled) {
    reflectionProgram = link(root, "surface.vert", "surface.frag", true, weather);
    glGenBuffers(1, &reflectionIndices);
  }
  shadowProgram = link(root, "surface_shadow.vert", "surface_shadow.frag");
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vertices);
  glGenBuffers(1, &indices);
  glGenBuffers(1, &waterIndices);
  glBindBuffer(GL_ARRAY_BUFFER, vertices);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
  glGenBuffers(1, &stagingVertices);
  vertexAttributes();
  png_image img{};
  img.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(
          &img, (root + "/assets/world/material.png").c_str()))
    throw std::runtime_error("Missing surface material");
  img.format = PNG_FORMAT_RGBA;
  std::vector<unsigned char> data(PNG_IMAGE_SIZE(img));
  if (!png_image_finish_read(&img, nullptr, data.data(), 0, nullptr))
    throw std::runtime_error(img.message);
  glGenTextures(1, &material);
  glBindTexture(GL_TEXTURE_2D, material);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data.data());
  png_image_free(&img);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  png_image crowns{};
  crowns.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(
          &crowns, (root + "/assets/world/canopy.png").c_str()))
    throw std::runtime_error("Missing canopy field");
  crowns.format = PNG_FORMAT_RGBA;
  std::vector<unsigned char> crownData(PNG_IMAGE_SIZE(crowns));
  if (!png_image_finish_read(&crowns, nullptr, crownData.data(), 0, nullptr))
    throw std::runtime_error(crowns.message);
  glGenTextures(1, &canopy);
  glBindTexture(GL_TEXTURE_2D, canopy);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, crowns.width, crowns.height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, crownData.data());
  png_image_free(&crowns);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  reliefMaterial =
      arrayTexture({root + "/assets/world/relief-material.png",
                    root + "/assets/world/landforms/arid-material.png",
                    root + "/assets/world/landforms/coastal-material.png"},
                   2048);
  physicalAlbedo = materialArray(root, "albedo");
  physicalNormal = materialArray(root, "normal");
  crownShadows = materialArray(root, "", true);
#ifdef __EMSCRIPTEN__
  // WebGL exposes SAMPLES directly, but not GLES NUM_SAMPLE_COUNTS.
  std::array<GLint, 4> available{};
  glGetInternalformativ(GL_RENDERBUFFER, GL_RGBA16F, GL_SAMPLES,
                        available.size(), available.data());
#else
  GLint counts = 0;
  glGetInternalformativ(GL_RENDERBUFFER, GL_RGBA16F, GL_NUM_SAMPLE_COUNTS, 1,
                        &counts);
  std::vector<GLint> available(counts);
  if (counts)
    glGetInternalformativ(GL_RENDERBUFFER, GL_RGBA16F, GL_SAMPLES, counts,
                          available.data());
#endif
  for (auto v : available)
    if (v <= 4)
      samples = std::max(samples, int(v));
  glGenFramebuffers(4, shadowFbo);
  glGenTextures(4, shadowTex);
  for (int i = 0; i < 4; ++i) {
    glBindTexture(GL_TEXTURE_2D, shadowTex[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 2048, 2048, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                    GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           shadowTex[i], 0);
    GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    check();
  }
}
void SurfaceRenderer::vertexAttributes() {
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vertices);
  for (int i = 0; i < 3; ++i) {
    glEnableVertexAttribArray(i);
    glVertexAttribPointer(
        i, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex),
        reinterpret_cast<void *>(size_t(i) * 3 * sizeof(float)));
  }
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex),
                        reinterpret_cast<void *>(9 * sizeof(float)));
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex),
                        reinterpret_cast<void *>(11 * sizeof(float)));
  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex),
                        reinterpret_cast<void *>(14 * sizeof(float)));
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex),
                        reinterpret_cast<void *>(13 * sizeof(float)));
}
void SurfaceRenderer::releaseTargets() {
  glDeleteFramebuffers(1, &multisample);
  glDeleteFramebuffers(1, &resolved);
  glDeleteRenderbuffers(1, &colorBuffer);
  glDeleteRenderbuffers(1, &depthBuffer);
  glDeleteRenderbuffers(1, &distanceBuffer);
  distanceBuffer = 0;
  glDeleteTextures(1, &texture);
  glDeleteTextures(1, &distanceTextureId);
  distanceTextureId = 0;
  multisample = resolved = colorBuffer = depthBuffer = texture = 0;
}
SurfaceRenderer::~SurfaceRenderer() {
  if (pending.valid())
    pending.wait();
  releaseTargets();
  glDeleteProgram(reflectionProgram);
  glDeleteFramebuffers(1, &reflectionFbo);
  glDeleteTextures(1, &reflectionTexture);
  glDeleteRenderbuffers(1, &reflectionDepth);
  glDeleteBuffers(1, &reflectionIndices);
  glDeleteProgram(waterProgram);
  glDeleteProgram(landProgram);
  glDeleteProgram(program);
  glDeleteProgram(shadowProgram);
  glDeleteBuffers(1, &vertices);
  glDeleteBuffers(1, &stagingVertices);
  glDeleteBuffers(1, &indices);
  glDeleteBuffers(1, &waterIndices);
  glDeleteVertexArrays(1, &vao);
  glDeleteTextures(1, &material);
  glDeleteTextures(1, &canopy);
  glDeleteTextures(1, &reliefMaterial);
  glDeleteTextures(1, &physicalAlbedo);
  glDeleteTextures(1, &physicalNormal);
  glDeleteTextures(1, &crownShadows);
  glDeleteTextures(4, shadowTex);
  glDeleteFramebuffers(4, shadowFbo);
}
void SurfaceRenderer::resize(int w, int h) {
  if (width == w && height == h)
    return;
  releaseTargets();
  width = w;
  height = h;
  glGenFramebuffers(1, &multisample);
  glBindFramebuffer(GL_FRAMEBUFFER, multisample);
  glGenRenderbuffers(1, &colorBuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, colorBuffer);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_RENDERBUFFER, colorBuffer);
  glGenRenderbuffers(1, &depthBuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                   GL_DEPTH_COMPONENT24, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, depthBuffer);
  if (weatherEnabled) {
    glGenRenderbuffers(1, &distanceBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, distanceBuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_R16F, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                              GL_RENDERBUFFER, distanceBuffer);
  }
  const GLenum outputs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(weatherEnabled ? 2 : 1, outputs);
  check();
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &resolved);
  glBindFramebuffer(GL_FRAMEBUFFER, resolved);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  if (weatherEnabled) {
    glGenTextures(1, &distanceTextureId);
    glBindTexture(GL_TEXTURE_2D, distanceTextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, w, h, 0, GL_RED, GL_HALF_FLOAT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                           distanceTextureId, 0);
  }
  glDrawBuffers(weatherEnabled ? 2 : 1, outputs);
  check();
}
void SurfaceRenderer::resizeReflection(int w, int h) {
  if (w == reflectionWidth && h == reflectionHeight)
    return;
  glDeleteFramebuffers(1, &reflectionFbo);
  glDeleteTextures(1, &reflectionTexture);
  glDeleteRenderbuffers(1, &reflectionDepth);
  reflectionWidth = w;
  reflectionHeight = h;
  glGenFramebuffers(1, &reflectionFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, reflectionFbo);
  glGenTextures(1, &reflectionTexture);
  glBindTexture(GL_TEXTURE_2D, reflectionTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         reflectionTexture, 0);
  glGenRenderbuffers(1, &reflectionDepth);
  glBindRenderbuffer(GL_RENDERBUFFER, reflectionDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, reflectionDepth);
  check();
}
void SurfaceRenderer::upload(SurfaceMesh m, bool prepared) {
  const bool topologyChanged = m.indices != mesh.indices;
  if (!mesh.vertices.empty()) {
    Vec3 oldAxes[] = {geographicEast(mesh.center), geographicUp(mesh.center),
                      geographicNorth(mesh.center)};
    Vec3 newAxes[] = {geographicEast(m.center), geographicUp(m.center),
                      geographicNorth(m.center)};
    Vec3 delta =
        (geographicUp(m.center) - geographicUp(mesh.center)) * surfaceRadius;
    if (length(delta) > 2000.)
      shadowsValid = false;
    double t[16]{};
    t[15] = 1;
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j)
        t[i * 4 + j] = dot(newAxes[i], oldAxes[j]);
      t[12 + i] = dot(delta, oldAxes[i]);
    }
    // Keep each cached shadow in the same moon-fixed location as the mesh
    // streams.
    for (auto &matrix : shadowMatrices) {
      float product[16]{};
      for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
          for (int k = 0; k < 4; ++k)
            product[col * 4 + row] += matrix[k * 4 + row] * t[col * 4 + k];
      std::copy(product, product + 16, matrix);
    }
  }
  mesh = std::move(m);
  ++revision;
  const size_t vertexBytes = mesh.vertices.size() * sizeof(SurfaceVertex);
  if (vertexBytes > vertexCapacity) {
    vertexCapacity = vertexBytes;
    for (auto buffer : {vertices, stagingVertices}) {
      glBindBuffer(GL_ARRAY_BUFFER, buffer);
      glBufferData(GL_ARRAY_BUFFER, vertexCapacity, nullptr, GL_DYNAMIC_DRAW);
    }
  }
  vertexAttributes();
  if (!prepared) {
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexBytes, mesh.vertices.data());
    lastUploadBytes += vertexBytes;
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
  if (topologyChanged) {
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 mesh.indices.size() * sizeof(uint32_t), mesh.indices.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, waterIndices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 mesh.waterIndices.size() * sizeof(uint32_t),
                 mesh.waterIndices.data(), GL_STATIC_DRAW);
    // Reuse every fourth row/column of the already resident mesh. The reflected
    // view is quarter-resolution and wave-filtered; no second terrain build
    // exists.
    constexpr int columns = surfaceAngularSamples, rows = surfaceRadialSamples,
                  step = 4;
    static_assert(columns % step == 0 && rows % step == 0);
    std::vector<uint32_t> coarse;
    for (int row = 0; row < rows; row += step)
      for (int column = 0; column < columns; column += step) {
        uint32_t a = row * columns + column,
                 b = row * columns + (column + step) % columns;
        uint32_t c = a + step * columns, d = b + step * columns;
        coarse.insert(coarse.end(), {a, c, b, b, c, d});
      }
    reflectionIndexCount = coarse.size();
    if (reflectionsEnabled) {
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, reflectionIndices);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, coarse.size() * sizeof(uint32_t),
                   coarse.data(), GL_STATIC_DRAW);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
  }
}
void SurfaceRenderer::stageUpload() {
  if (!staged)
    return;
  // Populate the inactive buffer in bounded chunks while the complete old
  // mesh remains visible. Switch geometry, bounds and its physical frame
  // together.
  constexpr size_t chunk = 2 * 1024 * 1024;
  const size_t bytes = staged->vertices.size() * sizeof(SurfaceVertex);
  if (bytes > vertexCapacity)
    throw std::runtime_error(
        "Streamed surface topology exceeds reserved capacity");
  const size_t count = std::min(chunk, bytes - stagedBytes);
  glBindBuffer(GL_ARRAY_BUFFER, stagingVertices);
  glBufferSubData(
      GL_ARRAY_BUFFER, stagedBytes, count,
      reinterpret_cast<const unsigned char *>(staged->vertices.data()) +
          stagedBytes);
  stagedBytes += count;
  lastUploadBytes += count;
  if (stagedBytes == bytes) {
    std::swap(vertices, stagingVertices);
    upload(std::move(*staged), true);
    staged.reset();
    stagedBytes = 0;
  }
}
void SurfaceRenderer::draw(const std::array<SurfacePlane, 6> &planes,
                           bool waterLod) {
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
  waterLod &= waterLodEnabled;
  if (!cullingEnabled && !waterLod) {
    glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
    return;
  }
  // Adjacent visible blocks share one draw. No index rebuilding/upload or
  // per-triangle CPU work occurs as the aircraft moves.
  std::array<SurfacePlane, 6> unrestricted{};
  bool waterBuffer = false;
  const GLuint dryProgram = waterLod && mesh.pureLand ? landProgram : program;
  if (waterLod && dryProgram != program)
    glUseProgram(dryProgram);
  for (auto range : surfaceDrawRanges(
           mesh, cullingEnabled ? planes : unrestricted, waterLod)) {
    if (range.water != waterBuffer) {
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                   range.water ? waterIndices : indices);
      if (waterLod)
        glUseProgram(range.water ? waterProgram : dryProgram);
      waterBuffer = range.water;
    }
    glDrawElements(
        GL_TRIANGLES, range.indexCount, GL_UNSIGNED_INT,
        reinterpret_cast<void *>(size_t(range.firstIndex) * sizeof(uint32_t)));
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
  if (waterLod && (waterBuffer || dryProgram != program))
    glUseProgram(program);
}
void SurfaceRenderer::shadowMap(int slot, int cascade, Vec3 sun, Vec3 eye) {
  Vec3 side = cross(Vec3{0, 1, 0}, sun);
  if (length(side) < 1.e-8)
    side = cross(Vec3{0, 0, 1}, sun);
  const Vec3 right = normalized(side), up = cross(sun, right);
  const double extent = cascade ? 60000. : 8000., depth = extent * 3.;
  auto *m = shadowMatrices[slot];
  std::fill(m, m + 16, 0.f);
  m[0] = right.x / extent;
  m[4] = right.y / extent;
  m[8] = right.z / extent;
  m[12] = -dot(eye, right) / extent;
  m[1] = up.x / extent;
  m[5] = up.y / extent;
  m[9] = up.z / extent;
  m[13] = -dot(eye, up) / extent;
  m[2] = -sun.x / depth;
  m[6] = -sun.y / depth;
  m[10] = -sun.z / depth;
  m[14] = dot(eye, sun) / depth;
  m[15] = 1;
  glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo[slot]);
  glViewport(0, 0, 2048, 2048);
  glClear(GL_DEPTH_BUFFER_BIT);
  glUseProgram(shadowProgram);
  glUniformMatrix4fv(glGetUniformLocation(shadowProgram, "uShadowMatrix"), 1,
                     GL_FALSE, m);
  draw({{{right, extent - dot(eye, right)},
         {-right, extent + dot(eye, right)},
         {up, extent - dot(eye, up)},
         {-up, extent + dot(eye, up)},
         {sun, depth - dot(eye, sun)},
         {-sun, depth + dot(eye, sun)}}});
  ++shadowDraws;
}
void SurfaceRenderer::shadows(const Scene &s, Vec3 eye, double seconds,
                              double daySeconds) {
  if (s.sunLocal.y <= 0. || !mesh.hasLand)
    return;
  // Calculate the actual future solar direction in the fixed mesh frame.
  // Geographic north has the opposite sign from sceneAt's historical frame.
  auto futureSun = [&](double instant) {
    auto future = sceneAt(
        instant, daySeconds, false,
        {mesh.center.latitude, mesh.center.longitude, s.altitudeMeters});
    return Vec3{future.sunLocal.x, future.sunLocal.y, -future.sunLocal.z};
  };
  auto interval = [&](int cascade, Vec3 sun) {
    return std::max(1. / 30., (sun.y > .5 ? 1. : .5) * daySeconds / 1800.) *
           (cascade ? 2. : 1.);
  };
  bool reset = !shadowsValid || shadowDay != daySeconds;
  for (int i = 0; i < 2; ++i)
    reset |= seconds < shadowBegin[i] ||
             seconds > shadowEnd[i] + interval(i, s.sunLocal);
  bool update = reset;
  for (int i = 0; i < 2; ++i)
    update |= seconds >= shadowEnd[i];
  if (!update)
    return;
  GLint oldPolygon = glIsEnabled(GL_POLYGON_OFFSET_FILL);
  GLfloat oldFactor, oldUnits;
  glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &oldFactor);
  glGetFloatv(GL_POLYGON_OFFSET_UNITS, &oldUnits);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(2.5f, 4.f);
  for (int i = 0; i < 2; ++i) {
    if (reset) {
      shadowBegin[i] = seconds;
      // Stagger the far cascade so normal updates do not share a frame.
      shadowEnd[i] = seconds + interval(i, s.sunLocal) * (i ? .75 : 1.);
      shadowMap(shadowCurrent[i], i, s.sunLocal, eye);
      shadowMap(shadowNext[i], i, futureSun(shadowEnd[i]), eye);
    } else if (seconds >= shadowEnd[i]) {
      // The old future becomes current without changing a single depth pixel.
      // Only the now-hidden old map is recycled for another future state.
      std::swap(shadowCurrent[i], shadowNext[i]);
      shadowBegin[i] = shadowEnd[i];
      shadowEnd[i] += interval(i, futureSun(shadowBegin[i]));
      shadowMap(shadowNext[i], i, futureSun(shadowEnd[i]), eye);
    }
  }
  shadowsValid = true;
  shadowDay = daySeconds;
  glPolygonOffset(oldFactor, oldUnits);
  if (!oldPolygon)
    glDisable(GL_POLYGON_OFFSET_FILL);
}
GLuint SurfaceRenderer::render(const Scene &s, TravelLocation location, int w,
                               int h, double seconds, double daySeconds,
                               GLuint scene,
                               const std::function<void(GLuint)> &lighting) {
  lastUploadBytes = 0;
  auto delta = [&](TravelLocation c) {
    Vec3 point = geographicUp(location) * surfaceRadius -
                 geographicUp(c) * surfaceRadius;
    return Vec3{dot(point, geographicEast(c)), dot(point, geographicUp(c)),
                dot(point, geographicNorth(c))};
  };
  if (mesh.vertices.empty()) {
    ++buildStarts;
    upload(buildSurfaceMesh(terrain, location));
  }
  if (pending.valid() &&
      pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    staged = pending.get();
    stagedBytes = 0;
  }
  stageUpload();
  Vec3 eye = delta(mesh.center);
  if (length(eye) > 2000.) {
    staged.reset();
    stagedBytes = 0;
    if (pending.valid())
      pending.wait();
    if (pending.valid())
      pending.get();
    ++buildStarts;
    upload(buildSurfaceMesh(terrain, location));
    eye = delta(mesh.center);
  } else if (length(eye) > 750. && !pending.valid() && !staged) {
    ++buildStarts;
    pending = std::async(std::launch::async, [this, location] {
      // A continuous CPU burst competes with the GPU on shared-power APUs,
      // even before any mesh upload. Spread only background work over short
      // slices; the resident mesh covers the flight while this one is built.
      // Reset after sleeping so an oversleep never causes catch-up work.
      using Clock = std::chrono::steady_clock;
      auto activeUntil = Clock::now() + std::chrono::milliseconds(1);
      return buildSurfaceMesh(terrain, location, [&] {
        if (Clock::now() >= activeUntil) {
          std::this_thread::sleep_for(std::chrono::milliseconds(4));
          activeUntil = Clock::now() + std::chrono::milliseconds(1);
        }
      });
    });
  }
  double altitude = s.altitudeMeters;
  Vec3 bodyEye = geographicUp(location) * (surfaceRadius + altitude) -
                 geographicUp(mesh.center) * surfaceRadius;
  Vec3 oldAxes[] = {geographicEast(mesh.center), geographicUp(mesh.center),
                    geographicNorth(mesh.center)};
  Vec3 newAxes[] = {geographicEast(location), geographicUp(location),
                    geographicNorth(location)};
  eye = {dot(bodyEye, oldAxes[0]), dot(bodyEye, oldAxes[1]),
         dot(bodyEye, oldAxes[2])};
  float frame[9];
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      frame[i * 3 + j] = dot(oldAxes[i], newAxes[j]);
  Vec3 meshSun{};
  meshSun.x = frame[0] * s.sunLocal.x + frame[1] * s.sunLocal.y +
              frame[2] * s.sunLocal.z;
  meshSun.y = frame[3] * s.sunLocal.x + frame[4] * s.sunLocal.y +
              frame[5] * s.sunLocal.z;
  meshSun.z = frame[6] * s.sunLocal.x + frame[7] * s.sunLocal.y +
              frame[8] * s.sunLocal.z;
  Scene shadowScene = s;
  shadowScene.sunLocal = meshSun;
  // Geometry bounds limit expensive multisample storage/resolve to ground
  // pixels.
  int crop = 1;
  double tanFov = std::tan(29 * pi / 180);
  for (auto box : mesh.bounds) {
    double minForward = 1.e30, maxForward = -1.e30, minRight = 1.e30,
           maxRight = -1.e30, maxUp = -1.e30;
    for (int corner = 0; corner < 8; ++corner) {
      Vec3 p{(corner & 1 ? box.maximum.x : box.minimum.x) - eye.x,
             (corner & 2 ? box.maximum.y : box.minimum.y) - eye.y,
             (corner & 4 ? box.maximum.z : box.minimum.z) - eye.z};
      Vec3 local{frame[0] * p.x + frame[3] * p.y + frame[6] * p.z,
                 frame[1] * p.x + frame[4] * p.y + frame[7] * p.z,
                 frame[2] * p.x + frame[5] * p.y + frame[8] * p.z};
      double z = dot(local, s.forward), x = dot(local, s.right);
      minForward = std::min(minForward, z);
      maxForward = std::max(maxForward, z);
      minRight = std::min(minRight, x);
      maxRight = std::max(maxRight, x);
      maxUp = std::max(maxUp, dot(local, s.cameraUp));
    }
    if (maxForward <= 1 || minRight > maxForward * tanFov * w / h * 1.05 ||
        maxRight < -maxForward * tanFov * w / h * 1.05)
      continue;
    double denominator = maxUp < 0 ? maxForward : std::max(1., minForward);
    double y = .5 + .5 * maxUp / (denominator * tanFov);
    crop = std::max(crop,
                    int(std::clamp(std::ceil(y * h) + h * .03, 1., double(h))));
  }
  // Quantized capacity avoids framebuffer reallocation for subpixel ridge
  // motion.
  crop = std::min(h, ((crop + 127) / 128) * 128);
  if (width == w)
    crop = std::min(h, std::max(crop, height));
  fraction = double(crop) / h;
  resize(w, crop);
  glBindVertexArray(vao);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glClearDepthf(1.);
  shadows(shadowScene, eye, seconds, daySeconds);
  glClearColor(0, 0, 0, 0);
  auto uniforms = [&](GLuint drawProgram, const Scene &view, Vec3 passEye,
                      int passWidth, int passHeight) {
    const GLuint program = drawProgram;
    lighting(program);
    vec(program, "uEye", passEye);
    glUniformMatrix3fv(glGetUniformLocation(program, "uMeshFrame"), 1, GL_FALSE,
                       frame);
    vec(program, "uForward", view.forward);
    vec(program, "uRight", view.right);
    vec(program, "uUp", view.cameraUp);
    vec(program, "uSun", s.sunLocal);
    scalar(program, "uTanFov", std::tan(29 * pi / 180));
    glUniform2f(glGetUniformLocation(program, "uResolution"), passWidth,
                passHeight);
    scalar(program, "uPlanetLight", s.giantLight);
    vec(program, "uPlanetDirection", normalized(s.local(-s.observer)));
    scalar(program, "uEclipse", s.sunVisibility);
    scalar(program, "uSceneScale", 4096);
    scalar(program, "uObserverHeight", s.altitudeMeters / 1000.);
    glUniform1i(glGetUniformLocation(program, "uTerrainShadows"), mesh.hasLand);
    double globalX = surfaceRadius * mesh.center.longitude,
           globalZ = surfaceRadius * mesh.center.latitude;
    glUniform2f(glGetUniformLocation(program, "uMaterialOffset"),
                std::remainder(globalX, 24. * 310.),
                std::remainder(globalZ, 24. * 310.));
    scalar(program, "uGeologyPhase",
           std::remainder((globalX * .03 + globalZ * .015) * (2 * pi / 50.),
                          2 * pi));
    glUniform2f(glGetUniformLocation(program, "uCanopyOffset"),
                std::remainder(globalX, 512.), std::remainder(globalZ, 512.));
    auto reliefCenter =
        terrain.reliefCoordinates(mesh.center.latitude, mesh.center.longitude);
    glUniform2f(
        glGetUniformLocation(program, "uReliefOffset"),
        std::remainder(reliefCenter[0] + .5 * terrain.reliefTexelMeters(),
                       terrain.reliefSpan()),
        std::remainder(reliefCenter[1] + .5 * terrain.reliefTexelMeters(),
                       terrain.reliefSpan()));
    scalar(program, "uReliefSpan", terrain.reliefSpan());
    scalar(program, "uReliefLongitudeScale", 1. / std::cos(location.latitude));
    // Deep-water dispersion sets each wave's real-time period. Wrapping the
    // moon-fixed phase on CPU retains precision over Unix-scale scene clocks.
    constexpr double wavelengths[] = {48., 19., 7., 2.5};
    constexpr double angles[] = {.27, -1.05, 2.62, .89};
    constexpr double slopes[] = {.032, .024, .016, .010};
    float waves[16];
    for (int i = 0; i < 4; ++i) {
      double k = 2 * pi / wavelengths[i], kx = std::cos(angles[i]) * k,
             kz = std::sin(angles[i]) * k;
      waves[4 * i] = kx;
      waves[4 * i + 1] = kz;
      waves[4 * i + 2] = std::remainder(
          globalX * kx + globalZ * kz - seconds * std::sqrt(9.81 * k) + i * 1.7,
          2 * pi);
      waves[4 * i + 3] = slopes[i];
    }
    glUniform4fv(glGetUniformLocation(program, "uWaves[0]"), 4, waves);
    glUniformMatrix4fv(glGetUniformLocation(program, "uShadowMatrix[0]"), 4,
                       GL_FALSE, shadowMatrices[0]);

    bind(program, 2, "uScene", scene);
    bind(program, 3, "uMaterial", material);
    bind(program, 4, "uShadowNear", shadowTex[shadowCurrent[0]]);
    bind(program, 5, "uShadowFar", shadowTex[shadowCurrent[1]]);
    bind(program, 9, "uShadowNearNext", shadowTex[shadowNext[0]]);
    bind(program, 10, "uShadowFarNext", shadowTex[shadowNext[1]]);
    float mix[2]{};
    for (int i = 0; i < 2; ++i) {
      const double duration = shadowEnd[i] - shadowBegin[i];
      double t = duration > 0
                     ? std::clamp((seconds - shadowBegin[i]) / duration, 0., 1.)
                     : 0.;
      mix[i] = t * t * (3. - 2. * t);
    }
    glUniform2fv(glGetUniformLocation(program, "uShadowBlend"), 1, mix);
    glUniform4i(glGetUniformLocation(program, "uShadowSlots"), shadowCurrent[0],
                shadowNext[0], shadowCurrent[1], shadowNext[1]);
    bind(program, 7, "uCanopy", canopy);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, reliefMaterial);
    glUniform1i(glGetUniformLocation(program, "uReliefMaterial"), 6);
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D_ARRAY, physicalAlbedo);
    glUniform1i(glGetUniformLocation(program, "uPhysicalAlbedo"), 11);
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D_ARRAY, physicalNormal);
    glUniform1i(glGetUniformLocation(program, "uPhysicalNormal"), 12);
    glActiveTexture(GL_TEXTURE13);
    glBindTexture(GL_TEXTURE_2D_ARRAY, crownShadows);
    glUniform1i(glGetUniformLocation(program, "uCrownShadows"), 13);
  };
  const bool reflectedLand =
      reflectionsEnabled && mesh.hasLand && mesh.hasWater;
  if (reflectedLand) {
    resizeReflection(std::max(1, w / 4), std::max(1, h / 4));
    glBindFramebuffer(GL_FRAMEBUFFER, reflectionFbo);
    glViewport(0, 0, reflectionWidth, reflectionHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Scene mirror = s;
    mirror.forward.y = -mirror.forward.y;
    mirror.right.y = -mirror.right.y;
    mirror.cameraUp.y = -mirror.cameraUp.y;
    Vec3 virtualEye = geographicUp(location) * (surfaceRadius - altitude) -
                      geographicUp(mesh.center) * surfaceRadius;
    virtualEye = {dot(virtualEye, oldAxes[0]), dot(virtualEye, oldAxes[1]),
                  dot(virtualEye, oldAxes[2])};
    uniforms(reflectionProgram, mirror, virtualEye, reflectionWidth,
             reflectionHeight);
    // Overscan catches wave-deflected rays outside the primary view.
    scalar(reflectionProgram, "uTanFov", std::tan(29 * pi / 180) * 1.5);
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, reflectionIndices);
    glDrawElements(GL_TRIANGLES, reflectionIndexCount, GL_UNSIGNED_INT,
                   nullptr);
    ++reflectionDraws;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, multisample);
  glViewport(0, 0, w, h);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  auto surfaceUniforms = [&](GLuint pass) {
    uniforms(pass, s, eye, w, h);
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_2D, reflectedLand ? reflectionTexture : scene);
    glUniform1i(glGetUniformLocation(pass, "uLandReflection"), 14);
    glUniform1i(glGetUniformLocation(pass, "uLandReflectionEnabled"),
                reflectedLand);
  };
  if (waterProgram && mesh.hasWater)
    surfaceUniforms(waterProgram);
  if (landProgram && mesh.pureLand)
    surfaceUniforms(landProgram);
  else
    surfaceUniforms(program);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
  auto meshDirection = [&](Vec3 v) {
    return Vec3{frame[0] * v.x + frame[1] * v.y + frame[2] * v.z,
                frame[3] * v.x + frame[4] * v.y + frame[5] * v.z,
                frame[6] * v.x + frame[7] * v.y + frame[8] * v.z};
  };
  const Vec3 forward = meshDirection(s.forward), right = meshDirection(s.right),
             up = meshDirection(s.cameraUp);
  std::array<SurfacePlane, 6> planes;
  planes[0].normal = forward * (tanFov * w / h) + right;
  planes[1].normal = forward * (tanFov * w / h) - right;
  planes[2].normal = forward * tanFov + up;
  planes[3].normal = forward * tanFov - up;
  planes[4].normal = forward;
  planes[5].normal = -forward;
  for (auto &plane : planes)
    plane.offset = -dot(plane.normal, eye);
  planes[4].offset -= 1.;
  planes[5].offset += 220000.;
  draw(planes, true);
  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, multisample);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolved);
  // Resolve distance with the same MSAA weights as radiance/coverage. Dividing
  // by coverage recovers foreground distance even when sample zero is
  // uncovered.
  const GLenum colorOnly[] = {GL_COLOR_ATTACHMENT0, GL_NONE};
  glDrawBuffers(2, colorOnly);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glBlitFramebuffer(0, 0, w, height, 0, 0, w, height, GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
  if (weatherEnabled) {
    const GLenum distanceOnly[] = {GL_NONE, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, distanceOnly);
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glBlitFramebuffer(0, 0, w, height, 0, 0, w, height, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
  }
  const GLenum outputs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(weatherEnabled ? 2 : 1, outputs);
  return texture;
}
} // namespace observatory
