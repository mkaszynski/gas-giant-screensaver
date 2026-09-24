#pragma once
#include "system.hpp"
#include <array>
namespace observatory {
// Real seconds, independent of orbital compression and unlock motionScale.
// Negative time disables emissions for isolated renderer/reference tests.
struct EmissionFrame {
  double seconds = -1;
  double exposure = 1. / 30;
  bool lightning = true, aurora = true;
};
struct LightningFlash {
  Vec3 normal;
  double sigmaKm = 0;
  double powerWatts =
      0; // upward escaping visible power, averaged over exposure
};
constexpr int stormCount = 24;
constexpr double visibleSolarIrradiance = 535.; // W/m2, approximate 400–700 nm
std::vector<LightningFlash> lightningAt(EmissionFrame);
// Integral of a normalized multi-stroke optical flash between two real times.
double lightningEnergyFraction(double begin, double end, int strokes);
double auroraRayleighs(double realSeconds);
Vec3 bodyDirection(Vec3 normal, const Body &);
} // namespace observatory
