#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace observatory;
void require(bool ok, const char *s) {
  if (!ok)
    throw std::runtime_error(s);
}
struct Fixture {
  std::filesystem::path path;
  Fixture() {
    char name[] = "/tmp/observatory-emissions-XXXXXX";
    path = mkdtemp(name);
    auto root = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(root / "shaders", path / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(root / "assets", path / "assets");
    auto edit = [&](const char *file, auto transform) {
      auto p = path / "shaders" / file;
      std::ifstream f(p);
      std::ostringstream b;
      b << f.rdbuf();
      auto s = b.str();
      transform(s);
      std::ofstream(p) << s;
    };
    for (const char *file :
         {"background.frag", "stars.frag", "body.frag", "rings.frag"})
      edit(file,
           [](auto &s) { s.insert(s.rfind('}'), "color.rgb=vec3(0.);\n"); });
    edit("scene.glsl", [](auto &s) {
      for (const std::string fn :
           {"vec3 sky(vec3 d){", "vec3 viewT(vec3 d){"}) {
        auto at = s.find(fn);
        require(at != std::string::npos, "fixture stage");
        s.insert(at + fn.size(), fn.find("sky") != std::string::npos
                                     ? "return vec3(0.);"
                                     : "return vec3(1.);");
      }
    });
    edit("post.frag", [](auto &s) {
      s.insert(s.rfind('}'), "color=vec4(sceneColor(uv),1.);\n");
    });
  }
  ~Fixture() { std::filesystem::remove_all(path); }
};
Scene scene(Vec3 observer) {
  Scene s{};
  for (int i = 0; i < 21; i++)
    s.bodies[i] = {{1e6 + i * 100., 1e6, 1e6}, .01, 3, 0};
  s.bodies[0] = {{0, 0, 0}, 1, 0, 0};
  s.bodies[0].pole = {0, 1, 0};
  s.observer = observer;
  s.north = normalized(-observer);
  s.east = normalized(cross(Vec3{0, 1, 0}, s.north));
  s.up = cross(s.north, s.east);
  s.forward = {0, 0, 1};
  s.right = {1, 0, 0};
  s.cameraUp = {0, 1, 0};
  s.sun = normalized(observer);
  s.sunLocal = s.local(s.sun);
  s.sunVisibility = 1;
  return s;
}
std::vector<float> render(Renderer &r, const Scene &s, EmissionFrame frame,
                          int w = 320, int h = 180, bool rings = false) {
  GLuint fbo, tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT,
               nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "HDR framebuffer");
  r.renderScene(w, h, s, 0, lightningTimeScale(1800) * 1800, 1, true, rings,
                frame);
  std::vector<float> p(w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, p.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &tex);
  return p;
}
// Validate the actual display path as well as isolated linear radiance. A
// physically nonzero signal can otherwise disappear in exposure/quantization.
int displayDifference(Renderer &r, double time, EmissionFrame frame) {
  constexpr int w = 1920, h = 1080;
  GLuint fbo, texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "display framebuffer");
  std::vector<unsigned char> off(w * h * 4), on(off.size());
  r.render(w, h, time);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, off.data());
  r.render(w, h, time, 1800, 1, true, true, frame);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, on.data());
  int peak = 0;
  for (size_t i = 0; i < on.size(); ++i)
    peak = std::max(peak, int(on[i]) - int(off[i]));
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &texture);
  return peak;
}
double flux(const std::vector<float> &p) {
  double f = 0;
  for (size_t i = 0; i < p.size(); i++)
    if (i % 4 != 3) {
      require(std::isfinite(p[i]) && p[i] >= 0, "finite nonnegative radiance");
      f += p[i];
    }
  return f;
}
int main() {
  try {
    require(glfwInit(), "GLFW");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto win =
        glfwCreateWindow(320, 180, "Emission validation", nullptr, nullptr);
    require(win, "GLES context");
    glfwMakeContextCurrent(win);
    {
      Fixture fixture;
      Renderer r(fixture.path.string());
      double t = 0;
      LightningFlash flash;
      for (int i = 10000; i < 20000; i++) {
        auto f = lightningAt({i * .01, 1. / 30});
        if (f.size() == 1 && f[0].powerWatts > 1.e10) {
          t = i * .01;
          flash = f[0];
          break;
        }
      }
      require(t > 0, "strong isolated flash fixture");
      std::cout << "Flash weather time " << t << '\n';
      auto s = scene(flash.normal * 3);
      EmissionFrame frame{t, 1. / 30, true, false};
      double min = 1e9, max = 0;
      for (double shift : {0., .25, .5, .75}) {
        s.forward =
            normalized(Vec3{shift * 2 * std::tan(29 * pi / 180) / 180., 0, 1});
        s.right = normalized(cross(s.cameraUp, s.forward));
        auto low = render(r, s, frame), high = render(r, s, frame, 2560, 1440);
        double l = flux(low), h = flux(high) / 64.;
        std::cout << "Flash shift " << shift << " flux " << l << " reference "
                  << h << '\n';
        require(l > 0 && std::abs(l / h - 1) < .04,
                "subpixel lightning energy agrees with 8x reference");
        min = std::min(min, l);
        max = std::max(max, l);
      }
      require(max / min - 1 < .02, "moving lightning does not sparkle");
      s = scene(flash.normal * 3);
      double clear = flux(render(r, s, frame));
      s.sun = -s.sun;
      s.sunLocal = s.local(s.sun);
      s.sunVisibility = 0;
      require(std::abs(flux(render(r, s, frame)) / clear - 1) < 1.e-5,
              "eclipse does not extinguish electrical emission");
      s.bodies[2] = {flash.normal * 2, .08, 3, 0};
      require(flux(render(r, s, frame)) < clear * .001,
              "foreground moon blocks flash");
      s.bodies[2] = {flash.normal * (-2), .08, 3, 0};
      require(std::abs(flux(render(r, s, frame)) / clear - 1) < 1.e-5,
              "moon behind giant does not block flash");
      s = scene(flash.normal * (-3));
      require(flux(render(r, s, frame)) == 0,
              "far-side flash hidden by planet");
      LightningFlash ringFlash;
      for (int i = 10000; i < 100000; ++i) {
        auto events = lightningAt({i * .01, 1. / 30});
        if (events.size() == 1 && events[0].powerWatts > 1.e9 &&
            std::abs(dot(events[0].normal, giantPole())) < .35) {
          ringFlash = events[0];
          frame.seconds = i * .01;
          break;
        }
      }
      require(ringFlash.powerWatts > 0, "low latitude ring flash");
      bool ringFixture = false;
      for (int lat = -70; lat <= 70 && !ringFixture; lat += 5)
        for (int lon = 0; lon < 360 && !ringFixture; lon += 5) {
          const double a = lat * pi / 180, b = lon * pi / 180;
          Vec3 observer = Vec3{std::cos(a) * std::cos(b), std::sin(a),
                               std::cos(a) * std::sin(b)} *
                          3;
          if (dot(ringFlash.normal, normalized(observer - ringFlash.normal)) <
              .2)
            continue;
          const Vec3 direction = normalized(ringFlash.normal - observer),
                     pole = giantPole();
          const double distance = -dot(observer, pole) / dot(direction, pole);
          const double radius = length(observer + direction * distance);
          if (distance <= 0 ||
              distance >= length(ringFlash.normal - observer) ||
              radius < 1.26 || radius > 1.34)
            continue;
          s = scene(observer);
          double off = flux(render(r, s, frame)),
                 on = flux(render(r, s, frame, 320, 180, true));
          if (off <= 0)
            continue;
          const double expected = std::exp(-ringOpticalDepth(radius) /
                                           std::abs(dot(direction, pole)));
          std::cout << "Ring transmission " << on / off << " analytic "
                    << expected << '\n';
          require(on > 0 && on < off * .98,
                  "rings attenuate emission without being opaque");
          require(std::abs(on / off - expected) < .12,
                  "ring line-of-sight optical depth");
          ringFixture = true;
        }
      require(ringFixture, "front-ring geometry fixture");
      frame = {t, 1. / 30, false, true};
      s = scene({0, 1.5, -2.598076211});
      min = 1e9;
      max = 0;
      for (double shift : {0., .25, .5, .75}) {
        s.forward =
            normalized(Vec3{shift * 2 * std::tan(29 * pi / 180) / 180., 0, 1});
        s.right = normalized(cross(s.cameraUp, s.forward));
        const double low = flux(render(r, s, frame)),
                     high = flux(render(r, s, frame, 2560, 1440)) / 64;
        std::cout << "Aurora shift " << shift << " flux " << low
                  << " reference " << high << '\n';
        require(low > 0 && std::abs(low / high - 1) < .15,
                "narrow aurora agrees with supersampled energy");
        min = std::min(min, low);
        max = std::max(max, low);
      }
      require(max / min - 1 < .06, "aurora stable under subpixel motion");
      // Each multiplier scales only its own emission, before tone mapping.
      for (bool lightning : {false, true}) {
        auto controlledScene =
            lightning ? scene(flash.normal * 3) : scene({0, 1.5, -2.598076211});
        EmissionFrame controlled{t, 1. / 30, lightning, !lightning, 1, 1};
        const double baseline = flux(render(r, controlledScene, controlled));
        require(baseline > 0, "brightness fixture has visible emission");
        double &gain = lightning ? controlled.lightningBrightness
                                 : controlled.auroraBrightness;
        for (double value : {0., .5, 5., 1000.}) {
          gain = value;
          const double measured = flux(render(r, controlledScene, controlled));
          if (value == 0)
            require(measured == 0, "zero brightness disables selected effect");
          else
            require(std::abs(measured / (baseline * value) - 1) < .08,
                    "independent brightness scales HDR emission linearly");
        }
        gain = 1;
        (lightning ? controlled.auroraBrightness
                   : controlled.lightningBrightness) = 1000000;
        require(flux(render(r, controlledScene, controlled)) == baseline,
                "disabled effect brightness cannot alter the other effect");
      }
      // Keep the planet fixed: the aurora itself must flow continuously.
      s = scene({0, 1.5, -2.598076211});
      EmissionFrame moving{t, 1. / 30, false, true};
      const auto startArc = render(r, s, moving);
      const Vec3 center = s.local(-s.observer);
      const double pixelMargin =
          2 * length(center) * std::tan(29 * pi / 180) / 180;
      for (int y = 0; y < 180; ++y)
        for (int x = 0; x < 320; ++x) {
          const Vec3 ray =
              normalized(s.forward +
                         s.right * ((2 * (x + .5) / 320 - 1) * 320 / 180. *
                                    std::tan(29 * pi / 180)) +
                         s.cameraUp * ((2 * (y + .5) / 180 - 1) *
                                       std::tan(29 * pi / 180)));
          if (length(cross(center, ray)) > 1 + 120. / 71492 + pixelMargin)
            for (int channel = 0; channel < 3; ++channel)
              require(startArc[(y * 320 + x) * 4 + channel] == 0,
                      "wide aurora cannot spill beyond its atmospheric shell");
        }
      auto shapeDifference = [&](double elapsed) {
        moving.seconds = t + elapsed;
        const auto nextArc = render(r, s, moving);
        const double a = flux(startArc), b = flux(nextArc);
        double difference = 0;
        for (size_t i = 0; i < startArc.size(); ++i)
          if (i % 4 != 3)
            difference += std::abs(startArc[i] / a - nextArc[i] / b);
        return difference;
      };
      require(shapeDifference(30) > .05,
              "auroral shape and bright patches move without planet rotation");
      require(shapeDifference(.001) < .02,
              "auroral motion is continuous rather than frame-random flicker");
      // Exercise the actual foreground compositor, not the isolated HDR
      // fixture.
      Renderer actual(Renderer::defaultDataDirectory());
      s = scene(flash.normal * 3);
      s.forward = normalized(Vec3{0, .9 * std::tan(29 * pi / 180), 1});
      s.cameraUp = normalized(cross(s.forward, s.right));
      s.sunLocal = {0, -1, 0};
      auto hidden = render(actual, s, {t, 1. / 30, true, false});
      auto background = render(actual, s, {-1});
      for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 320; ++x)
          for (int c = 0; c < 3; ++c)
            require(hidden[(y * 320 + x) * 4 + c] ==
                        background[(y * 320 + x) * 4 + c],
                    "opaque mountains block emission in the actual compositor");
      const int flashDisplay = displayDifference(
          actual, 430350, {430354.833333333, 1. / 30, true, false});
      const int auroraDisplay =
          displayDifference(actual, 437625, {103.04, 1. / 30, false, true});
      std::cout << "Final display lightning " << flashDisplay << " aurora "
                << auroraDisplay << '\n';
      require(flashDisplay >= 20,
              "eclipse flash survives final exposure and display quantization");
      require(auroraDisplay >= 2,
              "visible part of aurora survives the actual eclipse compositor");
      require(displayDifference(actual, 1400,
                                {430354.833333333, 1. / 30, true, false}) <= 2,
              "sunlight continues to wash out calibrated lightning");
      require(glGetError() == GL_NO_ERROR, "GL errors");
    }
    glfwDestroyWindow(win);
    glfwTerminate();
    std::cout << "Emission GPU behavior passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
