#include "surface.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace observatory {
namespace {
double smooth(double a, double b, double x) {
  x = std::clamp((x - a) / (b - a), 0., 1.);
  return x * x * (3 - 2 * x);
}
uint32_t hash(int64_t x, int64_t y, uint32_t seed) {
  uint32_t n = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ seed;
  n = (n ^ (n >> 13)) * 1274126177u;
  return n ^ (n >> 16);
}
double noise(double x, double z, double scale, uint32_t seed) {
  x /= scale;
  z /= scale;
  auto ix = int64_t(std::floor(x)), iz = int64_t(std::floor(z));
  double fx = smooth(0, 1, x - ix), fz = smooth(0, 1, z - iz);
  auto h = [&](int64_t a, int64_t b) {
    return double(hash(a, b, seed)) / 4294967295.;
  };
  return std::lerp(std::lerp(h(ix, iz), h(ix + 1, iz), fx),
                   std::lerp(h(ix, iz + 1), h(ix + 1, iz + 1), fx), fz);
}
struct Grid {
  int x, y;
  double fx, fy;
};
Grid grid(double lat, double lon, int w, int h) {
  double x = (std::remainder(lon, 2 * pi) + pi) / (2 * pi) * w - .5;
  double y = (std::clamp(lat, -pi * .5, pi * .5) + pi * .5) / pi * h - .5;
  int iy = std::clamp(int(std::floor(y)), 0, h - 2);
  return {int(std::floor(x)), iy, x - std::floor(x),
          std::clamp(y - iy, 0., 1.)};
}
template <class T> double interpolate(const std::vector<T> &a, Grid g, int w) {
  auto at = [&](int x, int y) {
    x = (x % w + w) % w;
    return double(a[y * w + x]);
  };
  return std::lerp(std::lerp(at(g.x, g.y), at(g.x + 1, g.y), g.fx),
                   std::lerp(at(g.x, g.y + 1), at(g.x + 1, g.y + 1), g.fx),
                   g.fy);
}
uint32_t read32(std::ifstream &f) {
  unsigned char b[4]{};
  f.read(reinterpret_cast<char *>(b), 4);
  if (!f)
    throw std::runtime_error("Truncated surface header");
  return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) |
         (uint32_t(b[3]) << 24);
}
} // namespace
Vec3 geographicUp(TravelLocation p) {
  return {std::cos(p.latitude) * std::cos(p.longitude), std::sin(p.latitude),
          std::cos(p.latitude) * std::sin(p.longitude)};
}
Vec3 geographicEast(TravelLocation p) {
  return {-std::sin(p.longitude), 0, std::cos(p.longitude)};
}
Vec3 geographicNorth(TravelLocation p) {
  return cross(geographicEast(p), geographicUp(p));
}
TravelLocation travelLocation(double seconds, double radialMetres) {
  if (!std::isfinite(seconds))
    throw std::invalid_argument("Nonfinite travel clock");
  const double angle =
      std::remainder(seconds, travelPeriod) / travelPeriod * 2 * pi + pi / 4;
  const double rho = 59.5 * pi / 180 + radialMetres / surfaceRadius;
  return {std::asin(std::sin(rho) * std::cos(angle)),
          std::atan2(std::sin(rho) * std::sin(angle), std::cos(rho))};
}
SurfaceWorld::SurfaceWorld(const std::string &file, bool allowMissingRoute,
                           const std::string &circuitFile) {
  std::ifstream f(file, std::ios::binary);
  char magic[8]{};
  f.read(magic, 8);
  if (std::string(magic, 8) != "GGWORLD1")
    throw std::runtime_error("Invalid world elevation: " + file);
  width = read32(f);
  height = read32(f);
  if (width < 64 || height < 32 || width > 8192 || height > 4096 ||
      width != height * 2)
    throw std::runtime_error("Invalid world dimensions");
  size_t n = size_t(width) * height;
  f.seekg(0, std::ios::end);
  auto bytesAvailable = f.tellg();
  if (bytesAvailable != std::streamoff(16 + n * 4))
    throw std::runtime_error("Invalid world file length");
  f.seekg(16);
  std::vector<unsigned char> bytes(n * 2);
  f.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
  heights.resize(n);
  for (size_t i = 0; i < n; ++i)
    heights[i] =
        int16_t(uint16_t(bytes[2 * i]) | (uint16_t(bytes[2 * i + 1]) << 8));
  moisture.resize(n);
  uplift.resize(n);
  f.read(reinterpret_cast<char *>(moisture.data()), n);
  f.read(reinterpret_cast<char *>(uplift.data()), n);
  if (!f || f.peek() != std::char_traits<char>::eof())
    throw std::runtime_error("Truncated or trailing world data");
  std::ifstream rf(std::filesystem::path(file).parent_path() /
                       "relief.ggrelief",
                   std::ios::binary);
  rf.read(magic, 8);
  if (std::string(magic, 8) != "GGRELIF1")
    throw std::runtime_error("Invalid measured relief");
  reliefWidth = read32(rf);
  uint32_t reliefHeight = read32(rf);
  uint32_t bits = read32(rf);
  float spacing;
  std::memcpy(&spacing, &bits, 4);
  reliefSpacing = spacing;
  if (reliefWidth != int(reliefHeight) || reliefWidth < 64 ||
      reliefWidth > 4096 || !std::isfinite(reliefSpacing) || reliefSpacing <= 0)
    throw std::runtime_error("Invalid relief dimensions");
  size_t count = size_t(reliefWidth) * reliefWidth;
  rf.seekg(0, std::ios::end);
  if (rf.tellg() != std::streamoff(20 + count * 2))
    throw std::runtime_error("Invalid relief file length");
  rf.seekg(20);
  relief.resize(count);
  bytes.resize(relief.size() * 2);
  rf.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
  if (!rf || rf.peek() != std::char_traits<char>::eof())
    throw std::runtime_error("Truncated measured relief");
  for (size_t i = 0; i < relief.size(); ++i)
    relief[i] =
        int16_t(uint16_t(bytes[2 * i]) | (uint16_t(bytes[2 * i + 1]) << 8));
  std::ifstream path(circuitFile.empty()
                         ? std::filesystem::path(file).parent_path() /
                               "circuit.ggroute"
                         : std::filesystem::path(circuitFile),
                     std::ios::binary);
  if (!path && allowMissingRoute)
    return;
  path.read(magic, 8);
  if (std::string(magic, 8) != "GGROUTE1")
    throw std::runtime_error("Missing/invalid surface circuit");
  uint32_t size = read32(path);
  if (size < 512 || size > 1000000)
    throw std::runtime_error("Invalid circuit point count");
  path.seekg(0, std::ios::end);
  if (path.tellg() != std::streamoff(12 + size * 4))
    throw std::runtime_error("Invalid circuit file length");
  path.seekg(12);
  route.resize(size);
  for (float &v : route) {
    uint32_t bits = read32(path);
    std::memcpy(&v, &bits, 4);
    if (!std::isfinite(v) || std::abs(v) > 6000)
      throw std::runtime_error("Invalid circuit detour");
  }
}
double SurfaceWorld::field(const std::vector<uint8_t> &a, double lat,
                           double lon) const {
  return interpolate(a, grid(lat, lon, width, height), width) / 255.;
}
double SurfaceWorld::coarse(double lat, double lon) const {
  return interpolate(heights, grid(lat, lon, width, height), width);
}
double SurfaceWorld::localRelief(double x, double z) const {
  x /= reliefSpacing;
  z /= reliefSpacing;
  int ix = int(std::floor(std::remainder(x, double(reliefWidth)))),
      iz = int(std::floor(std::remainder(z, double(reliefWidth))));
  auto at = [&](int a, int b) {
    a = (a % reliefWidth + reliefWidth) % reliefWidth;
    b = (b % reliefWidth + reliefWidth) % reliefWidth;
    return double(relief[b * reliefWidth + a]);
  };
  double fx = x - std::floor(x), fz = z - std::floor(z);
  return std::lerp(std::lerp(at(ix, iz), at(ix + 1, iz), fx),
                   std::lerp(at(ix, iz + 1), at(ix + 1, iz + 1), fx), fz);
}
double SurfaceWorld::elevation(double lat, double lon) const {
  // Longitude/pole-safe local relief, anchored in moon-fixed coordinates.
  // The orbit never reaches the poles, but the field remains well-defined
  // there.
  const double x = surfaceRadius * std::remainder(lon, 2 * pi),
               z = surfaceRadius * lat;
  double h = coarse(lat, lon), u = field(uplift, lat, lon);
  double seam =
      1. - smooth(pi - .04, pi, std::abs(std::remainder(lon, 2 * pi)));
  double measured = (localRelief(x + (noise(x, z, 19000, 338) - .5) * 2200,
                                 z + (noise(x, z, 27000, 611) - .5) * 1700) +
                     .6 * localRelief(z + 3700, -x + 1900)) *
                    (.45 + 1.15 * u);
  h += seam * smooth(-100, 500, h) * measured;
  double ridge = 1. - std::abs(2 * noise(x, z, 2600, 1931) - 1);
  h += seam * smooth(-250, 450, h) *
       ((ridge * ridge - .35) * (160 + 300 * u) +
        (noise(x, z, 430, 827) - .5) * (35 + 80 * u) +
        (noise(x, z, 85, 977) - .5) * (5 + 16 * u));
  h += seam * (noise(x, z, 17, 477) - .5) * 1.4 * smooth(-20, 100, h);
  return h;
}
SurfaceSample SurfaceWorld::sample(double lat, double lon) const {
  double h = elevation(lat, lon), m = field(moisture, lat, lon),
         u = field(uplift, lat, lon);
  const double temp =
      30. - 55. * std::pow(std::sin(lat), 2) - std::max(0., h) * .006;
  return {h, 0., m, u, temp};
}

TravelLocation SurfaceWorld::location(double seconds) const {
  if (!std::isfinite(seconds))
    throw std::invalid_argument("Nonfinite travel clock");
  if (route.empty())
    return travelLocation(seconds);
  double f = std::remainder(seconds, travelPeriod) / travelPeriod;
  f -= std::floor(f);
  f *= route.size();
  int i = int(std::floor(f));
  f -= i;
  auto at = [&](int j) {
    return double(
        route[(j % int(route.size()) + int(route.size())) % int(route.size())]);
  };
  double a = at(i - 1), b = at(i), c = at(i + 1), d = at(i + 2);
  double offset =
      .5 * (2 * b + (-a + c) * f + (2 * a - 5 * b + 4 * c - d) * f * f +
            (-a + 3 * b - 3 * c + d) * f * f * f);
  return travelLocation(seconds, offset);
}
double SurfaceWorld::horizon(TravelLocation p, Vec3 heading, double altitude,
                             int samples) const {
  double highest = -pi / 2;
  for (int j = 0; j < samples; ++j) {
    double d = 2. * std::pow(50000., double(j) / (samples - 1));
    Vec3 direction = geographicUp(p) * std::cos(d / surfaceRadius) +
                     heading * std::sin(d / surfaceRadius);
    double height =
        std::max(0., elevation(std::asin(direction.y),
                               std::atan2(direction.z, direction.x)));
    highest = std::max(
        highest,
        std::atan2(height - altitude - d * d / (2 * surfaceRadius), d));
  }
  return highest;
}
Scene travelScene(const SurfaceWorld &world, double seconds,
                  double routeSeconds, double day, bool rings) {
  auto p = world.location(routeSeconds);
  auto ground = world.sample(p.latitude, p.longitude);
  auto s = sceneAt(
      seconds, day, rings,
      {p.latitude, -p.longitude, std::max(ground.ground, ground.water) + 50.});
  // The legacy fixed observer measures longitude toward +Z from the -X
  // subplanet meridian: its "north" basis points toward decreasing latitude.
  // The geographic world uses eastward longitude and increasing-latitude north.
  // Reverse longitude and both tangent axes to keep an orthonormal frame and
  // make terrain, astronomical rays and illumination share those conventions.
  s.longitude = p.longitude;
  s.east = -s.east;
  s.north = -s.north;
  s.sunLocal = s.local(s.sun);
  Vec3 bearing = normalized(s.local(-s.observer));
  double az = std::atan2(bearing.x, bearing.z) + .12;
  constexpr double elevation = 19 * pi / 180;
  s.forward = {std::sin(az) * std::cos(elevation), std::sin(elevation),
               std::cos(az) * std::cos(elevation)};
  s.right = normalized(cross(Vec3{0, 1, 0}, s.forward));
  s.cameraUp = cross(s.forward, s.right);
  return s;
}
SurfaceMesh buildSurfaceMesh(const SurfaceWorld &world, TravelLocation center) {
  // A bounded radial mesh. Fine rings near the camera, progressively larger
  // triangles past the visible horizon. One immutable CPU result per stream
  // job.
  constexpr int angular = 384, radial = 256;
  constexpr double far = 145000.;
  SurfaceMesh mesh;
  mesh.center = center;
  mesh.vertices.reserve((radial + 1) * angular);
  Vec3 origin = geographicUp(center), east = geographicEast(center),
       north = geographicNorth(center);
  for (int j = 0; j <= radial; ++j) {
    double r = j == 0 ? 0. : 2. * std::expm1(std::log1p(far / 2.) * j / radial);
    for (int i = 0; i < angular; ++i) {
      double a = 2 * pi * i / angular, x = r * std::sin(a), z = r * std::cos(a);
      Vec3 direction =
          origin * std::sqrt(1. - r * r / (surfaceRadius * surfaceRadius)) +
          east * (x / surfaceRadius) + north * (z / surfaceRadius);
      double lat = std::asin(direction.y),
             lon = std::atan2(direction.z, direction.x);
      auto v = world.sample(lat, lon);
      double h = std::max(v.ground, v.water);
      double curvature =
          -(r * r) /
          (surfaceRadius + std::sqrt(surfaceRadius * surfaceRadius - r * r));
      mesh.vertices.push_back(
          {float(x * (1. + h / surfaceRadius)),
           float(h + curvature * (1. + h / surfaceRadius)),
           float(z * (1. + h / surfaceRadius)), 0, 0, 0, float(v.ground),
           float(v.moisture), float(v.temperature),
           float(std::remainder(lon - center.longitude, 2 * pi) *
                 surfaceRadius),
           float((lat - center.latitude) * surfaceRadius)});
    }
  }
  // Sparse, stable moon-fixed tree cells, never screen-space random placement.
  const double cosine = std::cos(center.latitude);
  const double cell = 35.;
  int64_t cx = int64_t(std::floor(center.longitude * surfaceRadius / cell)),
          cz = int64_t(std::floor(center.latitude * surfaceRadius / cell));
  for (int iz = -58; iz <= 58; ++iz)
    for (int ix = -int(58 / cosine); ix <= int(58 / cosine); ++ix) {
      int64_t gx = cx + ix, gz = cz + iz;
      uint32_t seed = hash(gx, gz, 719);
      double lon = (gx + .15 + .7 * double(seed & 65535) / 65535.) * cell /
                   surfaceRadius;
      double lat =
          (gz + .15 + .7 * double(seed >> 16) / 65535.) * cell / surfaceRadius;
      double dx = std::remainder(lon - center.longitude, 2 * pi) *
                  surfaceRadius * cosine,
             dz = (lat - center.latitude) * surfaceRadius;
      if (dx * dx + dz * dz > 2000. * 2000.)
        continue;
      auto v = world.sample(lat, lon);
      double density = smooth(.35, .63, v.moisture) *
                       smooth(-5., 8., v.temperature) *
                       (1. - smooth(1800, 2600, v.ground));
      if (v.ground < 2 ||
          double(hash(gx, gz, 833)) / 4294967295. > density * .75)
        continue;
      double slope =
          std::hypot(
              world.elevation(lat + 4 / surfaceRadius, lon) - v.ground,
              world.elevation(lat, lon + 4 / (surfaceRadius * std::cos(lat))) -
                  v.ground) /
          4.;
      if (slope > .65)
        continue;
      Vec3 p = geographicUp({lat, lon}) * (surfaceRadius + v.ground) -
               origin * surfaceRadius;
      float height = 12. + 12. * double(hash(gx, gz, 372)) / 4294967295.;
      mesh.trees.push_back(
          {float(dot(p, east)), float(dot(p, origin)), float(dot(p, north)),
           height, float(v.temperature > 18), float((seed & 255) / 255.)});
    }
  std::sort(mesh.trees.begin(), mesh.trees.end(), [](auto a, auto b) {
    return a.x * a.x + a.z * a.z > b.x * b.x + b.z * b.z;
  });
  mesh.indices.reserve(radial * angular * 6);
  for (int j = 0; j < radial; ++j)
    for (int i = 0; i < angular; ++i) {
      uint32_t a = j * angular + i, b = j * angular + (i + 1) % angular,
               c = a + angular, d = b + angular;
      mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
    }
  // Area-weighted normals from the actual spherical triangles. Four extra
  // procedural height queries per vertex dominated streaming CPU work and
  // competed with rendering on integrated graphics; the existing triangles
  // already contain the rendered slopes and curvature.
  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
    auto point = [&](uint32_t index) {
      auto &v = mesh.vertices[index];
      return Vec3{v.x, v.y, v.z};
    };
    auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
    Vec3 n = cross(point(b) - point(a), point(c) - point(a));
    for (auto index : {a, b, c}) {
      auto &v = mesh.vertices[index];
      v.nx += n.x;
      v.ny += n.y;
      v.nz += n.z;
    }
  }
  for (auto &v : mesh.vertices) {
    Vec3 n{v.nx, v.ny, v.nz};
    n = length(n) > 1.e-20 ? normalized(n) : Vec3{0, 1, 0};
    v.nx = n.x;
    v.ny = n.y;
    v.nz = n.z;
  }
  return mesh;
}
} // namespace observatory
