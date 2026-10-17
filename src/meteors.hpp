#pragma once
#include "meteor_control.hpp"
#include "system.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace observatory {
struct MeteorFrame {
  double seconds = -1, exposure = 1. / 30;
  MeteorOptions options{};
};
struct MeteorWindow {
  double start = 0, duration = 0;
  uint64_t id = 0;
};
struct MeteorSegment {
  Vec3 tail, head, color;
  double flux = 0, sigma = .55;
};
struct MeteorBatch {
  std::array<MeteorSegment, 96> segments{};
  size_t count = 0;
};
namespace meteor_detail {
inline uint64_t hash(uint64_t x) {
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}
inline double unit(uint64_t x) { return (hash(x) >> 11) * 0x1.0p-53; }
inline double smooth(double a, double b, double x) {
  x = std::clamp((x - a) / (b - a), 0., 1.);
  return x * x * (3 - 2 * x);
}
inline Vec3 world(const Scene &s, Vec3 local) {
  return s.east * local.x + s.up * local.y + s.north * local.z;
}
struct MoonFrame {
  Vec3 east, up, north, eye;
  explicit MoonFrame(const Scene &s) {
    up = {std::cos(s.latitude) * std::cos(s.longitude), std::sin(s.latitude),
          std::cos(s.latitude) * std::sin(s.longitude)};
    east = {-std::sin(s.longitude), 0, std::cos(s.longitude)};
    // sceneAt retains its historic south-positive convention; airborne scenes
    // use north-positive coordinates. Preserve both public renderer paths.
    north = cross(east, up) * -dot(cross(s.east, s.up), s.north);
    eye = up * (6371 + s.altitudeMeters / 1000.);
  }
  Vec3 global(Vec3 local) const {
    return east * local.x + up * local.y + north * local.z;
  }
  Vec3 local(Vec3 global) const {
    return {dot(global, east), dot(global, up), dot(global, north)};
  }
};
inline Vec3 rotate(Vec3 p, Vec3 axis, double angle) {
  return p * std::cos(angle) + cross(axis, p) * std::sin(angle) +
         axis * (dot(axis, p) * (1 - std::cos(angle)));
}
} // namespace meteor_detail
// A stratified renewal clock can be queried at any Unix time in O(1), without
// replaying hours of events. Consecutive starts are 1..8 hours apart
// (mean 4.5).
inline double meteorPeriod(double hours) {
  return std::isfinite(hours) && hours >= .5 && hours <= 12 ? hours * 3600 : 0;
}
inline double meteorShowerStart(uint64_t slot, double intervalHours = 4.5) {
  const double period = meteorPeriod(intervalHours);
  return double(slot) * period +
         meteor_detail::unit(slot ^ 0x478ab32) * (period * 7. / 9.);
}
inline MeteorWindow meteorWindowAt(double seconds, double intervalHours = 4.5) {
  const double period = meteorPeriod(intervalHours);
  if (!std::isfinite(seconds) || seconds < 0 || seconds > 1.e10 || period <= 0)
    return {};
  const uint64_t id = uint64_t(std::floor(seconds / period));
  // Shorter debug intervals also shorten encounters, retaining a bounded,
  // non-overlapping O(1) schedule. The default 6..16 minutes is unchanged.
  const double duration =
      std::min(360 + 600 * meteor_detail::unit(id ^ 0x55aa), period * .15);
  return {meteorShowerStart(id, intervalHours), duration, id};
}
inline double meteorNight(const Scene &s) {
  // Natural encounters are night-only. An explicit manual trigger bypasses
  // this gate; an eclipse alone does not change the automatic night schedule.
  return 1 - meteor_detail::smooth(-.24, -.10, s.sunLocal.y);
}
// Exact integral of a sin-squared ablation pulse. A sub-frame meteor retains
// its optical energy at 24/30/60 Hz instead of flashing on for a whole frame.
inline double meteorLightIntegral(double begin, double end, double duration) {
  auto primitive = [duration](double t) {
    t = std::clamp(t, 0., duration);
    return .5 * t - duration / (4 * pi) * std::sin(2 * pi * t / duration);
  };
  return std::max(0., primitive(end) - primitive(begin));
}
inline MeteorBatch meteorsAt(MeteorFrame frame, const Scene &s,
                             double aspect = 16. / 9., double day = 1800,
                             Vec3 previewRadiant = {},
                             const Scene *fieldScene = nullptr) {
  using namespace meteor_detail;
  MeteorBatch batch;
  const bool preview = frame.options.mode == MeteorMode::Preview;
  if (!std::isfinite(frame.seconds) || frame.seconds < 0 ||
      frame.seconds > 1.e10 || !std::isfinite(frame.exposure) ||
      !std::isfinite(frame.options.rate) ||
      meteorPeriod(frame.options.intervalHours) <= 0 ||
      !std::isfinite(aspect) || !std::isfinite(day) || day <= 0 ||
      aspect <= 0 || frame.options.mode == MeteorMode::Off ||
      (!preview && meteorNight(s) <= 0))
    return batch;
  auto window = meteorWindowAt(frame.seconds, frame.options.intervalHours);
  if (preview) {
    if (!std::isfinite(frame.options.previewStart) ||
        frame.options.previewStart < 0 || frame.options.previewStart > 1.e10)
      return batch;
    window.start = frame.options.previewStart;
    window.id = hash(uint64_t(std::max(0., window.start) * 1000));
    window.duration = 1.e11;
  }
  const double phase = frame.seconds - window.start;
  if (phase < -1 || phase > window.duration + 6)
    return batch;
  const double exposure = std::clamp(frame.exposure, .001, .25);
  const double rate = std::clamp(frame.options.rate, .5, 60.);
  const double interval = 60. / rate;
  const int64_t first =
      std::max<int64_t>(0, int64_t(std::floor((phase - 6) / interval)) - 1);
  const int64_t last = std::max<int64_t>(
      0, int64_t(std::floor((phase + exposure) / interval)) + 1);
  Vec3 radiantWorld = normalized(Vec3{2 * unit(window.id + 51) - 1,
                                      2 * unit(window.id + 52) - 1,
                                      2 * unit(window.id + 53) - 1});
  if (preview && length(previewRadiant) > .5)
    radiantWorld = normalized(previewRadiant);
  // Real velocities and sub-second streaks do not inherit the accelerated
  // astronomical clock. Correct the radiant for the moon's spin since birth.
  const double spinRate =
      2 * pi * simulationTimeScale(day) / homeOrbitalSeconds();
  const Scene &reference = fieldScene ? *fieldScene : s;
  const MoonFrame current(s), field(reference);
  const Vec3 eye = field.eye;
  auto add = [&](Vec3 tail, Vec3 head, Vec3 color, double flux, double sigma) {
    if (flux > 1.e-9 && batch.count < batch.segments.size())
      batch.segments[batch.count++] = {current.local(tail - current.eye),
                                       current.local(head - current.eye), color,
                                       flux * (preview ? 1. : meteorNight(s)),
                                       sigma};
  };
  for (int64_t i = first; i <= last; ++i) {
    const uint64_t seed = hash(window.id ^ (uint64_t(i) * 0x718aa7));
    const double birth = preview && i == 0 ? .55 : (i + unit(seed)) * interval;
    if (birth > window.duration)
      continue;
    if (!preview) {
      const double envelope =
          smooth(0, 30, birth) *
          (1 - smooth(window.duration - 60, window.duration, birth));
      if (unit(seed + 1) > envelope)
        continue;
    }
    const double age = phase - birth;
    if (age < -exposure || age > 5.5)
      continue;
    const Vec3 dispersion{2 * unit(seed + 21) - 1, 2 * unit(seed + 22) - 1,
                          2 * unit(seed + 23) - 1};
    const Vec3 radiant =
        s.local(rotate(normalized(radiantWorld + dispersion * .008),
                       s.bodies[1].pole, spinRate * age));
    if (radiant.y <= .08)
      continue;
    // Importance sample the visible atmospheric field rather than simulating
    // thousands of meteors behind the camera. Positions lie on a 105..115 km
    // spherical ablation shell, never on the celestial bodies or a sky image.
    const double px =
        (unit(seed + 2) * 1.8 - .9) * aspect * std::tan(29 * pi / 180);
    const double py = (unit(seed + 3) * 1.10 - .15) * std::tan(29 * pi / 180);
    const Vec3 direction = normalized(field.global(
        reference.forward + reference.right * px + reference.cameraUp * py));
    const double startHeight = 105 + 10 * unit(seed + 4);
    const double radius = 6371 + startHeight, b = dot(eye, direction);
    const double distance =
        -b + std::sqrt(b * b + radius * radius - dot(eye, eye));
    const Vec3 start = eye + direction * distance;
    const double entry = dot(normalized(start), current.global(radiant));
    if (entry <= .10)
      continue;
    const double speed = 26 + 42 * unit(seed + 5); // km/s, not clock-scaled
    const double duration =
        std::clamp((startHeight - 88) / (speed * entry), .25, 1.7);
    const Vec3 velocity = current.global(radiant) * -speed;
    // Sample the bright, naked-eye population (+2 or brighter). The rate
    // describes these events; unresolved faint dust needs no rendering work.
    const double magnitude = std::clamp(
        2 + std::log(std::max(1.e-7, unit(seed + 6))) / std::log(2.5), -4., 2.);
    const double flux = std::pow(10., -.4 * magnitude);
    const double tint = .35 * std::clamp((1.5 - magnitude) / 4., 0., 1.);
    const Vec3 baseColor = speed > 46 ? Vec3{.80, .92, 1.} : Vec3{1., .88, .73};
    const Vec3 color = Vec3{.94, .97, 1.} * (1 - tint) + baseColor * tint;
    const double headFlux =
        flux * meteorLightIntegral(age, age + exposure, duration) / exposure;
    if (headFlux > 0) {
      add(start + velocity * std::clamp(age, 0., duration),
          start + velocity * std::clamp(age + exposure, 0., duration), color,
          headFlux, .55);
    }
    const double wakeEnd = std::clamp(age, 0., duration);
    const double wakeStart = std::max(0., wakeEnd - .16);
    const double wakeFlux = .24 * flux *
                            meteorLightIntegral(wakeStart, wakeEnd, duration) /
                            .16 * std::exp(-std::max(0., age - duration) / .12);
    if (age > 0 && age < duration + .8)
      add(start + velocity * wakeStart, start + velocity * wakeEnd, color,
          wakeFlux, .65);
    // A few bright meteors leave a weak stationary ion train. Wind shears the
    // deposited air; it does not remain attached to the moving head.
    if (magnitude < .5 && unit(seed + 7) < .25 && age > .2 &&
        age < duration + 3.5) {
      for (int part = 0; part < 3; ++part) {
        const double a = duration * part / 3,
                     b = std::min(wakeEnd, duration * (part + 1) / 3);
        if (b <= a)
          continue;
        auto deposit = [&](double t) {
          const double delay = std::max(0., age - t);
          return start + velocity * t +
                 field.global(Vec3{.025, .002, -.012}) * std::pow(delay, 1.15);
        };
        const double trainFade =
            smooth(.20, .35, age) *
            (1 - smooth(duration + 2.8, duration + 3.5, age));
        const double deposited =
            meteorLightIntegral(a, b, duration) / (.5 * duration);
        const double glow = .035 * flux * trainFade * deposited *
                            std::exp(-std::max(0., age - (a + b) * .5) / 1.2);
        add(deposit(a), deposit(b), {.87, .93, 1.}, glow, .9);
      }
    }
  }
  return batch;
}
} // namespace observatory
