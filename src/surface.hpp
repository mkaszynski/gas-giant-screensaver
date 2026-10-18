#pragma once
#include "system.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace observatory {
constexpr double surfaceRadius = 6371000.;
constexpr double travelPeriod = 3. * 86400.;
constexpr int surfaceForwardSamples = 768, surfaceAngularSamples = 800,
              surfaceRadialSamples = 640;
struct SurfaceOptions {
  bool enabled = false;
  double speed = 1, offset = 0;
  bool reflections = false;
  bool culling = true;
  bool waterLod = true;
  bool weather = true;
  bool weatherCache = true;
  bool weatherCompute = true;
};
struct TravelLocation {
  double latitude, longitude;
};
TravelLocation travelLocation(double realSeconds, double radialMetres = 0.);
struct SurfaceSample {
  double ground, water, moisture, uplift, temperature, reliefStrength,
      reliefHeight;
  std::array<double, 2> relief, landforms;
};
class SurfaceWorld {
public:
  explicit SurfaceWorld(const std::string &file,
                        bool allowMissingFlight = false,
                        const std::string &flightFile = {});
  TravelLocation location(double realSeconds) const;
  double flightAltitude(double realSeconds) const;
  double horizon(TravelLocation, Vec3 heading, double altitude,
                 int samples = 80) const;
  SurfaceSample sample(double latitude, double longitude) const;
  double elevation(double latitude, double longitude) const;
  std::array<double, 2> reliefCoordinates(double latitude,
                                          double longitude) const;
  double reliefSpan() const { return reliefWidth * reliefSpacing; }
  double reliefTexelMeters() const { return reliefSpacing; }

private:
  struct BaseSample {
    double height, moisture, uplift, seam;
    std::array<double, 2> landforms;
  };
  int width = 0, height = 0;
  std::vector<int16_t> heights, relief;
  std::array<std::vector<int16_t>, 2> extraRelief;
  std::vector<float> flight;
  int reliefWidth = 0;
  double reliefSpacing = 8;
  double localRelief(double x, double z, int layer = 0) const;
  double elevationAt(double latitude, double longitude,
                     std::array<double, 2> coordinates, const BaseSample &,
                     double *reliefHeight = nullptr) const;
  std::vector<uint8_t> moisture, uplift;
  BaseSample baseSample(double latitude, double longitude) const;
};
Vec3 geographicUp(TravelLocation);
Vec3 geographicEast(TravelLocation);
Vec3 geographicNorth(TravelLocation);
Scene travelScene(const SurfaceWorld &, double sceneSeconds,
                  double routeSeconds, double daySeconds = 1800,
                  bool rings = true);
struct SurfaceVertex {
  float x, y, z, nx, ny, nz, ground, moisture, temperature, materialX,
      materialZ, reliefX, reliefZ, reliefStrength, aridWeight, coastalWeight;
};
struct SurfaceMesh {
  struct Bounds {
    Vec3 minimum, maximum;
    uint32_t firstIndex = 0, indexCount = 0;
    uint32_t waterFirstIndex = 0, waterIndexCount = 0;
    bool deepWater = true;
  };
  TravelLocation center{};
  bool hasLand = false, hasWater = false, pureLand = false;
  std::vector<SurfaceVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<uint32_t> waterIndices;
  std::vector<Bounds> bounds;
};
// Optional cooperative checkpoints let the streaming worker share CPU/APU
// resources without changing the mesh. Offline and initial builds run directly.
SurfaceMesh buildSurfaceMesh(const SurfaceWorld &, TravelLocation,
                             const std::function<void()> &checkpoint = {});
struct SurfacePlane {
  Vec3 normal;
  double offset;
};
struct SurfaceDrawRange {
  uint32_t firstIndex, indexCount;
  bool water = false;
};
std::vector<SurfaceDrawRange>
surfaceDrawRanges(const SurfaceMesh &, const std::array<SurfacePlane, 6> &,
                  bool waterLod = false);
} // namespace observatory
