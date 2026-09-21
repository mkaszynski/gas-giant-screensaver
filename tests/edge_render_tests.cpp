#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace observatory;
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
struct Fixture {
  std::filesystem::path path;
  void append(const char *file, const std::string &code) {
    auto target = path / "shaders" / file;
    std::ifstream input(target);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    auto source = buffer.str();
    source.insert(source.rfind('}'), code);
    std::ofstream(target) << source;
  }
  Fixture(bool mountains, bool giant = false) {
    char name[] = "/tmp/observatory-edges-XXXXXX";
    auto dir = mkdtemp(name);
    require(dir, "edge fixture directory");
    path = dir;
    auto root = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    if (giant) {
      append("background.frag", "color=vec4(0.);\n");
      append("stars.frag", "color=vec4(0.);\n");
      append("rings.frag", "discard;\n");
      append("body.frag", "\n#ifdef "
                          "GIANT_ATMOSPHERE\ncolor=vec4(vec3(color.a),color.a);"
                          "\n#else\ndiscard;\n#endif\n");
      append("post.frag", "color=vec4(texture(uScene,uv).rgb,1.);\n");
    } else if (mountains) {
      std::ifstream input(path / "shaders/post.frag");
      std::ostringstream buffer;
      buffer << input.rdbuf();
      const auto source = buffer.str();
      const std::string marker = "float ridgeCoverage=";
      const auto begin = source.find(marker) + marker.size();
      const auto expression =
          source.substr(begin, source.find(';', begin) - begin);
      append("post.frag", "vec4 "
                          "mountain=texture(uMountains,clamp(vec2(uv.x,1.-uv.y/"
                          ".30),0.,1.));\ncolor=vec4(vec3(uv.y<.30?(" +
                              expression + "):0.),1.);\n");
    } else {
      append("background.frag", "color=vec4(0.);\n");
      append("stars.frag", "color=vec4(0.);\n");
      append("body.frag", "discard;\n");
      append("post.frag", "color=vec4(texture(uScene,uv).rgb,1.);\n");
      // Isolate geometric annulus coverage, independent of density/lighting.
      auto target = path / "shaders/ring_layer.glsl";
      std::ifstream input(target);
      std::ostringstream buffer;
      buffer << input.rdbuf();
      auto source = buffer.str();
      const auto begin = source.find(" float tau=");
      source.replace(begin, source.find(';', begin) - begin + 1,
                     " float tau=1.;");
      const auto end = source.rfind(" return vec4(");
      source.replace(end, source.find(';', end) - end + 1,
                     " return vec4(vec3(coverage),coverage);");
      std::ofstream(target) << source;
    }
  }
  ~Fixture() { std::filesystem::remove_all(path); }
};
std::vector<unsigned char> render(Renderer &r, int w, int h, double time) {
  r.render(w, h, time);
  std::vector<unsigned char> p(w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  return p;
}
void check(Renderer &r, double time, const char *name) {
  constexpr int w = 320, h = 180, scale = 4;
  auto low = render(r, w, h, time),
       high = render(r, w * scale, h * scale, time);
  double error = 0;
  int edges = 0, filtered = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      double reference = 0;
      for (int dy = 0; dy < scale; ++dy)
        for (int dx = 0; dx < scale; ++dx)
          reference +=
              high[((y * scale + dy) * w * scale + x * scale + dx) * 4];
      reference /= scale * scale;
      if (reference > 2 && reference < 253) {
        int value = low[(y * w + x) * 4];
        error += std::abs(value - reference);
        ++edges;
        filtered += value > 2 && value < 253;
      }
    }
  std::cout << name << " reference edge count " << edges << std::endl;
  require(edges > 20, "reference must contain a substantial silhouette");
  std::cout << name << " time " << time << ": edge MAE " << error / edges
            << ", filtered " << filtered << "/" << edges << '\n';
  require(error / edges < 25, "edge coverage must agree with 4x reference");
  require(filtered > edges * .65,
          "most partially covered pixels must retain coverage");
}
} // namespace
int main() {
  try {
    require(glfwInit(), "GLFW init");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window =
        glfwCreateWindow(1280, 720, "Edge coverage tests", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      Fixture mountainFixture(true), ringFixture(false),
          giantFixture(false, true);
      Renderer giant(giantFixture.path.string());
      check(giant, 900., "Giant atmospheric silhouette");
      check(giant, 900.5, "Moving giant atmospheric silhouette");
      Renderer mountains(mountainFixture.path.string()),
          rings(ringFixture.path.string());
      check(mountains, 0, "Mountain");
      for (double t : {0., 300., 900., 900.5})
        check(rings, t, "Ring");
      require(glGetError() == GL_NO_ERROR, "edge rendering GL errors");
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    glfwTerminate();
    return 1;
  }
}
