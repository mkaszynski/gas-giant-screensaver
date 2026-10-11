#include "sky_catalog.hpp"
#include <bit>
#include <cstdint>
#include <fstream>
#include <stdexcept>
namespace observatory {
Vec3 equatorialDirection(double ra, double dec) {
  constexpr double tilt = 23.4392911 * pi / 180;
  const double y = std::sin(dec), z = std::cos(dec) * std::sin(ra);
  return {std::cos(dec) * std::cos(ra), y * std::cos(tilt) - z * std::sin(tilt),
          y * std::sin(tilt) + z * std::cos(tilt)};
}
std::vector<CatalogStar> loadStarCatalog(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  auto fail = [&]() {
    throw std::runtime_error("Invalid star catalog: " + path);
  };
  char magic[8];
  if (!file.read(magic, 8) || std::string(magic, 8) != "GGSSTAR1")
    fail();
  auto word = [&]() {
    unsigned char b[4];
    if (!file.read(reinterpret_cast<char *>(b), 4))
      fail();
    return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) |
           (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
  };
  const auto count = word();
  if (count == 0 || count > 20000)
    fail();
  std::vector<CatalogStar> catalog(count);
  for (auto &star : catalog) {
    for (auto &value : star.values) {
      value = std::bit_cast<float>(word());
      if (!std::isfinite(value))
        fail();
    }
    const auto &v = star.values;
    if (std::abs(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] - 1) > 2.e-5 ||
        v[3] <= 0 || v[3] > 10 || v[7] < -2 || v[7] > 6.5)
      fail();
    for (int i = 4; i < 7; ++i)
      if (v[i] < 0 || v[i] > 8)
        fail();
  }
  if (file.peek() != std::char_traits<char>::eof())
    fail();
  return catalog;
}
} // namespace observatory
