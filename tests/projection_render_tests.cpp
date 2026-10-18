#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace observatory;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
std::vector<unsigned char> capture(Renderer &r, const Scene &s, int w, int h,
                                   double time, bool scissor) {
  r.setCelestialScissor(scissor);
  glEnable(GL_SCISSOR_TEST);
  glScissor(7, 11, 13, 17);
  r.renderScene(w, h, s, time, 1800, 1, true, true, {1790900000., 1. / 30},
                1790900000.);
  GLint box[4]{};
  glGetIntegerv(GL_SCISSOR_BOX, box);
  require(glIsEnabled(GL_SCISSOR_TEST) && box[0] == 7 && box[1] == 11 &&
              box[2] == 13 && box[3] == 17,
          "celestial bounds must restore host scissor state");
  glDisable(GL_SCISSOR_TEST);
  std::vector<unsigned char> pixels(w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  require(glGetError() == GL_NO_ERROR, "projection render GL errors");
  return pixels;
}
void compare(Renderer &r, const Scene &s, int w, int h, double time) {
  const auto bounded = capture(r, s, w, h, time, true);
  const auto full = capture(r, s, w, h, time, false);
  int peak = 0;
  double sum = 0;
  for (size_t i = 0; i < full.size(); ++i) {
    if (i % 4 == 3)
      continue;
    const int error = std::abs(int(bounded[i]) - int(full[i]));
    peak = std::max(peak, error);
    sum += error;
  }
  const double mean = sum / (w * h * 3);
  std::cout << "Celestial scissor " << w << 'x' << h << " time=" << time
            << " peak=" << peak << " mean=" << mean << '\n';
  require(peak <= 2 && mean <= .02,
          "celestial scissor must preserve unrestricted reference image");
}
// Vary ring orientation and camera framing independently of the usual route.
Scene framing(double distance, double tilt, double horizontal, double vertical,
              double time) {
  auto s = sceneAt(time);
  const Vec3 pole = giantPole(), u = normalized(cross(pole, {0, 0, 1}));
  s.observer = (u * std::cos(tilt) + pole * std::sin(tilt)) * distance;
  s.east = {1, 0, 0};
  s.up = {0, 1, 0};
  s.north = {0, 0, 1};
  const Vec3 forward = normalized(-s.observer);
  const Vec3 right = normalized(cross({0, 1, 0}, forward));
  const Vec3 up = cross(forward, right);
  s.forward = normalized(forward + right * horizontal + up * vertical);
  s.right = normalized(cross({0, 1, 0}, s.forward));
  s.cameraUp = cross(s.forward, s.right);
  s.sunLocal = s.sun;
  for (int i = 2; i < 8; ++i) {
    s.bodies[i].position = u * (1.25 + .08 * i) + pole * (.03 * (i - 4));
    s.bodies[i].radius = .025 * (i - 1);
  }
  return s;
}
} // namespace
int main() {
  try {
    require(glfwInit(), "GLFW init");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto *window = glfwCreateWindow(1920, 1080, "Celestial projection parity",
                                    nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';
    const auto data = Renderer::defaultDataDirectory();
    {
      Renderer renderer(data);
      for (double time : {0., 450., 900., 1350., 437625.})
        compare(renderer, sceneAt(time), 640, 360, time);
      for (double tilt : {0., .00001, .10, -.17, .8, 1.56})
        for (double offset : {0., -.55, .8})
          compare(renderer, framing(3., tilt, offset, .08, 437625.), 641, 359,
                  437625.);
      for (double distance : {1.05, 1.8, 15., 1500.})
        compare(renderer, framing(distance, .2, .45, -.3, 900.), 640, 360,
                900.);
    }
    {
      SurfaceWorld world(data + "/assets/world/moon.ggworld");
      for (auto [route, time] : {std::array<double, 2>{162000., 510.},
                                 {5600.390625, 1740.},
                                 {210600., 360.},
                                 {0., 1740.}}) {
        SurfaceOptions options{true, 0, route};
        Renderer renderer(data, {}, options);
        compare(renderer, travelScene(world, time, route), 1920, 1080, time);
      }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Celestial scissor preserves complete images PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    glfwTerminate();
    return 1;
  }
}
