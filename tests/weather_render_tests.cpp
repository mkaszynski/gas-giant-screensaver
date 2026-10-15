#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace observatory;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
using Pixels = std::vector<unsigned char>;
constexpr int width = 640, height = 360;
Pixels capture(Renderer &r, const Scene &s, double weather,
               double sceneSeconds = 510) {
  r.renderScene(width, height, s, sceneSeconds, 1800, 1, true, true, {},
                weather);
  require(r.weatherResolution() == std::array<int, 2>{width, height},
          "clouds and terrain must actually share display resolution");
  Pixels p(width * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  require(glGetError() == GL_NO_ERROR, "weather GLES draw/depth resolve error");
  return p;
}
struct Difference {
  double mean = 0;
  int peak = 0, changed = 0;
};
Difference difference(const Pixels &a, const Pixels &b) {
  Difference d;
  for (size_t i = 0; i < a.size(); ++i)
    if (i % 4 != 3) {
      int v = std::abs(int(a[i]) - int(b[i]));
      d.mean += v;
      d.peak = std::max(d.peak, v);
      d.changed += v > 1;
    }
  d.mean /= width * height * 3;
  return d;
}
struct Fixture {
  std::filesystem::path root;
  Fixture() {
    char path[] = "/tmp/observatory-weather-XXXXXX";
    root = mkdtemp(path);
    auto data = std::filesystem::absolute(Renderer::defaultDataDirectory());
    std::filesystem::copy(data / "shaders", root / "shaders",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directory_symlink(data / "assets", root / "assets");
  }
  ~Fixture() { std::filesystem::remove_all(root); }
  Fixture(const Fixture &) = delete;
  Fixture &operator=(const Fixture &) = delete;
  void copyShaders(const Fixture &source) {
    std::filesystem::copy(
        source.root / "shaders", root / "shaders",
        std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::overwrite_existing);
  }
  void replace(const char *file, const std::string &term,
               const std::string &replacement) {
    auto path = root / "shaders" / file;
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    auto s = text.str();
    auto offset = s.find(term);
    require(offset != std::string::npos, "weather fixture target exists");
    s.replace(offset, term.size(), replacement);
    std::ofstream(path) << s;
  }
  void append(const char *file, const std::string &code) {
    auto path = root / "shaders" / file;
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    auto source = text.str();
    source.insert(source.rfind('}'), code);
    std::ofstream(path) << source;
  }
  void layer(double base) {
    // Deliberately homogeneous cloud slabs isolate depth clipping from
    // morphology.
    auto path = root / "shaders/weather_common.glsl";
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    auto s = text.str();
    auto start = s.find("float weatherDensity("),
         end = s.find("float weatherCirrus(", start);
    require(start != std::string::npos && end != std::string::npos,
            "cloud slab fixture");
    s.replace(start, end - start,
              "float weatherDensity(vec3 p,float eye,float lod){float "
              "h=weatherHeight(p,eye);return h>" +
                  std::to_string(base) + "&&h<" + std::to_string(base + 1.) +
                  "?20.:0.;}\n");
    std::ofstream(path) << s;
    replace("weather.frag", "uWeatherLayer.x",
            "(" + std::to_string(base) + ")");
    // Replace every remaining base/layer/fog reference in this fixture only.
    for (const char *file : {"weather_common.glsl", "weather.frag"}) {
      auto f = root / "shaders" / file;
      std::ifstream input(f);
      std::ostringstream t;
      t << input.rdbuf();
      auto code = t.str();
      for (auto [term, value] :
           {std::pair<std::string, std::string>{
                "uWeatherLayer.x", "(" + std::to_string(base) + ")"},
            {"uWeatherLayer.y", "1.0"},
            {"uWeatherLayer.z", "0.0"}}) {
        size_t at = 0;
        while ((at = code.find(term, at)) != std::string::npos) {
          code.replace(at, term.size(), value);
          at += value.size();
        }
      }
      std::ofstream(f) << code;
    }
    replace("weather.frag",
            "if(uWeatherKind.w>.001&&highDistance>0.&&highDistance<end)",
            "if(false)");
    replace("weather.frag", "if(uWeatherRain>.02", "if(false");
  }
  void opacity() {
    replace("post.frag", "return L*cloud.a+cloud.rgb/uSceneScale;",
            "return vec3(1.-cloud.a);");
    replace("post.frag",
            "color=displayColor(applyWeather(surface.rgb/uSceneScale));",
            "color=vec4(applyWeather(vec3(0.)),1.);");
    replace("post.frag", "color=displayColor(applyWeather(L));",
            "color=vec4(applyWeather(vec3(0.)),1.);");
    replace("post.frag", "if(weatherApplied)color=displayColor(L);",
            "if(weatherApplied)color=vec4(applyWeather(vec3(0.)),1.);");
  }
};
int main() {
  try {
    require(glfwInit(), "GLFW");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window =
        glfwCreateWindow(width, height, "Weather tests", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    {
      SurfaceWorld world(Renderer::defaultDataDirectory() +
                         "/assets/world/moon.ggworld");
      auto forest = travelScene(world, 510, 162000);
      Renderer weather(Renderer::defaultDataDirectory(), {}, {true, 0, 162000});
      auto a = capture(weather, forest, 1790900000.),
           b = capture(weather, forest, 1790900000.001);
      auto continuity = difference(a, b);
      std::cout << "Unix subframe weather: peak=" << continuity.peak
                << " mean=" << continuity.mean << '\n';
      require(continuity.peak <= 5 && continuity.mean < .02,
              "weather advances smoothly at epoch times");
      {
        Fixture fallback;
        fallback.append("weather-filter-probe.frag", "color=vec4(1.);\n");
        Renderer floating(fallback.root.string(), {}, {true, 0, 162000});
        require(floating.floatingWeatherNoise(),
                "imprecise filtering must select the floating-point cache");
        auto fallbackImage = capture(floating, forest, 1790900000.);
        auto delta = difference(a, fallbackImage);
        std::cout << "Compact/floating mip interpolation: mean=" << delta.mean
                  << " peak=" << delta.peak << '\n';
        require(
            delta.mean < .1 && delta.peak <= 6,
            "compact and floating mip caches must preserve cloud appearance");
      }
      {
        auto previous = capture(weather, forest, 1790900000.);
        int peak = 0;
        for (int frame = 1; frame <= 16; ++frame) {
          auto next = capture(weather, forest, 1790900000. + frame / 30.);
          auto delta = difference(previous, next);
          peak = std::max(peak, delta.peak);
          require(
              delta.peak <= 12 && delta.mean < .05,
              "weather must advance without visible frame-to-frame flicker");
          previous = std::move(next);
        }
        std::cout << "30fps cloud motion: peak=" << peak << '\n';
      }
      {
        SurfaceOptions fullOptions{true, 0, 162000};
        fullOptions.weatherCache = false;
        Renderer exact(Renderer::defaultDataDirectory(), {}, fullOptions);
        Renderer cachedSky(Renderer::defaultDataDirectory(), {},
                           {true, 0, 162000});
        for (double route : {162000., 5600.390625}) {
          double worstMean = 0;
          int worstPeak = 0;
          for (int frame = 0; frame < 48; ++frame) {
            double elapsed = frame / 30.;
            double sky = (route == 162000. ? 510. : 1740.) + elapsed;
            auto view = travelScene(world, sky, route + elapsed);
            auto cached = capture(cachedSky, view, 1790900000. + elapsed, sky);
            auto traced = capture(exact, view, 1790900000. + elapsed, sky);
            require(cachedSky.weatherSkyCacheResolution() ==
                            std::array<int, 2>{width / 2, height / 2} &&
                        exact.weatherSkyCacheResolution() ==
                            std::array<int, 2>{0, 0},
                    "cache allocation follows the selected rendering path");
            auto error = difference(cached, traced);
            worstMean = std::max(worstMean, error.mean);
            worstPeak = std::max(worstPeak, error.peak);
            if (error.mean >= .15 || error.peak > 16)
              std::cout << "Sky cache mismatch frame=" << frame
                        << " route=" << route << " mean=" << error.mean
                        << " peak=" << error.peak << '\n';
            require(error.mean < .15 && error.peak <= 16,
                    "moving cached sky clouds must agree with current full "
                    "tracing");
          }
          std::cout << "Sky cache route=" << route << ": mean=" << worstMean
                    << " peak=" << worstPeak << '\n';
        }
        auto night = travelScene(world, 1200, 162000);
        for (int frame = 0; frame < 24; ++frame) {
          double clock = 1512.4 + frame / 30.;
          auto cached = capture(cachedSky, night, clock, 1200);
          auto traced = capture(exact, night, clock, 1200);
          auto error = difference(cached, traced);
          require(error.mean < .15 && error.peak <= 16,
                  "sky reuse must preserve lightning flashes");
        }
        auto jumped = travelScene(world, 900, 0);
        auto cached = capture(cachedSky, jumped, 1790901000., 900);
        Renderer fresh(Renderer::defaultDataDirectory(), {}, {true, 0, 162000});
        auto traced = capture(fresh, jumped, 1790901000., 900);
        require(difference(cached, traced).peak <= 1,
                "time and viewpoint changes retain exact current cloud state");
      }
      {
        // Alternate very different cloud emission/transmission pairs whose
        // final color is identical over the matching background. This checks
        // actual reuse, rather than merely comparing the decision formula.
        Fixture balanced;
        balanced.replace("weather_sky_cache.glsl", "bool cachedSkyWeather(",
                         "bool cacheUsed=false;\nbool cachedSkyWeather(");
        balanced.replace("weather_sky_cache.glsl", " return true;",
                         " cacheUsed=true;return true;");
        const std::string background =
            "vec3 background=texelFetch(uWeatherScene,ivec2(gl_FragCoord.xy),"
            "0).rgb*(uExposure/uSceneScale);";
        balanced.replace("weather_sky_cache.glsl", background,
                         "vec3 background=vec3(.1)*uExposure;");
        balanced.append("weather.frag", R"(
 if(uWeatherSkyPass){
  float t=.1+.8*mod(floor(gl_FragCoord.x),2.);
  color=vec4(vec3(.1)*(1.-t)*uSceneScale,t);
 }else color=vec4(vec3(cacheUsed?1.:0.),1.);
)");
        balanced.replace("post.frag", "if(coverage>=1.)", "if(false)");
        balanced.append("post.frag", R"(
 color=vec4(texelFetch(uWeather,ivec2(gl_FragCoord.xy),0).rgb,1.);
)");
        Fixture contrasting;
        contrasting.copyShaders(balanced);
        contrasting.replace("weather_sky_cache.glsl",
                            "vec3 background=vec3(.1)*uExposure;",
                            "vec3 background=vec3(.03,.1,.3)*uExposure;");
        Renderer cancellation(balanced.root.string(), {}, {true, 0, 162000});
        Renderer contrast(contrasting.root.string(), {}, {true, 0, 162000});
        auto view = travelScene(world, 1740, 5600.390625);
        auto matching = capture(cancellation, view, 1790900000., 1740);
        auto different = capture(contrast, view, 1790900000., 1740);
        int reusedMatching = 0, reusedDifferent = 0;
        for (size_t i = 0; i < matching.size(); i += 4) {
          reusedMatching += matching[i] > 127;
          reusedDifferent += different[i] > 127;
        }
        std::cout << "Projected cloud cache: matching=" << reusedMatching
                  << " contrasting=" << reusedDifferent << '\n';
        require(reusedMatching > width * height * .7 &&
                    reusedDifferent < reusedMatching / 2,
                "cache must reuse cancelling optical columns and reject "
                "visible background-dependent contrast");
      }
      auto storm = capture(weather, forest, 1260.);
      auto changed = difference(a, storm);
      std::cout << "Clear/storm: " << changed.changed
                << " channels, mean=" << changed.mean << '\n';
      require(changed.changed > width * height / 2,
              "independent weather clock changes the rendered sky");
      {
        Fixture anchored;
        anchored.replace("post.frag", "if(coverage>=1.)", "if(false)");
        anchored.append("weather.frag", R"(
 vec2 horizontal=(uv-.5)*40.;
 float h=uWeatherLayer.x+uWeatherLayer.y*.35;
 vec3 point=vec3(horizontal.x,h-uObserverHeight-dot(horizontal,horizontal)/(2.*Rg),horizontal.y);
 vec3 high=point+vec3(0.,9.5-h,0.);
 vec3 field=vec3(weatherDensity(point,uObserverHeight,0.)/14.,
                 weatherDensity(point+vec3(0.,.1,0.),uObserverHeight,0.)/14.,
                 weatherCirrus(high)/.11+.5*weatherDensity(point+vec3(0.,uWeatherLayer.y*.48,0.),uObserverHeight,0.)/14.);
 color=vec4(field*uSceneScale,1.);
)");
        anchored.append("post.frag", "color=vec4(texelFetch(uWeather,ivec2(gl_"
                                     "FragCoord.xy),0).rgb/uSceneScale,1.);\n");
        Renderer field(anchored.root.string(), {}, {true, 0, 162000});
        auto view = forest;
        view.altitudeMeters = 600;
        auto low = capture(field, view, 0);
        view.altitudeMeters = 5000;
        auto high = capture(field, view, 0);
        auto delta = difference(low, high);
        Fixture unpruned;
        unpruned.copyShaders(anchored);
        unpruned.replace("weather_common.glsl",
                         "if(shape<=threshold&&(uWeatherKind.y<=0.||z<=.72||"
                         "n.r<=threshold-.12))return 0.;",
                         "");
        Renderer exhaustive(unpruned.root.string(), {}, {true, 0, 162000});
        for (double clock : {0., 1260., 1790900000.}) {
          auto pruned = capture(field, view, clock),
               original = capture(exhaustive, view, clock);
          auto error = difference(pruned, original);
          require(error.peak <= 1 && error.mean < .001,
                  "empty-space pruning must retain body and anvil density");
        }
        int minimum = 255, maximum = 0;
        for (size_t i = 0; i < low.size(); i += 4) {
          minimum = std::min(minimum, int(low[i]));
          maximum = std::max(maximum, int(low[i]));
        }
        std::cout
            << "Fixed-world cloud field at changing observer heights: peak="
            << delta.peak << '\n';
        require(maximum - minimum > 32 && delta.peak <= 1,
                "clouds must stay anchored to the world when flight altitude "
                "changes");
      }
      {
        Fixture precision;
        const std::string transmitted = ".0001+uWeatherOffset.x*.001";
        precision.replace("weather.frag",
                          "return vec4(min(L*uSceneScale,vec3(60000.)),T);",
                          "return vec4(vec3(0.)," + transmitted + ");");
        precision.replace("post.frag",
                          "vec3 sceneColor(vec2 p){return "
                          "texture(uScene,p).rgb/uSceneScale;}",
                          "vec3 sceneColor(vec2 p){return vec3(15.);}");
        precision.replace("post.frag", "tonemap(L*uExposure)",
                          "tonemap(L*12.)");
        Fixture opacity;
        opacity.copyShaders(precision);
        opacity.replace("weather.frag",
                        "return vec4(vec3(0.)," + transmitted + ");",
                        "return vec4(vec3(0.),1.-(" + transmitted + "));");
        opacity.replace("post.frag", "return L*cloud.a+cloud.rgb/uSceneScale;",
                        "return L*(1.-cloud.a)+cloud.rgb/uSceneScale;");
        Renderer transmittedLight(precision.root.string(), {},
                                  {true, 0, 162000});
        Renderer quantizedOpacity(opacity.root.string(), {}, {true, 0, 162000});
        double position = geographicUp({forest.latitude, forest.longitude}).x *
                          (surfaceRadius / 1000.) / 14.;
        double rate = .008 / 14.;
        double turn = std::floor(position - 1790900000. * rate);
        auto view = forest;
        view.sunLocal = normalized({.3, -.5, .8});
        view.sunVisibility = 0;
        Pixels before, badBefore;
        int peak = 0, badPeak = 0;
        // Sweep several half-float bins independently of framebuffer rounding.
        for (int frame = 0; frame <= 64; ++frame) {
          double phase = .1 + .8 * frame / 64.;
          double clock = (position - turn - phase) / rate;
          auto after = capture(transmittedLight, view, clock);
          auto badAfter = capture(quantizedOpacity, view, clock);
          if (!before.empty())
            for (int y = height * 3 / 4; y < height; ++y)
              for (int x = 0; x < width; ++x)
                for (int c = 0; c < 3; ++c) {
                  int i = (y * width + x) * 4 + c;
                  peak =
                      std::max(peak, std::abs(int(before[i]) - int(after[i])));
                  badPeak = std::max(
                      badPeak, std::abs(int(badBefore[i]) - int(badAfter[i])));
                }
          before = std::move(after);
          badBefore = std::move(badAfter);
        }
        std::cout << "Dense-cloud transmission: jump=" << peak
                  << " old opacity=" << badPeak << '\n';
        require(peak <= 3 && badPeak > 32,
                "transmission storage must avoid half-float opaque-cloud "
                "quantization jumps");
      }
      {
        Fixture original;
        original.replace("weather.frag",
                         "vec3 lighting=solar*(lightingTerms.forward*shadow.x+"
                         ".032*(.35+.65*shadow.y))+lightingTerms.ambient;",
                         "vec3 lighting=weatherLightTrans(d,weatherHeight(p,"
                         "uObserverHeight),shadow);");
        Renderer scalar(original.root.string(), {}, {true, 0, 162000});
        for (auto [sky, clock, route] :
             {std::array<double, 3>{510., 1790900000., 162000.},
              {900., 1790900000., 162000.},
              {1200., 1512.8, 162000.},
              {510., 1260., 162000.},
              {1740., 1790900000., 5600.390625}}) {
          auto view = travelScene(world, sky, route);
          auto fast = capture(weather, view, clock, sky),
               precise = capture(scalar, view, clock, sky);
          auto delta = difference(fast, precise);
          std::cout << "Prepared/original lighting: peak=" << delta.peak
                    << " mean=" << delta.mean << '\n';
          require(delta.peak <= 1 && delta.mean < .002,
                  "cached ray lighting may change only sub-code rounding");
        }
      }
      {
        Fixture reference;
        reference.replace("weather.frag",
                          "vec2 shadow=cloudSunTransmission(p,density,lod);",
                          "float step=clamp(uWeatherLayer.y*.25,.06,1.);float "
                          "tau=step*(density*.35+.9*weatherDensity(p+uSun*("
                          "step*.5),uObserverHeight,lod+1.)+1.4*weatherDensity("
                          "p+uSun*(step*1.5),uObserverHeight,lod+1.));vec2 "
                          "shadow=exp(-tau*vec2(1.,.2));");
        Renderer direct(reference.root.string(), {}, {true, 0, 162000});
        for (auto [sky, clock] :
             {std::pair<double, double>{510., 0.}, {900., 0.}, {510., 1260.}}) {
          auto view = travelScene(world, sky, 162000);
          auto cached = capture(weather, view, clock, sky),
               exact = capture(direct, view, clock, sky);
          auto delta = difference(cached, exact);
          std::cout << "Cached solar column sky=" << sky << " weather=" << clock
                    << ": mean=" << delta.mean << " peak=" << delta.peak
                    << '\n';
          require(delta.mean < .5 && delta.peak <= 16,
                  "solar cache must preserve broad cloud lighting and energy");
        }
      }
      {
        Fixture twoTaps;
        twoTaps.replace("weather.frag",
                        "stepLength*2.3*weatherDensity(p+uSun*(stepLength*(2."
                        "55/2.3)),uObserverHeight,lod+1.)",
                        "stepLength*(.9*weatherDensity(p+uSun*(stepLength*.5),"
                        "uObserverHeight,lod+1.)+"
                        "1.4*weatherDensity(p+uSun*(stepLength*1.5),"
                        "uObserverHeight,lod+1.))");
        SurfaceOptions precise{true, 0, 162000};
        precise.weatherCache = false;
        Renderer centroid(Renderer::defaultDataDirectory(), {}, precise);
        Renderer reference(twoTaps.root.string(), {}, precise);
        for (double elapsed : {0., 4., 10.}) {
          auto view = travelScene(world, 1740 + elapsed, 5600.390625 + elapsed);
          auto a =
              capture(centroid, view, 1790900000. + elapsed, 1740 + elapsed);
          auto b =
              capture(reference, view, 1790900000. + elapsed, 1740 + elapsed);
          auto delta = difference(a, b);
          std::cout << "Far solar centroid elapsed=" << elapsed
                    << ": mean=" << delta.mean << " peak=" << delta.peak
                    << '\n';
          require(delta.mean < .1 && delta.peak <= 16,
                  "far sunlight approximation must preserve coastal cloud "
                  "lighting");
        }
      }
      {
        Fixture reference;
        reference.replace("weather.frag", "span/clamp(span/.15,12.,48.)",
                          "span/48.");
        reference.replace("weather.frag",
                          "baseStep*(1.+2.*(1.-smoothstep(.02,.2,T)))",
                          "baseStep");
        Renderer fixed(reference.root.string(), {}, {true, 0, 162000});
        for (auto [sky, clock, route] :
             {std::array<double, 3>{510., 0., 162000.},
              {900., 0., 162000.},
              {510., 1260., 162000.},
              {510., 1790900000., 162000.},
              {900., 1790900000., 162000.},
              {1740., 1790900000., 5600.390625},
              {1740., 0., 0.},
              {360., 1790900000., 210600.}}) {
          auto view = travelScene(world, sky, route);
          auto adaptive = capture(weather, view, clock, sky),
               exact = capture(fixed, view, clock, sky);
          auto delta = difference(adaptive, exact);
          std::cout << "Adaptive quadrature sky=" << sky << " weather=" << clock
                    << ": mean=" << delta.mean << " peak=" << delta.peak
                    << '\n';
          require(delta.mean < .5 && delta.peak <= 16,
                  "adaptive integration must preserve cloud skins and visible "
                  "radiance");
        }
      }
      {
        Fixture diagnostic;
        diagnostic.opacity();
        Renderer opacity(diagnostic.root.string(), {}, {true, 0, 162000});
        for (auto [routeTime, skyTime] :
             {std::pair<double, double>{162000., 510.},
              {0., 1740.},
              {16200., 1710.},
              {210600., 360.}}) {
          auto view = travelScene(world, skyTime, routeTime);
          Vec3 planet = view.local(view.bodies[0].position - view.observer);
          int open = 0;
          for (int frame = 0; frame < 24; ++frame) {
            auto mask =
                capture(opacity, view, 1790900000. + frame * 1800., skyTime);
            double transmission = 0;
            int count = 0;
            for (int y = 0; y < height; ++y)
              for (int x = 0; x < width; ++x) {
                double px = (2 * (x + .5) / width - 1) * double(width) /
                            height * std::tan(29 * pi / 180);
                double py =
                    (2 * (y + .5) / height - 1) * std::tan(29 * pi / 180);
                Vec3 ray = normalized(view.forward + view.right * px +
                                      view.cameraUp * py);
                double along = dot(planet, ray);
                if (along > 0 && length(planet - ray * along) <
                                     view.bodies[0].radius * .85) {
                  transmission += 1 - mask[(y * width + x) * 4] / 255.;
                  ++count;
                }
              }
            require(count > 1000, "meaningful giant visibility region");
            open += transmission / count > .5;
          }
          std::cout << "Giant mostly unobscured at route " << routeTime << ": "
                    << open << "/24 weather states\n";
          require(open >= 15, "usual view keeps most of the giant visible in "
                              "representative biomes");
        }
      }
      {
        Fixture noFlash;
        noFlash.replace("weather.frag", "if(uWeatherFlash.w>0.)", "if(false)");
        Renderer without(noFlash.root.string(), {}, {true, 0, 162000});
        auto location = TravelLocation{forest.latitude, forest.longitude};
        auto climate = weatherClimate(world, location);
        double instant = -1, best = 0;
        for (int i = 0; i < 50000; ++i) {
          double t = 800 + i * .02;
          auto w = weatherAt(climate, location, t);
          if (w.flashRadiance <= .1)
            continue;
          Vec3 point{w.flash.x, w.flash.y - forest.altitudeMeters / 1000.,
                     w.flash.z};
          Vec3 d = normalized(point);
          double z = dot(d, forest.forward);
          if (z > .8 && dot(d, forest.cameraUp) / z > -.4 &&
              dot(d, forest.cameraUp) / z < .4 &&
              w.flashRadiance * std::exp(-dot(point, point) / 18.) > best) {
            instant = t;
            best = w.flashRadiance * std::exp(-dot(point, point) / 18.);
          }
        }
        require(instant >= 0, "a cloud flash occurs within camera view");
        auto night = travelScene(world, 1200, 162000);
        auto flash = capture(weather, night, instant, 1200),
             dark = capture(without, night, instant, 1200);
        auto delta = difference(flash, dark);
        std::cout << "Lightning time=" << instant << " peak=" << delta.peak
                  << " changed=" << delta.changed << '\n';
        require(delta.peak > 5 && delta.changed > 100,
                "local lightning visibly illuminates participating cloud");
      }
      {
        Fixture high;
        high.replace("weather_common.glsl", "*uWeatherKind.w*.055", "*.5");
        high.replace("weather.frag", "uWeatherKind.w>.001&&", "");
        Renderer wisps(high.root.string(), {}, {true, 0, 210600});
        auto view = travelScene(world, 360, 210600);
        Vec3 p = geographicUp({view.latitude, view.longitude}) *
                 (surfaceRadius / 1000.);
        double coordinates[] = {p.x / 96., p.y * 3.5 / 96., p.z * .7 / 96.};
        double rates[] = {.018 / 96., -.003 * 3.5 / 96., .012 * .7 / 96.};
        for (int axis = 0; axis < 3; ++axis) {
          double turns =
              std::round(coordinates[axis] - 1790900000. * rates[axis]);
          double seam = (coordinates[axis] - turns) / rates[axis];
          auto before = capture(wisps, view, seam - .0001, 360);
          auto after = capture(wisps, view, seam + .0001, 360);
          auto delta = difference(before, after);
          std::cout << "Cirrus wrap axis " << axis << ": peak=" << delta.peak
                    << " mean=" << delta.mean << '\n';
          require(delta.peak <= 5 && delta.mean < .02,
                  "noninteger cloud shear must wrap without a visible pop");
        }
      }
      {
        Fixture edge;
        edge.layer(4.);
        edge.replace("weather.frag",
                     "return vec4(min(L*uSceneScale,vec3(60000.)),T);",
                     "return vec4(vec3(1.-T)*uSceneScale,T);");
        edge.replace("surface.frag",
                     "color=vec4(min(land*uSceneScale,vec3(60000.)),1.);",
                     "color=vec4(0.,0.,0.,1.);");
        edge.replace("post.frag",
                     "vec3 sceneColor(vec2 p){return "
                     "texture(uScene,p).rgb/uSceneScale;}",
                     "vec3 sceneColor(vec2 p){return vec3(0.);}");
        edge.replace("post.frag", "if(coverage>=1.)", "if(false)");
        edge.replace("post.frag",
                     "L+=transmission(uTrans,uObserverHeight,uSun.y)*halo*"
                     "solarVisibility;",
                     "");
        edge.replace("post.frag",
                     "vec3 mapped=pow(tonemap(L*uExposure),vec3(1./2.2));",
                     "vec3 mapped=L;");
        edge.replace("post.frag",
                     "return vec4((mapped+dither)*uOpacity,uOpacity);",
                     "return vec4(mapped,1.);");
        edge.append("post.frag", "color.g=surface.a;\n");
        edge.replace(
            "post.frag", "L=skyL*(1.-surface.a)+groundL;",
            R"(L=skyL*(1.-surface.a)+groundL;L.b=front.a>.999&&back.a<.001?.5:0.;)");
        Renderer test(edge.root.string(), {}, {true, 0, 162000});
        auto view = travelScene(world, 510, 162000);
        view.altitudeMeters = 1000;
        auto pixels = capture(test, view, 0, 510);
        Fixture broken;
        broken.copyShaders(edge);
        broken.replace("post.frag",
                       "if(uWeatherEnabled&&surface.a>0.&&surface.a<1.)",
                       "if(false)");
        Renderer bad(broken.root.string(), {}, {true, 0, 162000});
        auto aliased = capture(bad, view, 0, 510);
        int tested = 0, peak = 0, brokenPeak = 0;
        for (int y = 1; y < height - 1; ++y)
          for (int x = 1; x < width - 1; ++x) {
            int i = (y * width + x) * 4, alpha = pixels[i + 1];
            if (alpha < 4 || alpha > 251 || pixels[i + 2] < 127 ||
                pixels[i + 2] > 128)
              continue;
            bool cloudySky = false;
            for (int offset : {-1, 1, -width, width}) {
              int j = i + offset * 4;
              cloudySky |= pixels[j + 1] == 0 && pixels[j] > 253;
            }
            if (!cloudySky)
              continue;
            ++tested;
            peak = std::max(peak, std::abs(int(pixels[i]) - (255 - alpha)));
            brokenPeak =
                std::max(brokenPeak, std::abs(int(aliased[i]) - (255 - alpha)));
          }
        std::cout << "Cloud/terrain MSAA edge: " << tested
                  << " partial pixels, peak error=" << peak
                  << " broken reference=" << brokenPeak << '\n';
        require(tested > 20,
                "cloud-backed ridge has meaningful antialiased silhouette");
        require(brokenPeak > 32, "regression fixture detects the former "
                                 "one-depth composite aliasing");
        require(peak <= 3, "clouds must preserve partial terrain coverage "
                           "rather than restoring a jagged edge");
      }
      {
        Fixture slab;
        slab.layer(1.);
        slab.opacity();
        Renderer test(slab.root.string(), {}, {true, 0, 0});
        auto s = travelScene(world, 510, 0);
        s.altitudeMeters = 600;
        auto below = capture(test, s, 0);
        for (int y = height * 3 / 4; y < height; ++y)
          for (int x = 4; x < width - 4; ++x)
            require(below[(y * width + x) * 4] > 250,
                    "cloud above observer blocks sky");
        for (int y = 0; y < 8; ++y)
          for (int x = 4; x < width - 4; ++x)
            require(below[(y * width + x) * 4] < 2,
                    "cloud layer above eye does not cover foreground water");
        s.altitudeMeters = 3000;
        auto above = capture(test, s, 0);
        for (int y = height * 3 / 4; y < height; ++y)
          for (int x = 4; x < width - 4; ++x)
            require(above[(y * width + x) * 4] < 2,
                    "clouds below observer leave upper sky clear");
        int opaque = 0;
        for (int y = 0; y < 8; ++y)
          for (int x = 4; x < width - 4; ++x)
            opaque += above[(y * width + x) * 4] > 250;
        require(opaque > width * 4, "low cloud banks obscure water from above");
      }
      {
        Fixture hidden;
        hidden.layer(-2.);
        hidden.opacity();
        Renderer test(hidden.root.string(), {}, {true, 0, 0});
        auto s = travelScene(world, 510, 0);
        s.altitudeMeters = 3000;
        auto p = capture(test, s, 0);
        for (int y = 0; y < height / 8; ++y)
          for (int x = 4; x < width - 4; ++x)
            require(
                p[(y * width + x) * 4] < 2,
                "resolved terrain depth hides all cloud behind solid surface");
      }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Weather altitude, terrain occlusion, continuity and "
                 "lightning passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
