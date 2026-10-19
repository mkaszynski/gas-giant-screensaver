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
using namespace observatory;
namespace {
constexpr int width = 960, height = 540;
void require(bool b, const char *message) {
  if (!b)
    throw std::runtime_error(message);
}
void replaceBody(std::string &code, const std::string &signature) {
  auto at = code.find(signature);
  require(at != std::string::npos, "whitecap lighting fixture entry");
  auto begin = code.find('{', at), end = begin + 1;
  int depth = 1;
  while (end < code.size() && depth) {
    depth += code[end] == '{';
    depth -= code[end++] == '}';
  }
  require(depth == 0, "balanced fixture source");
  code.replace(begin, end - begin, "{return 0.;}");
}
struct Fixture {
  std::filesystem::path path;
  explicit Fixture(bool fullEvaluation = false) {
    char temporary[] = "/tmp/observatory-whitecaps-XXXXXX";
    require(mkdtemp(temporary), "whitecap fixture directory");
    path = temporary;
    auto root = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    auto file = path / "shaders/weather_common.glsl";
    std::ifstream input(file);
    std::ostringstream text;
    text << input.rdbuf();
    auto code = text.str();
    // Prescribed storm wind under unobstructed lighting isolates foam from
    // procedural cloud placement. Water shading/filtering stays in production.
    replaceBody(code,
                "float weatherDensity(vec3 p,float observerHeight,float lod)");
    replaceBody(code, "float weatherCirrus(vec3 p)");
    std::ofstream(file) << code;
    if (fullEvaluation) {
      file = path / "shaders/ocean_foam.glsl";
      std::ifstream foamInput(file);
      std::ostringstream foamText;
      foamText << foamInput.rdbuf();
      code = foamText.str();
      const std::string skip = " if(curvature>=2.*extent)return uWhitecaps.x;";
      const auto at = code.find(skip);
      require(at != std::string::npos, "full crest evaluation reference");
      code.erase(at, skip.size());
      std::ofstream(file) << code;
    }
  }
  ~Fixture() { std::filesystem::remove_all(path); }
};
std::vector<unsigned char> pixels() {
  std::vector<unsigned char> p(width * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  require(glGetError() == GL_NO_ERROR, "whitecaps preserve GL state");
  return p;
}
double difference(const std::vector<unsigned char> &a,
                  const std::vector<unsigned char> &b, int &peak) {
  double sum = 0.;
  peak = 0;
  // Lower view contains the sea; sky and celestial objects cannot satisfy it.
  for (int i = 0; i < width * height / 5; ++i)
    for (int c = 0; c < 3; ++c) {
      int d = std::abs(int(a[4 * i + c]) - int(b[4 * i + c]));
      peak = std::max(peak, d);
      sum += d;
    }
  return sum / (width * height / 5 * 3);
}
} // namespace
int main(int argc, char **argv) {
  try {
    require(glfwInit(), "GLFW");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto *window =
        glfwCreateWindow(width, height, "Whitecap optics", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      Fixture fixture;
      Renderer r(fixture.path.string(), {}, SurfaceOptions{true, 0, 0.});
      SurfaceWorld world(Renderer::defaultDataDirectory() +
                         "/assets/world/moon.ggworld");
      auto s = travelScene(world, 1740, 0);
      s.giantLight = 0.;
      s.sunLocal = normalized(s.right * .8 + Vec3{0., .6, 0.});
      s.sun =
          s.east * s.sunLocal.x + s.up * s.sunLocal.y + s.north * s.sunLocal.z;
      s.sunVisibility = 1.;
      const auto captures =
          argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{};
      if (!captures.empty())
        std::filesystem::create_directories(captures);
      auto capture = [&](bool enabled, double time, const char *name) {
        r.setWhitecaps(enabled);
        r.renderScene(width, height, s, time, 1800, 1, false, true, {},
                      1790900000.);
        auto image = pixels();
        if (!captures.empty() && name)
          savePng((captures / name).string(), width, height);
        return image;
      };
      r.setWeatherPreset(WeatherPreset::Storm);
      auto on = capture(true, 1740, "whitecaps-day.png");
      auto off = capture(false, 1740, "whitecaps-disabled.png");
      int peak = 0;
      const double day = difference(on, off, peak);
      std::cout << "Lit whitecaps mean=" << day << " peak=" << peak << '\n';
      require(day > .05 && peak > 3,
              "breaking wind waves are visibly shaded on the sea");
      {
        Fixture full(true);
        Renderer reference(full.path.string(), {}, SurfaceOptions{true, 0, 0.});
        reference.setWeatherPreset(WeatherPreset::Storm);
        reference.renderScene(width, height, s, 1740, 1800, 1, false, true, {},
                              1790900000.);
        auto expected = pixels();
        difference(on, expected, peak);
        require(peak <= 1,
                "unresolved-crest shortcut preserves complete evaluation");
      }
      auto next = capture(true, 1740.001, nullptr);
      difference(on, next, peak);
      require(peak <= 3, "filtered aerial foam has no subframe sparkle");
      auto rewind = capture(true, 1740, nullptr);
      difference(on, rewind, peak);
      require(peak == 0,
              "seeking reconstructs the same foam without temporal state");
      r.setWeatherPreset(WeatherPreset::Clear);
      auto calm = capture(true, 1740, nullptr);
      auto calmOff = capture(false, 1740, nullptr);
      difference(calm, calmOff, peak);
      require(peak == 0, "calm conditions do not paint foam across the ocean");
      r.setWeatherPreset(WeatherPreset::Storm);
      s.sunLocal.y = -.6;
      s.sun =
          s.east * s.sunLocal.x + s.up * s.sunLocal.y + s.north * s.sunLocal.z;
      s.sunVisibility = 0.;
      on = capture(true, 1740, "whitecaps-night.png");
      off = capture(false, 1740, nullptr);
      const double night = difference(on, off, peak);
      std::cout << "Unlit whitecaps mean=" << night << " peak=" << peak << '\n';
      require(night < day * .1, "foam does not emit its own daylight at night");
    }
    glfwDestroyWindow(window);
    glfwTerminate();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
