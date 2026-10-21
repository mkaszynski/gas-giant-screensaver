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
  double flux = 0, sigma = .16, taper = 0;
};
struct MeteorBatch {
  // At most ((6 + .25) * 30 + 3) candidates, with five segments each.
  // This covers the full 1800/min ceiling without dropping newer heads behind
  // older ion trains. Storage and work remain bounded at every timestamp.
  std::array<MeteorSegment, 1024> segments{};
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
struct Appearance {
  double magnitude, flux, endHeight, wakeSeconds, wakeFade, peakFraction,
      widthMeters;
};
inline Appearance appearance(uint64_t seed) {
  // Keep the naked-eye population calibration. Brighter, larger ablation
  // events penetrate farther and leave longer-lived luminous wakes, rather
  // than drawing resolved rocks or inflating the optical point-spread core.
  const double magnitude = std::clamp(
      2 + std::log(std::max(1.e-7, unit(seed + 6))) / std::log(2.5), -4., 2.);
  const double size = (2 - magnitude) / 6.;
  return {magnitude,
          std::pow(10., -.4 * magnitude),
          94 - 10 * size - 3 * unit(seed + 8),
          .012 + .065 * size + .014 * unit(seed + 9),
          .018 + .035 * size,
          .32 + .38 * unit(seed + 10),
          5 + 15 * unit(seed + 11)};
}
inline Vec3 emissionColor(uint64_t seed, double speed, double magnitude,
                          bool wake = false) {
  // A low-saturation display approximation to Fe/Mg/Na emission and excited
  // air, not a blackbody or a palette picked solely from entry speed. Color
  // becomes more apparent in brighter meteors; faint events stay nearly white.
  const double sodium = .25 + unit(seed + 31), magnesium = .4 + unit(seed + 32),
               iron = .8 + unit(seed + 33),
               air = std::clamp((speed - 30) / 38., 0., 1.);
  Vec3 spectrum = Vec3{1.12, .98, .78} * iron +
                  Vec3{.57, 1.04, .88} * magnesium +
                  Vec3{1.78, .82, .14} * sodium + Vec3{.72, .98, 1.35} * air;
  if (wake)
    spectrum = spectrum + Vec3{.65, 1.12, .72} * (.4 + air);
  spectrum = spectrum / dot(spectrum, Vec3{.2126, .7152, .0722});
  const double chroma = .025 + .19 * std::clamp((1.5 - magnitude) / 4., 0., 1.);
  return Vec3{1, 1, 1} * (1 - chroma) + spectrum * chroma;
}
inline double lightAt(double t, double duration, double peakFraction) {
  if (t <= 0 || t >= duration)
    return 0;
  const double rise = duration * peakFraction;
  return t < rise ? .5 * (1 - std::cos(pi * t / rise))
                  : .5 * (1 + std::cos(pi * (t - rise) / (duration - rise)));
}
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
inline uint64_t meteorPreviewId(const MeteorOptions &options) {
  if (!std::isfinite(options.previewStart) || options.previewStart < 0 ||
      options.previewStart > 1.e10)
    return 0;
  return meteor_detail::hash(uint64_t(options.previewStart * 1000) ^
                             options.restart);
}
// Pick once at the encounter's start. A radiant outside the visible field
// produces a coherent stream across the sky rather than a central starburst.
// Keep it above the horizon so an explicit trigger is immediately useful.
inline Vec3 meteorPreviewRadiant(const Scene &field, uint64_t id,
                                  double aspect = 16. / 9.) {
  const double horizontalLimit =
      std::tan(29 * pi / 180) *
      (std::isfinite(aspect) && aspect > 0 ? aspect : 16. / 9.) * 1.2;
  const double verticalLimit = std::tan(29 * pi / 180) * 1.2;
  const double minimumHeight = std::sin(25 * pi / 180),
               maximumHeight = std::sin(70 * pi / 180);
  for (uint64_t attempt = 0; attempt < 16; ++attempt) {
    const double azimuth =
        2 * pi * meteor_detail::unit(id ^ (0x6a09e667ull + 2 * attempt));
    const double y = minimumHeight + (maximumHeight - minimumHeight) *
                                        meteor_detail::unit(
                                            id ^ (0xbb67ae85ull + 2 * attempt));
    const double horizontal = std::sqrt(1 - y * y);
    const Vec3 direction{horizontal * std::cos(azimuth), y,
                         horizontal * std::sin(azimuth)};
    const double forward = dot(direction, field.forward);
    if (forward <= 0 ||
        std::abs(dot(direction, field.right)) > forward * horizontalLimit ||
        std::abs(dot(direction, field.cameraUp)) > forward * verticalLimit)
      return normalized(meteor_detail::world(field, direction));
  }
  // Bounded fallback for unusually wide views or a near-zenith camera.
  Vec3 horizontal{field.forward.x, 0, field.forward.z};
  if (length(horizontal) < 1.e-6)
    horizontal = {field.cameraUp.x, 0, field.cameraUp.z};
  horizontal = normalized(horizontal);
  return normalized(meteor_detail::world(
      field, horizontal * -std::sqrt(1 - minimumHeight * minimumHeight) +
                 Vec3{0, minimumHeight, 0}));
}
inline double meteorNight(const Scene &s) {
  // Natural encounters are night-only. An explicit manual trigger bypasses
  // this gate; an eclipse alone does not change the automatic night schedule.
  return 1 - meteor_detail::smooth(-.24, -.10, s.sunLocal.y);
}
// Exact integral of a sin-squared ablation pulse. A sub-frame meteor retains
// its optical energy at 24/30/60 Hz instead of flashing on for a whole frame.
inline double meteorLightIntegral(double begin, double end, double duration,
                                  double peakFraction = .5) {
  auto primitive = [duration, peakFraction](double t) {
    t = std::clamp(t, 0., duration);
    const double rise = duration * peakFraction, fall = duration - rise;
    return t <= rise
               ? .5 * t - rise / (2 * pi) * std::sin(pi * t / rise)
               : .5 * t + fall / (2 * pi) * std::sin(pi * (t - rise) / fall);
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
    window.id = meteorPreviewId(frame.options);
    window.duration = 1.e11;
  }
  const double phase = frame.seconds - window.start;
  if (phase < -1 || phase > window.duration + 6)
    return batch;
  const double exposure = std::clamp(frame.exposure, .001, .25);
  const double rate =
      std::clamp(frame.options.rate, meteorMinRate, meteorMaxRate);
  const double interval = 60. / rate;
  const double lead = preview ? .55 : 0.;
  const double streamPhase = phase - lead;
  const int64_t first = std::max<int64_t>(
      0, int64_t(std::floor((streamPhase - 6) / interval)) - 1);
  const int64_t last = std::max<int64_t>(
      0, int64_t(std::floor((streamPhase + exposure) / interval)) + 1);
  Vec3 radiantWorld = normalized(Vec3{2 * unit(window.id + 51) - 1,
                                      2 * unit(window.id + 52) - 1,
                                      2 * unit(window.id + 53) - 1});
  if (preview)
    radiantWorld = length(previewRadiant) > .5
                       ? normalized(previewRadiant)
                       : meteorPreviewRadiant(fieldScene ? *fieldScene : s,
                                              window.id, aspect);
  // Real velocities and sub-second streaks do not inherit the accelerated
  // astronomical clock. Correct the radiant for the moon's spin since birth.
  const double spinRate =
      2 * pi * simulationTimeScale(day) / homeOrbitalSeconds();
  const Scene &reference = fieldScene ? *fieldScene : s;
  const MoonFrame current(s), field(reference);
  const Vec3 eye = field.eye;
  auto add = [&](Vec3 tail, Vec3 head, Vec3 color, double flux,
                 double widthMeters, double taper) {
    const double distance = length((tail + head) * .5 - current.eye);
    const double physicalSigma = 1080. / (2 * std::tan(29 * pi / 180)) *
                                 widthMeters /
                                 (1000. * std::max(1., distance) * 2.35482);
    if (flux > 1.e-9 && batch.count < batch.segments.size())
      batch.segments[batch.count++] = {current.local(tail - current.eye),
                                       current.local(head - current.eye),
                                       color,
                                       flux * (preview ? 1. : meteorNight(s)),
                                       std::hypot(.16, physicalSigma),
                                       taper};
  };
  for (int64_t i = first; i <= last; ++i) {
    const uint64_t seed = hash(window.id ^ (uint64_t(i) * 0x718aa7));
    const double birth =
        lead + (preview && i == 0 ? 0 : (i + unit(seed)) * interval);
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
    const auto look = appearance(seed);
    const double duration =
        std::clamp((startHeight - look.endHeight) / (speed * entry), .25, 1.7);
    const Vec3 velocity = current.global(radiant) * -speed;
    // Sample the bright, naked-eye population (+2 or brighter). The rate
    // describes these events; unresolved faint dust needs no rendering work.
    const double magnitude = look.magnitude, flux = look.flux;
    const Vec3 color = emissionColor(seed, speed, magnitude);
    const double headFlux =
        flux *
        meteorLightIntegral(age, age + exposure, duration, look.peakFraction) /
        exposure;
    if (headFlux > 0) {
      const double before = lightAt(age, duration, look.peakFraction),
                   after = lightAt(age + exposure, duration, look.peakFraction);
      const double taper = std::clamp(
          (after - before) / std::max(1.e-9, after + before), -.9, .9);
      add(start + velocity * std::clamp(age, 0., duration),
          start + velocity * std::clamp(age + exposure, 0., duration), color,
          headFlux, look.widthMeters, taper);
    }
    const double wakeEnd = std::clamp(age, 0., duration);
    const double wakeStart = std::max(0., wakeEnd - look.wakeSeconds);
    const double wakeFlux =
        .10 * flux *
        meteorLightIntegral(wakeStart, wakeEnd, duration, look.peakFraction) /
        look.wakeSeconds *
        std::exp(-std::max(0., age - duration) / look.wakeFade);
    if (age > 0 && age < duration + .8)
      add(start + velocity * wakeStart, start + velocity * wakeEnd,
          emissionColor(seed, speed, magnitude, true), wakeFlux,
          look.widthMeters * 1.6, .88);
    // A few bright meteors leave a weak stationary ion train. Wind shears the
    // deposited air; it does not remain attached to the moving head.
    if (magnitude < -.5 && speed > 44 && unit(seed + 7) < .18 && age > .2 &&
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
            meteorLightIntegral(a, b, duration, look.peakFraction) /
            (.5 * duration);
        const double glow = .014 * flux * trainFade * deposited *
                            std::exp(-std::max(0., age - (a + b) * .5) / 1.2);
        add(deposit(a), deposit(b), {.96, 1.015, .98}, glow,
            std::hypot(look.widthMeters * 2., 45 * std::sqrt(age)), .35);
      }
    }
  }
  return batch;
}
} // namespace observatory
