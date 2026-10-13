#pragma once
#include "surface.hpp"
#include <GLES3/gl3.h>
#include <functional>
#include <future>
namespace observatory {
class SurfaceRenderer {
public:
  explicit SurfaceRenderer(const std::string &root);
  ~SurfaceRenderer();
  SurfaceRenderer(const SurfaceRenderer &) = delete;
  const SurfaceWorld &world() const { return terrain; }
  double coverageFraction() const { return fraction; }
  GLuint render(const Scene &, TravelLocation, int, int, double, GLuint,
                const std::function<void(GLuint)> &);

private:
  SurfaceWorld terrain;
  SurfaceMesh mesh;
  std::future<SurfaceMesh> pending;
  GLuint treeVao = 0, treeBuffer = 0, treeProgram = 0, treeShadowProgram = 0,
         treeTexture = 0;
  GLuint vao = 0, vertices = 0, indices = 0, program = 0, shadowProgram = 0,
         material = 0;
  GLuint multisample = 0, colorBuffer = 0, depthBuffer = 0, resolved = 0,
         texture = 0;
  GLuint shadowFbo[2]{}, shadowTex[2]{};
  float shadowMatrices[2][16]{};
  int width = 0, height = 0, samples = 1;
  size_t vertexCapacity = 0, treeCapacity = 0;
  double shadowTime[2] = {-1e30, -1e30};
  double fraction = 1;
  Vec3 shadowSun[2]{};
  void upload(SurfaceMesh);
  void resize(int, int);
  void releaseTargets();
  void shadows(const Scene &, Vec3, double);
};
} // namespace observatory
