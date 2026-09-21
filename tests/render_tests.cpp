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
void require(bool v, const char *message) {
  if (!v)
    throw std::runtime_error(message);
}
std::vector<unsigned char> pixels(int w, int h) {
  std::vector<unsigned char> p(w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  return p;
}
struct BrightSkyFixture {
  std::filesystem::path path;
  BrightSkyFixture() {
    char name[] = "/tmp/observatory-occlusion-XXXXXX";
    auto dir = mkdtemp(name);
    require(dir, "temporary shader fixture");
    path = dir;
    const auto source = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(source / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(source / "assets", path / "assets");
    auto post = path / "shaders/post.frag";
    std::ifstream input(post);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    auto shader = buffer.str();
    // Inject extreme sky and glare radiance at their actual compositing stages.
    // If either stage moves in front of the ridge, its interior must fail below.
    for (const std::string needle : {"vec3 L=texture(uScene,uv).rgb;",
         "L+=transmission(uTrans,.2,uSun.y)*halo*solarVisibility;"}) {
      auto at = shader.find(needle);
      require(at != std::string::npos, "sky/glare injection stage");
      shader.insert(at + needle.size(), "L+=vec3(1000.);");
    }
    std::ofstream(post) << shader;
  }
  ~BrightSkyFixture() { std::filesystem::remove_all(path); }
};
int main() {
  try {
    require(glfwInit(), "GLFW initialization");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window =
        glfwCreateWindow(640, 360, "Renderer tests", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      Renderer r(Renderer::defaultDataDirectory());
      require(glIsEnabled(GL_BLEND), "constructor must preserve host blending");
      r.render(640, 360, 1350);
      auto with = pixels(640, 360);
      require(glIsEnabled(GL_BLEND), "render must preserve host blending");
      GLint blendSource = 0;
      glGetIntegerv(GL_BLEND_SRC_RGB, &blendSource);
      require(blendSource == GL_SRC_ALPHA,
              "render must preserve blend factors");
      r.render(640, 360, 1350, 1800, 1, false);
      auto without = pixels(640, 360);
      long visibleDifference = 0;
      int bottomDifference = 0;
      for (int y = 0; y < 360; ++y)
        for (int x = 0; x < 640; ++x)
          for (int c = 0; c < 3; ++c) {
            int delta = std::abs(int(with[(y * 640 + x) * 4 + c]) -
                                 int(without[(y * 640 + x) * 4 + c]));
            visibleDifference += delta;
            if (y < 20)
              bottomDifference = std::max(bottomDifference, delta);
          }
      require(visibleDifference > 100000, "giant must be visibly rendered");
      std::cout << "Mountain interior max channel delta: " << bottomDifference
                << "\n";
      require(bottomDifference == 0, "mountain interior must completely "
                                     "occlude the giant (alpha regression)");
      r.render(640, 360, 0);
      auto day = pixels(640, 360);
      long dayLight = 0, nightLight = 0;
      for (size_t i = 0; i < day.size(); i += 4) {
        dayLight += day[i] + day[i + 1] + day[i + 2];
        nightLight += with[i] + with[i + 1] + with[i + 2];
      }
      require(dayLight > nightLight * 1.5,
              "daylight must exceed nighttime brightness");
      // Sample empty upper-right sky, excluding the giant and mountain strip.
      long nightSky = 0;
      for (int y = 260; y < 340; ++y)
        for (int x = 500; x < 620; ++x)
          nightSky += without[(y * 640 + x) * 4 + 2];
      double skyBlue = double(nightSky) / (80 * 120);
      require(skyBlue > 3. && skyBlue < 25.,
              "night sky should retain faint detail without looking like daylight");
      r.render(320, 180, 850);
      r.render(640, 360, 0);
      auto repeat = pixels(640, 360);
      require(repeat == day,
              "resize and time changes must return to deterministic output");
      {
        BrightSkyFixture fixture;
        Renderer brightSky(fixture.path.string());
        for (double time : {0., 800., 880., 1350.}) {
          r.render(640, 360, time);
          auto normal = pixels(640, 360);
          brightSky.render(640, 360, time);
          auto bright = pixels(640, 360);
          for (int i = 0; i < 640 * 20 * 4; ++i)
            require(normal[i] == bright[i],
                    "mountains must occlude all sky and solar glare at every time");
          require(normal != bright, "bright sky fixture must change visible sky");
        }
      }
      require(glGetError() == GL_NO_ERROR, "OpenGL errors");
      std::cout << "GPU render: opaque mountains, visible bodies, "
                   "daylight/night contrast, resize determinism passed\n";
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
