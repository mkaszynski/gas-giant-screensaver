#include "projection.hpp"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace observatory;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void contains(ScreenBounds bounds, Vec3 point, int w, int h, double tanFov) {
  require(bounds.x >= 0 && bounds.y >= 0 && bounds.width >= 0 &&
              bounds.height >= 0 && bounds.x + bounds.width <= w &&
              bounds.y + bounds.height <= h,
          "scissor must remain within viewport");
  if (point.z <= 0)
    return;
  const double x = w * .5 + point.x / point.z * h * .5 / tanFov;
  const double y = h * .5 + point.y / point.z * h * .5 / tanFov;
  if (x < 0 || x >= w || y < 0 || y >= h)
    return;
  require(x >= bounds.x && x <= bounds.x + bounds.width && y >= bounds.y &&
              y <= bounds.y + bounds.height,
          "projected surface point was clipped");
}
} // namespace
int main() {
  try {
    const double tanFov = std::tan(29 * pi / 180);
    const auto sphere = projectedSphereBounds({0, 0, 3}, 1, 1920, 1080, tanFov);
    const double tangent = 1080 * .5 / tanFov / std::sqrt(8.);
    require(sphere.x == int(std::floor(960 - tangent - 8)) &&
                sphere.y == int(std::floor(540 - tangent - 8)),
            "on-axis sphere matches angular tangent, including filter guard");
    const auto disk =
        projectedDiskBounds({0, 0, 3}, 1, {0, 0, 1}, 1920, 1080, tanFov);
    require(disk.width < sphere.width && disk.height < sphere.height,
            "face-on disk has smaller projected radius than a sphere");
    const auto edge =
        projectedDiskBounds({0, 0, 3}, 1, {0, 1, 0}, 1920, 1080, tanFov);
    require(edge.height == 1080 && edge.width == 1920,
            "edge-on ring retains unrestricted implicit-conic filtering");
    require(projectedSphereBounds({100, 0, 3}, 1, 1920, 1080, tanFov).empty(),
            "fully offscreen sphere can be skipped");
    for (Vec3 center : {Vec3{0, 0, .5}, Vec3{0, 0, 1}, Vec3{0, 0, -3}}) {
      const auto b = projectedSphereBounds(center, 1, 1920, 1080, tanFov);
      require(b.width == 1920 && b.height == 1080,
              "camera-plane ambiguity must use full viewport");
    }
    require(projectedDiskBounds({0, 0, .5}, 1, {1, 0, 0}, 1920, 1080, tanFov)
                    .width == 1920,
            "disk intersecting eye plane must use full viewport");
    require(projectedSphereBounds({0, 0, 3}, 1, 0, 1080, tanFov).empty(),
            "zero-sized viewport remains empty");
    require(projectedDiskBounds({0, 0, 3}, 1, {}, 1920, 1080, tanFov).width ==
                1920,
            "invalid normal cannot clip a ring");

    std::mt19937_64 random(0x716268923ULL);
    std::uniform_real_distribution<double> unit(-1, 1);
    for (int fixture = 0; fixture < 800; ++fixture) {
      const int w = fixture % 2 ? 1920 : 713, h = fixture % 3 ? 1080 : 397;
      const double radius = std::pow(10., unit(random) * 6);
      const double distance =
          fixture % 7 == 0 ? .75 : 1.00001 + 50 * std::abs(unit(random));
      const Vec3 center{unit(random) * radius * distance * 2,
                        unit(random) * radius * distance, radius * distance};
      const Vec3 normal =
          normalized(Vec3{unit(random), unit(random), unit(random)});
      const Vec3 u = normalized(cross(
          normal, std::abs(normal.z) < .9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0}));
      const Vec3 v = cross(normal, u);
      const auto sb = projectedSphereBounds(center, radius, w, h, tanFov);
      const auto db = projectedDiskBounds(center, radius, normal, w, h, tanFov);
      // Independent sampled geometry: test both the rim and interior points.
      // No quadratic projection formula is reused by the coverage oracle.
      for (int sample = 0; sample < 512; ++sample) {
        const double angle = 2 * pi * sample / 512.;
        const Vec3 radial = u * std::cos(angle) + v * std::sin(angle);
        contains(db, center + radial * radius, w, h, tanFov);
        contains(db, center + radial * (radius * .5), w, h, tanFov);
        const double z = unit(random), azimuth = 2 * pi * unit(random);
        const double rho = std::sqrt(1 - z * z);
        const Vec3 surface{rho * std::cos(azimuth), rho * std::sin(azimuth), z};
        contains(sb, center + surface * radius, w, h, tanFov);
      }
    }
    std::cout << "Conservative sphere and ring projection PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
