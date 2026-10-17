#pragma once
#include <array>
#include <string_view>
namespace observatory {
enum class WeatherPreset { Auto, Clear, Fair, Overcast, Fog, Storm };
inline constexpr std::array<std::string_view, 6> weatherPresetNames{
    "AUTO", "CLEAR", "FAIR", "OVERCAST", "FOG", "STORM"};
} // namespace observatory
