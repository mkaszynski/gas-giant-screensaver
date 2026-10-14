#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace observatory;
constexpr int width = 640, height = 360;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
std::vector<unsigned char> pixels() {
  std::vector<unsigned char> p(width * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  require(glGetError() == GL_NO_ERROR, "water GL errors");
  return p;
}
struct Fixture {
  std::filesystem::path path;
  Fixture() {
    char name[] = "/tmp/observatory-water-XXXXXX";
    path = mkdtemp(name);
    auto root = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    auto file = path / "shaders/surface.frag";
    std::ifstream in(file);
    std::ostringstream source;
    source << in.rdbuf();
    auto code = source.str();
    const std::string term = "*spec;";
    auto at = code.find(term);
    require(at != std::string::npos, "solar glint fixture");
    code.replace(at, term.size(), "*0.;");
    std::ofstream(file) << code;
  }
  ~Fixture() { std::filesystem::remove_all(path); }
};
int main() {
  try {
    require(glfwInit(), "GLFW init");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window =
        glfwCreateWindow(width, height, "Water tests", nullptr, nullptr);
    require(window, "GLES water context");
    glfwMakeContextCurrent(window);
    SurfaceWorld world(Renderer::defaultDataDirectory() +
                       "/assets/world/moon.ggworld");
    double route = -1;
    for (int i = 0; i < 8192; ++i) {
      double time = travelPeriod * i / 8192.;
      auto location = world.location(time);
      auto sample = world.sample(location.latitude, location.longitude);
      if (sample.ground > 0 && sample.ground < 80 && sample.temperature > 12) {
        route = time;
        break;
      }
    }
    require(route >= 0, "warm shoreline exists on route");
    double instant = 0, best = -1;
    for (int t = 0; t < 1800; t += 30) {
      auto s = travelScene(world, t, route);
      if (s.sunLocal.y > best) {
        best = s.sunLocal.y;
        instant = t;
      }
    }
    std::cout << "Coast fixture route=" << route << " daylight=" << instant
              << '\n';
    for (double position : {0., route}) {
      Renderer coarse(Renderer::defaultDataDirectory(), {},
                      {true, 0, position, true});
      Renderer fine(Renderer::defaultDataDirectory(), {},
                    {true, 0, position, true, true, false});
      for (double time : {instant, 1200.}) {
        coarse.render(width, height, time);
        auto a = pixels();
        fine.render(width, height, time);
        auto b = pixels();
        double sum = 0;
        int peak = 0;
        for (size_t i = 0; i < a.size(); ++i) {
          int delta = std::abs(int(a[i]) - int(b[i]));
          sum += delta;
          peak = std::max(peak, delta);
        }
        std::cout << "Deep-water LOD route=" << position << " time=" << time
                  << " mean=" << sum / a.size() << " peak=" << peak
                  << " codes\n";
        require(sum / a.size() < .005 && peak <= 3,
                "deep-water geometry preserves coast and ocean appearance");
      }
    }
    {
      Renderer r(Renderer::defaultDataDirectory(), {}, {true, 0, route, true});
      r.render(width, height, instant);
      auto mirror = pixels();
      require(r.surfaceReflectionRevision() == 1,
              "coasts render one coarse reflection view");
      {
        Renderer none(Renderer::defaultDataDirectory(), {},
                      {true, 0, route, false});
        none.render(width, height, instant);
        auto plain = pixels();
        require(none.surfaceReflectionRevision() == 0,
                "reflection comparison skips the pass");
        int changed = 0, peak = 0;
        for (int i = 0; i < width * height / 5; ++i)
          for (int c = 0; c < 3; ++c) {
            int delta =
                std::abs(int(mirror[4 * i + c]) - int(plain[4 * i + c]));
            changed += delta > 0;
            peak = std::max(peak, delta);
          }
        std::cout << "Reflected terrain: " << changed
                  << " changed channels, peak " << peak << " codes\n";
        require(changed > 100 && peak > 1,
                "coastal land is actually reflected on water");
      }
      auto stationaryScene = travelScene(world, instant, route);
      r.renderScene(width, height, stationaryScene, instant);
      mirror = pixels();
      r.renderScene(width, height, stationaryScene, instant + .0001);
      auto next = pixels();
      int jump = 0;
      size_t peakIndex = 0;
      for (size_t i = 0; i < next.size(); ++i)
        if (std::abs(int(mirror[i]) - int(next[i])) > jump) {
          jump = std::abs(int(mirror[i]) - int(next[i]));
          peakIndex = i;
        }
      std::cout << "Subframe water/reflection jump " << jump << " codes at "
                << (peakIndex / 4) % width << "," << (peakIndex / 4) / width
                << "\n";
      require(jump <= 5,
              "reflections and filtered waves have no subframe jump");
    }
    {
      // Isolate the physical water response under an unobstructed sun. This is
      // a light fixture, independent of ephemerides and the giant's eclipse
      // shadow.
      auto s = travelScene(world, 1740, 0);
      Vec3 ray =
          normalized(s.forward + s.cameraUp * (-.8 * std::tan(29 * pi / 180)));
      s.sunLocal = {ray.x, -ray.y, ray.z};
      s.sun =
          s.east * s.sunLocal.x + s.up * s.sunLocal.y + s.north * s.sunLocal.z;
      s.sunVisibility = 1;
      Renderer r(Renderer::defaultDataDirectory(), {}, {true, 0, 0});
      r.renderScene(width, height, s, 1740, 1800, 1, false);
      auto glint = pixels();
      require(r.surfaceReflectionRevision() == 0,
              "open ocean never draws terrain reflections");
      Fixture fixture;
      Renderer without(fixture.path.string(), {}, {true, 0, 0});
      without.renderScene(width, height, s, 1740, 1800, 1, false);
      auto plain = pixels();
      int bright = 0, peak = 0;
      for (int i = 0; i < width * height / 5; ++i)
        for (int c = 0; c < 3; ++c) {
          int delta = int(glint[4 * i + c]) - int(plain[4 * i + c]);
          bright += delta > 0;
          peak = std::max(peak, delta);
        }
      std::cout << "Filtered solar glints: " << bright
                << " brighter channels, peak " << peak << " codes\n";
      require(bright > 100 && peak > 5,
              "solar water BRDF contributes visible energy");
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
