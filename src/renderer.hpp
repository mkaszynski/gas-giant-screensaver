#pragma once
#include "emissions.hpp"
#include "system.hpp"
#include <GLES3/gl3.h>
#include <string>
#include <vector>
namespace observatory {
class Renderer {
public:
  explicit Renderer(std::string dataDirectory);
  ~Renderer();
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  void render(int width, int height, double seconds, double daySeconds = 1800,
              float opacity = 1, bool drawBodies = true, bool drawRings = true,
              EmissionFrame emission = {});
  void renderScene(int width, int height, const Scene &, double seconds,
                   double daySeconds = 1800, float opacity = 1,
                   bool drawBodies = true, bool drawRings = true,
                   EmissionFrame emission = {});
  static std::string defaultDataDirectory();

private:
  struct Target {
    GLuint texture = 0, framebuffer = 0, depth = 0;
    int width = 0, height = 0;
  };
  std::string root;
  std::vector<GLuint> programs, textures;
  GLuint transProgram = 0, multiProgram = 0, skyProgram = 0,
         backgroundProgram = 0, bodyProgram = 0, starProgram = 0,
         postProgram = 0, ringProgram = 0, giantProgram = 0,
         giantLimbProgram = 0, emissionProgram = 0;
  GLuint giant = 0, mountains = 0, noise = 0, ringProfile = 0, giantSlab = 0,
         vao = 0, starBuffer = 0, starVao = 0, emissionBuffer = 0,
         emissionVao = 0;
  Target trans, multi, sky, scene;
  double lastSky = -1e30, lastDay = 0;
  double lastVisibility = -1;
  Vec3 lastSun;
  bool lastRings = false;
  int stars = 5000;
  std::string shader(const std::string &);
  GLuint program(const std::string &, const std::string & = "fullscreen.vert",
                 int giantAtmosphere = 0);
  GLuint imageTexture(const std::string &);
  void target(Target &, int, int, bool depth = false);
  void release(Target &);
  void bind(GLuint, int, const char *, GLuint);
  void common(GLuint, const Scene &, int, int, double);
  void rings(GLuint, const Scene &, bool enabled);
  void quad(GLuint);
  void emissions(const Scene &, int, int, EmissionFrame, bool ringsEnabled);
};
void savePng(const std::string &path, int w, int h);
} // namespace observatory
