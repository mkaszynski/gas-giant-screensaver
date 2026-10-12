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
    const auto path = std::filesystem::path(SKY_ASSET_DIR) / "galaxy-px.bc7";
    const auto mips = loadSkyTexture(path.string());
    require(mips.size() == 13 && mips.front().size == 4096 &&
                mips.back().size == 1,
            "full-resolution BC7 sky with complete linear-light mip chain");
    size_t bytes = 0;
    for (const auto &mip : mips)
      bytes += mip.blocks.size();
    require(bytes == 22369648,
            "bounded 128 MiB total sky storage including all mipmaps");
    const auto smallest = decodeSkyMip(mips.back());
    require(smallest.size() == 4 && smallest[3] == 255,
            "portable fallback handles a partial BC7 block");
    const SkyMip constant{4,
                          {0x08, 0x40, 0x20, 0x10, 0x08, 0x08, 0x04, 0x02, 0x01,
                           0x81, 0x40, 0x20, 0xac, 0xaa, 0xaa, 0xaa}};
    const auto color = decodeSkyMip(constant);
    for (size_t i = 0; i < color.size(); i += 4)
      require(color[i] == 32 && color[i + 1] == 64 && color[i + 2] == 128 &&
                  color[i + 3] == 255,
              "portable BC7 decode preserves channels and opaque alpha");
    char name[] = "/tmp/observatory-sky-texture-XXXXXX";
    const std::filesystem::path temporary = mkdtemp(name);
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() { std::filesystem::remove_all(path); }
    } cleanup{temporary};
    std::ifstream input(path, std::ios::binary);
    const std::string original((std::istreambuf_iterator<char>(input)), {});
    auto reject = [&](const std::string &data) {
      const auto invalid = temporary / "invalid.bc7";
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
    std::cout << "Sky texture behavior passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
