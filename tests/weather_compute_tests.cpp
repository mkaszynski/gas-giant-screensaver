#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLES3/gl31.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

using namespace observatory;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
using ImageState = std::array<std::array<GLint, 6>, 2>;
ImageState imageState() {
  constexpr GLenum names[] = {
      GL_IMAGE_BINDING_NAME,  GL_IMAGE_BINDING_LEVEL,  GL_IMAGE_BINDING_LAYERED,
      GL_IMAGE_BINDING_LAYER, GL_IMAGE_BINDING_ACCESS, GL_IMAGE_BINDING_FORMAT};
  ImageState state{};
  for (GLuint unit = 0; unit < state.size(); ++unit)
    for (size_t field = 0; field < std::size(names); ++field)
      glGetIntegeri_v(names[field], unit, &state[unit][field]);
  return state;
}
std::vector<unsigned char> capture(Renderer &renderer, const Scene &scene,
                                   int width, int height, double seconds,
                                   double weather) {
  const auto images = imageState();
  renderer.renderScene(width, height, scene, seconds, 1800, 1, true, true,
                       {weather, 1. / 30}, weather);
  require(imageState() == images, "weather must restore host image bindings");
  std::vector<unsigned char> result(width * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
  require(glGetError() == GL_NO_ERROR, "weather compute GL state");
  return result;
}
void compare(Renderer &compute, Renderer &fragment, const Scene &scene,
             int width, int height, double seconds, double weather) {
  const auto a = capture(compute, scene, width, height, seconds, weather);
  const auto b = capture(fragment, scene, width, height, seconds, weather);
  int peak = 0;
  double sum = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    if (i % 4 == 3)
      continue;
    const int error = std::abs(int(a[i]) - int(b[i]));
    peak = std::max(peak, error);
    sum += error;
  }
  const double mean = sum / (width * height * 3);
  std::cout << "Compute/fragment " << width << 'x' << height
            << " sky=" << seconds << " weather=" << weather << " peak=" << peak
            << " mean=" << mean << '\n';
  require(peak <= 2 && mean <= .02,
          "compute must preserve the complete reference image");
}
} // namespace
int main() {
  GLFWwindow *window = nullptr;
  try {
    require(glfwInit(), "GLFW");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window = glfwCreateWindow(1920, 1080, "Weather compute parity", nullptr,
                              nullptr);
    require(window, "GLES 3.1 compute test context");
    glfwMakeContextCurrent(window);
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';
    const auto data = Renderer::defaultDataDirectory();
    {
      // Nondefault host bindings must survive both paths and viewport changes.
      std::array<GLuint, 2> host{};
      glGenTextures(host.size(), host.data());
      for (GLuint unit = 0; unit < host.size(); ++unit) {
        glBindTexture(GL_TEXTURE_2D, host[unit]);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 2, 2);
        glBindImageTexture(unit, host[unit], 0, GL_FALSE, 0,
                           unit ? GL_WRITE_ONLY : GL_READ_WRITE, GL_RGBA8);
      }
      SurfaceWorld world(data + "/assets/world/moon.ggworld");
      for (auto [route, sky] : {std::array<double, 2>{162000., 510.},
                                {5600.390625, 1740.},
                                {210600., 360.},
                                {0., 1740.}}) {
        SurfaceOptions options{true, 0, route};
        Renderer compute(data, {}, options);
        require(compute.computingWeather(),
                "native compute path must be active");
        require(compute.computingWeatherCache(),
                "coarse sky compute path must be active");
        options.weatherCompute = false;
        Renderer fragment(data, {}, options);
        require(!fragment.computingWeather(), "explicit fragment fallback");
        require(!fragment.computingWeatherCache(),
                "explicit fallback must retain fragment sky cache");
        const auto scene = travelScene(world, sky, route);
        compare(compute, fragment, scene, 1920, 1080, sky, 1790900000.);
        require(compute.clearWeatherRows() == fragment.clearWeatherRows(),
                "fragment and compute use identical clear-row bounds");
        if (route == 162000.)
          require(compute.clearWeatherRows() > 0,
                  "above-cloud sky must exercise the smaller exact solver");
        if (route == 5600.390625)
          require(compute.clearWeatherRows() > 0,
                  "below-base coastal rows must exercise the smaller solver");
        if (route == 0.)
          require(compute.clearWeatherRows() == 0,
                  "inside/below-cloud view must keep every volume column");
        require(compute.computingCalmWeather() == (route != 0.),
                "only exact zero-storm states select calm compute");
        if (route != 162000.)
          continue;
        // Repeated width-only, height-only and full changes exercise both
        // coordinate strips and partial compute workgroups at the last pixel.
        for (auto size : {std::array<int, 2>{640, 360},
                          {641, 360},
                          {641, 359},
                          // Cover dispatch boundaries and a viewport smaller
                          // than one workgroup, including the sky-cache pass.
                          {31, 1},
                          {32, 2},
                          {33, 3},
                          {65, 5},
                          {1920, 1080}})
          compare(compute, fragment, scene, size[0], size[1], sky, 1790900000.);
        for (auto preset : {WeatherPreset::Clear, WeatherPreset::Fair,
                            WeatherPreset::Overcast, WeatherPreset::Fog,
                            WeatherPreset::Storm}) {
          compute.setWeatherPreset(preset);
          fragment.setWeatherPreset(preset);
          compare(compute, fragment, scene, 640, 360, sky, 1260.);
          require(compute.computingCalmWeather() ==
                      (preset != WeatherPreset::Storm),
                  "preset change must select the matching compute solver");
          const auto night = travelScene(world, 1200, route);
          compare(compute, fragment, night, 640, 360, 1200, 1512.8);
        }
        // Jump between calm, storm and an exposure-integrated lightning pulse
        // on the same renderer. A previous specialization must never suppress
        // a newly active anvil or flash, including after a backwards time seek.
        compute.setWeatherPreset(WeatherPreset::Auto);
        fragment.setWeatherPreset(WeatherPreset::Auto);
        for (double clock : {1790900000., 1260., 1512.8, 1790900000., 1512.8}) {
          compare(compute, fragment, scene, 640, 360, sky, clock);
          require(
              compute.computingCalmWeather() == (clock == 1790900000.),
              "weather clock seek must switch calm/full solver immediately");
        }
        // The manual meteor pass blends into both weather attachments after
        // compute. This checks the image-to-framebuffer visibility barrier.
        MeteorOptions meteors;
        meteors.mode = MeteorMode::Preview;
        meteors.rate = 1800;
        meteors.previewStart = 1254.;
        compute.setMeteorFrame({1254.9, 1. / 30, meteors});
        fragment.setMeteorFrame({1254.9, 1. / 30, meteors});
        compare(compute, fragment, travelScene(world, 1254.9, route), 640, 360,
                1254.9, 1512.8);
        for (int frame = 1; frame <= 16; ++frame) {
          const double step = frame / 30.;
          compare(compute, fragment,
                  travelScene(world, 1254.9 + step, route + step), 640, 360,
                  1254.9 + step, 1512.8 + step);
        }
      }
      {
        SurfaceOptions options{true, 0, 162000};
        options.weatherCache = false;
        Renderer compute(data, {}, options);
        require(!compute.computingWeatherCache(),
                "disabled sky cache must not allocate a compute program");
        options.weatherCompute = false;
        Renderer fragment(data, {}, options);
        compare(compute, fragment, travelScene(world, 900, 162000), 641, 359,
                900, 1790900000.);
      }
      {
        SurfaceOptions options{true};
        options.weather = false;
        Renderer thin(data, {}, options);
        require(!thin.computingWeather(),
                "thin-cloud comparison needs no compute");
      }
      for (GLuint unit = 0; unit < host.size(); ++unit)
        glBindImageTexture(unit, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
      glDeleteTextures(host.size(), host.data());
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    if (window)
      glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }
}
