#pragma once
#include <cmath>
#include <cstddef>

namespace observatory {
// Measure completed preview swaps using wall time, including rendering and
// frame-cap sleep. Simulation time and GPU queries are deliberately unrelated.
class PreviewFrameRate {
public:
  void presented(double realSeconds) {
    if (!std::isfinite(realSeconds))
      return;
    if (!initialized || realSeconds < last) {
      start = last = realSeconds;
      frames = 0;
      value = 0;
      initialized = true;
      return;
    }
    last = realSeconds;
    ++frames;
    const double elapsed = realSeconds - start;
    if (elapsed >= .5) {
      value = frames / elapsed;
      start = realSeconds;
      frames = 0;
    }
  }
  double fps() const { return value; }

private:
  bool initialized = false;
  double start = 0, last = 0, value = 0;
  size_t frames = 0;
};
} // namespace observatory
