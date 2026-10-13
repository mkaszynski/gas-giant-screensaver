#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
using namespace observatory;
void require(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
std::vector<unsigned char> pixels(int w, int h) {
  std::vector<unsigned char> p(w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  return p;
}
struct Fixture {
  std::filesystem::path path;
  Fixture() {
    char n[] = "/tmp/observatory-surface-XXXXXX";
    path = mkdtemp(n);
    auto root = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    auto f = path / "shaders/post.frag";
    std::ifstream in(f);
    std::ostringstream b;
    b << in.rdbuf();
    auto s = b.str();
    s.insert(s.rfind('}'),
             "vec4 "
             "coverage=texture(uSurface,vec2(uv.x,uv.y/"
             "uSurfaceFraction));if(uv.y>uSurfaceFraction)coverage=vec4(0.);"
             "color=vec4(vec3(coverage.a),1.);\n");
    std::ofstream(f) << s;
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
    auto window = glfwCreateWindow(640, 360, "Surface tests", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      constexpr int w = 640, h = 360;
      Fixture fixture;
      Renderer mask(fixture.path.string(), {}, {true, 0, 162000});
      mask.render(w, h, 1710);
      auto coverage = pixels(w, h);
      int opaque = 0, partial = 0;
      for (int i = 0; i < w * h; ++i) {
        int a = coverage[i * 4];
        opaque += a == 255;
        partial += a > 0 && a < 255;
      }
      require(opaque > w * h * .15, "substantial terrain at 50m height");
      require(partial > 30, "terrain and foliage silhouette antialiasing "
                            "actually produces partial coverage");
      for (int y = h * 3 / 4; y < h; ++y)
        for (int x = 0; x < w; ++x)
          require(coverage[(y * w + x) * 4] == 0,
                  "no cropped framebuffer rectangle in upper sky");
      Renderer r(Renderer::defaultDataDirectory(), {}, {true, 0, 162000});
      r.render(w, h, 1710, 1800, 1, true);
      auto bodies = pixels(w, h);
      r.render(w, h, 1710, 1800, 1, false);
      auto none = pixels(w, h);
      int solid = 0, maxDiff = 0;
      for (int y = 0; y < h / 8; ++y)
        for (int x = 0; x < w; ++x) {
          int i = y * w + x;
          if (coverage[i * 4] != 255)
            continue;
          ++solid;
          for (int c = 0; c < 3; ++c)
            maxDiff = std::max(maxDiff, std::abs(int(bodies[i * 4 + c]) -
                                                 int(none[i * 4 + c])));
        }
      require(solid > w * h * .08, "meaningful opaque foreground");
      require(maxDiff == 0, "celestial bodies never bleed through opaque land");
      // Compare identical phases across the period. CPU tests separately bound
      // motion through the boundary; two different positions can legitimately
      // change bright, subpixel ring edges by more than two display codes.
      Renderer endA(Renderer::defaultDataDirectory(), {},
                    {true, 0, travelPeriod - .00001});
      Renderer endB(Renderer::defaultDataDirectory(), {}, {true, 0, -.00001});
      endA.render(w, h, 450);
      auto a = pixels(w, h);
      endB.render(w, h, 450);
      auto b = pixels(w, h);
      int wrap = 0;
      for (size_t i = 0; i < a.size(); ++i)
        wrap = std::max(wrap, std::abs(int(a[i]) - int(b[i])));
      std::cout << "Periodic image maximum " << wrap << " codes\n";
      require(wrap <= 2,
              "identical route phases render identically across a circuit");
      Renderer moving(Renderer::defaultDataDirectory(), {},
                      {true, 1, 162000 - 1710});
      moving.render(w, h, 1710);
      auto first = pixels(w, h);
      moving.render(w, h, 1711);
      auto beforeRefresh = pixels(w, h);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      moving.render(w, h, 1711);
      auto travelled = pixels(w, h);
      long pop = 0;
      for (int i = 0; i < w * h / 4; ++i)
        for (int c = 0; c < 3; ++c)
          pop += std::abs(int(beforeRefresh[i * 4 + c]) -
                          int(travelled[i * 4 + c]));
      double popMean = double(pop) / (w * h / 4 * 3);
      std::cout << "Mesh refresh mean " << popMean << " codes\n";
      require(popMean < 3.,
              "refresh does not introduce a visible landscape jump");
      long delta = 0;
      for (int i = 0; i < w * h / 4; ++i)
        for (int c = 0; c < 3; ++c)
          delta += std::abs(int(first[i * 4 + c]) - int(travelled[i * 4 + c]));
      require(delta > w * h, "surface actually moves through space");
      Renderer fresh(Renderer::defaultDataDirectory(), {},
                     {true, 1, 162000 - 1710});
      fresh.render(w, h, 1711);
      auto reference = pixels(w, h);
      long seam = 0;
      for (int i = 0; i < w * h / 4; ++i)
        for (int c = 0; c < 3; ++c)
          seam +=
              std::abs(int(reference[i * 4 + c]) - int(travelled[i * 4 + c]));
      double seamMean = double(seam) / (w * h / 4 * 3);
      std::cout << "Streaming delta mean " << seamMean << " codes\n";
      require(seamMean < 3., "streaming and fresh geometry agree visually");
      GLfloat expected[4] = {.13f, .17f, .21f, .29f};
      glClearColor(expected[0], expected[1], expected[2], expected[3]);
      moving.render(w, h, 1711.1);
      GLfloat restored[4];
      glGetFloatv(GL_COLOR_CLEAR_VALUE, restored);
      for (int i = 0; i < 4; ++i)
        require(restored[i] == expected[i], "host clear color restored");
      require(glGetError() == GL_NO_ERROR, "moving surface GL validity");
      std::cout << "Surface: " << opaque << " opaque, " << partial
                << " antialiased pixels; opacity, route closure and streaming "
                   "passed\n";
    }
    glfwDestroyWindow(window);
    glfwTerminate();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
