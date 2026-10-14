#pragma once
#include <algorithm>
#include <chrono>
#include <stdexcept>
namespace observatory {
// Native draw callbacks begin on a compositor frame. Account for CPU work
// before scheduling the next callback, and round down so a millisecond timer
// does not miss the desired vblank. Keep one pending timer, including on DPMS.
inline std::chrono::milliseconds
frameDelay(std::chrono::steady_clock::time_point start,
           std::chrono::steady_clock::time_point now, int fps) {
  if (fps < 1 || fps > 60)
    throw std::invalid_argument("Invalid frame rate");
  const auto period = std::chrono::duration<double>(1. / fps);
  auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      period -
      std::max(now - start, std::chrono::steady_clock::duration::zero()));
  return std::max(remaining, std::chrono::milliseconds(1));
}
} // namespace observatory
