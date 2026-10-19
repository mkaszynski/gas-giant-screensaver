#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace observatory {
enum class GraphicsPreset { Low, Medium, High, Custom };
inline constexpr std::array<std::string_view, 8> graphicsFeatureNames{
    "TERRAIN", "CLOUDS",    "GALAXY",  "STARS",
    "RINGS",   "EMISSIONS", "METEORS", "REFLECTIONS"};
struct GraphicsSettings {
  // Canonical policy, shared by every host. High retains native scene fidelity.
  std::array<bool, 8> features{true, true, true, true, true, true, true, false};
  int height = 1080, fps = 30, textureSize = 4096;
  double clouds = 1;
  GraphicsPreset preset = GraphicsPreset::High;
  bool operator==(const GraphicsSettings &) const = default;
  static GraphicsSettings forPreset(GraphicsPreset p) {
    GraphicsSettings q;
    q.preset = p;
    if (p == GraphicsPreset::Low) {
      q.height = 360;
      q.textureSize = 1024;
      q.features[0] = q.features[1] = q.features[2] = q.features[5] = false;
      q.clouds = .5;
    } else if (p == GraphicsPreset::Medium) {
      q.height = 720;
      q.textureSize = 2048;
      q.clouds = .5;
    }
    return q;
  }
  std::array<int, 2> size(int width, int height_) const {
    width = std::max(1, width);
    height_ = std::max(1, height_);
    const double scale = std::min(
        {1., double(height) / height_,
         std::sqrt(double(height) * height * 16. / 9. / width / height_),
         4096. / width, 4096. / height_});
    const int outputHeight = std::max(1, int(height_ * scale));
    return {std::max(1, int(double(outputHeight) * width / height_)),
            outputHeight};
  }
  std::string assetGroups() const {
    std::string groups = "[\"base\",\"";
    groups += features[0] ? "world" : "terrain";
    groups += "\"";
    if (features[2])
      groups += ",\"galaxy\"";
    if (features[3])
      groups += ",\"stars\"";
    return groups + "]";
  }
};
// Browser memory is a coarse capability hint, never a claim about free RAM.
// Unknown devices and software adapters start conservatively, without a stress
// test.
inline GraphicsPreset recommendedGraphics(double memoryGiB, int cores,
                                          bool software) {
  if (software || memoryGiB <= 4 || cores <= 4)
    return GraphicsPreset::Low;
  if (memoryGiB < 16 || cores < 8)
    return GraphicsPreset::Medium;
  return GraphicsPreset::High;
}
struct GraphicsControl {
  GraphicsSettings applied{}, draft{};
  unsigned revision = 0;
  bool open = false, dragging = false;
  void initialize(GraphicsSettings q) { applied = draft = q; }
  void select(double fraction) {
    draft = GraphicsSettings::forPreset(
        GraphicsPreset(std::clamp(int(fraction * 2 + .5), 0, 2)));
  }
  void toggle(int i) {
    draft.features.at(i) = !draft.features.at(i);
    draft.preset = GraphicsPreset::Custom;
  }
  void apply() {
    if (draft != applied) {
      applied = draft;
      ++revision;
    }
  }
};
} // namespace observatory
