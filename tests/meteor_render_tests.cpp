#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace observatory;
void require(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
constexpr int width = 640, height = 360;
using Pixels = std::vector<unsigned char>;
Pixels shot(Renderer &r, Scene s, double phase, MeteorMode mode) {
  r.setMeteorFrame({1200 + phase, 1. / 30, {mode, 60, 1200}});
  r.renderScene(width, height, s, 1200, 1800, 1, false, false);
  Pixels p(width * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  require(glGetError() == GL_NO_ERROR,
          "meteor GLES state and shader compilation");
  return p;
}
struct Delta {
  int peak = 0, channels = 0;
  double sum = 0;
};
Delta difference(const Pixels &a, const Pixels &b) {
  Delta d;
  for (size_t i = 0; i < a.size(); ++i)
    if (i % 4 != 3) {
      int v = std::abs(int(a[i]) - int(b[i]));
      d.peak = std::max(d.peak, v);
      d.channels += v > 1;
      d.sum += v;
    }
  return d;
}
int main() {
  GLFWwindow *window = nullptr;
  try {
    require(glfwInit(), "GLFW");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window = glfwCreateWindow(width, height, "Meteor tests", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      Renderer renderer(Renderer::defaultDataDirectory(), {false, false});
      auto s = sceneAt(1200);
      s.sunLocal.y = -.4;
      s.giantLight = 0;
      s.bodies[0].radius = 1.e-5;
      auto reference = shot(renderer, s, .8, MeteorMode::Off);
      int peak = 0, changed = 0;
      double energy = 0;
      for (int frame = 0; frame < 64; ++frame) {
        auto image = shot(renderer, s, .45 + frame / 30., MeteorMode::Preview);
        auto delta = difference(reference, image);
        peak = std::max(peak, delta.peak);
        changed += delta.channels;
        energy += delta.sum;
      }
      std::cout << "Night meteor peak=" << peak << " changed=" << changed
                << " energy=" << energy << '\n';
      require(peak > 8 && changed > 100,
              "meteors must be visibly rendered in a dark sky");
      double minFlux = 1.e30, maxFlux = 0;
      for (double phase : {0., .25, .5, .75}) {
        auto moved = s;
        const double pixelAngle = 2 * std::tan(29 * pi / 180) / height;
        moved.forward = normalized(s.forward + s.right * (phase * pixelAngle));
        moved.right = normalized(cross(Vec3{0, 1, 0}, moved.forward));
        moved.cameraUp = cross(moved.forward, moved.right);
        auto off = shot(renderer, moved, .9, MeteorMode::Off);
        auto on = shot(renderer, moved, .9, MeteorMode::Preview);
        double flux = 0;
        for (size_t i = 0; i < on.size(); ++i)
          if (i % 4 != 3)
            flux += std::max(0., std::pow(on[i] / 255., 2.2) -
                                     std::pow(off[i] / 255., 2.2));
        minFlux = std::min(minFlux, flux);
        maxFlux = std::max(maxFlux, flux);
      }
      std::cout << "Subpixel meteor flux: " << minFlux << ".." << maxFlux
                << '\n';
      require(
          maxFlux > 0 && (maxFlux - minFlux) / maxFlux < .15,
          "subpixel streaks retain brightness instead of aliasing or blinking");
      auto day = s;
      day.sunLocal.y = .3;
      auto dayOff = shot(renderer, day, .9, MeteorMode::Off),
           dayOn = shot(renderer, day, .9, MeteorMode::Preview);
      require(difference(dayOff, dayOn).peak == 0,
              "daytime adds no meteor pixels");
      auto disabled = shot(renderer, s, .8, MeteorMode::Off);
      require(difference(reference, disabled).peak == 0,
              "disabling restores exact scene and GL state");
      // Full airborne weather must share the normal depth/composition path.
      Renderer weather(Renderer::defaultDataDirectory(), {false, false},
                       {true, 0, 162000});
      SurfaceWorld world(Renderer::defaultDataDirectory() +
                         "/assets/world/moon.ggworld");
      auto view = travelScene(world, 1200, 162000);
      view.sunLocal.y = -.4;
      view.giantLight = 0;
      view.bodies[0].radius = 1.e-5;
      weather.setWeatherPreset(WeatherPreset::Clear);
      auto base = shot(weather, view, .8, MeteorMode::Off);
      int clearPeak = 0;
      for (int i = 0; i < 48; ++i)
        clearPeak = std::max(clearPeak,
                             difference(base, shot(weather, view, .55 + i / 30.,
                                                   MeteorMode::Preview))
                                 .peak);
      require(clearPeak > 5, "meteors pass through the airborne renderer's "
                             "HDR/weather composition");
      {
        char temp[] = "/tmp/observatory-meteor-clouds-XXXXXX";
        auto fixture = std::filesystem::path(mkdtemp(temp));
        try {
          const auto data =
              std::filesystem::absolute(Renderer::defaultDataDirectory());
          std::filesystem::copy(data / "shaders", fixture / "shaders",
                                std::filesystem::copy_options::recursive);
          std::filesystem::create_directory_symlink(data / "assets",
                                                    fixture / "assets");
          auto path = fixture / "shaders/weather_common.glsl";
          std::ifstream in(path);
          std::ostringstream text;
          text << in.rdbuf();
          auto code = text.str();
          auto a = code.find("float weatherDensity("),
               b = code.find("float weatherCirrus(", a);
          require(a != std::string::npos && b != std::string::npos,
                  "opaque cloud fixture");
          code.replace(a, b - a,
                       "float weatherDensity(vec3 p,float eye,float "
                       "lod){return 100.;}\n");
          std::ofstream(path) << code;
          Renderer overcast(fixture.string(), {false, false},
                            {true, 0, 162000});
          overcast.setWeatherPreset(WeatherPreset::Storm);
          auto off = shot(overcast, view, .9, MeteorMode::Off);
          auto on = shot(overcast, view, .9, MeteorMode::Preview);
          require(difference(off, on).peak <= 1,
                  "opaque real cloud volume hides meteors above it");
          std::filesystem::remove_all(fixture);
        } catch (...) {
          std::filesystem::remove_all(fixture);
          throw;
        }
      }
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
