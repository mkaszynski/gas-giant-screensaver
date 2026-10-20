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
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void replace(std::string &source, const std::string &from,
             const std::string &to) {
  const auto at = source.find(from);
  require(at != std::string::npos, "water preparation reference marker");
  source.replace(at, from.size(), to);
}
// Reconstruct the previous evaluation order from the current expressions.
// Everything else, including materials and all cloud/shadow code, stays shared.
struct EarlyPreparation {
  std::filesystem::path path;
  EarlyPreparation(const std::string &root) {
    char name[] = "/tmp/observatory-water-preparation-XXXXXX";
    const auto dir = mkdtemp(name);
    require(dir, "temporary water reference directory");
    path = dir;
    std::filesystem::copy(root + "/shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root + "/assets",
                                              path / "assets");
    const auto file = path / "shaders/surface.frag";
    std::ifstream input(file);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    auto source = buffer.str();
    const std::string phaseHeader = "vec4 wavePhase(){return ";
    const auto phaseBegin = source.find(phaseHeader);
    require(phaseBegin != std::string::npos, "wave phase reference expression");
    const auto expressionBegin = phaseBegin + phaseHeader.size();
    const auto expressionEnd = source.find(";}", expressionBegin);
    require(expressionEnd != std::string::npos, "wave phase expression end");
    const auto expression =
        source.substr(expressionBegin, expressionEnd - expressionBegin);
    const auto helperBegin =
        source.find("vec3 preparedOcean(vec3 d,vec4 foot){");
    require(helperBegin != std::string::npos,
            "water preparation reference body");
    const auto bodyBegin = source.find('{', helperBegin) + 1;
    const auto helperEnd = source.find("\n}", bodyBegin);
    require(helperEnd != std::string::npos, "water preparation reference end");
    auto preparation = source.substr(bodyBegin, helperEnd - bodyBegin);
    replace(preparation, " vec4 phase=wavePhase();", "");
    replace(preparation,
            " return oceanRadiance(d,waterNormal,phase,amplitude,foot);", "");
    source.erase(phaseBegin, helperEnd + 2 - phaseBegin);
    replace(source, " vec4 foot=fwidth(wavePhase());",
            " vec4 phase=" + expression + ";\n vec4 foot=fwidth(phase);" +
                preparation);
    for (int call = 0; call < 2; ++call)
      replace(source, "preparedOcean(d,foot)",
              "oceanRadiance(d,waterNormal,phase,amplitude,foot)");
    require(source.find("preparedOcean") == std::string::npos &&
                source.find("wavePhase") == std::string::npos,
            "reference must use the previous preparation order");
    std::ofstream(file) << source;
  }
  ~EarlyPreparation() { std::filesystem::remove_all(path); }
};
std::vector<unsigned char> capture(Renderer &renderer, const Scene &scene,
                                   int w, int h, double time, double weather) {
  renderer.renderScene(w, h, scene, time, 1800, 1, true, true,
                       {weather, 1. / 30}, weather);
  std::vector<unsigned char> result(w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
  require(glGetError() == GL_NO_ERROR, "water preparation GL errors");
  return result;
}
void compare(Renderer &current, Renderer &reference, const Scene &scene, int w,
             int h, double time, double weather) {
  const auto a = capture(current, scene, w, h, time, weather);
  const auto b = capture(reference, scene, w, h, time, weather);
  int peak = 0;
  double sum = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    if (i % 4 == 3)
      continue;
    const int error = std::abs(int(a[i]) - int(b[i]));
    peak = std::max(peak, error);
    sum += error;
  }
  const double mean = sum / (w * h * 3);
  std::cout << "Water preparation " << w << 'x' << h << " time=" << time
            << " weather=" << weather << " peak=" << peak << " mean=" << mean
            << '\n';
  require(peak <= 2 && mean <= .02,
          "water preparation must preserve the complete reference image");
}
} // namespace
int main() {
  try {
    require(glfwInit(), "GLFW init");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto *window = glfwCreateWindow(1920, 1080, "Water preparation parity",
                                    nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';
    const auto root =
        std::filesystem::absolute(Renderer::defaultDataDirectory()).string();
    EarlyPreparation original(root);
    SurfaceWorld world(root + "/assets/world/moon.ggworld");
    for (auto [route, time] : {std::array<double, 2>{162000., 510.},
                               {5600.390625, 1740.},
                               {210600., 360.},
                               {0., 1740.}}) {
      SurfaceOptions options{true, 0, route};
      Renderer current(root, {}, options),
          reference(original.path.string(), {}, options);
      for (auto [sky, weather] : {std::array<double, 2>{time, 1790900000.},
                                  {1200., 1512.8},
                                  {1180., 1260.}})
        compare(current, reference, travelScene(world, sky, route), 1920, 1080,
                sky, weather);
      if (route == 5600.390625)
        for (int frame = 0; frame < 8; ++frame) {
          const double dt = frame / 60.;
          compare(current, reference, travelScene(world, time + dt, route + dt),
                  641, 359, time + dt, 1790900000. + dt);
        }
    }
    for (bool reflections : {false, true}) {
      SurfaceOptions options{true, 0, 5600.390625};
      options.weather = false;
      options.reflections = reflections;
      Renderer current(root, {}, options),
          reference(original.path.string(), {}, options);
      for (double time : {510., 1180., 1740.})
        compare(current, reference, travelScene(world, time, options.offset),
                640, 360, time, 1790900000.);
    }
    {
      SurfaceOptions options{true, 0, 5600.390625};
      options.reflections = true;
      Renderer current(root, {}, options),
          reference(original.path.string(), {}, options);
      for (double time : {510., 1200.})
        compare(current, reference, travelScene(world, time, options.offset),
                640, 360, time, 1790900000.);
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Water preparation preserves reference images PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    glfwTerminate();
    return 1;
  }
}
