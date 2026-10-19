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
} // namespace observatory
