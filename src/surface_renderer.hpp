#pragma once
#include "surface.hpp"
#include <GLES3/gl3.h>
#include <functional>
#include <future>
#include <optional>
namespace observatory {
class SurfaceRenderer {
public:
  explicit SurfaceRenderer(const std::string &root);
  ~SurfaceRenderer();
  SurfaceRenderer(const SurfaceRenderer &) = delete;
  const SurfaceWorld &world() const { return terrain; }
  double coverageFraction() const { return fraction; }
  size_t meshRevision() const { return revision; }
  size_t shadowRevision() const { return shadowDraws; }
  size_t uploadBytes() const { return lastUploadBytes; }
  GLuint render(const Scene &, TravelLocation, int, int, double, double, GLuint,
                const std::function<void(GLuint)> &);

private:
  SurfaceWorld terrain;
  SurfaceMesh mesh;
  std::future<SurfaceMesh> pending;
  std::optional<SurfaceMesh> staged;
  size_t stagedBytes = 0, lastUploadBytes = 0;
  GLuint stagingVertices = 0;
  GLuint vao = 0, vertices = 0, indices = 0, program = 0, shadowProgram = 0,
         material = 0, canopy = 0, reliefMaterial = 0;
  GLuint multisample = 0, colorBuffer = 0, depthBuffer = 0, resolved = 0,
         texture = 0;
  GLuint shadowFbo[4]{}, shadowTex[4]{};
  float shadowMatrices[4][16]{};
  int width = 0, height = 0, samples = 1;
  size_t vertexCapacity = 0;
  size_t revision = 0;
  double shadowBegin[2]{}, shadowEnd[2]{}, shadowDay = 0;
  int shadowCurrent[2] = {0, 2}, shadowNext[2] = {1, 3};
  size_t shadowDraws = 0;
  bool shadowsValid = false;
  double fraction = 1;
  void upload(SurfaceMesh, bool prepared = false);
  void stageUpload();
  void vertexAttributes();
  void resize(int, int);
  void releaseTargets();
  void shadows(const Scene &, Vec3, double, double);
  void shadowMap(int slot, int cascade, Vec3 sun, Vec3 eye);
};
} // namespace observatory
