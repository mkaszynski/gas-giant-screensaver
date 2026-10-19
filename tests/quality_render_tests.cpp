#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace observatory;
int main() {
  const auto folder = std::filesystem::temp_directory_path() /
                      ("observatory-quality-" + std::to_string(getpid()));
  GLFWwindow *window = nullptr;
  try {
    if (!glfwInit())
      throw std::runtime_error("GLFW initialization");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window = glfwCreateWindow(320, 180, "Quality test", nullptr, nullptr);
    if (!window)
      throw std::runtime_error("GLES context");
    glfwMakeContextCurrent(window);
    const auto source =
        std::filesystem::canonical(Renderer::defaultDataDirectory());
    std::filesystem::create_directories(folder / "assets");
    std::filesystem::create_directory_symlink(source / "shaders",
                                              folder / "shaders");
    std::filesystem::create_symlink(source / "assets/giant.png",
                                    folder / "assets/giant.png");
    std::filesystem::create_directory_symlink(source / "assets/terrain",
                                              folder / "assets/terrain");
    // No world, galaxy, or star files are accessible to this renderer.
    auto q = GraphicsSettings::forPreset(GraphicsPreset::Low);
    q.features.fill(false);
    q.height = 90;
    {
      Renderer renderer(folder.string(), {}, SurfaceOptions{true}, q);
      renderer.render(320, 180, 510);
      unsigned char corner[4]{};
      glReadPixels(300, 160, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
      if (corner[0] + corner[1] + corner[2] < 20)
        throw std::runtime_error(
            "Low-resolution output must fill the full native framebuffer");
      if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("Quality render GL error");
    }
    {
      auto medium = GraphicsSettings::forPreset(GraphicsPreset::Medium);
      medium.features[2] = medium.features[3] = false;
      Renderer renderer(source.string(), {}, SurfaceOptions{true}, medium);
      renderer.render(320, 180, 510, 1800, 1, true, true, {}, 1260);
      if (renderer.weatherResolution() != std::array<int, 2>{160, 90})
        throw std::runtime_error(
            "Medium must reduce cloud targets in both dimensions");
      if (renderer.weatherSkyCacheResolution() != std::array<int, 2>{54, 45})
        throw std::runtime_error(
            "Medium must reduce the weather sky cache too");
      unsigned char corner[4]{};
      glReadPixels(300, 160, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
      if (corner[0] + corner[1] + corner[2] < 20)
        throw std::runtime_error(
            "Half-resolution clouds must cover the whole scene");
      if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("Medium cloud quality GL error");
    }
    {
      auto medium = GraphicsSettings::forPreset(GraphicsPreset::Medium);
      medium.features[2] = medium.features[3] = false;
      medium.height = 90;
      Renderer renderer(source.string(), {}, SurfaceOptions{true}, medium);
      renderer.render(320, 180, 510, 1800, 1, true, true, {}, 1260);
      unsigned char corner[4]{};
      glReadPixels(300, 160, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
      if (corner[0] + corner[1] + corner[2] < 20)
        throw std::runtime_error(
            "Scaled moving terrain and clouds must fill presentation");
      if (renderer.weatherResolution() != std::array<int, 2>{80, 45})
        throw std::runtime_error("Presentation must not enlarge cloud targets");
      if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("Scaled presentation GL error");
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    std::filesystem::remove_all(folder);
    std::cout
        << "Native quality: stripped assets, full framebuffer presentation "
           "and Medium cloud/cache targets passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    if (window)
      glfwDestroyWindow(window);
    glfwTerminate();
    std::filesystem::remove_all(folder);
    return 1;
  }
}
