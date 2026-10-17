#pragma once
#include "debug_menu.hpp"
#include <GLES3/gl3.h>
#include <string>
#include <vector>
namespace observatory {
// Shared drawing only; hosts supply input without sharing authentication state.
class DebugOverlay {
public:
  DebugOverlay(double start, double daySeconds, bool open = true);
  ~DebugOverlay();
  DebugOverlay(const DebugOverlay &) = delete;
  DebugOverlay &operator=(const DebugOverlay &) = delete;
  DebugMenu menu;
  void draw(int width, int height, int windowWidth, int windowHeight,
            double real, float opacity = 1);
  double time(double real) const { return menu.time(real); }

private:
  struct Vertex {
    float x, y, r, g, b, a;
  };
  GLuint program = 0, vao = 0, buffer = 0;
  std::vector<Vertex> vertices;
  void initialize();
  void rectangle(DebugRect, float r, float g, float b, float a = 1);
  void text(double x, double y, double scale, const std::string &);
};
} // namespace observatory
