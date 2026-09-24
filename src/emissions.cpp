#include "emissions.hpp"
#include <algorithm>
#include <cstdint>
namespace observatory {
namespace {
// Integer hashing keeps events deterministic across launches and frame rates.
double random(std::uint64_t key) {
  key += 0x9e3779b97f4a7c15ULL;
  key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9ULL;
  key = (key ^ (key >> 27)) * 0x94d049bb133111ebULL;
  return ((key ^ (key >> 31)) >> 11) * 0x1.0p-53;
}
Vec3 direction(double latitude, double longitude) {
  return {std::cos(latitude) * std::cos(longitude), std::sin(latitude),
          std::cos(latitude) * std::sin(longitude)};
}
} // namespace
double lightningEnergyFraction(double begin, double end, int strokes) {
  if (end <= begin || strokes < 1)
    return 0;
  double energy = 0, weightSum = 0;
  for (int i = 0; i < strokes; ++i) {
    const double weight = std::pow(.55, i), start = i * .047;
    const double tau = .012 + i * .004, cutoff = 8 * tau;
    auto cumulative = [&](double t) {
      t = std::clamp(t - start, 0., cutoff);
      return (1 - std::exp(-t / tau)) / (1 - std::exp(-8.));
    };
    energy += weight * (cumulative(end) - cumulative(begin));
    weightSum += weight;
  }
  return energy / weightSum;
}
std::vector<LightningFlash> lightningAt(EmissionFrame frame) {
  std::vector<LightningFlash> flashes;
  if (!std::isfinite(frame.seconds) || frame.seconds < 0 ||
      !std::isfinite(frame.exposure) || frame.exposure <= 0)
    return flashes;
  // A short shutter, not the full duration of a suspended display.
  const double exposure = std::clamp(frame.exposure, 1. / 1000, .1);
  for (int storm = 0; storm < stormCount; ++storm) {
    const std::uint64_t seed = 0x671a23ULL + storm * 7919;
    const double interval = 7 + 9 * random(seed),
                 phase = interval * random(seed + 1);
    const auto epoch = static_cast<std::int64_t>(
        std::floor((frame.seconds + phase) / interval));
    for (auto slot = epoch - 1; slot <= epoch; ++slot) {
      if (slot < 0)
        continue;
      const std::uint64_t key = seed + std::uint64_t(slot) * 104729;
      const double onset =
          slot * interval - phase + interval * .65 * random(key + 2);
      const int strokes = 1 + int(3 * random(key + 3));
      const double fraction = lightningEnergyFraction(
          frame.seconds - exposure - onset, frame.seconds - onset, strokes);
      if (fraction <= 0)
        continue;
      // Water-cloud convection: compact upper-cloud and broader buried flashes.
      // Escaping energies are assumptions within the measured Jovian range;
      // no multiplier for moving the planet to the habitable zone.
      const double energy =
          std::pow(10., 7 + 2.5 * std::pow(random(key + 4), 2.));
      const bool compact = random(key + 5) < .35;
      const double width =
          compact ? 35 + 35 * random(key + 6) : 90 + 70 * random(key + 6);
      const double latitude =
          (storm % 2 ? 1 : -1) * (18 + 43 * random(seed + 7)) * pi / 180;
      const double longitude = 2 * pi * random(seed + 8);
      // Repeated events remain in a ~1000-km storm, not randomly over the disk.
      const double jitter = 450. / 71492.;
      flashes.push_back(
          {direction(latitude + jitter * (random(key + 9) - .5),
                     longitude +
                         jitter * (random(key + 10) - .5) / std::cos(latitude)),
           width / 2.354820045, energy * fraction / exposure});
    }
  }
  return flashes;
}
double auroraRayleighs(double seconds) {
  // Measured Jovian broadband zenith radiances span ~80–300 kR.
  // Slow real-time variation, without importing UV storm brightness into RGB.
  return 160000 *
         (1 + .22 * std::sin(seconds / 173.) + .12 * std::sin(seconds / 61.));
}
Vec3 bodyDirection(Vec3 n, const Body &body) {
  const Vec3 y = body.pole, x = normalized(cross(y, {0, 0, 1})),
             z = cross(x, y);
  const double c = std::cos(body.spin), s = std::sin(body.spin);
  return x * (c * n.x - s * n.z) + y * n.y + z * (s * n.x + c * n.z);
}
} // namespace observatory
