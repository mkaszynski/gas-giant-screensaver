#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include "surface_renderer.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace observatory;
namespace {
constexpr int width = 480, height = 270;
void require(bool ok, const char *reason) {
  if (!ok)
    throw std::runtime_error(reason);
}
// Keep the old uniform branches in an independent shader fixture. Both paths
// consume identical assets and scene inputs, including the real coastal water.
struct UniformBranches {
  std::filesystem::path path;
  UniformBranches() {
    char name[] = "/tmp/observatory-weather-specialization-XXXXXX";
    path = mkdtemp(name);
    const auto root =
        std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    const auto file = path / "shaders/weather_common.glsl";
    std::ifstream in(file);
    std::ostringstream text;
    text << in.rdbuf();
    auto source = text.str();
    const std::string constant =
        "const bool uWeatherEnabled=OBSERVATORY_WEATHER_MODE!=0;";
    const auto at = source.find(constant);
    require(at != std::string::npos, "weather specialization fixture");
    source.replace(at, constant.size(), "uniform bool uWeatherEnabled;");
    std::ofstream(file) << source;
    const auto surfaceFile = path / "shaders/surface.frag";
    std::ifstream surfaceIn(surfaceFile);
    std::ostringstream surfaceText;
    surfaceText << surfaceIn.rdbuf();
    source = surfaceText.str();
    const std::string deepWater = "#ifdef SURFACE_DEEP_WATER";
    const auto waterAt = source.find(deepWater);
    require(waterAt != std::string::npos, "deep-water specialization fixture");
    source.replace(waterAt, deepWater.size(), "#if 0");
    const std::string pureLand = "#ifdef SURFACE_PURE_LAND";
    const auto landAt = source.find(pureLand);
    require(landAt != std::string::npos, "pure-land specialization fixture");
    source.replace(landAt, pureLand.size(), "#if 0");
    std::ofstream(surfaceFile) << source;
  }
  ~UniformBranches() { std::filesystem::remove_all(path); }
};
std::vector<unsigned char> shot(Renderer &renderer, double time) {
  renderer.render(width, height, time);
  std::vector<unsigned char> pixels(width * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  require(glGetError() == GL_NO_ERROR, "specialized shader GL state");
  return pixels;
}
void compare(const std::vector<unsigned char> &a,
             const std::vector<unsigned char> &b) {
  int peak = 0;
  double sum = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    const int difference = std::abs(int(a[i]) - int(b[i]));
    peak = std::max(peak, difference);
    sum += difference;
  }
  std::cout << "Surface specialization mean=" << sum / a.size()
            << " peak=" << peak << " codes\n";
  require(peak <= 2 && sum / a.size() < .003,
          "specialization preserves the complete rendered image");
}
void distanceTarget(const std::string &root) {
  GLuint noise = 0;
  glGenTextures(1, &noise);
  glActiveTexture(GL_TEXTURE15);
  glBindTexture(GL_TEXTURE_3D, noise);
  const unsigned char empty = 0;
  glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, 1, 1, 1, 0, GL_RED,
                GL_UNSIGNED_BYTE, &empty);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  for (bool weather : {false, true}) {
    SurfaceRenderer surface(root, false, true, true, weather);
    const auto scene = travelScene(surface.world(), 1740, 5600.390625);
    for (int size : {64, 80}) {
      surface.render(scene, {scene.latitude, scene.longitude}, size, size / 2,
                     1740, 1800, 0, [&](GLuint program) {
                       glUseProgram(program);
                       glUniform1i(glGetUniformLocation(program, "uWeatherNoise"),
                                   15);
                       glActiveTexture(GL_TEXTURE15);
                       glBindTexture(GL_TEXTURE_3D, noise);
                       glUniform4f(glGetUniformLocation(program, "uWeatherLayer"),
                                   1, 2, 0, 1);
                     });
      GLint attachment = 0;
      glGetFramebufferAttachmentParameteriv(
          GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attachment);
      require(weather ? attachment == GL_TEXTURE : attachment == GL_NONE,
              "only volumetric weather requests a distance target after resize");
      require(weather ? glIsTexture(surface.distanceTexture())
                      : surface.distanceTexture() == 0,
              "distance texture availability follows the selected cloud model");
      require(glGetError() == GL_NO_ERROR, "single/double target resolve");
    }
    glUseProgram(0);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  glDeleteTextures(1, &noise);
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
    window = glfwCreateWindow(width, height, "Weather variants", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      distanceTarget(Renderer::defaultDataDirectory());
      UniformBranches original;
      for (double route : {0., 5600.390625, 162000., 210600.}) {
        SurfaceOptions options{true, 0, route};
        options.weather = false;
        Renderer specialized(Renderer::defaultDataDirectory(), {}, options);
        Renderer dynamic(original.path.string(), {}, options);
        for (double time : {510., 1180., 1740.}) {
          std::cout << "Route " << route << " time " << time << '\n';
          compare(shot(specialized, time), shot(dynamic, time));
        }
      }
      // Native/default weather stays available; specialization must not turn
      // off its actual clouds, transmission or reflected lighting.
      for (double route : {0., 5600.390625, 162000., 210600.}) {
        SurfaceOptions options{true, 0, route};
        Renderer weather(Renderer::defaultDataDirectory(), {}, options);
        Renderer dynamicWeather(original.path.string(), {}, options);
        for (double time : {510., 1180., 1740.})
          compare(shot(weather, time), shot(dynamicWeather, time));
      }
      // The historic fixed-view path has no weather program either.
      Renderer fixed(Renderer::defaultDataDirectory());
      Renderer dynamicFixed(original.path.string());
      compare(shot(fixed, 1200), shot(dynamicFixed, 1200));
    }
    glfwDestroyWindow(window);
    glfwTerminate();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    if (window)
      glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }
}
