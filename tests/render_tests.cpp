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
enum class FixtureMode { BrightSky, LongitudeChart, SlabReference };
struct ShaderFixture {
  std::filesystem::path path;
  ShaderFixture(FixtureMode mode = FixtureMode::BrightSky) {
    char name[] = "/tmp/observatory-occlusion-XXXXXX";
    auto dir = mkdtemp(name);
    require(dir, "temporary shader fixture");
    path = dir;
    const auto source =
        std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(source / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(source / "assets",
                                              path / "assets");
    if (mode == FixtureMode::SlabReference) {
      const auto target = path / "shaders/body.frag";
      std::ifstream input(target);
      std::ostringstream buffer;
      buffer << input.rdbuf();
      auto shader = buffer.str();
      shader.insert(shader.find('\n') + 1, "#undef GIANT_SLAB_LUT\n");
      std::ofstream(target) << shader;
      return;
    }
    if (mode == FixtureMode::LongitudeChart) {
      const auto target = path / "shaders/body.frag";
      std::ifstream input(target);
      std::ostringstream buffer;
      buffer << input.rdbuf();
      auto shader = buffer.str();
      // Rotate the longitude coordinate chart by half a turn and compensate
      // the texture coordinate. The physical image must remain identical;
      // only atan's branch cut moves to the opposite side of the sphere.
      const std::string angle = "atan(sn.z,sn.x)", spin = "-uSpin/(2.*PI)";
      auto at = shader.find(angle);
      require(at != std::string::npos, "longitude chart angle exists");
      shader.replace(at, angle.size(), "atan(-sn.z,-sn.x)");
      at = shader.find(spin);
      require(at != std::string::npos, "longitude chart spin exists");
      shader.replace(at, spin.size(), spin + "+.5");
      std::ofstream(target) << shader;
      return;
    }
    auto post = path / "shaders/post.frag";
    std::ifstream input(post);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    auto shader = buffer.str();
    // Inject extreme sky and glare radiance at their actual compositing stages.
    // If either stage moves in front of the ridge, its interior must fail
    // below.
    for (const std::string needle :
         {"vec3 L=sceneColor(uv);",
          "L+=transmission(uTrans,.2,uSun.y)*halo*solarVisibility;"}) {
      auto at = shader.find(needle);
      require(at != std::string::npos, "sky/glare injection stage");
      shader.insert(at + needle.size(), "L+=vec3(1000.);");
    }
    std::ofstream(post) << shader;
  }
  ~ShaderFixture() { std::filesystem::remove_all(path); }
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
      glDepthFunc(GL_GREATER);
      glDepthMask(GL_FALSE);
      glClearDepthf(.4f);
      Renderer r(Renderer::defaultDataDirectory());
      require(glIsEnabled(GL_BLEND), "constructor must preserve host blending");
      r.render(640, 360, 900);
      auto with = pixels(640, 360);
      require(glIsEnabled(GL_BLEND), "render must preserve host blending");
      GLint depthFunction = 0;
      GLboolean depthWrite = GL_TRUE;
      GLfloat clearDepth = 0;
      glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
      glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
      glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clearDepth);
      require(depthFunction == GL_GREATER && depthWrite == GL_FALSE &&
                  clearDepth == .4f,
              "ring depth rendering must preserve host depth state");
      GLint blendSource = 0;
      glGetIntegerv(GL_BLEND_SRC_RGB, &blendSource);
      require(blendSource == GL_SRC_ALPHA,
              "render must preserve blend factors");
      r.render(640, 360, 900, 1800, 1, false);
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
      require(
          skyBlue > 3. && skyBlue < 25.,
          "night sky should retain faint detail without looking like daylight");
      r.render(320, 180, 850);
      r.render(640, 360, 0);
      auto repeat = pixels(640, 360);
      require(repeat == day,
              "resize and time changes must return to deterministic output");
      {
        ShaderFixture fixture;
        Renderer brightSky(fixture.path.string());
        for (double time : {0., 1320., 1180., 900.}) {
          r.render(640, 360, time);
          auto normal = pixels(640, 360);
          brightSky.render(640, 360, time);
          auto bright = pixels(640, 360);
          for (int i = 0; i < 640 * 20 * 4; ++i)
            require(
                normal[i] == bright[i],
                "mountains must occlude all sky and solar glare at every time");
          require(normal != bright,
                  "bright sky fixture must change visible sky");
        }
      }
      {
        ShaderFixture fixture(FixtureMode::LongitudeChart);
        Renderer otherChart(fixture.path.string());
        for (double time : {550., 650., 900.}) {
          r.render(640, 360, time, 1800, 1, true, false);
          auto normal = pixels(640, 360);
          otherChart.render(640, 360, time, 1800, 1, true, false);
          auto rotated = pixels(640, 360);
          int maximum = 0;
          for (size_t i = 0; i < normal.size(); ++i)
            maximum =
                std::max(maximum, std::abs(int(normal[i]) - int(rotated[i])));
          std::cout << "Longitude chart invariance at " << time << ": "
                    << maximum << '\n';
          require(maximum <= 3,
                  "longitude chart must not create pixelated texture stripes");
        }
      }
      {
        ShaderFixture fixture(FixtureMode::SlabReference);
        Renderer analytical(fixture.path.string());
        for (double time : {0., 900., 376368.}) {
          r.render(640, 360, time);
          auto cached = pixels(640, 360);
          analytical.render(640, 360, time);
          auto exact = pixels(640, 360);
          int maximum = 0;
          for (size_t i = 0; i < cached.size(); ++i)
            maximum =
                std::max(maximum, std::abs(int(cached[i]) - int(exact[i])));
          std::cout << "Atmosphere lookup vs analytical at " << time << ": "
                    << maximum << '\n';
          require(maximum <= 3,
                  "cached scattering preserves analytical color/intensity");
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
