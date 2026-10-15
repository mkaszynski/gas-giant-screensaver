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
  Fixture(const char *term, const char *replacement,
          const char *shader = "surface.frag") {
    char name[] = "/tmp/observatory-fidelity-XXXXXX";
    path = mkdtemp(name);
    auto root = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    auto file = path / "shaders" / shader;
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

      {
        Renderer complete(Renderer::defaultDataDirectory(), {},
                          {true, 0, 162000, false, false});
        for (double instant : {510., 900., 1200.}) {
          auto culled = capture(reference, instant);
          auto full = capture(complete, instant);
          require(culled == full,
                  "terrain/shadow block culling must preserve every pixel");
        }
        std::cout << "Terrain/shadow culling: pixel-identical in daylight, "
                     "sunset and night\n";
      }

      {
        Fixture full("if(coverage>=1.)", "if(false)", "post.frag");
        Renderer without(full.path.string(), {}, {true, 0, 162000});
        auto fast = capture(reference, 510), complete = capture(without, 510);
        require(fast == complete,
                "opaque post shortcut must preserve every display pixel");
        std::cout << "Opaque post shortcut: pixel-identical to full sky "
                     "computation\n";
      }
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
      // Material behavior must remain measurable even when the weather model
      // happens to put an opaque cloud bank between the camera and the forest.
      const SurfaceOptions clearTerrain{true, 0,    162000, false,
                                        true, true, false};
      Renderer materialReference(Renderer::defaultDataDirectory(), {},
                                 clearTerrain);
      for (auto c : cases) {
        Fixture fixture(c.term, c.replacement);
        Renderer without(fixture.path.string(), {}, clearTerrain);
        auto lit = capture(materialReference, c.instant);
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
