#pragma once
#include <string>
#include <vector>
namespace observatory {
struct SkyMip {
  unsigned size;
  std::vector<unsigned char> blocks;
};
std::vector<SkyMip> loadSkyTexture(const std::string &path);
std::vector<unsigned char> decodeSkyMip(const SkyMip &);
} // namespace observatory
