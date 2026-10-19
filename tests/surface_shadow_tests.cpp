#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace observatory;
void require(bool b, const char *message) {
  if (!b)
    throw std::runtime_error(message);
}
struct Fixture {
  std::filesystem::path path;
  Fixture(bool stepped = false, bool landOnly = false) {
    char name[] = "/tmp/observatory-shadow-XXXXXX";
    path = mkdtemp(name);
    auto root = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    std::ifstream in(path / "shaders/surface.frag");
    std::ostringstream source;
    source << in.rdbuf();
    auto code = source.str();
    code.resize(code.find("void main()"));
    if (stepped) {
      const std::string blend = "return mix(before,after,weight);";
      auto offset = code.find(blend);
      require(offset != std::string::npos, "temporal shadow reference fixture");
      code.replace(offset, blend.size(), "return before;");
    }
    // Isolate linear shadow visibility: no changing sky LUT, cloud weather,
    // exposure, albedo or camera can disguise a cache rollover jump.
    code += "void main(){vec3 dx=dFdx(vMesh),dy=dFdy(vMesh);";
    if (landOnly)
      code += "if(vProperties.x<1.)discard;";
    code += "color=vec4(vec3(surfaceShadow(normalize(vNormal),dx,dy)),1.);}";
    std::ofstream(path / "shaders/surface.frag") << code;
  }
  ~Fixture() { std::filesystem::remove_all(path); }
};
std::vector<unsigned char> render(SurfaceRenderer &r, double instant,
                                  double day = 1800, double route = 162000) {
  constexpr int width = 640, height = 360;
  const auto location = r.world().location(route);
  const auto scene = travelScene(r.world(), instant, route, day);
  GLuint texture = r.render(scene, location, width, height, instant, day, 0,
                            [](GLuint program) { glUseProgram(program); });
  auto error = glGetError();
  if (error != GL_NO_ERROR)
    throw std::runtime_error("Shadow render error " + std::to_string(error));
  GLuint fbo;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "shadow readback framebuffer");
  // The cropped texture only contains the lower portion of the viewport.
  int rows = int(std::round(r.coverageFraction() * height));
  std::vector<unsigned char> pixels(width * rows * 4);
  std::vector<float> hdr(pixels.size());
  glReadPixels(0, 0, width, rows, GL_RGBA, GL_FLOAT, hdr.data());
  for (size_t i = 0; i < pixels.size(); ++i)
    pixels[i] = std::lround(std::clamp(hdr[i], 0.f, 1.f) * 255);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  return pixels;
}
int maximum(const std::vector<unsigned char> &a,
            const std::vector<unsigned char> &b) {
  require(a.size() == b.size(), "shadow capture size");
  int result = 0;
  for (size_t i = 0; i < a.size(); ++i)
    result = std::max(result, std::abs(int(a[i]) - int(b[i])));
  return result;
}
int main() {
  try {
    require(glfwInit(), "GLFW init");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window =
        glfwCreateWindow(640, 360, "Shadow continuity tests", nullptr, nullptr);
    require(window, "shadow GLES context");
    glfwMakeContextCurrent(window);
    {
      // Land receivers and their visible geometry are identical. Only the
      // sea's shadow-casting mesh changes, isolating depth-pass water LOD.
      Fixture fixture(false, true);
      SurfaceRenderer coarse(fixture.path.string(), false, true, true, false);
      SurfaceRenderer fine(fixture.path.string(), false, true, false, false);
      for (double time : {100., 510., 900., 1740.}) {
        auto a = render(coarse, time, 1800, 5600.390625);
        auto b = render(fine, time, 1800, 5600.390625);
        const int error = maximum(a, b);
        std::cout << "Coastal land shadow water LOD time=" << time
                  << " peak=" << error << "/255\n";
        require(error <= 1,
                "coarse ocean casters preserve coastal land visibility");
        require(coarse.shadowRevision() == fine.shadowRevision(),
                "water shadow LOD retains cache update timing");
      }
    }
    {
      Fixture fixture;
      SurfaceRenderer r(fixture.path.string());
      glDisable(GL_BLEND);
      auto start = render(r, 900);
      require(r.shadowRevision() == 4, "current and future maps precomputed");
      render(r, 900.25);
      require(r.shadowRevision() == 4,
              "interpolation needs no new depth draws");
      int largestJump = 0;
      for (double boundary : {.5, .75, 1., 1.5, 1.75, 2.}) {
        auto before = render(r, 900 + boundary - .0001);
        auto draws = r.shadowRevision();
        auto after = render(r, 900 + boundary + .0001);
        require(r.shadowRevision() == draws + 1,
                "staggered rollovers render only one hidden future map");
        int jump = maximum(before, after);
        largestJump = std::max(largestJump, jump);
        std::cout << "Shadow rollover " << boundary << " s: max " << jump
                  << "/255\n";
        require(jump <= 2, "shadow cache rollover must be visually continuous");
      }
      auto end = render(r, 902.05);
      std::cout << "Shadow motion max " << maximum(start, end) << "/255\n";
      require(maximum(start, end) > 8,
              "shadows actually move, rather than freezing");
      auto resetDraws = r.shadowRevision();
      auto reset = render(r, 900);
      require(r.shadowRevision() == resetDraws + 4,
              "backward debug seeks rebuild the bounded cache once");
      require(maximum(start, reset) == 0,
              "rewind reconstructs identical shadow state");
      render(r, 900.0001);
      require(r.shadowRevision() == resetDraws + 4,
              "no repeat rebuild after rewind");
      require(glGetError() == GL_NO_ERROR, "shadow GL errors");
      {
        Fixture stepped(true);
        SurfaceRenderer old(stepped.path.string());
        render(old, 900);
        auto before = render(old, 900.7499);
        auto after = render(old, 900.7501);
        int jump = maximum(before, after);
        std::cout << "Stepped shadow reference jump " << jump << "/255\n";
        require(jump > 2,
                "continuity regression detects unblended cache jumps");
      }
      std::cout << "Predicted shadows: moving, continuous rollovers, staggered "
                   "draw budget and deterministic rewind PASS; worst jump "
                << largestJump << "/255\n";
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
