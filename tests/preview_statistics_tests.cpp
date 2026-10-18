#include "preview_statistics.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using observatory::PreviewFrameRate;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main() {
  try {
    for (double rate : {15., 29.97, 30., 60.}) {
      PreviewFrameRate counter;
      require(counter.fps() == 0, "initial FPS is unknown");
      for (int frame = 0; frame < 300; ++frame)
        counter.presented(1234. + frame / rate);
      require(std::abs(counter.fps() - rate) < 1.e-7,
              "FPS measures completed intervals without an off-by-one frame");
    }
    PreviewFrameRate changing;
    for (int frame = 0; frame <= 60; ++frame)
      changing.presented(frame / 30.);
    for (int frame = 1; frame <= 60; ++frame)
      changing.presented(2. + frame / 15.);
    require(std::abs(changing.fps() - 15.) < 1.e-7,
            "FPS follows slow frames instead of displaying the configured cap");
    const double before = changing.fps();
    changing.presented(std::numeric_limits<double>::quiet_NaN());
    require(changing.fps() == before, "nonfinite clocks cannot poison FPS");
    changing.presented(0);
    require(changing.fps() == 0, "backward clock resets the measurement");
    changing.presented(1.);
    require(changing.fps() == 1.,
            "a long frame is included in the measurement");
    std::cout << "Preview wall-clock frame rate PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
