#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace observatory;
namespace {
void require(bool v, const char *m) {
  if (!v)
    throw std::runtime_error(m);
}
std::vector<unsigned char> pixels(int w, int h) {
  std::vector<unsigned char> p(w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  return p;
}
struct Fixture {
  std::filesystem::path path;
  Fixture(const std::string &code) {
    char name[] = "/tmp/observatory-terrain-XXXXXX";
    path = mkdtemp(name);
    auto root = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    const auto file = path / "shaders/post.frag";
    std::ifstream input(file);
    std::ostringstream b;
    b << input.rdbuf();
    auto text = b.str();
    text.insert(text.rfind('}'), code);
    std::ofstream(file) << text;
  }
  ~Fixture() { std::filesystem::remove_all(path); }
};
} // namespace
int main() {
  try {
    require(glfwInit(), "GLFW init");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window =
        glfwCreateWindow(960, 540, "Terrain regression", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      constexpr int w = 960, h = 540;
      Fixture mask("vec2 tp=terrainUV(ray());vec4 "
                   "tm=terrainSample(ray());color=vec4(step(1.,tm.a),texture("
                   "uTerrainGeometry,tp).b,0.,1.);\n");
      Renderer masks(mask.path.string());
      masks.render(w, h, 0.);
      auto m = pixels(w, h);
      int land = 0, lake = 0;
      for (int i = 0; i < w * h; ++i) {
        land += m[i * 4] == 255 && m[i * 4 + 1] == 0;
        lake += m[i * 4] == 255 && m[i * 4 + 1] > 250;
      }
      require(land > w * h * .08, "substantial dry mountain terrain");
      require(lake > 400, "lake surface is actually visible");
      Renderer r(Renderer::defaultDataDirectory());
      auto fixed = sceneAt(0.);
      r.renderScene(w, h, fixed, 0.);
      auto day = pixels(w, h);
      auto opposite = fixed;
      opposite.sunLocal = normalized(
          Vec3{-fixed.sunLocal.x, fixed.sunLocal.y, -fixed.sunLocal.z});
      r.renderScene(w, h, opposite, 0.);
      auto relit = pixels(w, h);
      long dryDelta = 0;
      for (int i = 0; i < w * h; ++i)
        if (m[i * 4] == 255 && m[i * 4 + 1] == 0)
          for (int c = 0; c < 3; ++c)
            dryDelta += std::abs(int(day[i * 4 + c]) - int(relit[i * 4 + c]));
      require(dryDelta > land * 12,
              "mountain faces respond to light direction");
      r.renderScene(w, h, fixed, 3.);
      auto moving = pixels(w, h);
      long waveDelta = 0;
      for (int i = 0; i < w * h; ++i)
        if (m[i * 4] == 255 && m[i * 4 + 1] > 250)
          for (int c = 0; c < 3; ++c)
            waveDelta += std::abs(int(day[i * 4 + c]) - int(moving[i * 4 + c]));
      require(waveDelta > lake, "wind waves move their reflected image");
      for (double frequency : {.496, .157})
        for (double t :
             {pi / frequency, 2 * pi / frequency, 10001 * pi / frequency,
              10000 * 2 * pi / frequency, 1790900000.}) {
          r.renderScene(w, h, fixed, t - .0001);
          auto a = pixels(w, h);
          r.renderScene(w, h, fixed, t + .0001);
          auto b = pixels(w, h);
          int maximum = 0;
          for (size_t i = 0; i < a.size(); ++i)
            maximum = std::max(maximum, std::abs(int(a[i]) - int(b[i])));
          std::cout << "Continuity frequency " << frequency << " time " << t
                    << " max channel delta " << maximum << std::endl;
          require(maximum <= 2,
                  "wave/cloud clocks remain continuous at epoch and wraps");
        }
      // Cache boundary was a visible white arc when implicit derivatives were
      // evaluated only by one side of a divergent fragment quad.
      Fixture coverage("color=vec4(vec3(terrainSample(ray()).a),1.);\n");
      Renderer boundary(coverage.path.string());
      boundary.render(w, h, 0.);
      auto p = pixels(w, h);
      for (int y = h * 3 / 4; y < h; ++y)
        for (int x = 0; x < w; ++x)
          require(p[(y * w + x) * 4] == 0,
                  "no phantom terrain arc in the high sky");
      require(glGetError() == GL_NO_ERROR,
              "terrain GL state and sampler validity");
      std::cout << "Terrain: " << land << " dry pixels, " << lake
                << " lake pixels, lighting and wave continuity passed\n";
    }
    glfwDestroyWindow(window);
    glfwTerminate();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
