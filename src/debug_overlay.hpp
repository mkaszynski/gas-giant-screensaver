#pragma once
#include "debug_timeline.hpp"
#include <GLES3/gl3.h>
#include <string>
#include <vector>
struct GLFWwindow;
namespace observatory {
// Linked into the standalone preview only, never the native lock widget.
class DebugOverlay {
public:
  DebugOverlay(double start, double daySeconds);
  ~DebugOverlay();
  DebugOverlay(const DebugOverlay &) = delete;
  DebugOverlay &operator=(const DebugOverlay &) = delete;
  void input(GLFWwindow *, double real);
  void draw(int width, int height, int windowWidth, int windowHeight,
            double real);
  double time(double real) const { return timeline.time(real); }

private:
  struct Vertex {
    float x, y, r, g, b, a;
  };
  DebugTimeline timeline;
  double day, year;
  bool mouseDown = false, spaceDown = false, homeDown = false, leftDown = false,
       rightDown = false;
  GLuint program = 0, vao = 0, buffer = 0;
  std::vector<Vertex> vertices;
  void rectangle(DebugRect, float r, float g, float b, float a = 1);
  void text(double x, double y, double scale, const std::string &);
};
} // namespace observatory
