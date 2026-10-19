#pragma once
// Development benchmark only; normal renderer builds contain no queries.
#include <GLES3/gl3.h>
#include <array>
namespace observatory {
inline constexpr std::array<const char *, 9> weatherProfileNames{
    "atmosphere", "celestial", "terrain_prepare", "terrain_shadows",
    "terrain", "solar_cache", "sky_cache", "clouds", "composite"};
extern GLuint weatherProfileQueries[weatherProfileNames.size()];
void weatherProfileStage(int index);
} // namespace observatory
