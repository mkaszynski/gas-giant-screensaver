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
  Fixture(bool mask = true, bool reflection = false) {
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
    if (mask) {
      auto shortcut = s.find("if(coverage>=1.)");
      require(shortcut != std::string::npos, "opaque post shortcut fixture");
      s.replace(shortcut, std::string("if(coverage>=1.)").size(), "if(false)");
      s.insert(s.rfind('}'),
               "vec4 "
               "coverage=texture(uSurface,vec2(uv.x,uv.y/"
               "uSurfaceFraction));if(uv.y>uSurfaceFraction)coverage=vec4(0.);"
               "color=vec4(vec3(coverage.a),1.);\n");
    }
    std::ofstream(f) << s;
    if (!mask) {
      auto shader = path / "shaders/surface.frag";
      std::ifstream source(shader);
      std::ostringstream text;
      text << source.rdbuf();
      auto code = text.str();
      const std::string term =
          reflection
              ? "L=cloudSceneAt(reflected,L,vPosition/1000.);"
              : "surfaceShadow(N,meshDx,meshDy)*terrainCloudTransmission("
                "vPosition/1000.)";
      auto offset = code.find(term);
      require(offset != std::string::npos, "cloud shadow fixture");
      code.replace(offset, term.size(),
                   reflection ? "" : "surfaceShadow(N,meshDx,meshDy)");
      std::ofstream(shader) << code;
    }
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
      std::vector<unsigned char> coverage;
      {
        Fixture fixture;
        Renderer mask(fixture.path.string(), {}, {true, 0, 162000});
        mask.render(w, h, 510);
        coverage = pixels(w, h);
      }
      int opaque = 0, partial = 0;
      for (int i = 0; i < w * h; ++i) {
        int a = coverage[i * 4];
        opaque += a == 255;
        partial += a > 0 && a < 255;
      }
      require(opaque > w * h * .15,
              "substantial terrain below airborne camera");
      require(partial > 30, "terrain and foliage silhouette antialiasing "
                            "actually produces partial coverage");
      for (int y = h * 3 / 4; y < h; ++y)
        for (int x = 0; x < w; ++x)
          require(coverage[(y * w + x) * 4] == 0,
                  "no cropped framebuffer rectangle in upper sky");
      {
        Renderer r(Renderer::defaultDataDirectory(), {}, {true, 0, 162000});
        r.render(w, h, 510, 1800, 1, true);
        auto bodies = pixels(w, h);
        r.render(w, h, 510, 1800, 1, false);
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
        require(maxDiff == 0,
                "celestial bodies never bleed through opaque land");
        {
          Fixture noClouds(false);
          Renderer noShadow(noClouds.path.string(), {}, {true, 0, 162000});
          // A low sun and fair banks leave visible land outside the cloud
          // footprint; an opaque storm can legitimately hide every shadow.
          constexpr double weather = 1790900000.;
          r.render(w, h, 900, 1800, 1, true, true, {}, weather);
          auto shadowed = pixels(w, h);
          noShadow.render(w, h, 900, 1800, 1, true, true, {}, weather);
          auto unshadowed = pixels(w, h);
          int cloudPixels = 0, cloudPeak = 0;
          for (int i = 0; i < w * h / 4; ++i)
            for (int c = 0; c < 3; ++c) {
              int difference = std::abs(int(shadowed[4 * i + c]) -
                                        int(unshadowed[4 * i + c]));
              cloudPixels += difference > 0;
              cloudPeak = std::max(cloudPeak, difference);
            }
          std::cout << "Cloud shadow changed " << cloudPixels
                    << " terrain channels, peak " << cloudPeak << " codes\n";
          require(cloudPixels > 100 && cloudPeak > 0,
                  "shared moving clouds actually shadow the land");
        }
        auto night = travelScene(SurfaceWorld(Renderer::defaultDataDirectory() +
                                              "/assets/world/moon.ggworld"),
                                 1200, 162000);
        require(night.sunLocal.y < -.2,
                "night lighting fixture has no direct sunlight");
        r.renderScene(w, h, night, 1200);
        auto litNight = pixels(w, h);
        night.giantLight = 0;
        r.renderScene(w, h, night, 1200);
        auto darkNight = pixels(w, h);
        int nightPixels = 0;
        for (int i = 0; i < w * h / 8; ++i)
          if (coverage[i * 4] == 255)
            for (int c = 0; c < 3; ++c)
              nightPixels += litNight[i * 4 + c] > darkNight[i * 4 + c];
        require(nightPixels > 100,
                "sunlit gas giant visibly lights the land during night");
      }
      // Compare identical phases across the period. CPU tests separately bound
      // motion through the boundary; two different positions can legitimately
      // change bright, subpixel ring edges by more than two display codes.
      std::vector<unsigned char> a, b;
      {
        Renderer endA(Renderer::defaultDataDirectory(), {},
                      {true, 0, travelPeriod - .00001});
        endA.render(w, h, 1740);
        a = pixels(w, h);
      }
      {
        Renderer endB(Renderer::defaultDataDirectory(), {}, {true, 0, -.00001});
        endB.render(w, h, 1740);
        b = pixels(w, h);
      }
      int wrap = 0;
      for (size_t i = 0; i < a.size(); ++i)
        wrap = std::max(wrap, std::abs(int(a[i]) - int(b[i])));
      std::cout << "Periodic image maximum " << wrap << " codes\n";
      require(wrap <= 2,
              "identical route phases render identically across a circuit");
      {
        Renderer moving(Renderer::defaultDataDirectory(), {},
                        {true, 1, 162000 - 510, false, true, true, false});
        moving.render(w, h, 510);
        auto first = pixels(w, h);
        size_t initialRevision = moving.surfaceMeshRevision();
        // Ten seconds gives measurable parallax at airplane height.
        moving.render(w, h, 520);
        auto beforeRefresh = pixels(w, h);
        auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (moving.surfaceMeshRevision() == initialRevision &&
               std::chrono::steady_clock::now() < deadline) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          moving.render(w, h, 520);
          require(moving.surfaceUploadBytes() <= 2 * 1024 * 1024,
                  "normal streaming uploads at most 2 MiB per frame");
        }
        require(moving.surfaceMeshRevision() > initialRevision,
                "background terrain replacement actually finishes and uploads");
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
            delta +=
                std::abs(int(first[i * 4 + c]) - int(travelled[i * 4 + c]));
        require(delta > w * h, "surface actually moves through space");
        std::vector<unsigned char> reference;
        {
          Renderer fresh(Renderer::defaultDataDirectory(), {},
                         {true, 1, 162000 - 510, false, true, true, false});
          fresh.render(w, h, 520);
          reference = pixels(w, h);
        }
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
        moving.render(w, h, 520.1);
        GLfloat restored[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, restored);
        for (int i = 0; i < 4; ++i)
          require(restored[i] == expected[i], "host clear color restored");
        require(glGetError() == GL_NO_ERROR, "moving surface GL validity");
      }
      std::vector<unsigned char> oceanImage;
      {
        Renderer ocean(Renderer::defaultDataDirectory(), {}, {true, 0, 0});
        ocean.render(w, h, 1740);
        oceanImage = pixels(w, h);
      }
      {
        Fixture noReflections(false, true);
        Renderer clearWater(noReflections.path.string(), {}, {true, 0, 0});
        clearWater.render(w, h, 1740);
        auto bare = pixels(w, h);
        int changed = 0;
        for (int i = 0; i < w * h / 8; ++i)
          for (int c = 0; c < 3; ++c)
            changed +=
                std::abs(int(bare[i * 4 + c]) - int(oceanImage[i * 4 + c])) > 1;
        std::cout << "Ocean cloud reflections changed " << changed
                  << " channels\n";
        require(changed > 100,
                "ocean reflects the same cloud sheet as the sky");
      }
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
