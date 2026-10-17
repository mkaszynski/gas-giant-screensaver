#pragma once
namespace observatory {
enum class MeteorMode { Auto, Preview, Off };
struct MeteorOptions {
  MeteorMode mode = MeteorMode::Auto;
  double rate = 3; // bright visible-field candidates per minute during a shower
  double previewStart = 0;
};
} // namespace observatory
