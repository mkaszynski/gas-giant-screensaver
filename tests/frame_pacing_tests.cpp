#include "frame_pacing.hpp"
#include <iostream>
#include <stdexcept>
using namespace std::chrono;
using observatory::frameDelay;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main() {
  try {
    const auto start = steady_clock::time_point{};
    require(frameDelay(start, start, 30) == milliseconds(33), "30 fps target");
    require(frameDelay(start, start + milliseconds(12), 30) == milliseconds(21),
            "rendering time must not reduce native 30 fps to 22 fps");
    require(frameDelay(start, start + milliseconds(40), 30) == milliseconds(1),
            "overrun retries without negative or zero timer");
    require(frameDelay(start, start - milliseconds(2), 30) == milliseconds(33),
            "clock anomaly cannot extend a frame");
    require(frameDelay(start, start + milliseconds(7), 60) == milliseconds(9),
            "60 fps remains available later");
    bool rejected = false;
    try {
      frameDelay(start, start, 0);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    require(rejected, "invalid rate rejected");
    std::cout << "Native frame deadlines account for render work PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
