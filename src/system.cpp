#include "system.hpp"
#include <algorithm>
namespace observatory {
namespace {
Vec3 ry(Vec3 v, double a) {
  return {std::cos(a) * v.x + std::sin(a) * v.z, v.y,
          -std::sin(a) * v.x + std::cos(a) * v.z};
}
Vec3 rx(Vec3 v, double a) {
  return {v.x, std::cos(a) * v.y - std::sin(a) * v.z,
          std::sin(a) * v.y + std::cos(a) * v.z};
}
} // namespace
Vec3 orbitPosition(const Orbit &o, double periods) {
  const double m = std::remainder(
      o.phase + 2 * pi * periods * std::pow(homeA / o.a, 1.5), 2 * pi);
  double e = m;
  for (int i = 0; i < 10; ++i)
    e -= (e - o.e * std::sin(e) - m) / (1 - o.e * std::cos(e));
  Vec3 p{o.a * (std::cos(e) - o.e), 0,
         -o.a * std::sqrt(1 - o.e * o.e) * std::sin(e)};
  return ry(rx(ry(p, o.periapsis), o.inclination), o.node);
}
std::array<Orbit, 20> makeOrbits() {
  std::array<Orbit, 20> a{};
  a[0] = {homeA, homeE, 0, 0, 0, 0};
  // Deliberately small inner moons; widely separated outer major moons.
  const double radii[19] = {1.30, 1.48, 1.68, 1.92, 2.18, 10.8, 18.8,
                            33,   59,   73,   78,   83,   88,   93,
                            98,   104,  110,  117,  125};
  for (int i = 1; i < 20; ++i)
    a[i] = {radii[i - 1],
            0.014 + 0.003 * (i % 5),
            (i >= 10 ? pi - 0.03 : 0.012 * std::sin(i * 2.7)),
            i * 0.73,
            i * 0.31,
            i * 2.399963};
  return a;
}
double diskVisibility(double d, double s, double r) {
  if (d >= s + r)
    return 1;
  if (r >= d + s)
    return 0;
  if (s >= d + r)
    return std::clamp(1 - r * r / (s * s), 0.0, 1.0);
  const double a =
      std::acos(std::clamp((d * d + s * s - r * r) / (2 * d * s), -1.0, 1.0));
  const double b =
      std::acos(std::clamp((d * d + r * r - s * s) / (2 * d * r), -1.0, 1.0));
  const double area =
      s * s * a + r * r * b -
      0.5 * std::sqrt(std::max(0.0, (-d + s + r) * (d + s - r) * (d - s + r) *
                                        (d + s + r)));
  return std::clamp(1 - area / (pi * s * s), 0.0, 1.0);
}
double visibility(Vec3 p, Vec3 sun, const std::array<Body, 21> &bodies,
                  int excluded) {
  double result = 1;
  for (int i = 0; i < 21; ++i)
    if (i != excluded) {
      const auto v = bodies[i].position - p;
      const double d = length(v);
      if (d <= bodies[i].radius || dot(v, sun) <= 0)
        continue;
      const double angle = std::acos(std::clamp(dot(v / d, sun), -1.0, 1.0));
      result =
          std::min(result, diskVisibility(angle, sunRadius,
                                          std::asin(bodies[i].radius / d)));
    }
  return result;
}
Scene sceneAt(double seconds, double daySeconds) {
  Scene s{};
  // Giant: 3 Jupiter masses; 1 solar mass star at 1 AU. Observer period ~24.6
  // h. Sidereal rotation is synchronous with mean anomaly. Solar and sidereal
  // days differ.
  const double orbitalSeconds =
      2 * pi * std::sqrt(std::pow(homeA * 71492000.0, 3) / (3 * 1.26686534e17));
  constexpr double yearSeconds = 365.256 * 86400;
  const double orbitPhase =
      seconds / daySeconds / (1 - orbitalSeconds / yearSeconds);
  const auto orbits = makeOrbits();
  s.bodies[0] = {{0, 0, 0}, 1, 0, orbitPhase * orbitalSeconds / 36000 * 2 * pi};
  for (int i = 0; i < 20; ++i) {
    double radius = i == 0 ? 6371.0 / 71492.0
                           : (i < 6 ? 0.001 + 0.0003 * (i % 3)
                                    : (i < 10 ? 0.065 + 0.007 * (i % 4)
                                              : 0.008 + 0.004 * (i % 5)));
    s.bodies[i + 1] = {
        orbitPosition(orbits[i], orbitPhase), radius * (i == 0 ? 1.0 : 3.0), 1 + (i % 4),
        orbitPhase * 2 * pi * std::pow(homeA / orbits[i].a, 1.5)};
  }
  const double rotation = 2 * pi * orbitPhase;
  // 38 N, 50 degrees from the subplanet meridian: giant stays low, with
  // libration.
  const double lat = 38 * pi / 180, lon = 50 * pi / 180;
  const Vec3 toward = ry({-1, 0, 0}, rotation),
             eastAxis = ry({0, 0, 1}, rotation);
  const Vec3 equator = toward * std::cos(lon) + eastAxis * std::sin(lon);
  s.up = equator * std::cos(lat) + Vec3{0, 1, 0} * std::sin(lat);
  s.east = -toward * std::sin(lon) + eastAxis * std::cos(lon);
  s.north = normalized(cross(s.east, s.up));
  s.observer =
      s.bodies[1].position + s.up * (s.bodies[1].radius + 0.2 / 71492.0);
  s.sun = normalized(ry({0.65, 0.08, 0.76},
                        2 * pi * orbitPhase * orbitalSeconds / yearSeconds));
  s.sunLocal = s.local(s.sun);
  // Fixed azimuth points at the mean giant bearing. 28-degree elevation,
  // 58-degree vertical FOV.
  Vec3 meanGiant = normalized(s.local(toward));
  const double az = std::atan2(meanGiant.x, meanGiant.z) + 0.12;
  const double elevation = 28 * pi / 180;
  s.forward = {std::sin(az) * std::cos(elevation), std::sin(elevation),
               std::cos(az) * std::cos(elevation)};
  s.right = normalized(cross(Vec3{0, 1, 0}, s.forward));
  s.cameraUp = cross(s.forward, s.right);
  s.sunVisibility = visibility(s.observer, s.sun, s.bodies, 1);
  const double phase = (1 + dot(normalized(s.observer), s.sun)) * 0.5;
  s.giantLight = 0.45 * phase / std::pow(length(s.observer), 2);
  return s;
}
} // namespace observatory
