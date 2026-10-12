#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace observatory {
struct SkyMip {
  unsigned size;
  std::vector<unsigned char> blocks;
  bool compressed = true;
};
std::vector<SkyMip> loadSkyTexture(const std::string &path);
std::vector<std::uint16_t> decodeSkyMip(const SkyMip &);
} // namespace observatory
