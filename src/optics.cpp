#include "optics.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
namespace observatory {
std::vector<uint16_t> readOpticsTable(const std::string &file) {
  std::ifstream in(file, std::ios::binary);
  std::array<unsigned char, 16> header{};
  in.read(reinterpret_cast<char *>(header.data()), header.size());
  auto u32 = [&](size_t i) {
    return uint32_t(header[i]) | uint32_t(header[i + 1]) << 8 |
           uint32_t(header[i + 2]) << 16 | uint32_t(header[i + 3]) << 24;
  };
  const std::array<unsigned char, 8> magic{'G', 'G', 'O', 'P',
                                           'T', '0', '1', 0};
  if (!in || !std::equal(magic.begin(), magic.end(), header.begin()) ||
      u32(8) != opticsWidth || u32(12) != opticsHeight)
    throw std::runtime_error("Invalid atmospheric optics table header: " +
                             file);
  std::vector<unsigned char> bytes(size_t(opticsWidth) * opticsHeight * 8);
  in.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
  if (!in || in.peek() != std::char_traits<char>::eof())
    throw std::runtime_error("Invalid atmospheric optics table length: " +
                             file);
  std::vector<uint16_t> result(bytes.size() / 2);
  for (size_t i = 0; i < result.size(); ++i) {
    const auto value = uint16_t(bytes[2 * i] | uint16_t(bytes[2 * i + 1]) << 8);
    if ((value & 0x8000) || (value & 0x7c00) == 0x7c00)
      throw std::runtime_error("Invalid atmospheric optics phase value: " +
                               file);
    result[i] = value;
  }
  return result;
}
} // namespace observatory
