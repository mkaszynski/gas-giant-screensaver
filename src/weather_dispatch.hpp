#pragma once
#include "system.hpp"
#include <algorithm>
#include <cmath>

namespace observatory {
// First complete four-row workgroup whose rays point above the local horizon.
// Above the cloud-top sphere those rays have no forward cloud intersection.
// Small geometric margins cover float uniform/interpolation roundoff. Any
// unsupported camera or observer position keeps the complete solver.
inline int clearWeatherFirstRow(int width, int height, Vec3 forward, Vec3 right,
                                Vec3 up, double observerKm, double cloudTopKm) {
  if (width <= 0 || height <= 0 || up.y <= 0. || !std::isfinite(observerKm) ||
      !std::isfinite(cloudTopKm) || observerKm < cloudTopKm + .002)
    return height;
  const double tanFov = std::tan(29 * pi / 180);
  const double minimumX = -std::abs(right.y) * width / height * tanFov;
  const double row =
      (1. + (.001 - forward.y - minimumX) / (up.y * tanFov)) * height / 2.;
  if (!std::isfinite(row))
    return height;
  if (row <= -2.)
    return 0;
  if (row >= height - 2.)
    return height;
  return std::min(height, (std::max(0, int(std::ceil(row)) + 2) + 3) / 4 * 4);
}

// Below the cloud base, rays can only enter the layer after leaving its inner
// sphere. Prove that exit lies beyond the weather solver's 100 km endpoint.
// Bound the normalized vertical direction over the entire horizontal view,
// including rolled/nonorthogonal camera bases, rather than testing its center.
inline int clearWeatherEndRow(int width, int height, Vec3 forward, Vec3 right,
                              Vec3 up, double observerKm, double cloudBaseKm) {
  if (width <= 0 || height <= 0 || up.y <= 0. || !std::isfinite(observerKm) ||
      !std::isfinite(cloudBaseKm) || observerKm < 0. ||
      observerKm >= cloudBaseKm - .002)
    return 0;
  constexpr double radius = 6371., distance = 100.;
  const double tanFov = std::tan(29 * pi / 180),
               horizontal = double(width) / height * tanFov;
  const double normBound = std::sqrt(
      dot(forward, forward) + horizontal * horizontal * dot(right, right) +
      tanFov * tanFov * dot(up, up) +
      2 * horizontal * std::abs(dot(forward, right)) +
      2 * tanFov * std::abs(dot(forward, up)) +
      2 * horizontal * tanFov * std::abs(dot(right, up)));
  const double vertical =
      ((cloudBaseKm - observerKm) * (2 * radius + cloudBaseKm + observerKm) -
       distance * distance) /
      (2 * (radius + observerKm) * distance);
  // A negative numerator divided by a smaller norm stays more negative.
  // Avoid needing a lower norm bound by keeping the threshold nonpositive.
  const double numerator = std::min(0., vertical - .001) * normBound;
  const double row =
      (1. + (numerator - forward.y - std::abs(right.y) * horizontal) /
                (up.y * tanFov)) *
      height / 2.;
  if (!std::isfinite(row) || !std::isfinite(normBound) || normBound <= 0. ||
      row <= 2.)
    return 0;
  if (row >= height + 2.)
    return height;
  return std::max(0, (int(std::floor(row)) - 2) / 4 * 4);
}
} // namespace observatory
