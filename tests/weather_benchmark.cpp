#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include "weather_profile.hpp"
#include <GLES2/gl2ext.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace observatory {
GLuint weatherProfileQueries[weatherProfileNames.size()]{};
}
int main(int argc, char **argv) {
  using namespace observatory;
  try {
    const std::string data =
        argc > 1 ? argv[1] : Renderer::defaultDataDirectory();
    const int width = argc > 2 ? std::stoi(argv[2]) : 1920;
    const int height = argc > 3 ? std::stoi(argv[3]) : 1080;
    const int count = argc > 4 ? std::stoi(argv[4]) : 120;
    const double sky = argc > 5 ? std::stod(argv[5]) : 1740;
    const double route = argc > 6 ? std::stod(argv[6]) : 5600.390625;
    const double clock = argc > 7 ? std::stod(argv[7]) : 1790900000.;
    const double step = argc > 8 ? std::stod(argv[8]) : 0.;
    const int cache = argc > 9 ? std::stoi(argv[9]) : 1;
    if (argc > 10 || cache < 0 || cache > 1 || width < 1 || height < 1 ||
        width > 16384 || height > 16384 || count < 1 || count > 10000 ||
        !std::isfinite(sky) || !std::isfinite(route) || !std::isfinite(clock) ||
        !std::isfinite(step) || step < 0 || step > 60)
      throw std::runtime_error("Usage: weather-benchmark [data width height "
                               "frames sky route weather step cache]");
    if (!glfwInit())
      throw std::runtime_error("GLFW initialization failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto *window =
        glfwCreateWindow(width, height, "Weather benchmark", nullptr, nullptr);
    if (!window)
      throw std::runtime_error("GLES context unavailable");
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    if (!glfwExtensionSupported("GL_EXT_disjoint_timer_query"))
      throw std::runtime_error("GPU elapsed queries unavailable");
    glGenQueries(weatherProfileNames.size(), weatherProfileQueries);
    std::cerr << "Renderer: " << glGetString(GL_RENDERER) << '\n';
    std::cout << "frame,scene_seconds,render_ms";
    for (auto name : weatherProfileNames)
      std::cout << ',' << name << "_ms";
    std::cout << '\n' << std::setprecision(12);
    {
      SurfaceWorld world(data + "/assets/world/moon.ggworld");
      SurfaceOptions options{true, 0, route};
      options.weatherCache = cache;
      Renderer renderer(data, {}, options);
      for (int frame = -30; frame < count; ++frame) {
        const double elapsed = std::max(frame, 0) * step;
        const auto scene = travelScene(world, sky + elapsed, route + elapsed);
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        renderer.renderScene(width, height, scene, sky + elapsed, 1800, 1, true,
                             true, {clock + elapsed, 1. / 30}, clock + elapsed);
        glFinish();
        const double total =
            std::chrono::duration<double, std::milli>(Clock::now() - start)
                .count();
        GLint disjoint = 0;
        glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
        if (disjoint)
          throw std::runtime_error("Disjoint GPU clock; rerun benchmark");
        if (frame >= 0)
          std::cout << frame << ',' << sky + elapsed << ',' << total;
        for (size_t stage = 0; stage < weatherProfileNames.size(); ++stage) {
          GLuint ns = 0;
          glGetQueryObjectuiv(weatherProfileQueries[stage], GL_QUERY_RESULT,
                              &ns);
          if (frame >= 0)
            std::cout << ',' << ns / 1.e6;
        }
        if (frame >= 0)
          std::cout << '\n';
        if (glGetError() != GL_NO_ERROR)
          throw std::runtime_error("GPU profiling error");
        std::this_thread::sleep_until(start + std::chrono::microseconds(33333));
      }
    }
    glDeleteQueries(weatherProfileNames.size(), weatherProfileQueries);
    glfwDestroyWindow(window);
    glfwTerminate();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
