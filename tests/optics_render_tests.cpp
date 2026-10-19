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
void require(bool b, const char *message) {
  if (!b)
    throw std::runtime_error(message);
}
std::vector<unsigned char> pixels(int w, int h) {
  std::vector<unsigned char> p(w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  return p;
}
void replaceBody(std::string &code, const std::string &signature,
                 const std::string &body) {
  const auto at = code.find(signature);
  require(at != std::string::npos, "physical cloud fixture entry");
  const auto begin = code.find('{', at);
  size_t end = begin + 1;
  int depth = 1;
  for (; end < code.size() && depth; ++end) {
    depth += code[end] == '{';
    depth -= code[end] == '}';
  }
  require(depth == 0, "balanced cloud fixture body");
  code.replace(begin, end - begin, "{" + body + "}");
}
struct Fixture {
  std::filesystem::path path;
  explicit Fixture(int mode) {
    char name[] = "/tmp/observatory-optics-render-XXXXXX";
    path = mkdtemp(name);
    const auto root =
        std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    auto file = path / "shaders/weather_common.glsl";
    std::ifstream in(file);
    std::ostringstream text;
    text << in.rdbuf();
    auto code = text.str();
    // Isolate an illuminated physical layer from procedural cloud placement.
    // The production phase functions, units, extinction and composition stay.
    replaceBody(code, "float weatherCirrus(vec3 p)",
                mode == 0 ? "return .018;" : "return 0.;");
    replaceBody(
        code, "float weatherDensity(vec3 p,float observerHeight,float lod)",
        mode == 0 ? "return 0.;"
                  : "float "
                    "z=(weatherHeight(p,observerHeight)-uWeatherLayer.x)/"
                    "uWeatherLayer.y;return z>0.&&z<1.?" +
                        std::string(mode == 1 ? "1.2" : "6.") + ":0.;");
    if (mode == 2)
      replaceBody(code, "float weatherShadowAt(vec3 p,float observerHeight)",
                  "return 1.;");
    std::ofstream(file) << code;
    if (mode == 2) {
      // A sunlit rain curtain outside the parent cloud isolates large-drop
      // scattering. Keep production rain integration and phase/composition.
      file = path / "shaders/weather.frag";
      std::ifstream rainIn(file);
      std::ostringstream rainText;
      rainText << rainIn.rdbuf();
      code = rainText.str();
      replaceBody(code,
                  "float marchDensity(vec3 direction,float distance,float lod)",
                  "return 0.;");
      std::ofstream(file) << code;
    }
  }
  ~Fixture() { std::filesystem::remove_all(path); }
};
void solarScene(Scene &s, double altitude, double azimuth,
                double viewElevation) {
  const Vec3 forward = normalized(Vec3{s.forward.x, 0, s.forward.z});
  s.right = normalized(Vec3{s.right.x, 0, s.right.z});
  const double a = altitude * pi / 180, az = azimuth * pi / 180,
               v = viewElevation * pi / 180;
  s.forward = forward * std::cos(v) + Vec3{0, std::sin(v), 0};
  s.cameraUp = normalized(cross(s.forward, s.right));
  s.sunLocal = (forward * std::cos(az) + s.right * std::sin(az)) * std::cos(a) +
               Vec3{0, std::sin(a), 0};
  s.sun = s.east * s.sunLocal.x + s.up * s.sunLocal.y + s.north * s.sunLocal.z;
  s.sunVisibility = 1.;
}
double difference(const std::vector<unsigned char> &a,
                  const std::vector<unsigned char> &b, int &peak) {
  double sum = 0;
  peak = 0;
  for (size_t i = 0; i < a.size(); ++i)
    if (i % 4 != 3) {
      int d = std::abs(int(a[i]) - int(b[i]));
      sum += d;
      peak = std::max(peak, d);
    }
  return sum / (a.size() / 4 * 3);
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
        glfwCreateWindow(960, 540, "Atmospheric optics", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';
    std::filesystem::path captures = argc > 1 ? argv[1] : "";
    if (!captures.empty())
      std::filesystem::create_directories(captures);
    {
      SurfaceWorld world(Renderer::defaultDataDirectory() +
                         "/assets/world/moon.ggworld");
      constexpr int w = 960, h = 540;
      for (int mode = 0; mode < 3; ++mode) {
        Fixture fixture(mode);
        Renderer renderer(fixture.path.string(), {},
                          SurfaceOptions{true, 0, 0.});
        renderer.setWeatherPreset(mode == 2 ? WeatherPreset::Storm
                                            : WeatherPreset::Fair);
        auto scene = travelScene(world, 510, 0.);
        if (mode == 2)
          scene.altitudeMeters = 100.;
        solarScene(scene, mode == 0 ? 18. : (mode == 2 ? 25. : 8.),
                   mode == 0 ? 0. : 180.,
                   mode == 0 ? 18. : (mode == 2 ? 10. : 2.));
        auto capture = [&](bool enabled) {
          renderer.setAtmosphericOptics(enabled);
          renderer.renderScene(w, h, scene, 510, 1800, 1, false, true, {},
                               1790900000.);
          require(glGetError() == GL_NO_ERROR,
                  "optics preserves valid GL state");
          return pixels(w, h);
        };
        auto on = capture(true);
        const std::string label = mode == 0   ? "halo-sundogs"
                                  : mode == 1 ? "glory"
                                              : "rainbow";
        if (!captures.empty())
          savePng((captures / ("optics-" + label + ".png")).string(), w, h);
        auto off = capture(false);
        int peak = 0;
        double mean = difference(on, off, peak);
        std::cout << label << " mean=" << mean << " peak=" << peak
                  << " codes\n";
        require(mean > .005 && peak > 2,
                "physical layers must show visible angular optics");
        if (mode == 0) {
          for (int y = 0; y < h / 32; ++y)
            for (int x = 0; x < w; ++x)
              for (int c = 0; c < 3; ++c)
                require(
                    on[(y * w + x) * 4 + c] == off[(y * w + x) * 4 + c],
                    "high ice cannot paint a halo through opaque foreground");
          scene.sunVisibility = 0.;
          on = capture(true);
          off = capture(false);
          difference(on, off, peak);
          require(peak == 0, "total eclipses remove solar halos and sundogs");
          solarScene(scene, -25., 0., 18.);
          on = capture(true);
          off = capture(false);
          difference(on, off, peak);
          require(peak == 0, "night has no self-luminous solar optics");
        }
      }
      Renderer clear(Renderer::defaultDataDirectory(), {},
                     SurfaceOptions{true, 0, 0.});
      clear.setWeatherPreset(WeatherPreset::Clear);
      auto scene = travelScene(world, 510, 0.);
      solarScene(scene, 18., 0., 18.);
      clear.setAtmosphericOptics(true);
      clear.renderScene(w, h, scene, 510);
      auto on = pixels(w, h);
      clear.setAtmosphericOptics(false);
      clear.renderScene(w, h, scene, 510);
      int peak = 0;
      difference(on, pixels(w, h), peak);
      require(peak == 0, "clear air has no crystal or droplet optics");
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout
        << "Optical layers, eclipses, darkness and opaque foreground passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
