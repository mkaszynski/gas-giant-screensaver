#include "frame_pacing.hpp"
#include <iostream>
#include <stdexcept>
using namespace std::chrono;
using observatory::FramePacer;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main() {
  try {
    const auto start = steady_clock::time_point{};
    require(FramePacer(30).delay(start, start) == milliseconds(33),
            "30 fps target");
    require(FramePacer(30).delay(start, start + milliseconds(12)) ==
                milliseconds(21),
            "rendering time must not reduce native 30 fps to 22 fps");
    require(FramePacer(30).delay(start, start + milliseconds(40)) ==
                milliseconds(1),
            "overrun retries without negative or zero timer");
    require(FramePacer(30).delay(start, start - milliseconds(2)) ==
                milliseconds(33),
            "clock anomaly cannot extend a frame");
    require(FramePacer(60).delay(start, start + milliseconds(7)) ==
                milliseconds(9),
            "60 fps remains available later");
    for (int fps : {30, 60}) {
      FramePacer cadence(fps);
      auto nextStart = start;
      for (int frame = 0; frame < fps * 10; ++frame) {
        const auto finished = nextStart + milliseconds(7);
        // A sub-millisecond timer delivery delay must not accumulate for
        // hundreds of frames. The old relative timer drifts by 125 ms at 30.
        nextStart =
            finished + cadence.delay(nextStart, finished) + microseconds(750);
      }
      auto error = duration_cast<microseconds>(nextStart - start - seconds(10));
      require(std::abs(error.count()) < 2000,
              "timer rounding and wakeup delays must not accumulate");
    }
    FramePacer resumed;
    resumed.delay(start, start);
    require(resumed.delay(start + seconds(60),
                          start + seconds(60) + milliseconds(12)) ==
                milliseconds(21),
            "resume starts one fresh interval without a catch-up burst");
    FramePacer overrun;
    require(overrun.delay(start, start + milliseconds(40)) == milliseconds(1),
            "late rendering retries promptly");
    require(overrun.delay(start + milliseconds(41), start + milliseconds(53)) ==
                milliseconds(20),
            "late frames restart cadence without a backlog");
    bool rejected = false;
    try {
      FramePacer invalid(0);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    require(rejected, "invalid rate rejected");
    for (int fps : {30, 60}) {
      FramePacer preview(fps);
      auto nextStart = start;
      for (int frame = 0; frame < fps * 20; ++frame) {
        const auto finished = nextStart + milliseconds(7);
        nextStart = preview.deadline(nextStart, finished) + microseconds(125);
      }
      const auto error =
          duration_cast<microseconds>(nextStart - start - seconds(20));
      require(std::abs(error.count()) < 250,
              "preview sleep overshoot must not accumulate across frames");
    }
    FramePacer previewOverrun;
    require(previewOverrun.deadline(start, start + milliseconds(40)) ==
                start + milliseconds(40),
            "late preview draws resume immediately without a catch-up queue");
    require(previewOverrun.deadline(start + seconds(60),
                                    start + seconds(60) + milliseconds(7)) ==
                start + seconds(60) + nanoseconds(33333333),
            "preview idle restart retains precise frame duration");
    std::cout << "Native frame deadlines account for render work PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
