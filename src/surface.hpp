#pragma once
#include "system.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace observatory {
constexpr double surfaceRadius = 6371000.;
constexpr double travelPeriod = 3. * 86400.;
struct SurfaceOptions {
  bool enabled = false;
  double speed = 1, offset = 0;
};
struct TravelLocation {
  double latitude, longitude;
};
TravelLocation travelLocation(double realSeconds, double radialMetres = 0.);
struct SurfaceSample {
  double ground, water, moisture, uplift, temperature;
};
class SurfaceWorld {
public:
  explicit SurfaceWorld(const std::string &file, bool allowMissingRoute = false,
                        const std::string &circuitFile = {});
  TravelLocation location(double realSeconds) const;
  double horizon(TravelLocation, Vec3 heading, double altitude,
                 int samples = 80) const;
  SurfaceSample sample(double latitude, double longitude) const;
  double elevation(double latitude, double longitude) const;

private:
  int width = 0, height = 0;
  std::vector<int16_t> heights, relief;
  std::vector<float> route;
  int reliefWidth = 0;
  double reliefSpacing = 8;
  double localRelief(double x, double z) const;
  std::vector<uint8_t> moisture, uplift;
  double field(const std::vector<uint8_t> &, double, double) const;
  double coarse(double, double) const;
};
Vec3 geographicUp(TravelLocation);
Vec3 geographicEast(TravelLocation);
Vec3 geographicNorth(TravelLocation);
Scene travelScene(const SurfaceWorld &, double sceneSeconds,
                  double routeSeconds, double daySeconds = 1800,
                  bool rings = true);
struct SurfaceVertex {
  float x, y, z, nx, ny, nz, ground, moisture, temperature, materialX,
      materialZ;
};
struct SurfaceTree {
  float x, y, z, height, type, variation;
};
struct SurfaceMesh {
  TravelLocation center{};
  std::vector<SurfaceVertex> vertices;
  std::vector<SurfaceTree> trees;
  std::vector<uint32_t> indices;
};
SurfaceMesh buildSurfaceMesh(const SurfaceWorld &, TravelLocation);
} // namespace observatory
