#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace observatory::detail {
// Keep a resumed/slow frame from granting a correspondingly long CPU burst.
inline double surfaceWorkSeconds(double framePeriod) {
  if (!std::isfinite(framePeriod) || framePeriod <= 0.)
    framePeriod = 1. / 30.;
  return std::clamp(framePeriod * .08, .0005, .003);
}
// One producer and one render-thread owner. CPU work runs only inside an
// explicit window; runFor returns after the worker has acknowledged its end.
class SurfaceWork {
public:
  using Clock = std::chrono::steady_clock;
  void reset() {
    std::lock_guard lock(mutex);
    if (!done)
      throw std::logic_error("Terrain worker reset before completion");
    done = allowed = running = unbounded = false;
  }
  void checkpoint() {
    std::unique_lock lock(mutex);
    if (done)
      throw std::logic_error("Terrain checkpoint after completion");
    while (!unbounded && (!allowed || Clock::now() >= deadline)) {
      allowed = running = false;
      changed.notify_all();
      changed.wait(lock, [&] { return allowed || unbounded; });
    }
    running = true;
  }
  void runFor(Clock::duration budget) {
    if (budget <= Clock::duration::zero())
      return;
    std::unique_lock lock(mutex);
    if (done || unbounded)
      return;
    deadline = Clock::now() + budget;
    allowed = true;
    changed.notify_all();
    changed.wait_until(lock, deadline, [&] { return done || !allowed; });
    allowed = false;
    changed.wait(lock, [&] { return done || !running; });
  }
  void complete() {
    std::lock_guard lock(mutex);
    done = true;
    allowed = running = false;
    changed.notify_all();
  }
  // Seeks and shutdown finish outstanding work before issuing more draws or
  // destroying its world. They must not wait for another displayed frame.
  void release() {
    std::lock_guard lock(mutex);
    unbounded = true;
    changed.notify_all();
  }

private:
  std::mutex mutex;
  std::condition_variable changed;
  Clock::time_point deadline{};
  bool done = true, allowed = false, running = false, unbounded = false;
};
} // namespace observatory::detail
