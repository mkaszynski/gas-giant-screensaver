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
std::string read(const std::filesystem::path &p) {
  std::ifstream input(p);
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}
struct Reference {
  std::filesystem::path path;
  explicit Reference(const std::filesystem::path &root) {
    char temporary[] = "/tmp/observatory-water-reuse-XXXXXX";
    require(mkdtemp(temporary), "water reference directory");
    path = temporary;
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    auto file = path / "shaders/surface.frag";
    auto code = read(file);
    const std::string foam = " float foam=oceanFoamCoverage(phase,footprint);\n";
    if (auto at = code.find(foam); at != std::string::npos) {
      code.erase(at, foam.size());
      const auto late = code.find(" vec3 base=L*fresnel");
      require(late != std::string::npos, "late foam preparation reference");
      code.insert(late, foam);
    }
    const auto begin = code.find(" vec3 reflected=reflect(d,wn);");
    const auto end = code.find(" // Solar energy", begin);
    require(begin != std::string::npos && end != std::string::npos,
            "unconditional reflection reference entry");
    code.replace(begin, end - begin,
        " vec3 reflected=reflect(d,wn);float z=dot(reflected,uForward);vec3 L=sky(reflected);\n"
        " if(z>0.){vec2 p=.5+.5*vec2(dot(reflected,uRight)/(z*uTanFov*uResolution.x/uResolution.y),dot(reflected,uUp)/(z*uTanFov));\n"
        "  if(all(greaterThanEqual(p,vec2(0)))&&all(lessThanEqual(p,vec2(1))))L=mix(L,texture(uScene,p).rgb/uSceneScale,frameConfidence(p));}\n");
    std::ofstream(file) << code;
    file = path / "shaders/weather_common.glsl";
    code = read(file);
    const std::string skip = " if(opacity<=0.)return L;";
    const auto at = code.find(skip);
    require(at != std::string::npos, "unconditional cloud lighting reference");
    code.erase(at, skip.size());
    std::ofstream(file) << code;
  }
  ~Reference() { std::filesystem::remove_all(path); }
};
std::vector<unsigned char> capture(Renderer &r, const Scene &s, int w, int h,
                                   double time, double weather) {
  r.renderScene(w, h, s, time, 1800, 1, true, true,
                 {weather, 1. / 30}, weather);
  std::vector<unsigned char> p(size_t(w) * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  require(glGetError() == GL_NO_ERROR, "water reuse GL state");
  return p;
}
} // namespace
int main() {
  try {
    require(glfwInit(), "GLFW");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto *window = glfwCreateWindow(1920, 1080, "Water reuse parity", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      const auto root = std::filesystem::absolute(Renderer::defaultDataDirectory());
      Reference reference(root);
      SurfaceWorld world((root / "assets/world/moon.ggworld").string());
      for (double route : {0., 5600.390625, 162000.}) {
        for (bool weather : {false, true}) {
          SurfaceOptions options{true, 0, route};
          options.weather = weather;
          options.reflections = route != 0.;
          Renderer current(root.string(), {}, options);
          Renderer previous(reference.path.string(), {}, options);
          for (auto preset : {WeatherPreset::Clear, WeatherPreset::Fair,
                              WeatherPreset::Storm}) {
            current.setWeatherPreset(preset);
            previous.setWeatherPreset(preset);
            for (double time : {510., 1180., 1740.}) {
              const int w = route == 5600.390625 && time == 1740. ? 1920 : 640;
              const int h = w * 9 / 16;
              const auto scene = travelScene(world, time, route);
              auto a = capture(current, scene, w, h, time, 1790900000.);
              auto b = capture(previous, scene, w, h, time, 1790900000.);
              int peak = 0;
              double sum = 0;
              for (size_t i = 0; i < a.size(); ++i) {
                int delta = std::abs(int(a[i]) - int(b[i]));
                peak = std::max(peak, delta);
                sum += delta;
              }
              std::cout << "Water reuse route=" << route << " sky=" << time
                        << " weather=" << weather << " preset=" << int(preset)
                        << " peak=" << peak << " mean=" << sum / a.size() << '\n';
              require(peak <= 1 && sum / a.size() < .0001,
                      "skipping unused water terms preserves the reference image");
            }
          }
        }
      }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
