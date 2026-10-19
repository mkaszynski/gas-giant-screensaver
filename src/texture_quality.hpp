#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>
namespace observatory {
// Shared decoded-asset reduction. Packed numerical pixels are sampled whole:
// averaging the high/low bytes of a distance independently changes its value.
inline void reduceTexture(std::vector<unsigned char> &pixels, unsigned &width,
                          unsigned &height, unsigned limit, bool packed) {
  if (!width || !height || !limit ||
      pixels.size() != std::size_t(width) * height * 4)
    throw std::runtime_error("Invalid decoded texture dimensions");
  const unsigned step = (std::max(width, height) - 1) / limit + 1;
  if (step <= 1)
    return;
  const unsigned w = std::max(1u, width / step),
                 h = std::max(1u, height / step);
  std::vector<unsigned char> reduced(std::size_t(w) * h * 4);
  for (unsigned y = 0; y < h; ++y)
    for (unsigned x = 0; x < w; ++x)
      for (unsigned c = 0; c < 4; ++c) {
        if (packed) {
          reduced[(y * w + x) * 4 + c] =
              pixels[(std::min(height - 1, y * step + step / 2) * width +
                      std::min(width - 1, x * step + step / 2)) *
                         4 +
                     c];
        } else {
          std::uint64_t sum = 0, count = 0;
          for (unsigned sy = y * step; sy < std::min(height, y * step + step);
               ++sy)
            for (unsigned sx = x * step; sx < std::min(width, x * step + step);
                 ++sx) {
              sum += pixels[(sy * width + sx) * 4 + c];
              ++count;
            }
          reduced[(y * w + x) * 4 + c] = sum / count;
        }
      }
  pixels = std::move(reduced);
  width = w;
  height = h;
}
} // namespace observatory
