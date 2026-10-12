#include "sky_texture.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#define BCDECDEF inline
#define BCDEC_IMPLEMENTATION
#include "third_party/bcdec.h"
namespace observatory {
std::vector<SkyMip> loadSkyTexture(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  auto fail = [&]() {
    throw std::runtime_error("Invalid sky texture: " + path);
  };
  char magic[8];
  if (!file.read(magic, 8) || std::string(magic, 8) != "GGSCUBE1")
    fail();
  auto word = [&]() {
    unsigned char b[4];
    if (!file.read(reinterpret_cast<char *>(b), 4))
      fail();
    return unsigned(b[0]) | (unsigned(b[1]) << 8) | (unsigned(b[2]) << 16) |
           (unsigned(b[3]) << 24);
  };
  unsigned size = word(), levels = word(), expectedLevels = 0;
  if (size < 256 || size > 4096 || size & (size - 1))
    fail();
  for (unsigned n = size; n; n /= 2)
    ++expectedLevels;
  if (levels != expectedLevels)
    fail();
  std::vector<SkyMip> result;
  for (unsigned level = 0; level < levels; ++level) {
    const unsigned count = word(),
                   expected = ((size + 3) / 4) * ((size + 3) / 4) * 16;
    if (count != expected)
      fail();
    SkyMip mip{size, std::vector<unsigned char>(count)};
    if (!file.read(reinterpret_cast<char *>(mip.blocks.data()), count))
      fail();
    result.push_back(std::move(mip));
    size = std::max(1u, size / 2);
  }
  if (file.peek() != std::char_traits<char>::eof())
    fail();
  return result;
}
std::vector<unsigned char> decodeSkyMip(const SkyMip &mip) {
  const unsigned blocks = (mip.size + 3) / 4;
  if (mip.size < 1 || mip.size > 4096 ||
      mip.blocks.size() != blocks * blocks * 16)
    throw std::runtime_error("Invalid BC7 mip size");
  std::vector<unsigned char> pixels(mip.size * mip.size * 4);
  alignas(16) std::array<unsigned char, 64> block;
  for (unsigned y = 0; y < blocks; ++y)
    for (unsigned x = 0; x < blocks; ++x) {
      bcdec_bc7(mip.blocks.data() + (y * blocks + x) * 16, block.data(), 16);
      for (unsigned row = 0; row < 4 && y * 4 + row < mip.size; ++row)
        std::copy_n(block.data() + row * 16, std::min(4u, mip.size - x * 4) * 4,
                    pixels.data() + ((y * 4 + row) * mip.size + x * 4) * 4);
    }
  return pixels;
}
} // namespace observatory
