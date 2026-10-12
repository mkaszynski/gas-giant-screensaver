#include "sky_catalog.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace observatory;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main() {
  try {
    const auto path = std::filesystem::path(SKY_ASSET_DIR) / "stars.bin";
    const auto catalog = loadStarCatalog(path.string());
    require(catalog.size() == 8920, "complete naked-eye HYG selection");
    auto find = [&](double hours, double degrees) -> const CatalogStar & {
      const Vec3 direction =
          equatorialDirection(hours * pi / 12, degrees * pi / 180);
      auto it = std::min_element(
          catalog.begin(), catalog.end(), [&](auto &a, auto &b) {
            auto error = [&](auto &s) {
              return length(Vec3{s.values[0], s.values[1], s.values[2]} -
                            direction);
            };
            return error(a) < error(b);
          });
      require(length(Vec3{it->values[0], it->values[1], it->values[2]} -
                     direction) < 1.e-6,
              "J2000 direction agrees with named reference star");
      return *it;
    };
    const auto &sirius = find(6.752481, -16.716116).values;
    require(std::abs(sirius[7] + 1.44) < 1.e-6, "Sirius V magnitude");
    const auto &betelgeuse = find(5.919529, 7.407063).values;
    const auto &rigel = find(5.242298, -8.20164).values;
    require(betelgeuse[4] > betelgeuse[6] * 3 && rigel[6] > rigel[4],
            "measured color indices produce warm Betelgeuse and blue Rigel");
    for (auto &star : catalog) {
      const auto &v = star.values;
      require(std::abs(v[3] / std::pow(10., -.4 * v[7]) - 1) < 1.e-6,
              "Pogson magnitude-to-flux law");
      require(std::abs(.2126 * v[4] + .7152 * v[5] + .0722 * v[6] - 1) < 1.e-6,
              "colors preserve catalog visual brightness");
    }
    require(length(equatorialDirection(0, 0) - Vec3{1, 0, 0}) < 1.e-12,
            "right ascension origin");
    require(equatorialDirection(pi / 2, 0).y < 0 &&
                equatorialDirection(pi / 2, 0).z > .9,
            "equatorial-to-ecliptic obliquity sign");
    std::ifstream file(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(file)), {});
    char name[] = "/tmp/observatory-catalog-XXXXXX";
    const auto temporary = std::filesystem::path(mkdtemp(name));
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() { std::filesystem::remove_all(path); }
    } cleanup{temporary};
    auto invalid = [&](std::string data) {
      const auto p = temporary / "invalid.bin";
      std::ofstream(p, std::ios::binary).write(data.data(), data.size());
      bool rejected = false;
      try {
        loadStarCatalog(p.string());
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      require(rejected, "malformed or unsafe catalog rejected");
    };
    invalid(bytes.substr(0, 10));
    invalid(bytes.substr(0, bytes.size() - 1));
    invalid(bytes + "x");
    auto changed = bytes;
    changed[0] = 'X';
    invalid(changed);
    changed = bytes;
    changed[8] = changed[9] = changed[10] = changed[11] = 0;
    invalid(changed);
    changed = bytes;
    changed[8] = changed[9] = changed[10] = changed[11] = char(255);
    invalid(changed);
    changed = bytes;
    changed[12] = changed[13] = 0;
    changed[14] = char(192);
    changed[15] = char(127);
    invalid(changed); // NaN
    changed = bytes;
    changed[12] = changed[13] = changed[14] = changed[15] = 0;
    invalid(changed); // nonunit direction
    std::cout << "Sky catalog behavior passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
