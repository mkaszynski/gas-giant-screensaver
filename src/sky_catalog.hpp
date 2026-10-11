#pragma once
#include "system.hpp"
#include <array>
#include <string>
#include <vector>
namespace observatory {
// Eight cached floats per star: ecliptic direction, magnitude-zero-relative
// flux, linear RGB with unit photopic luminance, and apparent V magnitude.
struct CatalogStar {
  std::array<float, 8> values;
};
std::vector<CatalogStar> loadStarCatalog(const std::string &path);
Vec3 equatorialDirection(double rightAscension, double declination);
} // namespace observatory
