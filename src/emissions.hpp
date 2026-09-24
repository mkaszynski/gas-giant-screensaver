#pragma once
#include "system.hpp"
#include <array>
namespace observatory {
// Display-clock seconds; lightning is converted to physical simulation time.
// Negative time disables emissions for isolated renderer/reference tests.
struct EmissionFrame {
  double seconds = -1;
  double exposure = 1. / 30;
  bool lightning = true, aurora = true;
  // Multipliers of modeled radiance: 1 = baseline, 5 = previous default.
  double lightningBrightness = 5, auroraBrightness = 5;
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
double lightningTimeScale(double daySeconds);
EmissionFrame acceleratedLightning(EmissionFrame, double daySeconds);
// Integral of a normalized multi-stroke optical flash between two physical
// times.
double lightningEnergyFraction(double begin, double end, int strokes);
double auroraRayleighs(double realSeconds);
Vec3 bodyDirection(Vec3 normal, const Body &);
} // namespace observatory
