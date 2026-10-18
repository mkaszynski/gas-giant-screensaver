#pragma once
#include "system.hpp"
#include <algorithm>
#include <limits>

namespace observatory {
struct ScreenBounds {
  int x, y, width, height;
  bool empty() const { return width == 0 || height == 0; }
};

namespace projection_detail {
// A sphere has covariance r^2 I. A circular disk has covariance
// r^2 (I - nn^T), where n is its unit normal. The extrema of x/z and y/z
// follow from the tangent planes through the camera, rather than from the
// projected center and an approximate angular radius.
inline ScreenBounds bounds(Vec3 center, double radius, Vec3 normal, int width,
                           int height, double tanHalfFov) {
  const ScreenBounds full{0, 0, std::max(0, width), std::max(0, height)};
  if (full.empty() || !std::isfinite(radius) || radius < 0 ||
      !std::isfinite(center.x) || !std::isfinite(center.y) ||
      !std::isfinite(center.z) || !std::isfinite(tanHalfFov) || tanHalfFov <= 0)
    return full;
  // Extended precision protects small, distant moons and nearly edge-on rings
  // against cancellation in the quadratic discriminant.
  const long double r2 = (long double)radius * radius;
  const long double z = center.z, nz = normal.z;
  const long double spread = r2 * (1 - nz * nz);
  const long double a = z * z - spread;
  // Objects touching the camera plane need unbounded perspective extents.
  // A full viewport is also the conservative fallback for uncertain inputs.
  if (z <= 0 ||
      a <= 32 * std::numeric_limits<double>::epsilon() * (z * z + spread))
    return full;
  const long double scale = height * .5L / tanHalfFov;
  constexpr long double guard = 8;
  if (dot(normal, normal) > 0) {
    const auto discriminant = [&](long double component, long double axis) {
      const long double b = component * z + r2 * axis * nz;
      const long double k = component * component - r2 * (1 - axis * axis);
      return std::max(0.L, b * b - a * k);
    };
    const long double traceNumerator =
        discriminant(center.x, normal.x) + discriminant(center.y, normal.y);
    const long double planeDistance = (long double)center.x * normal.x +
                                      (long double)center.y * normal.y + z * nz;
    // det(Q)/trace(Q) bounds the smaller squared semiaxis from below.
    // Extremely flattened ellipses need unrestricted drawing: the shader's
    // implicit-conic fwidth filter can extend far beyond their geometric rim.
    const long double minorLower = traceNumerator > 0
                                       ? scale * r2 * std::abs(planeDistance) /
                                             std::sqrt(a * traceNumerator)
                                       : 0;
    if (minorLower <= guard)
      return full;
  }
  const auto extent = [&](long double component, long double axis,
                          int dimension) {
    const long double b = component * z + r2 * axis * nz;
    const long double k = component * component - r2 * (1 - axis * axis);
    const long double root = std::sqrt(std::max(0.L, b * b - a * k));
    // Keep helper invocations, MSAA coverage, and analytic limb/ring filtering
    // outside the geometric silhouette. This changes only the scissor box.
    return std::array<long double, 2>{
        dimension * .5L + (b - root) / a * scale - guard,
        dimension * .5L + (b + root) / a * scale + guard};
  };
  const auto horizontal = extent(center.x, normal.x, width);
  const auto vertical = extent(center.y, normal.y, height);
  if (!std::isfinite(horizontal[0]) || !std::isfinite(horizontal[1]) ||
      !std::isfinite(vertical[0]) || !std::isfinite(vertical[1]))
    return full;
  const auto lower = [](long double value, int limit) {
    return int(std::floor(std::clamp(value, 0.L, (long double)limit)));
  };
  const auto upper = [](long double value, int limit) {
    return int(std::ceil(std::clamp(value, 0.L, (long double)limit)));
  };
  const int x = lower(horizontal[0], width), y = lower(vertical[0], height);
  return {x, y, upper(horizontal[1], width) - x,
          upper(vertical[1], height) - y};
}
} // namespace projection_detail

// Center and normal are in the camera's right/up/forward coordinate frame.
inline ScreenBounds projectedSphereBounds(Vec3 center, double radius, int width,
                                          int height, double tanHalfFov) {
  return projection_detail::bounds(center, radius, {}, width, height,
                                   tanHalfFov);
}
inline ScreenBounds projectedDiskBounds(Vec3 center, double radius, Vec3 normal,
                                        int width, int height,
                                        double tanHalfFov) {
  const double magnitude = length(normal);
  if (!std::isfinite(magnitude) || magnitude <= 0)
    return {0, 0, std::max(0, width), std::max(0, height)};
  return projection_detail::bounds(center, radius, normal / magnitude, width,
                                   height, tanHalfFov);
}
} // namespace observatory
