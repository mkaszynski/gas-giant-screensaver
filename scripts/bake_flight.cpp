#include "surface.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace observatory;
namespace {
constexpr int count = 32768, dense = 64, expand = 6, filter = 3;
constexpr double clearance = 620., maxGradient = .025;
void write32(std::ofstream &f, uint32_t v) {
  char b[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)};
  f.write(b, 4);
}
} // namespace
int main(int argc, char **argv) {
  std::string candidate;
  try {
    const std::string root = argc > 1 ? argv[1] : "assets/world";
    candidate = root + "/flight.ggflight.candidate";
    SurfaceWorld world(root + "/moon.ggworld", true);
    std::vector<double> bounds(count, 600.), height(count), smooth(count);
    double cell = travelPeriod / count;
    std::cout << "Sampling " << count * dense
              << " terrain points for a straight-course flight\n"
              << std::flush;
    for (int i = 0; i < count; ++i)
      for (int j = 0; j < dense; ++j) {
        auto p = travelLocation((i + double(j) / dense) * cell);
        bounds[i] = std::max(
            bounds[i],
            clearance + std::max(0., world.elevation(p.latitude, p.longitude)));
      }
    auto wrap = [](int i) { return (i % count + count) % count; };
    for (int i = 0; i < count; ++i)
      for (int j = -expand; j <= expand; ++j)
        height[i] = std::max(height[i], bounds[wrap(i + j)]);
    const double distance =
        2 * pi * surfaceRadius * std::sin(59.5 * pi / 180) / count;
    const double step = maxGradient * distance;
    // Periodic Lipschitz upper envelope: climb gradually before hills rather
    // than snapping the eye onto the current ground height. Two complete
    // wrapped sweeps propagate every constraint around the circle.
    for (int pass = 0; pass < 2; ++pass) {
      for (int i = 0; i < count; ++i)
        height[i] = std::max(height[i], height[wrap(i - 1)] - step);
      for (int i = count - 1; i >= 0; --i)
        height[i] = std::max(height[i], height[wrap(i + 1)] - step);
    }
    for (int i = 0; i < count; ++i) {
      double weight = 0;
      for (int j = -filter; j <= filter; ++j) {
        double w = 1 + std::cos(pi * j / (filter + 1.));
        smooth[i] += height[wrap(i + j)] * w;
        weight += w;
      }
      smooth[i] /= weight;
    }
    std::ofstream f(candidate, std::ios::binary);
    f.write("GGFLY001", 8);
    write32(f, count);
    for (double h : smooth) {
      float v = h;
      uint32_t bits;
      std::memcpy(&bits, &v, 4);
      write32(f, bits);
    }
    f.close();
    if (!f)
      throw std::runtime_error("Flight write failed");
    SurfaceWorld baked(root + "/moon.ggworld", false, candidate);
    double minimum = 1.e20, maxRate = 0, ridge = -pi / 2;
    for (int i = 0; i < count * dense; ++i) {
      double t = double(i) * cell / dense;
      auto p = baked.location(t);
      double eye = baked.flightAltitude(t);
      minimum = std::min(
          minimum,
          eye - std::max(0., world.elevation(p.latitude, p.longitude)));
      maxRate = std::max(maxRate, std::abs(baked.flightAltitude(t + 1) - eye));
      if (i % dense == 0) {
        Vec3 up = geographicUp(p),
             toward = normalized(Vec3{1, 0, 0} - up * up.x);
        ridge = std::max(ridge, world.horizon(p, toward, eye, 64));
      }
    }
    std::cout << "Minimum ground clearance " << minimum
              << " m; maximum climb/descent " << maxRate << " m/s; ridge "
              << ridge * 180 / pi << " deg\n";
    if (minimum < 600 || maxRate > 3.5 || ridge > 28 * pi / 180)
      throw std::runtime_error(
          "Flight fails clearance, smoothness or Jupiter visibility");
    std::filesystem::rename(candidate, root + "/flight.ggflight");
    return 0;
  } catch (const std::exception &e) {
    if (!candidate.empty()) {
      std::error_code ignored;
      std::filesystem::remove(candidate, ignored);
    }
    std::cerr << e.what() << '\n';
    return 1;
  }
}
