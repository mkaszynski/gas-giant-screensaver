#include "sky_texture.hpp"
#include <array>
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
    const auto path = std::filesystem::path(SKY_ASSET_DIR) / "galaxy-px.ggsky";
    const auto mips = loadSkyTexture(path.string());
    require(mips.size() == 12 && mips.front().size == 2048 &&
                mips.back().size == 1,
            "HDR model sky with complete linear-radiance mip chain");
    size_t bytes = 0;
    for (const auto &mip : mips)
      bytes += mip.blocks.size();
    require(bytes == 5592432,
            "bounded 32 MiB total sky storage including all mipmaps");
    const auto smallest = decodeSkyMip(mips.back());
    require(smallest.size() == 3 && smallest[0] > 0 && smallest[0] < 0x7c00,
            "portable HDR fallback handles a partial BC6H block");
    const SkyMip constant{4,
                          {0x0f, 0xb1, 0x94, 0x64, 0x04, 0x18, 0x4a, 0x18, 0x13,
                           0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}};
    const auto color = decodeSkyMip(constant);
    for (size_t i = 0; i < color.size(); i += 3)
      require(color[i] > 0 && color[i] < 0x1000 && color[i + 1] >= 0x4ff0 &&
                  color[i + 1] <= 0x5010 && color[i + 2] >= 0x5ff0 &&
                  color[i + 2] <= 0x6010,
              "BC6H preserves dim radiance and values far above display white");
    char name[] = "/tmp/observatory-sky-texture-XXXXXX";
    const std::filesystem::path temporary = mkdtemp(name);
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() { std::filesystem::remove_all(path); }
    } cleanup{temporary};
    std::ifstream input(path, std::ios::binary);
    const std::string original((std::istreambuf_iterator<char>(input)), {});
    auto reject = [&](const std::string &data) {
      const auto invalid = temporary / "invalid.ggsky";
      std::ofstream(invalid, std::ios::binary).write(data.data(), data.size());
      bool rejected = false;
      try {
        loadSkyTexture(invalid.string());
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      require(rejected, "unsafe, incomplete, or malformed sky assets rejected");
    };
    reject(original.substr(0, 10));
    reject(original.substr(0, original.size() - 1));
    reject(original + "x");
    auto changed = original;
    changed[0] = 'X';
    reject(changed);
    changed = original;
    changed[8] = changed[9] = changed[10] = changed[11] = char(255);
    reject(changed);
    changed = original;
    changed[12] = 0;
    reject(changed);
    changed = original;
    changed[16] = char(255);
    reject(changed);
    // The uncompressed reference retains the same mip chain in RGB16F, and
    // refuses NaNs, infinities and negative radiance before GPU upload.
    std::string raw = "GGSRAW01";
    auto word = [&](unsigned value) {
      for (int i = 0; i < 4; ++i)
        raw += char(value >> (8 * i));
    };
    word(256);
    word(9);
    for (unsigned n = 256; n; n /= 2) {
      word(n * n * 6);
      for (unsigned i = 0; i < n * n; ++i)
        for (unsigned half : {0x3c00, 0x5000, 0x6000}) {
          raw += char(half);
          raw += char(half >> 8);
        }
    }
    const auto reference = temporary / "reference.ggsky";
    std::ofstream(reference, std::ios::binary).write(raw.data(), raw.size());
    const auto uncompressed = loadSkyTexture(reference.string());
    require(!uncompressed.front().compressed &&
                uncompressed.front().blocks.size() == 256 * 256 * 6,
            "raw HDR reference keeps floating-point radiance");
    raw[21] = char(0x7c);
    reject(raw);
    raw[21] = char(0xbc);
    reject(raw);
    std::cout << "Sky texture behavior passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
