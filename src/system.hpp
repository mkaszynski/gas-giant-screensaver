#pragma once
#include <array>
#include <cmath>
#include <vector>
namespace observatory {
constexpr double pi = 3.14159265358979323846;
struct Vec3 {
  double x = 0, y = 0, z = 0;
  Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
  Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
  Vec3 operator-() const { return {-x, -y, -z}; }
  Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
  Vec3 operator/(double s) const { return *this * (1 / s); }
};
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(Vec3 v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalized(Vec3 v) { return v / length(v); }
struct Orbit {
  double a, e, inclination, node, periapsis, phase;
};
struct Body {
  Vec3 position;
  double radius;
  int material;
  double spin;
};
struct Scene {
  std::array<Body, 21> bodies;
  Vec3 observer, east, up, north, sun, sunLocal;
  Vec3 right, cameraUp, forward;
  double giantLight = 0, sunVisibility = 1;
  Vec3 local(Vec3 v) const { return {dot(v, east), dot(v, up), dot(v, north)}; }
};
// Length unit: Jupiter equatorial radii (71492 km). Time: observer orbital
// periods.
constexpr double homeA = 3.0, homeE = 1.0 / 9.0, sunRadius = 0.00465047;
constexpr double ringInner = 1.235, ringOuter = 1.780;
Vec3 ringNormal();
double ringOpticalDepth(double radius);
double ringSunVisibility(Vec3 point, Vec3 toSun);
Vec3 orbitPosition(const Orbit &, double periods);
std::array<Orbit, 20> makeOrbits();
Scene sceneAt(double displayedSeconds, double daySeconds = 1800,
              bool ringsEnabled = true);
double diskVisibility(double separation, double sourceRadius,
                      double occluderRadius);
double visibility(Vec3 point, Vec3 toSun, const std::array<Body, 21> &,
                  int excluded);
} // namespace observatory
