#include "surface_renderer.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <png.h>
#include <sstream>
#include <stdexcept>
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
GLuint link(const std::string &root, const char *vert, const char *frag) {
  GLuint v = compile(GL_VERTEX_SHADER, shader(root, vert)),
         f = compile(GL_FRAGMENT_SHADER, shader(root, frag)),
         p = glCreateProgram();
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
void check() {
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error("Surface framebuffer unavailable");
}
} // namespace
SurfaceRenderer::SurfaceRenderer(const std::string &root)
    : terrain(root + "/assets/world/moon.ggworld") {
  program = link(root, "surface.vert", "surface.frag");
  shadowProgram = link(root, "surface_shadow.vert", "surface_shadow.frag");
  treeProgram = link(root, "surface_tree.vert", "surface_tree.frag");
  treeShadowProgram =
      link(root, "surface_tree.vert", "surface_tree_shadow.frag");
  glGenVertexArrays(1, &treeVao);
  glBindVertexArray(treeVao);
  glGenBuffers(1, &treeBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, treeBuffer);
  for (int i = 0; i < 2; ++i) {
    glEnableVertexAttribArray(i);
    glVertexAttribPointer(
        i, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceTree),
        reinterpret_cast<void *>(size_t(i) * 3 * sizeof(float)));
    glVertexAttribDivisor(i, 1);
  }
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vertices);
  glGenBuffers(1, &indices);
  glBindBuffer(GL_ARRAY_BUFFER, vertices);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
  for (int i = 0; i < 3; ++i) {
    glEnableVertexAttribArray(i);
    glVertexAttribPointer(
        i, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex),
        reinterpret_cast<void *>(size_t(i) * 3 * sizeof(float)));
  }
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex),
                        reinterpret_cast<void *>(9 * sizeof(float)));
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
  png_image tree{};
  tree.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(
          &tree, (root + "/assets/world/trees.png").c_str()))
    throw std::runtime_error("Missing tree atlas");
  tree.format = PNG_FORMAT_RGBA;
  std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(tree));
  if (!png_image_finish_read(&tree, nullptr, pixels.data(), 0, nullptr))
    throw std::runtime_error(tree.message);
  glGenTextures(1, &treeTexture);
  glBindTexture(GL_TEXTURE_2D, treeTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, tree.width, tree.height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  png_image_free(&tree);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  GLint counts = 0;
  glGetInternalformativ(GL_RENDERBUFFER, GL_RGBA16F, GL_NUM_SAMPLE_COUNTS, 1,
                        &counts);
  std::vector<GLint> available(counts);
  if (counts)
    glGetInternalformativ(GL_RENDERBUFFER, GL_RGBA16F, GL_SAMPLES, counts,
                          available.data());
  for (auto v : available)
    if (v <= 4)
      samples = std::max(samples, int(v));
  glGenFramebuffers(2, shadowFbo);
  glGenTextures(2, shadowTex);
  for (int i = 0; i < 2; ++i) {
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
void SurfaceRenderer::releaseTargets() {
  glDeleteFramebuffers(1, &multisample);
  glDeleteFramebuffers(1, &resolved);
  glDeleteRenderbuffers(1, &colorBuffer);
  glDeleteRenderbuffers(1, &depthBuffer);
  glDeleteTextures(1, &texture);
  multisample = resolved = colorBuffer = depthBuffer = texture = 0;
}
SurfaceRenderer::~SurfaceRenderer() {
  if (pending.valid())
    pending.wait();
  releaseTargets();
  glDeleteProgram(treeProgram);
  glDeleteProgram(treeShadowProgram);
  glDeleteBuffers(1, &treeBuffer);
  glDeleteVertexArrays(1, &treeVao);
  glDeleteTextures(1, &treeTexture);
  glDeleteProgram(program);
  glDeleteProgram(shadowProgram);
  glDeleteBuffers(1, &vertices);
  glDeleteBuffers(1, &indices);
  glDeleteVertexArrays(1, &vao);
  glDeleteTextures(1, &material);
  glDeleteTextures(2, shadowTex);
  glDeleteFramebuffers(2, shadowFbo);
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
  check();
}
void SurfaceRenderer::upload(SurfaceMesh m) {
  const bool topologyChanged = m.indices != mesh.indices;
  if (!mesh.vertices.empty()) {
    Vec3 oldAxes[] = {geographicEast(mesh.center), geographicUp(mesh.center),
                      geographicNorth(mesh.center)};
    Vec3 newAxes[] = {geographicEast(m.center), geographicUp(m.center),
                      geographicNorth(m.center)};
    Vec3 delta =
        (geographicUp(m.center) - geographicUp(mesh.center)) * surfaceRadius;
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
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vertices);
  const size_t vertexBytes = mesh.vertices.size() * sizeof(SurfaceVertex);
  if (vertexBytes > vertexCapacity) {
    vertexCapacity = vertexBytes;
    glBufferData(GL_ARRAY_BUFFER, vertexCapacity, nullptr, GL_DYNAMIC_DRAW);
  }
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertexBytes, mesh.vertices.data());
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
  if (topologyChanged)
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 mesh.indices.size() * sizeof(uint32_t), mesh.indices.data(),
                 GL_STATIC_DRAW);
  glBindVertexArray(treeVao);
  glBindBuffer(GL_ARRAY_BUFFER, treeBuffer);
  const size_t treeBytes = mesh.trees.size() * sizeof(SurfaceTree);
  if (treeBytes > treeCapacity || !treeCapacity) {
    treeCapacity =
        ((mesh.trees.size() + 255) / 256 + 1) * 256 * sizeof(SurfaceTree);
    glBufferData(GL_ARRAY_BUFFER, treeCapacity, nullptr, GL_DYNAMIC_DRAW);
  }
  glBufferSubData(GL_ARRAY_BUFFER, 0, treeBytes, mesh.trees.data());
  glBindVertexArray(vao);
}
void SurfaceRenderer::shadows(const Scene &s, Vec3 eye, double seconds) {
  if (s.sunLocal.y <= 0.)
    return;
  bool initial = shadowMatrices[0][15] == 0 || shadowMatrices[1][15] == 0;
  double age[2] = {std::abs(seconds - shadowTime[0]) / .5,
                   std::abs(seconds - shadowTime[1])};
  int chosen = age[0] > age[1] ? 0 : 1;
  if (!initial && age[chosen] < 1. &&
      length(s.sunLocal - shadowSun[chosen]) < .005)
    return;
  GLint oldPolygon = glIsEnabled(GL_POLYGON_OFFSET_FILL);
  GLfloat oldFactor, oldUnits;
  glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &oldFactor);
  glGetFloatv(GL_POLYGON_OFFSET_UNITS, &oldUnits);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(2.5f, 4.f);
  Vec3 sun = s.sunLocal, right = normalized(cross(Vec3{0, 1, 0}, sun)),
       up = cross(sun, right);
  for (int i = 0; i < 2; ++i) {
    if (!initial && i != chosen)
      continue;
    double extent = i ? 18000. : 1800., depth = extent * 3.;
    auto *m = shadowMatrices[i];
    for (int j = 0; j < 16; ++j)
      m[j] = 0;
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
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo[i]);
    glViewport(0, 0, 2048, 2048);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(shadowProgram);
    glUniformMatrix4fv(glGetUniformLocation(shadowProgram, "uShadowMatrix"), 1,
                       GL_FALSE, m);
    glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
    glUseProgram(treeShadowProgram);
    glUniform1i(glGetUniformLocation(treeShadowProgram, "uShadowPass"), 1);
    glUniformMatrix4fv(
        glGetUniformLocation(treeShadowProgram, "uTreeShadowMatrix"), 1,
        GL_FALSE, m);
    vec(treeShadowProgram, "uTreeRight", right);
    bind(treeShadowProgram, 0, "uTrees", treeTexture);
    glBindVertexArray(treeVao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, mesh.trees.size());
    glBindVertexArray(vao);
    shadowTime[i] = seconds;
    shadowSun[i] = sun;
  }
  glPolygonOffset(oldFactor, oldUnits);
  if (!oldPolygon)
    glDisable(GL_POLYGON_OFFSET_FILL);
}
GLuint SurfaceRenderer::render(const Scene &s, TravelLocation location, int w,
                               int h, double seconds, GLuint scene,
                               const std::function<void(GLuint)> &lighting) {
  auto delta = [&](TravelLocation c) {
    Vec3 point = geographicUp(location) * surfaceRadius -
                 geographicUp(c) * surfaceRadius;
    return Vec3{dot(point, geographicEast(c)), dot(point, geographicUp(c)),
                dot(point, geographicNorth(c))};
  };
  if (mesh.vertices.empty())
    upload(buildSurfaceMesh(terrain, location));
  if (pending.valid() &&
      pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    upload(pending.get());
  Vec3 eye = delta(mesh.center);
  if (length(eye) > 2000.) {
    if (pending.valid())
      pending.wait();
    if (pending.valid())
      pending.get();
    upload(buildSurfaceMesh(terrain, location));
    eye = delta(mesh.center);
  } else if (length(eye) > 128. && !pending.valid())
    pending = std::async(std::launch::async, [this, location] {
      return buildSurfaceMesh(terrain, location);
    });
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
  for (auto v : mesh.vertices) {
    Vec3 p{v.x - eye.x, v.y - eye.y, v.z - eye.z};
    Vec3 local{frame[0] * p.x + frame[3] * p.y + frame[6] * p.z,
               frame[1] * p.x + frame[4] * p.y + frame[7] * p.z,
               frame[2] * p.x + frame[5] * p.y + frame[8] * p.z};
    double z = dot(local, s.forward);
    if (z <= 1.)
      continue;
    double x = dot(local, s.right) / (z * tanFov * w / h);
    if (std::abs(x) > 1.05)
      continue;
    double y = .5 + .5 * dot(local, s.cameraUp) / (z * tanFov);
    crop = std::max(crop,
                    int(std::clamp(std::ceil(y * h) + h * .03, 1., double(h))));
  }
  for (auto tree : mesh.trees) {
    Vec3 p{tree.x - eye.x, tree.y + tree.height - eye.y, tree.z - eye.z};
    Vec3 local{frame[0] * p.x + frame[3] * p.y + frame[6] * p.z,
               frame[1] * p.x + frame[4] * p.y + frame[7] * p.z,
               frame[2] * p.x + frame[5] * p.y + frame[8] * p.z};
    double z = dot(local, s.forward);
    if (z <= 1.)
      continue;
    double y = .5 + .5 * dot(local, s.cameraUp) / (z * tanFov);
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
  shadows(shadowScene, eye, seconds);
  glBindFramebuffer(GL_FRAMEBUFFER, multisample);
  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  lighting(program);
  vec(program, "uEye", eye);
  glUniformMatrix3fv(glGetUniformLocation(program, "uMeshFrame"), 1, GL_FALSE,
                     frame);
  vec(program, "uForward", s.forward);
  vec(program, "uRight", s.right);
  vec(program, "uUp", s.cameraUp);
  vec(program, "uSun", s.sunLocal);
  scalar(program, "uTanFov", std::tan(29 * pi / 180));
  glUniform2f(glGetUniformLocation(program, "uResolution"), w, h);
  scalar(program, "uPlanetLight", s.giantLight);
  scalar(program, "uEclipse", s.sunVisibility);
  scalar(program, "uSceneScale", 4096);
  scalar(program, "uObserverHeight", s.altitudeMeters / 1000.);
  double globalX = surfaceRadius * mesh.center.longitude,
         globalZ = surfaceRadius * mesh.center.latitude;
  glUniform2f(glGetUniformLocation(program, "uMaterialOffset"),
              std::remainder(globalX, 24. * 310.),
              std::remainder(globalZ, 24. * 310.));
  glUniform2f(
      glGetUniformLocation(program, "uWaveOffset"),
      std::remainder(globalX * .17 + globalZ * .23 * .4 + seconds * .496,
                     2 * pi),
      std::remainder(globalZ * .23 - globalX * .17 * .35 + seconds * .157,
                     2 * pi));
  glUniformMatrix4fv(glGetUniformLocation(program, "uShadowMatrix[0]"), 2,
                     GL_FALSE, shadowMatrices[0]);

  bind(program, 2, "uScene", scene);
  bind(program, 3, "uMaterial", material);
  bind(program, 4, "uShadowNear", shadowTex[0]);
  bind(program, 5, "uShadowFar", shadowTex[1]);
  glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
  lighting(treeProgram);
  vec(treeProgram, "uEye", eye);
  glUniformMatrix3fv(glGetUniformLocation(treeProgram, "uMeshFrame"), 1,
                     GL_FALSE, frame);
  vec(treeProgram, "uTreeRight",
      {frame[0] * s.right.x + frame[1] * s.right.y + frame[2] * s.right.z,
       frame[3] * s.right.x + frame[4] * s.right.y + frame[5] * s.right.z,
       frame[6] * s.right.x + frame[7] * s.right.y + frame[8] * s.right.z});
  glUniform1i(glGetUniformLocation(treeProgram, "uShadowPass"), 0);
  glUniformMatrix4fv(glGetUniformLocation(treeProgram, "uShadowMatrix[0]"), 2,
                     GL_FALSE, shadowMatrices[0]);
  bind(treeProgram, 3, "uTrees", treeTexture);
  bind(treeProgram, 4, "uShadowNear", shadowTex[0]);
  bind(treeProgram, 5, "uShadowFar", shadowTex[1]);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glBindVertexArray(treeVao);
  glDrawArraysInstanced(GL_TRIANGLES, 0, 6, mesh.trees.size());
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, multisample);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolved);
  glBlitFramebuffer(0, 0, w, height, 0, 0, w, height, GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
  return texture;
}
} // namespace observatory
