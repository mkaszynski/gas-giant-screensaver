#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace observatory;
namespace {
constexpr int width = 640, height = 360;
using Pixels = std::vector<unsigned char>;
void require(bool ok, const char *reason) {
  if (!ok)
    throw std::runtime_error(reason);
}
struct Fixture {
  std::filesystem::path root;
  Fixture() {
    char path[] = "/tmp/observatory-cloud-sampling-XXXXXX";
    root = mkdtemp(path);
    const auto data =
        std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(data / "shaders", root / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(data / "assets", root / "assets");
  }
  ~Fixture() { std::filesystem::remove_all(root); }
  void replace(const std::string &term, const std::string &replacement) {
    const auto file = root / "shaders/weather.frag";
    std::ifstream in(file);
    std::ostringstream text;
    text << in.rdbuf();
    auto source = text.str();
    const auto at = source.find(term);
    require(at != std::string::npos, "cloud sampling reference fixture");
    source.replace(at, term.size(), replacement);
    std::ofstream(file) << source;
  }
};
Pixels capture(Renderer &renderer, const Scene &scene) {
  renderer.setWeatherPreset(WeatherPreset::Fair);
  renderer.renderScene(width, height, scene, 1740, 1800, 1, true, true, {},
                       1790900000);
  Pixels pixels(width * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  require(glGetError() == GL_NO_ERROR, "cloud sampling GL state");
  return pixels;
}
double bandError(const Pixels &a, const Pixels &b) {
  double error = 0;
  size_t channels = 0;
  // Grazing sky columns immediately above the horizon; no terrain/reflections.
  for (int y = height * .175; y < height * .395; ++y)
    for (int x = 0; x < width; ++x)
      for (int c = 0; c < 3; ++c) {
        const size_t i = (y * width + x) * 4 + c;
        error += std::abs(int(a[i]) - int(b[i]));
        ++channels;
      }
  return error / channels;
}
} // namespace
int main() {
  GLFWwindow *window = nullptr;
  try {
    require(glfwInit(), "GLFW");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window = glfwCreateWindow(width, height, "Cloud sampling", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      Fixture old, reference;
      old.replace("clamp(span/.15,12.,96.)", "clamp(span/.15,12.,48.)");
      old.replace("i<96", "i<48");
      old.replace("if(stepLength>.18)", "if(false)");
      reference.replace("clamp(span/.15,12.,96.)",
                        "clamp(span/.035,48.,384.)");
      reference.replace("i<96", "i<384");
      SurfaceOptions options{true, 0, 5600.390625};
      options.weatherCache = false;
      SurfaceWorld world(Renderer::defaultDataDirectory() +
                         "/assets/world/moon.ggworld");
      const auto scene = travelScene(world, 1740, 5600.390625);
      Renderer legacy(old.root.string(), {}, options);
      Renderer improved(Renderer::defaultDataDirectory(), {}, options);
      Renderer precise(reference.root.string(), {}, options);
      const auto truth = capture(precise, scene);
      const auto before = capture(legacy, scene), after = capture(improved, scene);
      const double oldError = bandError(before, truth),
                   newError = bandError(after, truth);
      std::cout << "Fair-weather grazing cloud error: " << oldError << " -> "
                << newError << " display codes\n";
      require(oldError > .02, "fixture must expose the original cloud banding");
      require(newError < oldError * .65,
              "grazing integration must converge towards dense quadrature");
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    if (window)
      glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }
}
