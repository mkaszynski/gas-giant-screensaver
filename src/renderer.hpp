#pragma once
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
              float opacity = 1, bool drawBodies = true);
  static std::string defaultDataDirectory();

private:
  struct Target {
    GLuint texture = 0, framebuffer = 0;
    int width = 0, height = 0;
  };
  std::string root;
  std::vector<GLuint> programs, textures;
  GLuint transProgram = 0, multiProgram = 0, skyProgram = 0,
         backgroundProgram = 0, bodyProgram = 0, starProgram = 0,
         postProgram = 0;
  GLuint giant = 0, mountains = 0, noise = 0, vao = 0, starBuffer = 0,
         starVao = 0;
  Target trans, multi, sky, scene;
  double lastSky = -1e30, lastDay = 0;
  int stars = 5000;
  std::string shader(const std::string &);
  GLuint program(const std::string &, const std::string & = "fullscreen.vert");
  GLuint imageTexture(const std::string &);
  void target(Target &, int, int);
  void release(Target &);
  void bind(GLuint, int, const char *, GLuint);
  void common(GLuint, const Scene &, int, int, double);
  void quad(GLuint);
};
void savePng(const std::string &path, int w, int h);
} // namespace observatory
