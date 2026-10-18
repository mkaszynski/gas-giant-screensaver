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
std::array<double, 2> landformWeights(double latitude, double ground,
                                      double moisture) {
  const double coastal = 1. - smooth(180., 750., ground);
  const double temperature =
      30. - 55. * std::pow(std::sin(latitude), 2) - std::max(0., ground) * .006;
  const double dry =
      (1. - smooth(.30, .55, moisture)) * smooth(5., 17., temperature);
  return {dry * (1. - coastal), coastal};
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
SurfaceWorld::SurfaceWorld(const std::string &file, bool allowMissingFlight,
                           const std::string &flightFile) {
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
  for (int layer = 0; layer < 2; ++layer) {
    const auto name = layer == 0 ? "arid.ggrelief" : "coastal.ggrelief";
    std::ifstream input(std::filesystem::path(file).parent_path() /
                            "landforms" / name,
                        std::ios::binary);
    input.read(magic, 8);
    if (std::string(magic, 8) != "GGRELIF1")
      throw std::runtime_error("Missing/invalid biome relief: " +
                               std::string(name));
    uint32_t w = read32(input), h = read32(input), spacingBits = read32(input);
    float spacing;
    std::memcpy(&spacing, &spacingBits, 4);
    if (w != uint32_t(reliefWidth) || h != w || spacing != reliefSpacing)
      throw std::runtime_error("Biome relief grid must match alpine grid");
    input.seekg(0, std::ios::end);
    if (input.tellg() != std::streamoff(20 + count * 2))
      throw std::runtime_error("Invalid biome relief length");
    input.seekg(20);
    input.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
    if (!input)
      throw std::runtime_error("Truncated biome relief");
    extraRelief[layer].resize(count);
    for (size_t j = 0; j < count; ++j)
      extraRelief[layer][j] =
          int16_t(uint16_t(bytes[2 * j]) | (uint16_t(bytes[2 * j + 1]) << 8));
  }
  std::ifstream path(flightFile.empty()
                         ? std::filesystem::path(file).parent_path() /
                               "flight.ggflight"
                         : std::filesystem::path(flightFile),
                     std::ios::binary);
  if (!path && allowMissingFlight)
    return;
  path.read(magic, 8);
  if (std::string(magic, 8) != "GGFLY001")
    throw std::runtime_error("Missing/invalid flight profile");
  uint32_t size = read32(path);
  if (size < 512 || size > 1000000)
    throw std::runtime_error("Invalid flight point count");
  path.seekg(0, std::ios::end);
  if (path.tellg() != std::streamoff(12 + size * 4))
    throw std::runtime_error("Invalid flight file length");
  path.seekg(12);
  flight.resize(size);
  for (float &v : flight) {
    uint32_t bits = read32(path);
    std::memcpy(&v, &bits, 4);
    if (!std::isfinite(v) || v < 600 || v > 12000)
      throw std::runtime_error("Invalid flight altitude");
  }
}
SurfaceWorld::BaseSample SurfaceWorld::baseSample(double lat,
                                                  double lon) const {
  // Height, climate and landform weights refer to the same geographic texel.
  // Share its coordinates and values across elevation and material sampling.
  const auto g = grid(lat, lon, width, height);
  const double h = interpolate(heights, g, width);
  const double m = interpolate(moisture, g, width) / 255.;
  const double u = interpolate(uplift, g, width) / 255.;
  const double seam =
      1. - smooth(pi - .04, pi, std::abs(std::remainder(lon, 2 * pi)));
  return {h, m, u, seam, landformWeights(lat, h, m)};
}
double SurfaceWorld::localRelief(double x, double z, int layer) const {
  const auto &values = layer == 0 ? relief : extraRelief.at(layer - 1);
  x /= reliefSpacing;
  z /= reliefSpacing;
  int ix = int(std::floor(std::remainder(x, double(reliefWidth)))),
      iz = int(std::floor(std::remainder(z, double(reliefWidth))));
  auto at = [&](int a, int b) {
    a = (a % reliefWidth + reliefWidth) % reliefWidth;
    b = (b % reliefWidth + reliefWidth) % reliefWidth;
    return double(values[b * reliefWidth + a]);
  };
  double fx = x - std::floor(x), fz = z - std::floor(z);
  return std::lerp(std::lerp(at(ix, iz), at(ix + 1, iz), fx),
                   std::lerp(at(ix, iz + 1), at(ix + 1, iz + 1), fx), fz);
}
std::array<double, 2> SurfaceWorld::reliefCoordinates(double lat,
                                                      double lon) const {
  double x = surfaceRadius * std::remainder(lon, 2 * pi),
         z = surfaceRadius * lat;
  return {x + (noise(x, z, 37000, 338) - .5) * 900,
          z + (noise(x, z, 43000, 611) - .5) * 900};
}
double SurfaceWorld::elevation(double lat, double lon) const {
  return elevationAt(lat, lon, reliefCoordinates(lat, lon),
                     baseSample(lat, lon));
}
double SurfaceWorld::elevationAt(double lat, double lon,
                                 std::array<double, 2> coordinates,
                                 const BaseSample &base,
                                 double *reliefHeight) const {
  // Longitude/pole-safe local relief, anchored in moon-fixed coordinates.
  // The orbit never reaches the poles, but the field remains well-defined
  // there.
  const double x = surfaceRadius * std::remainder(lon, 2 * pi),
               z = surfaceRadius * lat;
  double h = base.height;
  const double seam = base.seam;
  const auto &weights = base.landforms;
  double measured = (1. - weights[0] - weights[1]) *
                    localRelief(coordinates[0], coordinates[1]);
  for (int i = 0; i < 2; ++i)
    if (weights[i] > 0.)
      measured +=
          weights[i] * localRelief(coordinates[0], coordinates[1], i + 1);
  measured *= .65 + .75 * base.uplift;
  measured *= seam * smooth(-100, 500, h);
  if (reliefHeight)
    *reliefHeight = measured;
  h += measured;
  h += seam * smooth(-250, 450, h) * ((noise(x, z, 85, 977) - .5) * 2.5);
  h += seam * (noise(x, z, 17, 477) - .5) * 1.4 * smooth(-20, 100, h);
  return h;
}
SurfaceSample SurfaceWorld::sample(double lat, double lon) const {
  auto coordinates = reliefCoordinates(lat, lon);
  const auto base = baseSample(lat, lon);
  double measured = 0;
  double h = elevationAt(lat, lon, coordinates, base, &measured);
  const double temp =
      30. - 55. * std::pow(std::sin(lat), 2) - std::max(0., h) * .006;
  return {h,
          0.,
          base.moisture,
          base.uplift,
          temp,
          base.seam * smooth(-100., 500., base.height) *
              (.65 + .75 * base.uplift),
          measured,
          coordinates,
          base.landforms};
}

TravelLocation SurfaceWorld::location(double seconds) const {
  // One constant-speed small circle. No terrain-dependent sideways detours.
  return travelLocation(seconds);
}
double SurfaceWorld::flightAltitude(double seconds) const {
  if (!std::isfinite(seconds))
    throw std::invalid_argument("Nonfinite travel clock");
  if (flight.empty())
    throw std::runtime_error("No baked flight profile");
  double f = std::remainder(seconds, travelPeriod) / travelPeriod;
  f -= std::floor(f);
  f *= flight.size();
  int i = int(std::floor(f));
  f -= i;
  auto at = [&](int j) {
    return double(flight[(j % int(flight.size()) + int(flight.size())) %
                         int(flight.size())]);
  };
  double a = at(i - 1), b = at(i), c = at(i + 1), d = at(i + 2);
  // Uniform cubic B-spline: C2 continuity and positive weights. The baked
  // envelope anticipates hills; no live max(ground, eye) introduces bobbing.
  return (a * std::pow(1 - f, 3) + b * (3 * f * f * f - 6 * f * f + 4) +
          c * (-3 * f * f * f + 3 * f * f + 3 * f + 1) + d * f * f * f) /
         6.;
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
  auto s =
      sceneAt(seconds, day, rings,
              {p.latitude, p.longitude, world.flightAltitude(routeSeconds)});
  // Legacy "north" points toward decreasing latitude. East already follows
  // the moon's prograde spin; only north needs correcting for geography.
  s.north = -s.north;
  s.sunLocal = s.local(s.sun);
  // Follow the fixed subplanet meridian, not the accelerated orbital libration.
  // The giant can move naturally within the frame without steering the camera.
  Vec3 up = geographicUp(p);
  Vec3 bearing = normalized(Vec3{1, 0, 0} - up * up.x);
  double az = std::atan2(dot(bearing, geographicEast(p)),
                         dot(bearing, geographicNorth(p))) +
              .12;
  constexpr double elevation = 19 * pi / 180;
  s.forward = {std::sin(az) * std::cos(elevation), std::sin(elevation),
               std::cos(az) * std::cos(elevation)};
  s.right = normalized(cross(Vec3{0, 1, 0}, s.forward));
  s.cameraUp = cross(s.forward, s.right);
  return s;
}
SurfaceMesh buildSurfaceMesh(const SurfaceWorld &world, TravelLocation center,
                             const std::function<void()> &checkpoint,
                             SurfaceMesh mesh) {
  // A bounded radial mesh. Fine rings near the camera, progressively larger
  // triangles past the visible horizon. One immutable CPU result per stream
  // job.
  // Spend almost all angular samples inside the visible hemisphere. A coarse
  // back sector retains full spherical ground/shadow coverage. More radial
  // samples resolve steep measured relief from the airplane-height view.
  constexpr int visible = surfaceForwardSamples,
                angular = surfaceAngularSamples, radial = surfaceRadialSamples;
  constexpr double far = 145000.;
  constexpr int inner = 32, detailEnd = 576;
  // Refill the retired CPU storage on the worker. Publishing a replacement
  // must not unmap the previous mesh's large vectors on the render thread.
  mesh.vertices.clear();
  mesh.indices.clear();
  mesh.waterIndices.clear();
  mesh.bounds.clear();
  mesh.hasLand = mesh.hasWater = false;
  mesh.pureLand = true;
  mesh.center = center;
  mesh.vertices.reserve((radial + 1) * angular);
  std::vector<Vec3> basePositions;
  basePositions.reserve((radial + 1) * angular);
  Vec3 origin = geographicUp(center), east = geographicEast(center),
       north = geographicNorth(center);
  auto reliefCenter =
      world.reliefCoordinates(center.latitude, center.longitude);
  Vec3 bearing = normalized(Vec3{1, 0, 0} - origin * origin.x);
  const double azimuth =
      std::atan2(dot(bearing, east), dot(bearing, north)) + .12;
  // Every radial ring uses the same azimuths. Evaluate these transcendental
  // functions once per column, retaining the original arithmetic and values.
  std::array<std::array<double, 2>, angular> directions;
  for (int i = 0; i < angular; ++i) {
    double a = azimuth - pi / 3 +
               (i < visible ? (2 * pi / 3) * i / visible
                            : 2 * pi / 3 + (4 * pi / 3) * (i - visible) /
                                               (angular - visible));
    directions[i] = {std::sin(a), std::cos(a)};
  }
  for (int j = 0; j <= radial; ++j) {
    // The bottom of the fixed aircraft view looks only ten degrees below
    // level. Spend most rings on the visible 1.5--45 km slopes, rather than
    // repeatedly sampling the ground beneath the aircraft. Inner rings retain
    // camera coverage and shadow casters; haze hides the sparse outer band.
    double r = j <= inner ? 1500. * j / inner
               : j <= detailEnd
                   ? 1500. * std::exp(std::log(45000. / 1500.) * (j - inner) /
                                      (detailEnd - inner))
                   : 45000. * std::exp(std::log(far / 45000.) *
                                       (j - detailEnd) / (radial - detailEnd));
    const double sphere =
        std::sqrt(1. - r * r / (surfaceRadius * surfaceRadius));
    const double curvature =
        -(r * r) /
        (surfaceRadius + std::sqrt(surfaceRadius * surfaceRadius - r * r));
    for (int i = 0; i < angular; ++i) {
      double x = r * directions[i][0], z = r * directions[i][1];
      Vec3 direction = origin * sphere + east * (x / surfaceRadius) +
                       north * (z / surfaceRadius);
      double lat = std::asin(direction.y),
             lon = std::atan2(direction.z, direction.x);
      auto v = world.sample(lat, lon);
      mesh.hasLand |= v.ground > 0;
      mesh.pureLand &= v.ground >= 1.;
      mesh.hasWater |=
          i < visible && r < 60000. && v.ground < .65 && v.temperature > -5.;
      auto coordinates = v.relief;
      double forest =
          smooth(.35, .63, v.moisture) * smooth(-5., 8., v.temperature) *
          (1. - smooth(1800, 2600, v.ground)) * smooth(2, 15, v.ground);
      // From airplane height the canopy is a continuous volume. Crown normals
      // and occlusion are mip-filtered in the material, rather than sparse
      // dots.
      double h = std::max(v.ground, v.water) +
                 forest * (12. + 6. * noise(surfaceRadius * lon,
                                            surfaceRadius * lat, 80, 1397));
      double baseHeight = std::max(v.ground - v.reliefHeight, v.water) + h -
                          std::max(v.ground, v.water);
      mesh.vertices.push_back(
          {float(x * (1. + h / surfaceRadius)),
           float(h + curvature * (1. + h / surfaceRadius)),
           float(z * (1. + h / surfaceRadius)), 0, 0, 0, float(v.ground),
           float(v.moisture), float(v.temperature),
           float(std::remainder(lon - center.longitude, 2 * pi) *
                 surfaceRadius),
           float((lat - center.latitude) * surfaceRadius),
           float(coordinates[0] - reliefCenter[0]),
           float(coordinates[1] - reliefCenter[1]), float(v.reliefStrength),
           float(v.landforms[0]), float(v.landforms[1])});
      basePositions.push_back(
          {x * (1. + baseHeight / surfaceRadius),
           baseHeight + curvature * (1. + baseHeight / surfaceRadius),
           z * (1. + baseHeight / surfaceRadius)});
    }
    if (checkpoint)
      checkpoint();
  }
  // Conservative blocks let each frame project thousands of boxes instead
  // of every terrain vertex. Back-sector blocks are narrower because those
  // angular cells are deliberately coarse.
  mesh.indices.reserve(radial * angular * 6);
  mesh.waterIndices.reserve(radial * angular * 2);
  for (int j = 0; j < radial; j += 16) {
    for (int i = 0; i < angular;) {
      int columns = i < visible ? 16 : 1;
      SurfaceMesh::Bounds b{{1.e30, 1.e30, 1.e30}, {-1.e30, -1.e30, -1.e30}};
      for (int z = j; z <= std::min(j + 16, radial); ++z)
        for (int x = i; x <= i + columns; ++x) {
          const auto &v = mesh.vertices[z * angular + x % angular];
          b.deepWater &= v.ground <= -64. && v.temperature >= -1.;
          b.minimum = {std::min(b.minimum.x, double(v.x)),
                       std::min(b.minimum.y, double(v.y)),
                       std::min(b.minimum.z, double(v.z))};
          b.maximum = {std::max(b.maximum.x, double(v.x)),
                       std::max(b.maximum.y, double(v.y)),
                       std::max(b.maximum.z, double(v.z))};
        }
      b.firstIndex = mesh.indices.size();
      for (int z = j; z < std::min(j + 16, radial); ++z)
        for (int x = i; x < i + columns; ++x) {
          uint32_t a = z * angular + x, c = a + angular,
                   next = z * angular + (x + 1) % angular, d = next + angular;
          mesh.indices.insert(mesh.indices.end(), {a, c, next, next, c, d});
        }
      b.indexCount = mesh.indices.size() - b.firstIndex;
      // The outer band is already sparse; preserve its silhouette curvature.
      b.deepWater &= j + 16 <= detailEnd;
      b.waterFirstIndex = mesh.waterIndices.size();
      if (columns == 1) {
        mesh.waterIndices.insert(
            mesh.waterIndices.end(), mesh.indices.begin() + b.firstIndex,
            mesh.indices.begin() + b.firstIndex + b.indexCount);
      } else {
        // Keep every fine boundary vertex: coarse interior fans meet adjacent
        // detailed land/water blocks without T-junctions or cracks.
        auto index = [](int row, int column) {
          return uint32_t(row * angular + column % angular);
        };
        for (int row = j; row < j + 16; row += 4)
          for (int col = i; col < i + columns; col += 4) {
            const auto centerIndex = index(row + 2, col + 2);
            // A four-cell fan has at most four vertices on each of its four
            // edges. Avoid repeated heap allocations in every terrain block.
            std::array<uint32_t, 16> edge;
            size_t edgeCount = 0;
            for (int k = 0, step = col == i ? 1 : 4; k < 4; k += step)
              edge[edgeCount++] = index(row + k, col);
            for (int k = 0, step = row + 4 == j + 16 ? 1 : 4; k < 4; k += step)
              edge[edgeCount++] = index(row + 4, col + k);
            for (int k = 0, step = col + 4 == i + columns ? 1 : 4; k < 4;
                 k += step)
              edge[edgeCount++] = index(row + 4 - k, col + 4);
            for (int k = 0, step = row == j ? 1 : 4; k < 4; k += step)
              edge[edgeCount++] = index(row, col + 4 - k);
            for (size_t k = 0; k < edgeCount; ++k)
              mesh.waterIndices.insert(
                  mesh.waterIndices.end(),
                  {centerIndex, edge[k], edge[(k + 1) % edgeCount]});
          }
      }
      b.waterIndexCount = mesh.waterIndices.size() - b.waterFirstIndex;
      mesh.bounds.push_back(b);
      i += columns;
    }
    if (checkpoint)
      checkpoint();
  }
  // Smooth continental, canopy and spherical normals. Measured relief is
  // deliberately excluded here: its full-resolution cached gradient is added
  // in the fragment shader, independently of the geometry's distance LOD.
  // Reuse the sampled heights; no extra procedural height queries are needed.
  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
    if (checkpoint && i % (4096 * 3) == 0)
      checkpoint();
    auto point = [&](uint32_t index) { return basePositions[index]; };
    auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
    Vec3 n = cross(point(b) - point(a), point(c) - point(a));
    for (auto index : {a, b, c}) {
      auto &v = mesh.vertices[index];
      v.nx += n.x;
      v.ny += n.y;
      v.nz += n.z;
    }
  }
  for (size_t i = 0; i < mesh.vertices.size(); ++i) {
    if (checkpoint && i % 4096 == 0)
      checkpoint();
    auto &v = mesh.vertices[i];
    Vec3 n{v.nx, v.ny, v.nz};
    n = length(n) > 1.e-20 ? normalized(n) : Vec3{0, 1, 0};
    v.nx = n.x;
    v.ny = n.y;
    v.nz = n.z;
  }
  return mesh;
}
std::vector<SurfaceDrawRange>
surfaceDrawRanges(const SurfaceMesh &mesh,
                  const std::array<SurfacePlane, 6> &planes, bool waterLod) {
  std::vector<SurfaceDrawRange> ranges;
  for (const auto &box : mesh.bounds) {
    Vec3 center = (box.minimum + box.maximum) * .5;
    Vec3 extent = (box.maximum - box.minimum) * .5;
    bool outside = false;
    for (const auto &plane : planes) {
      const auto &n = plane.normal;
      double radius = std::abs(n.x) * extent.x + std::abs(n.y) * extent.y +
                      std::abs(n.z) * extent.z;
      if (dot(n, center) + plane.offset + radius < -1.e-3) {
        outside = true;
        break;
      }
    }
    if (outside)
      continue;
    const bool water = waterLod && box.deepWater;
    uint32_t first = water ? box.waterFirstIndex : box.firstIndex,
             count = water ? box.waterIndexCount : box.indexCount;
    if (!ranges.empty() && ranges.back().water == water &&
        ranges.back().firstIndex + ranges.back().indexCount == first)
      ranges.back().indexCount += count;
    else
      ranges.push_back({first, count, water});
  }
  return ranges;
}
} // namespace observatory
