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
  if (!file.read(magic, 8))
    fail();
  const bool compressed = std::string(magic, 8) == "GGSHDR01";
  if (!compressed && std::string(magic, 8) != "GGSRAW01")
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
                   expected = compressed
                                  ? ((size + 3) / 4) * ((size + 3) / 4) * 16
                                  : size * size * 6;
    if (count != expected)
      fail();
    SkyMip mip{size, std::vector<unsigned char>(count), compressed};
    if (!file.read(reinterpret_cast<char *>(mip.blocks.data()), count))
      fail();
    if (!compressed)
      for (size_t i = 0; i < mip.blocks.size(); i += 2) {
        const unsigned half = mip.blocks[i] | unsigned(mip.blocks[i + 1]) << 8;
        if ((half & 0x8000) || (half & 0x7c00) == 0x7c00)
          fail(); // Raw references must contain finite, nonnegative radiance.
      }
    result.push_back(std::move(mip));
    size = std::max(1u, size / 2);
  }
  if (file.peek() != std::char_traits<char>::eof())
    fail();
  return result;
}
std::vector<std::uint16_t> decodeSkyMip(const SkyMip &mip) {
  const unsigned blocks = (mip.size + 3) / 4;
  if (!mip.compressed || mip.size < 1 || mip.size > 4096 ||
      mip.blocks.size() != blocks * blocks * 16)
    throw std::runtime_error("Invalid BC6H mip size");
  std::vector<std::uint16_t> pixels(mip.size * mip.size * 3);
  alignas(16) std::array<std::uint16_t, 48> block;
  for (unsigned y = 0; y < blocks; ++y)
    for (unsigned x = 0; x < blocks; ++x) {
      bcdec_bc6h_half(mip.blocks.data() + (y * blocks + x) * 16, block.data(),
                      12, 0);
      for (unsigned row = 0; row < 4 && y * 4 + row < mip.size; ++row)
        std::copy_n(block.data() + row * 12, std::min(4u, mip.size - x * 4) * 3,
                    pixels.data() + ((y * 4 + row) * mip.size + x * 4) * 3);
    }
  return pixels;
}
} // namespace observatory
