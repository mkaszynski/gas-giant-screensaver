#pragma once
#include <algorithm>
#include <chrono>
#include <stdexcept>
namespace observatory {
// Keep native timers on steady-clock deadlines across compositor callbacks.
// Account for CPU work and timer delivery delays without accumulating drift.
// The widget retains one pending timer per output, including on DPMS.
class FramePacer {
public:
  using Clock = std::chrono::steady_clock;
  explicit FramePacer(int fps = 30) {
    if (fps < 1 || fps > 60)
      throw std::invalid_argument("Invalid frame rate");
    period = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1. / fps));
  }

  std::chrono::milliseconds delay(Clock::time_point start,
                                  Clock::time_point now) {
    now = std::max(start, now);
    // Keep fractional milliseconds and compensate for previous timer wakeup
    // delays instead of restarting the cadence at every actual draw.
    if (!started || start >= next + period)
      next = start + period;
    else
      next += period;
    started = true;
    if (next <= now) {
      // A slow frame should retry promptly, without queuing missed frames.
      next = now;
      return std::chrono::milliseconds(1);
    }
    return std::max(
        std::chrono::duration_cast<std::chrono::milliseconds>(next - now),
        std::chrono::milliseconds(1));
  }

private:
  Clock::duration period{};
  Clock::time_point next{};
  bool started = false;
};
} // namespace observatory
