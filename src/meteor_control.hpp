#pragma once
#include <cstdint>
namespace observatory {
inline constexpr double meteorMinRate = .5, meteorMaxRate = 1800;
enum class MeteorMode { Auto, Preview, Off };
struct MeteorOptions {
  MeteorMode mode = MeteorMode::Auto;
  double rate = 3; // bright visible-field candidates per minute during a shower
  double previewStart = 0;
  double intervalHours =
      4.5;              // mean encounter interval; default random gap 1..8 h
  uint64_t restart = 0; // refresh the observer field even while paused
};
} // namespace observatory
