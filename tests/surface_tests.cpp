#include "surface.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
using namespace observatory;
void require(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
struct RouteFixture {
  std::filesystem::path path;
  RouteFixture() {
    char name[] = "/tmp/observatory-circuit-tests-XXXXXX";
    const char *directory = mkdtemp(name);
    require(directory, "temporary route fixture");
    path = std::filesystem::path(directory) / "bad.ggflight";
  }
  ~RouteFixture() { std::filesystem::remove_all(path.parent_path()); }
  void rejected(const std::string &bytes) {
    std::ofstream(path, std::ios::binary).write(bytes.data(), bytes.size());
    bool rejected = false;
    try {
      SurfaceWorld invalid(SURFACE_ASSET_DIR "/moon.ggworld", false,
                           path.string());
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected, "malformed cached circuit rejected before use");
  }
};
int main() {
  try {
    SurfaceWorld w(SURFACE_ASSET_DIR "/moon.ggworld");
    for (double lat : {-pi * .5 + 1.e-8, -.8, 0., .8, pi * .5 - 1.e-8})
      for (double lon : {-pi - .01, -pi + 1.e-9, -.3, 0., pi - .01, pi + .01}) {
        const auto sample = w.sample(lat, lon);
        require(
            sample.ground == w.elevation(lat, lon),
            "material and elevation sampling share exactly the same ground");
        require(
            sample.moisture >= 0 && sample.moisture <= 1 &&
                sample.uplift >= 0 && sample.uplift <= 1,
            "shared climate interpolation stays bounded at seams and poles");
      }
    RouteFixture invalid;
    invalid.rejected("GGFLY001");
    std::string bytes = "GGFLY001";
    bytes.append("\0\2\0\0", 4); // 512 points, little-endian.
    invalid.rejected(bytes);
    bytes.resize(12 + 512 * 4, '\0');
    float safe = 620.;
    uint32_t safeBits;
    std::memcpy(&safeBits, &safe, 4);
    for (int j = 0; j < 512; ++j)
      for (int k = 0; k < 4; ++k)
        bytes[12 + 4 * j + k] = char(safeBits >> (8 * k));
    std::ofstream(invalid.path, std::ios::binary)
        .write(bytes.data(), bytes.size());
    SurfaceWorld validFixture(SURFACE_ASSET_DIR "/moon.ggworld", false,
                              invalid.path.string());
    require(validFixture.flightAltitude(0) == 620,
            "valid bounded flight fixture accepted");
    uint32_t nan = 0x7fc00000;
    for (int i = 0; i < 4; ++i)
      bytes[12 + i] = char(nan >> (8 * i));
    invalid.rejected(bytes);
    float excessive = 13000;
    uint32_t bits;
    std::memcpy(&bits, &excessive, 4);
    for (int i = 0; i < 4; ++i)
      bytes[12 + i] = char(bits >> (8 * i));
    invalid.rejected(bytes);
    bool nonfiniteRejected = false;
    try {
      w.location(std::numeric_limits<double>::quiet_NaN());
    } catch (const std::invalid_argument &) {
      nonfiniteRejected = true;
    }
    require(nonfiniteRejected,
            "nonfinite route clock rejected before indexing");
    require(travelPeriod == 259200., "route takes precisely three real days");
    std::set<std::string> biomes;
    double maxLatitude = 0, minLatitude = 0, minGiant = 1;
    bool aridRelief = false, coastalRelief = false, alpineRelief = false;
    for (int i = 0; i < 8192; ++i) {
      double time = travelPeriod * i / 8192.;
      auto p = w.location(time);
      auto a = w.location(time + travelPeriod);
      require(length(geographicUp(p) - geographicUp(a)) < 1.e-12,
              "route closes without teleport");
      require(std::abs(dot(geographicUp(p), {1, 0, 0}) -
                       std::cos(59.5 * pi / 180)) < 1.e-12,
              "flight has no sideways detours");
      maxLatitude = std::max(maxLatitude, p.latitude);
      minLatitude = std::min(minLatitude, p.latitude);
      auto v = w.sample(p.latitude, p.longitude);
      require(v.landforms[0] >= 0 && v.landforms[1] >= 0 &&
                  v.landforms[0] + v.landforms[1] <= 1.000001,
              "biome relief weights conserve height and lighting blends");
      aridRelief |= v.ground > 0 && v.landforms[0] > .7;
      coastalRelief |= v.ground > 0 && v.landforms[1] > .7;
      alpineRelief |= v.ground > 0 && v.landforms[0] + v.landforms[1] < .3;
      require(std::isfinite(v.ground) && std::isfinite(v.temperature),
              "finite surface sample");
      if (v.ground < 0)
        biomes.insert(v.temperature < -5 ? "sea ice" : "ocean");
      else if (v.temperature < 1)
        biomes.insert("snow and tundra");
      else if (v.moisture < .35)
        biomes.insert("drylands");
      else if (v.moisture > .6)
        biomes.insert("forest");
      else
        biomes.insert("grassland");
      for (double skyTime : {time, 1790900000. + time}) {
        auto s = travelScene(w, skyTime, time);
        const double epsilon = 1.e-5;
        auto northward =
            sceneAt(skyTime, 1800, true,
                    {p.latitude + epsilon, p.longitude, s.altitudeMeters});
        auto eastward =
            sceneAt(skyTime, 1800, true,
                    {p.latitude, p.longitude + epsilon, s.altitudeMeters});
        require(dot(northward.up - s.up, s.north) > 0 &&
                    dot(eastward.up - s.up, s.east) > 0,
                "celestial tangent axes match geographic terrain axes");
        require(length(s.sunLocal - s.local(s.sun)) < 1.e-12,
                "surface sunlight follows corrected geographic frame");
        require(dot(s.east, normalized(cross(orbitNormal(makeOrbits()[0]),
                                             s.up))) > .999999,
                "east follows the moon's prograde spin");
        double expected = w.flightAltitude(time);
        require(expected - std::max(v.ground, v.water) >= 600,
                "airplane flight safely clears hills and forest canopy");
        require(std::abs(w.flightAltitude(time + 1) - expected) < 3.5,
                "altitude changes gradually without terrain-following bobbing");
        require(length(s.forward -
                       travelScene(w, skyTime + 900, time).forward) < 1.e-12,
                "camera bearing does not wiggle with accelerated orbital "
                "libration");
        require(std::abs(s.altitudeMeters - expected) < 1.e-9,
                "camera uses smooth precomputed flight altitude");
        double altitude =
            (length(s.observer - s.bodies[1].position) - s.bodies[1].radius) *
            71492000.;
        require(std::abs(altitude - expected) < 1.e-6,
                "celestial and terrain cameras agree");
        Vec3 giant = normalized(-s.observer);
        minGiant = std::min(minGiant, dot(giant, s.up));
        require(dot(giant, s.up) > .15, "giant stays above true horizon");
        Vec3 local = s.local(giant);
        {
          Vec3 up = geographicUp(p);
          Vec3 heading = normalized(Vec3{1, 0, 0} - up * dot({1, 0, 0}, up));
          double horizontal = dot(heading, geographicEast(p)) * local.x +
                              dot(heading, geographicNorth(p)) * local.z;
          double amplitude = std::hypot(horizontal, local.y);
          double limbCosine = std::sqrt(1. - 1. / dot(s.observer, s.observer));
          require(amplitude > limbCosine,
                  "mean bearing actually intersects the giant disk");
          double top =
              std::min(48 * pi / 180, std::atan2(local.y, horizontal) +
                                          std::acos(limbCosine / amplitude));
          double ridge = -pi / 2;
          for (int j = 0; j < 80; ++j) {
            double d = 2. * std::pow(50000., double(j) / 79.);
            Vec3 direction = geographicUp(p) * std::cos(d / surfaceRadius) +
                             heading * std::sin(d / surfaceRadius);
            double height =
                std::max(0., w.elevation(std::asin(direction.y),
                                         std::atan2(direction.z, direction.x)));
            ridge = std::max(ridge, std::atan2(height - s.altitudeMeters -
                                                   d * d / (2 * surfaceRadius),
                                               d));
          }
          if (ridge >= top)
            std::cerr << "Occlusion route=" << time << " sky=" << skyTime
                      << " lat=" << p.latitude * 180 / pi
                      << " lon=" << p.longitude * 180 / pi
                      << " ridge=" << ridge * 180 / pi
                      << " top=" << top * 180 / pi
                      << " altitude=" << s.altitudeMeters << "\n";
          require(ridge < top,
                  "terrain leaves some of Jupiter visible along route");
        }
        double forward = dot(local, s.forward);
        require(forward > 0 && std::abs(dot(local, s.right) / forward) <
                                   std::tan(29 * pi / 180),
                "giant stays in horizontal view");
      }
    }
    require(maxLatitude > 59 * pi / 180 && minLatitude < -59 * pi / 180,
            "route crosses north and south climate zones");
    require(biomes.size() >= 6,
            "route crosses ocean, ice, tundra, drylands, grassland, forests");
    // The offline route preserves one ray through the giant, not an entire
    // unobscured disk. Check its upper limb across the full libration cycle,
    // independently of the route/sky phase correlation at normal startup.
    double minimumLimb = pi / 2;
    for (int i = 0; i < 128; ++i) {
      double route = travelPeriod * i / 128.;
      auto p = w.location(route);
      Vec3 up = geographicUp(p);
      Vec3 heading = normalized(Vec3{1, 0, 0} - up * dot({1, 0, 0}, up));
      Vec3 localHeading{dot(heading, geographicEast(p)), 0,
                        dot(heading, geographicNorth(p))};
      for (int phase = 0; phase < 128; ++phase) {
        auto s = travelScene(w, 1800. * phase / 128., route);
        Vec3 giant = s.local(normalized(-s.observer));
        double horizontal = dot(giant, localHeading);
        double amplitude = std::hypot(horizontal, giant.y);
        double cosine = std::sqrt(1. - 1. / dot(s.observer, s.observer));
        require(amplitude > cosine,
                "preserved ray crosses the disk in every phase");
        double limb =
            std::atan2(giant.y, horizontal) + std::acos(cosine / amplitude);
        minimumLimb = std::min(minimumLimb, limb);
      }
    }
    require(
        minimumLimb > 35 * pi / 180,
        "preserved ray clears the 28-degree terrain bound through libration");
    std::cout << "Minimum upper limb on preserved bearing "
              << minimumLimb * 180 / pi << " deg\n";
    for (double t : {0., travelPeriod, 1790900000., 1.e10}) {
      auto a = geographicUp(w.location(t - .0001)),
           b = geographicUp(w.location(t + .0001));
      require(length(a - b) * surfaceRadius < .04,
              "epoch and route boundary are continuous");
    }
    for (double lat : {-.9, 0., .9})
      require(std::abs(w.elevation(lat, -pi + 1.e-9) -
                       w.elevation(lat, pi - 1.e-9)) < .01,
              "longitude seam is continuous");
    require(aridRelief && coastalRelief && alpineRelief,
            "flight actually crosses all three coherent landform types");
    auto center = w.location(16200);
    auto mesh = buildSurfaceMesh(w, center);
    {
      auto sameMesh = [&](const SurfaceMesh &other) {
        require(other.center.latitude == mesh.center.latitude &&
                    other.center.longitude == mesh.center.longitude &&
                    other.vertices.size() == mesh.vertices.size() &&
                    std::memcmp(other.vertices.data(), mesh.vertices.data(),
                                mesh.vertices.size() * sizeof(SurfaceVertex)) ==
                        0 &&
                    other.indices == mesh.indices &&
                    other.waterIndices == mesh.waterIndices &&
                    other.hasLand == mesh.hasLand &&
                    other.hasWater == mesh.hasWater &&
                    other.pureLand == mesh.pureLand &&
                    other.bounds.size() == mesh.bounds.size(),
                "scheduling and storage reuse preserve every vertex and index");
        for (size_t i = 0; i < mesh.bounds.size(); ++i) {
          const auto &a = mesh.bounds[i], &b = other.bounds[i];
          require(length(a.minimum - b.minimum) == 0 &&
                      length(a.maximum - b.maximum) == 0 &&
                      a.firstIndex == b.firstIndex &&
                      a.indexCount == b.indexCount &&
                      a.waterFirstIndex == b.waterFirstIndex &&
                      a.waterIndexCount == b.waterIndexCount &&
                      a.deepWater == b.deepWater,
                  "storage reuse preserves culling and water bounds");
        }
      };
      size_t checkpoints = 0;
      auto sliced = buildSurfaceMesh(w, center, [&] {
        ++checkpoints;
        std::this_thread::yield();
      });
      require(checkpoints > 0,
              "streaming mesh construction cooperates with its scheduler");
      require(checkpoints >= mesh.vertices.size() / 64 + mesh.bounds.size(),
              "vertex and block work expose frequent scheduling checkpoints");
      sameMesh(sliced);
      const std::array<const void *, 4> storage{
          sliced.vertices.data(), sliced.indices.data(),
          sliced.waterIndices.data(), sliced.bounds.data()};
      // Stale location, flags and contents must never survive a reused build.
      sliced.center = w.location(0.);
      sliced.hasLand = !mesh.hasLand;
      sliced.hasWater = !mesh.hasWater;
      sliced.pureLand = !mesh.pureLand;
      sliced.vertices.front().x = std::numeric_limits<float>::quiet_NaN();
      sliced.vertices.back().coastalWeight =
          std::numeric_limits<float>::quiet_NaN();
      sliced.indices.front() = std::numeric_limits<uint32_t>::max();
      sliced.waterIndices.front() = std::numeric_limits<uint32_t>::max();
      sliced.bounds.back().minimum.x = std::numeric_limits<double>::quiet_NaN();
      auto reused = buildSurfaceMesh(w, center, {}, std::move(sliced));
      sameMesh(reused);
      require(storage == std::array<const void *, 4>{reused.vertices.data(),
                                                     reused.indices.data(),
                                                     reused.waterIndices.data(),
                                                     reused.bounds.data()},
              "all four large CPU buffers are reused without reallocation");
      SurfaceBuildWorkspace workspace;
      auto warmed =
          buildSurfaceMesh(w, center, {}, std::move(reused), &workspace);
      sameMesh(warmed);
      const auto *scratchStorage = workspace.basePositions.data();
      require(workspace.basePositions.size() == mesh.vertices.size(),
              "workspace retains all temporary normal positions");
      workspace.basePositions.front().x =
          std::numeric_limits<double>::quiet_NaN();
      workspace.basePositions.back().z =
          std::numeric_limits<double>::quiet_NaN();
      warmed = buildSurfaceMesh(w, center, {}, std::move(warmed), &workspace);
      sameMesh(warmed);
      require(workspace.basePositions.data() == scratchStorage,
              "normal-position scratch is reused without reallocation");
    }
    require(mesh.vertices.size() <= 513000 && mesh.indices.size() <= 3100000,
            "bounded mesh complexity");
    require(mesh.bounds.size() <= 8192,
            "per-frame crop work uses bounded terrain blocks");
    uint32_t end = 0;
    for (auto box : mesh.bounds) {
      require(box.firstIndex == end && box.indexCount > 0 &&
                  box.indexCount % 3 == 0,
              "contiguous blocks cover complete triangles without gaps");
      for (uint32_t i = box.firstIndex; i < box.firstIndex + box.indexCount;
           ++i) {
        const auto &v = mesh.vertices.at(mesh.indices.at(i));
        require(v.x >= box.minimum.x && v.x <= box.maximum.x &&
                    v.y >= box.minimum.y && v.y <= box.maximum.y &&
                    v.z >= box.minimum.z && v.z <= box.maximum.z,
                "draw block contains every indexed vertex conservatively");
      }
      end += box.indexCount;
    }
    require(end == mesh.indices.size(), "no terrain triangles lost in blocks");
    auto all = surfaceDrawRanges(mesh, {});
    require(all.size() == 1 && all[0].firstIndex == 0 &&
                all[0].indexCount == mesh.indices.size(),
            "unrestricted culling coalesces the entire mesh into one draw");
    std::array<SurfacePlane, 6> nowhere{};
    nowhere[0] = {{0, 0, 0}, -1.};
    require(surfaceDrawRanges(mesh, nowhere).empty(),
            "empty frustum draws no terrain");
    int detailRings = 0;
    for (int row = 1; row < surfaceRadialSamples; ++row) {
      const auto &v = mesh.vertices[row * surfaceAngularSamples];
      const auto &next = mesh.vertices[(row + 1) * surfaceAngularSamples];
      double radius = std::hypot(v.x, v.z);
      if (radius >= 3500 && radius <= 25000) {
        require(std::hypot(next.x - v.x, next.z - v.z) / radius < .0065,
                "visible slopes retain fine radial height sampling");
        ++detailRings;
      }
    }
    require(detailRings > 300,
            "hundreds of terrain rows resolve the visible aircraft footprint");
    for (size_t i = 0; i < 1024; ++i) {
      auto v = mesh.vertices[(i * 7919) % mesh.vertices.size()];
      bool covered = false;
      for (auto box : mesh.bounds)
        if (v.x >= box.minimum.x && v.x <= box.maximum.x &&
            v.y >= box.minimum.y && v.y <= box.maximum.y &&
            v.z >= box.minimum.z && v.z <= box.maximum.z) {
          covered = true;
          break;
        }
      require(covered,
              "conservative terrain blocks contain sampled mesh vertices");
    }
    for (auto v : mesh.vertices) {
      require(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z),
              "finite spherical mesh");
      require(std::abs(length({v.nx, v.ny, v.nz}) - 1) < 1.e-6,
              "unit surface normals");
      require(v.ny > 0, "terrain normals follow upward triangle winding");
    }
    {
      auto inland = buildSurfaceMesh(w, w.location(162000.));
      require(inland.pureLand, "inland forest selects the pure-land program");
      for (const auto &vertex : inland.vertices)
        require(vertex.ground >= 1.,
                "whole land mesh cannot interpolate into the shoreline blend");
    }
    auto coast = buildSurfaceMesh(w, w.location(5600.390625));
    require(coast.hasLand && coast.hasWater && !coast.pureLand,
            "mixed coastal meshes retain the general shoreline shader");
    auto ocean = buildSurfaceMesh(w, w.location(0));
    require(!ocean.pureLand, "ocean cannot select the pure-land program");
    require(!ocean.hasLand,
            "open ocean skips unneeded terrain-shadow rendering");
    require(ocean.waterIndices.size() < ocean.indices.size() / 3,
            "deep water omits unneeded interior triangles");
    size_t liquidBlocks = 0;
    for (const auto &box : ocean.bounds) {
      if (!box.deepWater)
        continue;
      ++liquidBlocks;
      for (uint32_t i = box.waterFirstIndex;
           i < box.waterFirstIndex + box.waterIndexCount; ++i) {
        const auto &v = ocean.vertices.at(ocean.waterIndices.at(i));
        require(v.ground <= -64 && v.temperature >= -1,
                "specialized water triangles interpolate a pure liquid mask");
      }
    }
    require(liquidBlocks > 100, "deep-water shader has substantial coverage");
    for (size_t block :
         {size_t(0), size_t(47), size_t(48), size_t(79), size_t(1700),
          size_t(3120), size_t(3167), size_t(3168), size_t(3199)}) {
      const auto &box = ocean.bounds.at(block);
      using Edge = std::pair<uint32_t, uint32_t>;
      auto boundary = [](const std::vector<uint32_t> &indices, uint32_t first,
                         uint32_t count) {
        std::map<Edge, int> counts;
        for (uint32_t i = first; i < first + count; i += 3)
          for (int k = 0; k < 3; ++k) {
            auto a = indices[i + k], b = indices[i + (k + 1) % 3];
            ++counts[std::minmax(a, b)];
          }
        std::set<Edge> edges;
        for (auto [edge, count] : counts) {
          require(count <= 2, "water mesh has no non-manifold overlaps");
          if (count == 1)
            edges.insert(edge);
        }
        return edges;
      };
      require(boundary(ocean.indices, box.firstIndex, box.indexCount) ==
                  boundary(ocean.waterIndices, box.waterFirstIndex,
                           box.waterIndexCount),
              "coarse water retains every fine boundary edge without cracks");
    }
    for (auto index : ocean.waterIndices)
      require(index < ocean.vertices.size(),
              "fixed-capacity water fans only reference valid vertices");
    int curvedWaterNormals = 0;
    for (auto v : ocean.vertices) {
      double radius = std::hypot(v.x, v.z);
      if (radius < 100 || radius > 3000 || v.ground > -1000)
        continue;
      Vec3 expected{
          v.x / surfaceRadius,
          std::sqrt(1 - radius * radius / (surfaceRadius * surfaceRadius)),
          v.z / surfaceRadius};
      require(dot(expected, {v.nx, v.ny, v.nz}) > .99999,
              "ocean normals retain Earth's spherical curvature");
      ++curvedWaterNormals;
    }
    require(curvedWaterNormals > 1000,
            "substantial curved ocean normal coverage");
    // The actual streamed triangles beneath the camera must stay below it,
    // including while the cache center trails the traveller by one second.
    for (double dt : {0., .5, 1.}) {
      auto p = w.location(16200 + dt);
      Vec3 eye =
          geographicUp(p) * (surfaceRadius + w.flightAltitude(16200 + dt)) -
          geographicUp(center) * surfaceRadius;
      double x = dot(eye, geographicEast(center)),
             y = dot(eye, geographicUp(center)),
             z = dot(eye, geographicNorth(center));
      bool found = false;
      for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        auto a = mesh.vertices[mesh.indices[i]],
             b = mesh.vertices[mesh.indices[i + 1]],
             c = mesh.vertices[mesh.indices[i + 2]];
        double det = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
        if (std::abs(det) < 1.e-9)
          continue;
        double u = ((x - a.x) * (c.z - a.z) - (z - a.z) * (c.x - a.x)) / det;
        double v = ((b.x - a.x) * (z - a.z) - (b.z - a.z) * (x - a.x)) / det;
        if (u < -1.e-6 || v < -1.e-6 || u + v > 1.000001)
          continue;
        double ground = a.y + u * (b.y - a.y) + v * (c.y - a.y);
        require(y - ground > 500,
                "actual streamed mesh stays safely below airborne camera");
        found = true;
        break;
      }
      require(found, "streamed mesh actually covers camera position");
    }
    // Compare actual triangle interiors against the independent cached height
    // field. More vertices alone do not prove improved terrain fidelity.
    const double errorLimits[] = {2.5, 1.5, .08};
    int fixture = 0;
    for (double route : {16200., 162000., 5600.390625}) {
      auto c = w.location(route);
      auto detail = buildSurfaceMesh(w, c);
      double totalError = 0;
      int samples = 0;
      for (size_t i = 0; i < detail.indices.size(); i += 3 * 277) {
        if (detail.indices[i] % surfaceAngularSamples >= surfaceForwardSamples)
          continue;
        auto a = detail.vertices[detail.indices[i]],
             b = detail.vertices[detail.indices[i + 1]],
             d = detail.vertices[detail.indices[i + 2]];
        Vec3 point{(a.x + b.x + d.x) / 3., (a.y + b.y + d.y) / 3.,
                   (a.z + b.z + d.z) / 3.};
        double radius = std::hypot(point.x, point.z);
        if (radius < 3500 || radius > 25000)
          continue;
        Vec3 direction = normalized(
            geographicUp(c) * (surfaceRadius + point.y) +
            geographicEast(c) * point.x + geographicNorth(c) * point.z);
        double actual = w.elevation(std::asin(direction.y),
                                    std::atan2(direction.z, direction.x));
        totalError += std::abs(actual - (a.ground + b.ground + d.ground) / 3.);
        ++samples;
      }
      require(samples > 1500,
              "substantial independent terrain precision sample");
      std::cout << "Triangle height error route=" << route
                << " mean=" << totalError / samples << " m (" << samples
                << " samples)\n";
      require(totalError / samples < errorLimits[fixture++],
              "visible terrain interiors accurately follow measured relief");
    }
    auto forest = buildSurfaceMesh(w, w.location(162000));
    int canopyVertices = 0;
    for (auto v : forest.vertices) {
      double distance = std::hypot(v.x, v.z);
      if (distance > 3000 || distance < 500)
        continue;
      double crown = v.y + distance * distance / (2 * surfaceRadius) - v.ground;
      if (crown > 8 && crown < 22)
        ++canopyVertices;
    }
    require(canopyVertices > 1000,
            "forest is a continuous raised canopy, not spaced tree dots");
    for (double lat : {-.8, 0., .8}) {
      TravelLocation p{lat, .2};
      auto n = geographicNorth(p);
      require(n.y > 0 && std::abs(dot(n, geographicUp(p))) < 1.e-12,
              "north points toward increasing latitude");
    }
    for (auto name : biomes)
      std::cout << name << ' ';
    std::cout << "\nMinimum giant elevation " << std::asin(minGiant) * 180 / pi
              << " deg\n";
    // Report deterministic daytime preview instants for the route's biomes.
    for (double route : {0., 16200., 81000., 162000., 210600.}) {
      double best = -1, when = 0;
      for (int t = 0; t < 1800; t += 30) {
        auto s = travelScene(w, t, route);
        if (s.sunLocal.y > best) {
          best = s.sunLocal.y;
          when = t;
        }
      }
      std::cout << "Preview route=" << route << " daylight=" << when
                << " sun=" << best << '\n';
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
