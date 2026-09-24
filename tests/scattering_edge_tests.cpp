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
#include <vector>
using namespace observatory;
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
struct Fixture {
  std::filesystem::path path;
  Fixture(bool darkCloud = true) {
    char name[] = "/tmp/observatory-scattering-edge-XXXXXX";
    auto directory = mkdtemp(name);
    require(directory, "temporary scattering fixture");
    path = directory;
    auto root = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    auto edit = [&](const char *file, auto transform) {
      auto target = path / "shaders" / file;
      std::ifstream f(target);
      std::ostringstream b;
      b << f.rdbuf();
      auto s = b.str();
      transform(s);
      std::ofstream(target) << s;
    };
    for (const char *file : {"background.frag", "stars.frag"})
      edit(file, [](auto &s) { s.insert(s.rfind('}'), "color=vec4(0.);\n"); });
    edit("scene.glsl", [](auto &s) {
      for (const std::string fn :
           {"vec3 sky(vec3 d){", "vec3 viewT(vec3 d){"}) {
        auto at = s.find(fn);
        require(at != std::string::npos, "atmosphere fixture stage");
        s.insert(at + fn.size(), fn.find("sky") != std::string::npos
                                     ? "return vec3(0.);"
                                     : "return vec3(1.);");
      }
    });
    edit("body.frag", [darkCloud](auto &s) {
      auto at = s.find(" float mu=max");
      require(at != std::string::npos, "cloud lighting stage");
      s.insert(at, darkCloud ? " albedo=vec3(0.);\n" : " albedo=vec3(.3);\n");
    });
    edit("post.frag", [](auto &s) {
      s.insert(s.rfind('}'), "color=vec4(sceneColor(uv),1.);\n");
    });
  }
  ~Fixture() { std::filesystem::remove_all(path); }
};
Scene scene(double shift, double angle) {
  Scene s{};
  for (int i = 0; i < 21; ++i)
    s.bodies[i] = {{1e6 + i * 100., 1e6, 1e6}, .01, 3, 0};
  s.bodies[0] = {{0, 0, 0}, 1, 0, 0};
  s.bodies[0].pole = giantPole();
  s.observer = {0, 0, -3};
  s.east = {1, 0, 0};
  s.up = {0, 1, 0};
  s.north = {0, 0, 1};
  s.forward = normalized({shift * 2 * std::tan(29 * pi / 180) / 180., 0, 1});
  s.right = normalized(cross(s.up, s.forward));
  s.cameraUp = s.up;
  s.sun = s.sunLocal = {std::sin(angle), 0, std::cos(angle)};
  s.sunVisibility = 1;
  return s;
}
std::vector<float> render(Renderer &r, const Scene &s, int w, int h) {
  GLuint fbo, texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT,
               nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "linear HDR reference target");
  r.renderScene(w, h, s, 0, 1800, 1, true, false);
  std::vector<float> out(w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, out.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &texture);
  return out;
}
} // namespace
int main() {
  try {
    require(glfwInit(), "GLFW init");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto win =
        glfwCreateWindow(320, 180, "Scattering antialiasing", nullptr, nullptr);
    require(win, "GLES context");
    glfwMakeContextCurrent(win);
    {
      Fixture fixture;
      Renderer r(fixture.path.string());
      constexpr int w = 320, h = 180, scale = 8;
      double worst = 0;
      for (double angle : {std::asin(1. / 3.), .5, 2.5}) {
        double minFlux = 1e9, maxFlux = 0;
        for (double shift : {0., .25, .5, .75}) {
          auto s = scene(shift, angle);
          auto low = render(r, s, w, h),
               high = render(r, s, w * scale, h * scale);
          double error = 0, reference = 0, flux = 0;
          for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
              auto d = normalized(s.forward +
                                  s.right * ((2 * (x + .5) / w - 1) * w / h *
                                             std::tan(29 * pi / 180)) +
                                  s.cameraUp * ((2 * (y + .5) / h - 1) *
                                                std::tan(29 * pi / 180)));
              double b = length(cross(-s.observer, d));
              if (b < .97 || b > 1.04)
                continue;
              double expected = 0;
              for (int dy = 0; dy < scale; ++dy)
                for (int dx = 0; dx < scale; ++dx)
                  expected +=
                      high[(((y * scale + dy) * w * scale + x * scale + dx) *
                            4) +
                           2];
              expected /= scale * scale;
              double value = low[(y * w + x) * 4 + 2];
              require(std::isfinite(value) && value >= 0,
                      "finite scattered radiance");
              error += std::abs(value - expected);
              reference += expected;
              flux += value;
            }
          require(reference > .001, "reference has a visible atmospheric arc");
          worst = std::max(worst, error / reference);
          minFlux = std::min(minFlux, flux);
          maxFlux = std::max(maxFlux, flux);
          std::cout << "Scattering edge angle " << angle << " shift " << shift
                    << ": relative error " << error / reference << ", flux "
                    << flux << '\n';
        }
        std::cout << "Subpixel movement flux variation: "
                  << (maxFlux - minFlux) / maxFlux << '\n';
        require((maxFlux - minFlux) / maxFlux < .02,
                "moving subpixel arcs must conserve brightness");
      }
      std::cout << "Worst linear-light scattering edge error: " << worst
                << '\n';
      require(worst < .10, "scattered light must agree with 8x area reference, "
                           "not just silhouette alpha");
      {
        Fixture clouds(false);
        Renderer cloudRenderer(clouds.path.string());
        auto s = scene(.37, 2.5);
        auto low = render(cloudRenderer, s, w, h),
             high = render(cloudRenderer, s, w * scale, h * scale);
        double error = 0, reference = 0;
        for (int y = 0; y < h; ++y)
          for (int x = 0; x < w; ++x) {
            auto d = normalized(s.forward +
                                s.right * ((2 * (x + .5) / w - 1) * w / h *
                                           std::tan(29 * pi / 180)) +
                                s.cameraUp * ((2 * (y + .5) / h - 1) *
                                              std::tan(29 * pi / 180)));
            double b = length(cross(-s.observer, d));
            if (b < .98 || b > 1.02)
              continue;
            for (int c = 0; c < 3; ++c) {
              double expected = 0;
              for (int dy = 0; dy < scale; ++dy)
                for (int dx = 0; dx < scale; ++dx)
                  expected +=
                      high[(((y * scale + dy) * w * scale + x * scale + dx) *
                            4) +
                           c];
              expected /= scale * scale;
              error += std::abs(low[(y * w + x) * 4 + c] - expected);
              reference += expected;
            }
          }
        std::cout << "Lit cloud/atmosphere boundary relative error: "
                  << error / reference << '\n';
        require(
            error / reference < .08,
            "cloud illumination must not disappear in the inner edge samples");
      }
      require(glGetError() == GL_NO_ERROR, "scattering edge GL errors");
    }
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    glfwTerminate();
    return 1;
  }
}
