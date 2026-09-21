#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace observatory;
namespace {
constexpr int width = 640, height = 360;
using Pixels = std::vector<unsigned char>;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
struct ShadowFixture {
  std::filesystem::path path;
  ShadowFixture(bool ringShadows, bool sphereShadows, bool flat = false) {
    char name[] = "/tmp/observatory-rings-XXXXXX";
    auto dir = mkdtemp(name);
    require(dir, "temporary shader directory");
    path = dir;
    auto root = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    auto disable = [&](const char *file, const char *function) {
      auto target = path / "shaders" / file;
      std::ifstream input(target);
      std::ostringstream buffer;
      buffer << input.rdbuf();
      auto source = buffer.str();
      auto at = source.find(function);
      require(at != std::string::npos, "shadow fixture function exists");
      source.insert(at + std::string(function).size(), "return 1.;");
      std::ofstream(target) << source;
    };
    if (!ringShadows)
      disable("ring_profile.glsl", "float ringSunTransmissionFiltered(vec3 "
                                   "point,vec3 sun,float pixelFootprint){");
    if (!sphereShadows)
      disable("sphere_shadow.glsl",
              "float sphereSunVisibility(vec3 p,int count,vec4 blockers[21]){");
    if (flat) {
      for (const char *file : {"body.frag", "rings.frag"}) {
        const auto target = path / "shaders" / file;
        std::ifstream input(target);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        auto source = buffer.str();
        const auto end = source.rfind('}');
        require(end != std::string::npos, "flat fixture main exists");
        source.insert(end, std::string(file) == "body.frag"
                               ? "color=vec4(vec3(.15)*coverage,coverage);\n"
                               : "color=vec4(vec3(.15),1.);\n");
        std::ofstream(target) << source;
      }
    }
  }
  ~ShadowFixture() { std::filesystem::remove_all(path); }
};
Scene scene(Vec3 sun, double side = 1) {
  Scene s{};
  for (int i = 0; i < 21; ++i)
    s.bodies[i] = {{1e6 + i * 100., 1e6, 1e6}, .01, 3, 0};
  s.bodies[0] = {{0, 0, 0}, 1, 0, 0};
  s.bodies[0].pole = giantPole();
  s.observer = {0, 0, side * 5};
  s.east = {side, 0, 0};
  s.up = {0, std::sqrt(.75), -side * .5};
  s.north = {0, -.5, -side * std::sqrt(.75)};
  s.forward = normalized(s.local(-s.observer));
  s.right = normalized(cross({0, 1, 0}, s.forward));
  s.cameraUp = cross(s.forward, s.right);
  s.sun = normalized(sun);
  s.sunLocal = s.local(s.sun);
  s.sunVisibility = 1;
  s.giantLight = .01;
  return s;
}
Pixels render(Renderer &r, const Scene &s) {
  r.renderScene(width, height, s, 0);
  Pixels p(width * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  return p;
}
std::pair<int, int> project(const Scene &s, Vec3 point) {
  auto p = s.local(point - s.observer);
  const double scale =
      height * .5 / (dot(p, s.forward) * std::tan(29 * pi / 180));
  return {int(width * .5 + dot(p, s.right) * scale),
          int(height * .5 + dot(p, s.cameraUp) * scale)};
}
double difference(const Pixels &a, const Pixels &b, const Scene &s, Vec3 p,
                  int radius = 2) {
  const auto [cx, cy] = project(s, p);
  require(cx > radius && cy > radius && cx < width - radius &&
              cy < height - radius,
          "test geometry must be inside the viewport");
  double delta = 0;
  for (int y = cy - radius; y <= cy + radius; ++y)
    for (int x = cx - radius; x <= cx + radius; ++x)
      for (int c = 0; c < 3; ++c)
        delta += std::abs(int(a[(y * width + x) * 4 + c]) -
                          int(b[(y * width + x) * 4 + c]));
  return delta / ((2 * radius + 1) * (2 * radius + 1) * 3);
}
} // namespace
int main() {
  try {
    require(glfwInit(), "GLFW init");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window = glfwCreateWindow(width, height, "Ring rendering tests",
                                   nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      Renderer normal(Renderer::defaultDataDirectory());
      ShadowFixture noRings(false, true), noSpheres(true, false),
          noShadows(false, false);
      Renderer ringShadowOff(noRings.path.string()),
          sphereShadowOff(noSpheres.path.string()),
          allShadowsOff(noShadows.path.string());
      const Vec3 pole = giantPole(), x = normalized(cross(pole, {0, 0, 1}));
      const Vec3 ringPoint = x * 1.52;
      auto s = scene(-x + pole * .25);
      auto a = render(normal, s), b = render(sphereShadowOff, s);
      double delta = difference(a, b, s, ringPoint);
      std::cout << "Planet -> rings shadow delta: " << delta << '\n';
      require(delta > 10, "planet must cast a visible shadow onto rings");

      s = scene({.3, .7, 1});
      s.bodies[2] = {ringPoint + s.sun * .5, .09, 3, 0};
      a = render(normal, s);
      b = render(sphereShadowOff, s);
      delta = difference(a, b, s, ringPoint);
      std::cout << "Moon -> rings shadow delta: " << delta << '\n';
      require(delta > 10, "moon must cast a visible shadow onto rings");

      s = scene(x * .78 + pole * .625, -1);
      // Choose a genuinely visible surface point below the ring plane.
      // The former point was on the far hemisphere; its projection sampled
      // an unrelated grazing ray rather than the specified shadow receiver.
      Vec3 surface =
          (x * .7 + cross(pole, x) * std::sqrt(.51)) * std::sqrt(.75) -
          pole * .5;
      require(dot(surface, s.observer - surface) > 0,
              "ring shadow receiver faces the observer");
      require(dot(surface, s.sun) > 0, "ring shadow receiver faces the Sun");
      a = render(normal, s);
      b = render(ringShadowOff, s);
      delta = difference(a, b, s, surface);
      std::cout << "Rings -> planet shadow delta: " << delta << '\n';
      require(delta > 5, "rings must shadow the giant's surface");

      s = scene(pole, -1);
      Vec3 moon = ringPoint - pole * .4;
      s.bodies[2] = {moon, .14, 2, 0};
      a = render(normal, s);
      b = render(ringShadowOff, s);
      delta = difference(a, b, s, moon, 10);
      std::cout << "Rings -> moon shadow delta: " << delta << '\n';
      require(delta > 1, "rings must attenuate sunlight reaching a moon");

      for (double radius : {1.30, 1.52, 1.6075}) {
        s = scene({0, 0, 1});
        const Vec3 point = x * radius, toward = normalized(s.observer - point);
        auto empty = render(allShadowsOff, s);
        s.bodies[2] = {point + toward * .3, .12, 3, 0};
        auto front = render(allShadowsOff, s);
        s.bodies[2].position = point - toward * .3;
        auto back = render(allShadowsOff, s);
        double frontDelta = difference(front, empty, s, point);
        double backDelta = difference(back, empty, s, point);
        std::cout << "Moon depth/transparency at " << radius
                  << ": front=" << frontDelta << " behind=" << backDelta
                  << '\n';
        require(frontDelta > 10, "foreground moon must occlude rings");
        require(
            backDelta > .5,
            "background moon must remain visible through translucent rings");
        if (radius == 1.52)
          require(backDelta < frontDelta * .8,
                  "dense bands must attenuate a background moon");
        else
          require(backDelta > frontDelta * .3,
                  "thin bands and gaps must transmit a background moon");
      }
      // Equal-radiance opaque layers must not acquire a dark silhouette at
      // partially covered sphere pixels. The former depth-tested ring pass
      // rejected their entire pixel and exposed the dark sky under the edge.
      ShadowFixture flatFixture(false, false, true);
      Renderer flat(flatFixture.path.string());
      s = scene({0, 0, 1});
      const Vec3 toward = normalized(s.observer - ringPoint);
      auto empty = render(flat, s);
      s.bodies[2] = {ringPoint + toward * .3, .05, 3, 0};
      auto foreground = render(flat, s);
      const auto [cx, cy] = project(s, ringPoint);
      int maxSeam = 0;
      for (int y = cy - 12; y <= cy + 12; ++y)
        for (int xPixel = cx - 12; xPixel <= cx + 12; ++xPixel)
          for (int c = 0; c < 3; ++c) {
            const auto at = (y * width + xPixel) * 4 + c;
            maxSeam = std::max(maxSeam,
                               std::abs(int(empty[at]) - int(foreground[at])));
          }
      std::cout << "Equal-radiance silhouette maximum delta: " << maxSeam
                << '\n';
      require(maxSeam <= 2, "ring/moon antialiasing must not leave dark seams");
      require(glGetError() == GL_NO_ERROR, "ring rendering GL errors");
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "All ring shadows, moon depth and transparency passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    glfwTerminate();
    return 1;
  }
}
