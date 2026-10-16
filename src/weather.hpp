#pragma once
#include "surface.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace observatory {
// All lengths below are kilometres; time is continuous display seconds, never
// the 30-minute solar phase. This is a deterministic climate-driven visual
// weather model, not a numerical atmospheric forecast.
struct WeatherClimate {
  double moisture = .5, temperature = 15, elevation = 0, ocean = 0;
};
struct WeatherState {
  double cover = 0, storm = 0, stratus = 0, cirrus = 0;
  double base = 1, thickness = 1, fog = 0, fogTop = .4, rain = 0;
  Vec3 noiseOffset{}, regionalOffset{}, cirrusOffset{};
  Vec3 flash{};
  double flashRadiance = 0;
};
namespace weather_detail {
inline double smooth(double a, double b, double x) {
  x = std::clamp((x - a) / (b - a), 0., 1.);
  return x * x * (3 - 2 * x);
}
inline uint32_t hash(int x, int y, int z, uint32_t seed = 78113) {
  uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^
               uint32_t(z) * 83492791u ^ seed;
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  return h ^ (h >> 16);
}
inline double unit(uint32_t h) { return (h >> 8) * (1. / 16777216.); }
inline double noise(Vec3 p, int period = 0, uint32_t seed = 78113) {
  int x = int(std::floor(p.x)), y = int(std::floor(p.y)),
      z = int(std::floor(p.z));
  double fx = smooth(0, 1, p.x - x), fy = smooth(0, 1, p.y - y),
         fz = smooth(0, 1, p.z - z), sum = 0;
  auto wrap = [period](int n) {
    return period ? (n % period + period) % period : n;
  };
  for (int dz = 0; dz < 2; ++dz)
    for (int dy = 0; dy < 2; ++dy)
      for (int dx = 0; dx < 2; ++dx)
        sum += unit(hash(wrap(x + dx), wrap(y + dy), wrap(z + dz), seed)) *
               (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * (dz ? fz : 1 - fz);
  return sum;
}
inline double worley(Vec3 p, int period) {
  int x = int(std::floor(p.x)), y = int(std::floor(p.y)),
      z = int(std::floor(p.z));
  double best = 4;
  auto wrap = [period](int n) { return (n % period + period) % period; };
  for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        int cx = x + dx, cy = y + dy, cz = z + dz;
        Vec3 point{cx + unit(hash(wrap(cx), wrap(cy), wrap(cz), 1957)),
                   cy + unit(hash(wrap(cx), wrap(cy), wrap(cz), 5179)),
                   cz + unit(hash(wrap(cx), wrap(cy), wrap(cz), 7195))};
        Vec3 delta = point - p;
        best = std::min(best, dot(delta, delta));
      }
  return std::clamp(1. - std::sqrt(best) / 1.15, 0., 1.);
}
inline Vec3 wrap(Vec3 p) {
  return {p.x - std::floor(p.x), p.y - std::floor(p.y), p.z - std::floor(p.z)};
}
// Exposure-integrated optical pulses: frame rate and phase cannot make a
// subframe stroke disappear. A short secondary stroke follows the first.
inline double pulse(double time, double exposure, double start) {
  exposure = std::clamp(exposure, .001, 1.);
  auto integral = [](double t) {
    return .055 * std::clamp(t / .055, 0., 1.) +
           .025 * std::clamp((t - .12) / .04, 0., 1.);
  };
  return (integral(time + exposure - start) - integral(time - start)) /
         exposure;
}
} // namespace weather_detail
inline WeatherClimate weatherClimate(const SurfaceWorld &world,
                                     TravelLocation p) {
  WeatherClimate c{0, 0, 0, 0};
  // Regional rather than individual ridge climate keeps bases and fog banks
  // coherent across a mountain pass. Five samples, no terrain mesh generation.
  const Vec3 up = geographicUp(p), east = geographicEast(p),
             north = geographicNorth(p);
  const Vec3 offsets[] = {
      {0, 0, 0}, east * .006, east * -.006, north * .006, north * -.006};
  for (auto offset : offsets) {
    const Vec3 q = normalized(up + offset);
    auto s = world.sample(std::asin(q.y), std::atan2(q.z, q.x));
    c.moisture += s.moisture / 5;
    c.temperature += s.temperature / 5;
    c.elevation += std::max(0., s.ground) / 5000.;
    c.ocean += (1 - weather_detail::smooth(-100, 100, s.ground)) / 5;
  }
  return c;
}
inline WeatherState weatherAt(WeatherClimate c, TravelLocation location,
                              double seconds, double exposure = 1. / 30) {
  using namespace weather_detail;
  WeatherState w;
  Vec3 position = geographicUp(location) * (surfaceRadius / 1000.);
  // Incommensurate front scales avoid a daily reset or a repeated route's
  // identical weather. World-space sampling is continuous across longitudes.
  Vec3 front = position / 420. +
               Vec3{seconds / 3100., -seconds / 6700., seconds / 4700.};
  double pressure = .7 * noise(front) + .3 * noise(front * 2.17, 0, 977);
  double wet = std::clamp(.80 * c.moisture + .20 * c.ocean, 0., 1.);
  double warm = smooth(-3, 23, c.temperature);
  w.storm = smooth(.66, .82, pressure) * smooth(.32, .74, wet) * warm;
  w.stratus = smooth(.40, .68, pressure) * (.35 + .65 * (1 - warm)) * wet;
  w.cover = std::clamp(.08 + .60 * wet + (pressure - .5) * 1.25 + .28 * w.storm,
                       0., .96);
  // Fair-weather banks generally sit low in the boundary layer. Warm humid
  // instability builds cumulus; only mature convection reaches the upper sky.
  w.base =
      std::max(.08, .20 + .75 * std::min(3., c.elevation) + .65 * (1 - wet) +
                        1.3 * std::pow(1 - wet, 3) - .16 * c.ocean);
  double cumulus = smooth(.53, .68, pressure) * warm * wet;
  w.thickness = .35 + .45 * wet - .22 * c.ocean + 1.2 * cumulus + 6.0 * w.storm;
  w.cirrus = smooth(.40, .68, noise(front * .71 + Vec3{27, 18, 44}, 0, 271));
  w.rain = smooth(.32, .8, w.storm) + .2 * w.stratus;
  w.fog = .10 * wet * wet * smooth(.47, .72, pressure) * (1 - .5 * warm);
  w.fogTop = .20 + .65 * c.elevation + .22 * w.stratus;
  w.noiseOffset = wrap((position - Vec3{.008, .001, -.004} * seconds) / 14.);
  w.regionalOffset =
      wrap((position - Vec3{.006, .001, -.003} * seconds) / 384.);
  // Apply the noninteger shear before wrapping, exactly as in the shader's
  // local displacement. Wrapping first would pop the high cloud at a seam.
  Vec3 high = (position - Vec3{.018, -.003, .012} * seconds) / 96.;
  w.cirrusOffset = wrap({high.x, high.y * 3.5, high.z * .7});
  // One nearby convective cell per 24-second interval, irregularly timed.
  // The light is confined to participating cloud, not a fullscreen white flash.
  const int interval = int(std::floor(seconds / 24.));
  for (int slot = interval - 1; slot <= interval; ++slot) {
    const uint32_t h = hash(slot, int(std::floor(location.latitude * 8)),
                            int(std::floor(location.longitude * 8)), 9103);
    const double start = slot * 24. + unit(h) * 16.;
    const double flash =
        pulse(seconds, exposure, start) * smooth(.12, .55, w.storm);
    if (flash > 0) {
      double angle = 2 * pi * unit(hash(slot, 14, 27));
      double range = 2 + 20 * unit(hash(slot, 13, 28));
      w.flash = {std::sin(angle) * range, w.base + .65 * w.thickness,
                 std::cos(angle) * range};
      w.flashRadiance = .30 * flash;
    }
  }
  return w;
}
constexpr int weatherNoiseSize = 64;
inline std::vector<uint8_t> weatherNoise() {
  std::vector<uint8_t> data(weatherNoiseSize * weatherNoiseSize *
                            weatherNoiseSize * 4);
  for (int z = 0; z < weatherNoiseSize; ++z)
    for (int y = 0; y < weatherNoiseSize; ++y)
      for (int x = 0; x < weatherNoiseSize; ++x)
        for (int channel = 0; channel < 4; ++channel) {
          int frequency = 4 << channel;
          Vec3 p{double(x) / weatherNoiseSize * frequency,
                 double(y) / weatherNoiseSize * frequency,
                 double(z) / weatherNoiseSize * frequency};
          double value =
              weather_detail::noise(p, frequency, 78113 + channel * 311);
          if (channel < 3) {
            double cells = weather_detail::worley(p, frequency);
            value = channel == 0 ? .65 * value + .35 * cells : cells;
          }
          data[((z * weatherNoiseSize + y) * weatherNoiseSize + x) * 4 +
               channel] = uint8_t(std::round(255 * value));
        }
  return data;
}
} // namespace observatory
