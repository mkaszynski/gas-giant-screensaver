#include "weather.hpp"
#include <iostream>
#include <stdexcept>
using namespace observatory;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main() {
  try {
    TravelLocation location{.37, .81};
    WeatherClimate wet{.9, 26, .1, .2}, dry{.1, 26, .1, 0},
        cold{.8, -12, .4, .1};
    double wetCover = 0, dryCover = 0, wetStorm = 0, dryStorm = 0,
           coldStorm = 0;
    int changed = 0, open = 0, storm = 0, fog = 0, flashes = 0;
    for (int i = 0; i < 5000; ++i) {
      double t = 1790900000. + i * 171.;
      auto w = weatherAt(wet, location, t), d = weatherAt(dry, location, t),
           c = weatherAt(cold, location, t);
      wetCover += w.cover;
      dryCover += d.cover;
      wetStorm += w.storm;
      dryStorm += d.storm;
      coldStorm += c.storm;
      open += w.storm < .1;
      storm += w.storm > .2;
      fog += w.fog > .01;
      flashes += w.flashRadiance > 0;
      require(w.cover >= 0 && w.cover <= 1 && w.storm >= 0 && w.storm <= 1,
              "bounded weather");
      require(w.base > .1 && w.base + w.thickness < 12, "tropospheric clouds");
      require(w.noiseOffset.x >= 0 && w.noiseOffset.x < 1,
              "wrapped numeric noise offset");
      auto next = weatherAt(wet, location, t + .001),
           day = weatherAt(wet, location, t + 1800.);
      require(std::abs(next.cover - w.cover) < .00001,
              "weather does not jump at Unix-scale times");
      changed += std::abs(day.cover - w.cover) > .03;
    }
    std::cout << "Mean cover wet=" << wetCover / 5000
              << " dry=" << dryCover / 5000 << " storms=" << storm
              << " open=" << open << " fog=" << fog << " flashes=" << flashes
              << '\n';
    require(wetCover > dryCover * 1.8,
            "wet climates have substantially more cloud");
    require(wetStorm > dryStorm * 10 && coldStorm == 0,
            "deep convection favors warm humid biomes");
    require(storm > 30 && open > 4000,
            "rare tall storms preserve the usual giant view");
    require(fog > 100 && flashes > 0,
            "fog banks and exposure-integrated lightning occur");
    require(changed > 2000, "weather differs on consecutive solar days");
    double energy = 0;
    for (int i = 0; i < 30; ++i)
      energy += weather_detail::pulse(i / 30., 1. / 30, .017) / 30.;
    require(std::abs(energy - .08) < 1.e-12,
            "subframe flash preserves optical energy");
    for (double t : {0., 1800., 1790900000., 1.e10}) {
      auto a = weatherAt(wet, {.5, -pi}, t), b = weatherAt(wet, {.5, pi}, t);
      require(std::abs(a.cover - b.cover) < 1.e-10,
              "weather continuous across longitude seam");
    }
    SurfaceWorld world(WEATHER_ASSET_DIR "/moon.ggworld");
    auto c = weatherClimate(world, world.location(162000));
    require(c.moisture >= 0 && c.moisture <= 1 && c.elevation >= 0 &&
                c.ocean >= 0 && c.ocean <= 1,
            "regional climate reads actual biome data");
    auto noise = weatherNoise();
    require(noise.size() == 64 * 64 * 64 * 4,
            "noise cache has bounded one-MiB storage");
    int min = 255, max = 0;
    for (auto v : noise) {
      min = std::min(min, int(v));
      max = std::max(max, int(v));
    }
    require(min < 10 && max > 240, "noise cache retains cloud shape contrast");
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
