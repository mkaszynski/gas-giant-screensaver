#include "optics.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
using namespace observatory;
void require(bool b, const char *message) {
  if (!b)
    throw std::runtime_error(message);
}
int main() {
  std::filesystem::path temp;
  try {
    const std::string asset = std::string(OPTICS_ASSET_DIR) + "/phase.ggoptics";
    auto table = readOpticsTable(asset);
    require(table.size() == size_t(opticsWidth) * opticsHeight * 4,
            "complete RGBA16F phase table");
    for (size_t i = 3; i < table.size(); i += 4)
      require(table[i] == 0x3c00, "phase table alpha is one");
    char name[] = "/tmp/observatory-optics-XXXXXX";
    temp = mkdtemp(name);
    auto file = temp / "bad.ggoptics";
    std::ifstream in(asset, std::ios::binary);
    const std::vector<char> original{std::istreambuf_iterator<char>(in), {}};
    auto rejects = [&](std::vector<char> bytes) {
      {
        std::ofstream out(file, std::ios::binary);
        out.write(bytes.data(), bytes.size());
      }
      bool rejected = false;
      try {
        (void)readOpticsTable(file.string());
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      require(rejected, "malformed optics table must fail before GPU upload");
    };
    rejects({});
    auto bytes = original;
    bytes[0] = '?';
    rejects(bytes);
    bytes = original;
    bytes[9] = 0;
    rejects(bytes);
    bytes = original;
    bytes[12] = 0;
    rejects(bytes);
    bytes = original;
    bytes.pop_back();
    rejects(bytes);
    bytes = original;
    bytes.push_back(0);
    rejects(bytes);
    bytes = original;
    bytes[16] = 0;
    bytes[17] = char(0x7c);
    rejects(bytes); // infinity
    bytes = original;
    bytes[16] = 1;
    bytes[17] = char(0x7e);
    rejects(bytes); // NaN
    bytes = original;
    bytes[16] = 0;
    bytes[17] = char(0xbc);
    rejects(bytes); // negative
    std::filesystem::remove_all(temp);
    std::cout << "Optics schema, bounded allocation, half floats and malformed "
                 "input passed\n";
  } catch (const std::exception &e) {
    if (!temp.empty())
      std::filesystem::remove_all(temp);
    std::cerr << e.what() << '\n';
    return 1;
  }
}
