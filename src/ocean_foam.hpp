#pragma once
#include "ocean_quantiles.hpp"
#include <algorithm>
#include <cmath>
namespace observatory {
// Callaghan et al. 2008: measured stage-A/B whitecap area, converted from
// percent to fraction. Wind is the prescribed 10-m marine wind in m/s.
inline double whitecapFraction(double wind) {
  if (!std::isfinite(wind) || wind <= 3.7)
    return 0.;
  wind = std::min(wind, 23.09);
  const double ratio = std::cbrt(4.82 / 31.8);
  const double crossover = (3.7 + 1.98 * ratio) / (1. - ratio);
  return wind < crossover ? 3.18e-5 * std::pow(wind - 3.7, 3)
                          : 4.82e-6 * std::pow(wind + 1.98, 3);
}
inline double whitecapThreshold(double coverage) {
  if (!std::isfinite(coverage) || coverage <= 0.)
    return 1.;
  const double coordinate = std::log(std::clamp(coverage, 1.e-7, .12) / 1.e-7) /
                            std::log(.12 / 1.e-7) *
                            (oceanCrestQuantiles.size() - 1);
  const size_t i = std::min(size_t(coordinate), oceanCrestQuantiles.size() - 2);
  return std::lerp(double(oceanCrestQuantiles[i]),
                   double(oceanCrestQuantiles[i + 1]), coordinate - i);
}
// A prescribed sea-state estimate, not a forecast wind solver. It is tied to
// the same continuously varying cloud/front state used by waves and weather.
inline double marineWind(double cover, double storm, double fog) {
  return std::clamp((3. + 8. * cover + 10. * storm) *
                        (1. - .5 * std::clamp(fog * 2., 0., 1.)),
                    0., 23.09);
}
} // namespace observatory
