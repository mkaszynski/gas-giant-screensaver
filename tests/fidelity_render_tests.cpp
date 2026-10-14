#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace observatory;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
std::vector<unsigned char> capture(Renderer &renderer, double instant) {
  renderer.render(640, 360, instant);
  std::vector<unsigned char> pixels(640 * 360 * 4);
  glReadPixels(0, 0, 640, 360, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  require(glGetError() == GL_NO_ERROR, "fidelity GL errors");
  return pixels;
}
struct Fixture {
  std::filesystem::path path;
  Fixture(const char *term, const char *replacement) {
    char name[] = "/tmp/observatory-fidelity-XXXXXX";
    path = mkdtemp(name);
    auto root = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    auto file = path / "shaders/surface.frag";
    std::ifstream in(file);
    std::ostringstream text;
    text << in.rdbuf();
    auto source = text.str();
    auto offset = source.find(term);
    require(offset != std::string::npos, "physical effect fixture exists");
    source.replace(offset, std::string(term).size(), replacement);
    std::ofstream(file) << source;
  }
  ~Fixture() { std::filesystem::remove_all(path); }
};
int main() {
  try {
    require(glfwInit(), "GLFW init");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window =
        glfwCreateWindow(640, 360, "Fidelity tests", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      Renderer reference(Renderer::defaultDataDirectory(), {},
                         {true, 0, 162000});

      struct Case {
        const char *name, *term, *replacement;
        double instant;
      };
      const Case cases[] = {
          {"crown self-shadow", "visibility*=mix(1.,canopyShade,forest);", "",
           900},
          {"crown orientation",
           "uMeshFrame*vec3(crownNormal.x*uReliefLongitudeScale,0.,crownNormal."
           "z)/max(.10,crownNormal.y)*forest",
           "vec3(0.)", 510},
          {"dielectric reflection",
           "land+=irradiance*NoL*spec*mix(1.,.35,forest);", "", 510}};
      for (auto c : cases) {
        Fixture fixture(c.term, c.replacement);
        Renderer without(fixture.path.string(), {}, {true, 0, 162000});
        auto lit = capture(reference, c.instant);
        auto dark = capture(without, c.instant);
        int changed = 0, peak = 0;
        for (int i = 0; i < 640 * 360 / 5; ++i)
          for (int channel = 0; channel < 3; ++channel) {
            int delta = std::abs(int(lit[i * 4 + channel]) -
                                 int(dark[i * 4 + channel]));
            changed += delta > 0;
            peak = std::max(peak, delta);
          }
        std::cout << c.name << ": " << changed << " changed channels, peak "
                  << peak << " codes\n";
        require(changed > 100 && peak > 0,
                "physical landscape effect actually visible");
      }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
