#include "debug_timeline.hpp"
#include <iostream>
using namespace observatory;
void near(double a, double b, const char *message) {
  if (std::abs(a - b) > 1.e-6)
    throw std::runtime_error(message);
}
int main() {
  try {
    constexpr double epoch = 1790900000, route = 259200;
    DebugTimeline clock(epoch, route);
    near(clock.time(10), epoch + 10, "normal live clock");
    clock.play(false, 10);
    near(clock.time(100), epoch + 10, "pause freezes every scene clock");
    clock.seek(.5, 100);
    near(clock.time(101), epoch + route / 2, "seek preserves pause");
    clock.play(true, 110);
    near(clock.time(120), epoch + route / 2 + 10, "resume preserves seek");
    clock.beginDrag(.75, 120);
    near(clock.time(130), epoch + route / 2 + 10, "drag holds displayed scene");
    near(clock.target(130), epoch + route * .75, "drag previews target");
    clock.dragTo(1.5);
    near(clock.fraction(131), 1, "drag clamps to endpoint");
    clock.endDrag(132);
    near(clock.time(140), epoch + route, "endpoint does not loop the year");
    clock.range(1800, 140);
    near(clock.time(140), epoch + route,
         "range change preserves absolute time");
    near(clock.time(141), epoch + route + 1, "new window keeps playing");
    clock.seek(-1, 142);
    near(clock.time(142), epoch + route, "negative seek clamps");
    clock.play(false, 143);
    clock.beginDrag(.2, 150);
    clock.endDrag(155);
    near(clock.time(170), epoch + route + 360, "paused scrubbing stays paused");
    // At arbitrary Unix timestamps, sky delta also advances the route/weather
    // offsets; no modulo is applied to any of these independent clocks.
    const double startSky = 900, startTravel = 162000, startWeather = 103;
    DebugTimeline coupled(startSky, route);
    coupled.seek(.25, 0);
    const double delta = coupled.time(0) - startSky;
    near(startTravel + delta, 226800, "route follows scrubbed sky delta");
    near(startWeather + delta, 64903, "weather follows scrubbed sky delta");
    DebugLayout normal(1280, 720), hidpiLogical(1280, 720), small(320, 180);
    near(normal.fraction(normal.slider.x + normal.slider.width / 2), .5,
         "slider midpoint maps to half time");
    near(hidpiLogical.fraction(normal.slider.x + normal.slider.width / 2), .5,
         "input uses window coordinates, independent of framebuffer density");
    near(small.fraction(small.slider.x - 100), 0, "drag outside left clamps");
    near(small.fraction(small.slider.x + small.slider.width + 100), 1,
         "drag outside right clamps");
    if (!small.button(3).contains(small.button(3).x + 1, small.button(3).y + 1))
      throw std::runtime_error("scaled button hit target");
    bool rejected = false;
    try {
      clock.range(0, 170);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    if (!rejected)
      throw std::runtime_error("reject invalid range");
    std::cout
        << "Debug timeline: seek, pause, continuous epoch, coupled clocks "
           "and scaled input pass\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
