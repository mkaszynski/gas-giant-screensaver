#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace observatory {
constexpr uint32_t opticsWidth = 4096, opticsHeight = 67;
// Versioned, offline spectral phase functions, linear RGBA16F. Loading is
// independent of GL so malformed or mismatched assets fail before upload.
std::vector<uint16_t> readOpticsTable(const std::string &file);
} // namespace observatory
