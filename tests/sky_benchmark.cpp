#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLES2/gl2ext.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
namespace observatory {
GLuint skyProfileQueries[2]{};
}
int main(int argc, char **argv) {
  try {
    const double time = argc > 1 ? std::stod(argv[1]) : 900.;
    const int width = argc > 2 ? std::stoi(argv[2]) : 1920;
    const int height = argc > 3 ? std::stoi(argv[3]) : 1080;
    const int count = argc > 4 ? std::stoi(argv[4]) : 240;
    const std::string data =
        argc > 5 ? argv[5] : observatory::Renderer::defaultDataDirectory();
    if (!std::isfinite(time) || width < 1 || height < 1 || width > 16384 ||
        height > 16384 || count < 1 || count > 10000 || argc > 6)
      throw std::runtime_error(
          "Usage: sky-benchmark [time width height frames data-directory]");
    if (!glfwInit())
      throw std::runtime_error("GLFW initialization failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto *window =
        glfwCreateWindow(width, height, "Sky benchmark", nullptr, nullptr);
    if (!window)
      throw std::runtime_error("GLES context unavailable");
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    if (!glfwExtensionSupported("GL_EXT_disjoint_timer_query"))
      throw std::runtime_error("GPU elapsed timer queries unavailable");
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';
    glGenQueries(2, observatory::skyProfileQueries);
    {
      observatory::Renderer renderer(data);
      std::vector<double> total, background, stars;
      for (int i = 0; i < count + 40; ++i) {
        renderer.render(width, height, time + i / 30.);
        glFinish();
        GLint disjoint = 0;
        glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
        if (disjoint)
          throw std::runtime_error("Disjoint GPU clock; rerun benchmark");
        GLuint ns[2];
        for (int q = 0; q < 2; ++q)
          glGetQueryObjectuiv(observatory::skyProfileQueries[q],
                              GL_QUERY_RESULT, &ns[q]);
        if (i >= 40) {
          background.push_back(ns[0] / 1.e6);
          stars.push_back(ns[1] / 1.e6);
          total.push_back((ns[0] + ns[1]) / 1.e6);
        }
      }
      auto report = [](std::vector<double> a, const char *label) {
        std::sort(a.begin(), a.end());
        std::cout << std::fixed << std::setprecision(6) << label
                  << " median_ms=" << a[a.size() / 2]
                  << " p95_ms=" << a[a.size() * 95 / 100] << '\n';
      };
      report(background, "Background");
      report(stars, "Catalog");
      report(total, "Sky total");
      if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("GPU profiling error");
    }
    glDeleteQueries(2, observatory::skyProfileQueries);
    glfwDestroyWindow(window);
    glfwTerminate();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
