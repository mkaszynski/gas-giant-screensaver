#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include "sky_catalog.hpp"
#include <GLFW/glfw3.h>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
using namespace observatory;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
struct Fixture {
  std::filesystem::path path;
  Fixture() {
    char name[] = "/tmp/observatory-sky-XXXXXX";
    path = mkdtemp(name);
    const auto root =
        std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directories(path / "assets/sky");
    for (auto &entry : std::filesystem::directory_iterator(root / "assets"))
      if (entry.path().filename() != "sky")
        std::filesystem::create_symlink(
            entry.path(), path / "assets" / entry.path().filename());
    for (auto &entry : std::filesystem::directory_iterator(root / "assets/sky"))
      if (entry.path().filename() != "stars.bin")
        std::filesystem::create_symlink(
            entry.path(), path / "assets/sky" / entry.path().filename());
    edit("scene.glsl", [](auto &s) {
      for (const std::string fn :
           {"vec3 sky(vec3 d){", "vec3 viewT(vec3 d){"}) {
        auto at = s.find(fn);
        require(at != std::string::npos, "sky fixture stage");
        s.insert(at + fn.size(), fn.find("sky") != std::string::npos
                                     ? "return vec3(0.);"
                                     : "return vec3(1.);");
      }
    });
    edit("atmosphere.glsl", [](auto &s) {
      const std::string fn =
          "vec3 transmission(highp sampler2D lut,float h,float mu){";
      auto at = s.find(fn);
      require(at != std::string::npos, "transmission fixture stage");
      s.insert(at + fn.size(), "return vec3(1.);");
    });
    edit("post.frag", [](auto &s) {
      s.insert(s.rfind('}'), "color=vec4(sceneColor(uv),1.);\n");
    });
  }
  template <class Edit> void edit(const char *name, Edit transform) {
    const auto p = path / "shaders" / name;
    std::ifstream input(p);
    std::ostringstream b;
    b << input.rdbuf();
    auto source = b.str();
    transform(source);
    std::ofstream(p) << source;
  }
  void star(Vec3 d, float flux = 1, Vec3 color = {1, 1, 1}) {
    std::ofstream out(path / "assets/sky/stars.bin", std::ios::binary);
    out.write("GGSSTAR1", 8);
    auto word = [&](std::uint32_t value) {
      for (int i = 0; i < 4; ++i)
        out.put(char(value >> (8 * i)));
    };
    word(1);
    for (float v :
         {float(d.x), float(d.y), float(d.z), flux, float(color.x),
          float(color.y), float(color.z), float(-2.5 * std::log10(flux))})
      word(std::bit_cast<std::uint32_t>(v));
  }
  ~Fixture() { std::filesystem::remove_all(path); }
};
Scene emptySky(Vec3 forward) {
  Scene s{};
  for (int i = 0; i < 21; ++i)
    s.bodies[i] = {{1e6 + i * 100., 0, -1e6}, .01, 3, 0};
  s.east = {1, 0, 0};
  s.up = {0, 1, 0};
  s.north = {0, 0, 1};
  s.sun = s.sunLocal = {0, -1, 0};
  s.sunVisibility = 0;
  s.forward = normalized(forward);
  s.right = normalized(cross(Vec3{0, 1, 0}, s.forward));
  s.cameraUp = cross(s.forward, s.right);
  return s;
}
std::vector<float> render(Renderer &r, const Scene &s, int w = 480, int h = 270,
                          bool bodies = false) {
  GLuint texture, fbo;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT,
               nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "HDR sky framebuffer");
  r.renderScene(w, h, s, 0, 1800, 1, bodies, false, {-1});
  std::vector<float> pixels(w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, pixels.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &texture);
  for (size_t i = 0; i < pixels.size(); ++i)
    require(std::isfinite(pixels[i]) && pixels[i] >= 0,
            "finite nonnegative sky radiance");
  return pixels;
}
double flux(const std::vector<float> &p, int channel = 0) {
  double sum = 0;
  for (size_t i = channel; i < p.size(); i += 4)
    sum += p[i];
  return sum;
}
double fineVariation(const std::vector<float> &pixels, int width, int height) {
  double variation = 0, total = 0;
  for (int y = 1; y < height - 1; ++y)
    for (int x = 1; x < width - 1; ++x) {
      const int at = (y * width + x) * 4;
      const double here = pixels[at];
      variation += std::abs(4 * here - pixels[at - 4] - pixels[at + 4] -
                            pixels[at - width * 4] - pixels[at + width * 4]);
      total += here;
    }
  return variation / total;
}
int main() {
  try {
    require(glfwInit(), "GLFW");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto *window = glfwCreateWindow(480, 270, "Catalog and galaxy validation",
                                    nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      Fixture fixture;
      const Vec3 direction = normalized(Vec3{0, 1, 1});
      fixture.star(direction);
      auto s = emptySky(direction);
      Renderer stars(fixture.path.string(), {false, true});
      const double initial = flux(render(stars, s));
      double low = initial, high = initial;
      for (double shift : {.25, .5, .75, 1.}) {
        s.forward = normalized(
            direction - s.right * (shift * 2 * std::tan(29 * pi / 180) / 270));
        s.cameraUp = cross(s.forward, s.right);
        const double measured = flux(render(stars, s));
        low = std::min(low, measured);
        high = std::max(high, measured);
      }
      std::cout << "Subpixel star flux variation " << high / low - 1 << '\n';
      require(initial > 0 && high / low - 1 < .005,
              "star retains integrated brightness under subpixel motion");
      s = emptySky(direction);
      require(std::abs(flux(render(stars, s, 1920, 1080)) / (initial * 16) -
                       1) < .005,
              "star energy scales with resolution without aliasing");
      fixture.star(direction, .01);
      Renderer dim(fixture.path.string(), {false, true});
      require(std::abs(flux(render(dim, s)) / (initial * .01) - 1) < .01,
              "five magnitudes gives one hundredth flux");
      fixture.star(direction, .1, {2, .8, .2});
      Renderer colored(fixture.path.string(), {false, true});
      const auto pixels = render(colored, s);
      require(std::abs(flux(pixels, 0) / flux(pixels, 1) - 2.5) < .02 &&
                  std::abs(flux(pixels, 1) / flux(pixels, 2) - 4) < .03,
              "catalog color survives GPU upload and filtering");
      Renderer none(fixture.path.string(), {false, false});
      require(flux(render(none, s)) == 0,
              "disabling both sky layers leaves no celestial signal");
      {
        Fixture gradient;
        gradient.star(direction);
        gradient.edit("background.frag", [](auto &source) {
          const std::string output = "color=vec4(L,1);";
          const auto at = source.find(output);
          require(at != std::string::npos, "dim gradient fixture");
          source.insert(at, "L=vec3(3.e-8+uv.x*3.e-8);\n");
        });
        Renderer faint(gradient.path.string(), {false, false});
        const auto pixels = render(faint, s, 2048, 16);
        std::set<float> levels;
        double error = 0;
        for (int x = 0; x < 2048; ++x) {
          const double expected = 3.e-8 * (1 + (x + .5) / 2048);
          levels.insert(pixels[x * 4]);
          error = std::max(error, std::abs(pixels[x * 4] / expected - 1));
        }
        std::cout << "Dim gradient levels " << levels.size()
                  << " worst relative error " << error << '\n';
        require(
            levels.size() > 900 && error < .001,
            "scene storage preserves dim continuous radiance before exposure");
      }
      {
        Fixture atmosphere;
        atmosphere.star(direction);
        atmosphere.edit("atmosphere.glsl", [](auto &source) {
          const std::string bypass = "return vec3(1.);";
          const auto at = source.find(bypass);
          require(at != std::string::npos, "real transmission fixture");
          source.erase(at, bypass.size());
        });
        atmosphere.edit("background.frag", [](auto &source) {
          const auto at = source.find("color=vec4(L,1);");
          require(at != std::string::npos, "transmission gradient fixture");
          source.insert(at, "L=transmission(uTrans,.2,.15+uv.x*.03)-"
                            "transmission(uTrans,.2,.15);\n");
        });
        Renderer transmission(atmosphere.path.string(), {false, false});
        const auto pixels = render(transmission, s, 2048, 16);
        std::set<float> levels;
        for (int x = 0; x < 2048; ++x)
          levels.insert(pixels[x * 4]);
        std::cout << "Atmospheric transmission gradient levels "
                  << levels.size() << '\n';
        require(
            levels.size() > 900,
            "high precision atmospheric sampling avoids horizontal sky bands");
      }
      {
        Renderer galaxy(fixture.path.string(), {true, false});
        const auto galacticCenter =
            equatorialDirection(17.7603 * pi / 12, -28.9362 * pi / 180);
        const auto galacticPole =
            equatorialDirection(12.8573 * pi / 12, 27.1283 * pi / 180);
        const double center = flux(render(galaxy, emptySky(galacticCenter))),
                     pole = flux(render(galaxy, emptySky(galacticPole)));
        std::cout << "Galaxy center/pole contrast " << center / pole << '\n';
        require(center > pole * 2,
                "Rendered Milky Way has the expected aligned "
                "bright band and dark pole");
        const auto detail =
            render(galaxy, emptySky(galacticCenter), 1920, 1080);
        const double variation = fineVariation(detail, 1920, 1080);
        std::cout << "1080p diffuse galaxy detail " << variation << '\n';
        // Pixel-scale variation was 0.49 for the unfiltered Gaia raster and
        // 0.22 after mip bias. The model must integrate unresolved light into
        // a continuous field while retaining nonzero structure and band/pole
        // contrast; high brightness alone cannot pass this check.
        require(
            variation > .0001 && variation < .06,
            "continuous structured radiance without amplified star-map grain");
        // Cross every cube edge from either side; adjacent camera poses must
        // not introduce a face boundary or reset, including close to a cube
        // corner.
        for (int axis = 0; axis < 3; ++axis)
          for (int a : {-1, 1})
            for (int b : {-1, 1}) {
              Vec3 edge = axis == 0   ? Vec3{double(a), double(b), .2}
                          : axis == 1 ? Vec3{.2, double(a), double(b)}
                                      : Vec3{double(a), .2, double(b)};
              const auto before =
                  render(galaxy, emptySky(edge - Vec3{.000001, 0, 0}));
              const auto after =
                  render(galaxy, emptySky(edge + Vec3{.000001, 0, 0}));
              double difference = 0;
              for (size_t i = 0; i < before.size(); i += 4)
                difference += std::abs(before[i] - after[i]);
              require(difference / std::max(flux(before), 1.e-20) < .002,
                      "continuous cubemap edge");
            }
      }
      // The conservative cosine bound skips empty solar pixels without
      // changing the original limb darkening or angular antialiasing.
      fixture.edit("background.frag", [](auto &source) {
        const std::string bound =
            "if(uEclipse>0. && cosine>cos(.00465047+aa)){";
        const auto at = source.find(bound);
        require(at != std::string::npos, "solar bound fixture");
        source.replace(at, bound.size(), "{");
      });
      Renderer solarReference(fixture.path.string(), {false, false});
      for (double shift : {0., .25, .5, .75}) {
        auto solarScene = emptySky(direction);
        solarScene.sun = solarScene.sunLocal =
            normalized(direction + Vec3{shift * .001, 0, 0});
        solarScene.sunVisibility = .75;
        const auto optimized = render(none, solarScene, 960, 540),
                   reference = render(solarReference, solarScene, 960, 540);
        double error = 0;
        for (size_t i = 0; i < reference.size(); i += 4)
          error += std::abs(optimized[i] - reference[i]);
        require(flux(reference) > 0 && error / flux(reference) < .00001,
                "solar optimization preserves original integrated radiance");
      }
      // Through the real atmosphere/display path: visible in totality, washed
      // out by sunlight, blocked by the solid mountain interiors.
      Renderer actual(Renderer::defaultDataDirectory()),
          without(Renderer::defaultDataDirectory(), {false, false});
      auto night = sceneAt(437625), day = sceneAt(0);
      const auto on = render(actual, night, 960, 540, true),
                 off = render(without, night, 960, 540, true);
      Fixture terrainMask;
      terrainMask.star({0, 0, 1});
      terrainMask.edit("post.frag", [](auto &s) {
        s.insert(s.rfind('}'), "vec3 td=ray();vec4 tm=terrainSample(td);"
                               "color=vec4(step(1.,tm.a),step(1.e-20,texture("
                               "uTerrainGeometry,terrainUV(td)).b),0.,1.);\n");
      });
      Renderer maskRenderer(terrainMask.path.string(), {false, false});
      const auto mask = render(maskRenderer, night, 960, 540);
      int dryPixels = 0;
      double difference = 0;
      for (int y = 0; y < 540; ++y)
        for (int x = 0; x < 960; ++x)
          for (int c = 0; c < 3; ++c) {
            const auto i = (y * 960 + x) * 4 + c;
            difference += std::abs(on[i] - off[i]);
            if (mask[(y * 960 + x) * 4] == 1.f &&
                mask[(y * 960 + x) * 4 + 1] == 0.f) {
              ++dryPixels;
              require(on[i] == off[i],
                      "opaque dry terrain occludes galaxy and catalog");
            }
          }
      require(dryPixels > 1000,
              "sky occlusion test covers dry mountain interiors");
      require(difference > 1, "galaxy survives final display in eclipse");
      const auto dayOn = render(actual, day, 480, 270, true),
                 dayOff = render(without, day, 480, 270, true);
      double dayDifference = 0;
      for (size_t i = 0; i < dayOn.size(); i += 4)
        dayDifference += std::abs(dayOn[i] - dayOff[i]);
      require(dayDifference < .01, "daylight washes out celestial background");
      // A mathematically nonzero star is insufficient: require a populated
      // starfield in the actual tone-mapped eclipse sky, over the Milky Way.
      Renderer noStars(Renderer::defaultDataDirectory(), {true, false});
      const auto starOn = render(actual, night, 1920, 1080, true),
                 starOff = render(noStars, night, 1920, 1080, true);
      std::vector<unsigned char> visible(1920 * 1080);
      int peak = 0;
      for (int y = 324; y < 1080; ++y)
        for (int x = 0; x < 1920; ++x) {
          double contrast = 0;
          for (int c = 0; c < 3; ++c)
            contrast =
                std::max(contrast, double(starOn[(y * 1920 + x) * 4 + c] -
                                          starOff[(y * 1920 + x) * 4 + c]));
          visible[y * 1920 + x] = contrast >= 12. / 255;
          peak = std::max(peak, int(contrast * 255));
        }
      int points = 0;
      std::vector<int> pending;
      for (int start = 0; start < 1920 * 1080; ++start)
        if (visible[start]) {
          ++points;
          visible[start] = 0;
          pending.push_back(start);
          while (!pending.empty()) {
            const int at = pending.back();
            pending.pop_back();
            for (int dy = -1; dy <= 1; ++dy)
              for (int dx = -1; dx <= 1; ++dx) {
                const int x = at % 1920 + dx, y = at / 1920 + dy;
                if (x < 0 || x >= 1920 || y < 0 || y >= 1080)
                  continue;
                const int next = y * 1920 + x;
                if (visible[next]) {
                  visible[next] = 0;
                  pending.push_back(next);
                }
              }
          }
        }
      std::cout << "Visible eclipse catalog stars " << points
                << " peak contrast " << peak << "/255\n";
      require(points >= 80 && peak >= 100,
              "populated visible stars survive diffuse galaxy and display "
              "exposure");
      Renderer decoded(Renderer::defaultDataDirectory(), {true, true, false});
      const auto portable = render(decoded, night, 960, 540, true);
      double formatError = 0;
      for (size_t i = 0; i < portable.size(); i += 4)
        for (int c = 0; c < 3; ++c)
          formatError = std::max(formatError,
                                 std::abs(double(portable[i + c] - on[i + c])));
      std::cout << "BC6H GPU/portable display difference " << formatError * 255
                << "/255\n";
      require(
          formatError < 2. / 255,
          "hardware and portable BC6H decode preserve the same HDR sky colors");
      require(glGetError() == GL_NO_ERROR, "sky GL errors");
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Sky GPU behavior passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
