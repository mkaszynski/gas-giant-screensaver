#include "meteors.hpp"
#include <iostream>
#include <stdexcept>
using namespace observatory;
void require(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
int main() {
  try {
    double total = 0;
    for (uint64_t i = 0; i < 10000; ++i) {
      double gap = meteorShowerStart(i + 1) - meteorShowerStart(i);
      require(gap >= 3600 && gap <= 28800,
              "natural shower starts stay 1..8 hours apart");
      total += gap;
      auto window = meteorWindowAt(meteorShowerStart(i) + .01);
      require(window.id == i && window.duration >= 360 &&
                  window.duration <= 960,
              "continuous epoch window lookup");
      require(window.start + window.duration < meteorShowerStart(i + 1),
              "showers never overlap");
    }
    require(std::abs(total / 10000 - 16200) < 5,
            "mean recurrence is four and a half hours");
    for (int fps : {24, 30, 60}) {
      double energy = 0;
      constexpr double duration = .731;
      for (int i = -2; i < fps * 2; ++i)
        energy +=
            meteorLightIntegral(i / double(fps), (i + 1.) / fps, duration);
      require(std::abs(energy - duration * .5) < 1.e-12,
              "sub-frame meteor energy is frame-rate independent");
    }
    auto scene = sceneAt(1200);
    scene.sunLocal.y = -.4;
    const auto radiant = meteor_detail::world(
        scene, normalized(scene.forward * .65 + Vec3{0, .8, 0}));
    MeteorFrame frame{1200, 1. / 30, {MeteorMode::Preview, 3, 1200}};
    size_t low = 0, high = 0, maxSegments = 0;
    double peak = 0;
    for (int i = 0; i < 360 * 30; ++i) {
      frame.seconds = 1200 + i / 30.;
      frame.options.rate = 3;
      auto a = meteorsAt(frame, scene, 16. / 9., 1800, radiant);
      low += a.count;
      frame.options.rate = 60;
      auto b = meteorsAt(frame, scene, 16. / 9., 1800, radiant);
      high += b.count;
      maxSegments = std::max(maxSegments, b.count);
      require(b.count <= b.segments.size(), "bounded event/segment work");
      for (size_t j = 0; j < b.count; ++j) {
        auto &segment = b.segments[j];
        peak = std::max(peak, segment.flux);
        const double altitude =
            length(segment.head +
                   Vec3{0, 6371 + scene.altitudeMeters / 1000., 0}) -
            6371;
        require(std::isfinite(segment.head.x) && altitude > 75 &&
                    altitude < 117,
                "streaks occupy the mesosphere in km");
        require(segment.color.x > .5 && segment.color.y > .5 &&
                    segment.color.z > .5,
                "visual colors stay near white");
      }
    }
    require(low > 0 && high > low * 5 && peak > 0,
            "frequency controls actual events");
    frame.seconds = 1200.9;
    frame.options.rate = 60;
    const auto fixed = meteorsAt(frame, scene, 16. / 9., 1800, radiant, &scene);
    auto moved = sceneAt(
        1200, 1800, true,
        {scene.latitude + 1.e-5, scene.longitude, scene.altitudeMeters});
    moved.sunLocal.y = -.4;
    const auto translated =
        meteorsAt(frame, moved, 16. / 9., 1800, radiant, &scene);
    require(fixed.count > 0 && fixed.count == translated.count,
            "observer movement retains the same physical meteors");
    meteor_detail::MoonFrame originalFrame(scene), movedFrame(moved);
    for (size_t i = 0; i < fixed.count; ++i) {
      Vec3 a = originalFrame.eye + originalFrame.global(fixed.segments[i].head);
      Vec3 b = movedFrame.eye + movedFrame.global(translated.segments[i].head);
      require(length(a - b) < 1.e-6,
              "meteors remain anchored in moon coordinates, not the camera");
    }
    auto trainBatch = [&](double phase) {
      auto f = frame;
      f.options.previewStart = 1254;
      f.seconds = 1254 + phase;
      f.options.rate = .5;
      return meteorsAt(f, scene, 16. / 9., 1800, radiant, &scene);
    };
    auto trainPower = [&](double phase) {
      auto batch = trainBatch(phase);
      double sum = 0;
      for (size_t i = 0; i < batch.count; ++i)
        sum += batch.segments[i].flux;
      return sum;
    };
    require(std::abs(trainPower(.750001) - trainPower(.749999)) < .01,
            "persistent train emerges continuously instead of popping on");
    for (double phase = .57; phase < 2.5; phase += .02) {
      const auto count = trainBatch(phase - .02).count;
      if (trainBatch(phase).count <= count)
        continue;
      double lo = phase - .02, hi = phase;
      for (int step = 0; step < 40; ++step) {
        const double mid = (lo + hi) * .5;
        if (trainBatch(mid).count > count)
          hi = mid;
        else
          lo = mid;
      }
      require(std::abs(trainPower(hi + 1.e-6) - trainPower(lo - 1.e-6)) < .01,
              "new train sections deposit energy continuously");
    }
    auto daylight = scene;
    daylight.sunLocal.y = .1;
    require(!meteorsAt(frame, daylight, 16. / 9., 1800, radiant).count,
            "preview never bypasses daylight gate");
    frame.options.mode = MeteorMode::Off;
    require(!meteorsAt(frame, scene, 16. / 9., 1800, radiant).count,
            "off disables all events");
    frame.options.mode = MeteorMode::Auto;
    frame.seconds = meteorShowerStart(111) - 1;
    require(!meteorsAt(frame, scene).count,
            "no events before a natural shower");
    frame.options.mode = MeteorMode::Preview;
    frame.options.previewStart = 1.e10;
    frame.seconds = 1.e10;
    require(!meteorsAt(frame, scene, 16. / 9., 1800, radiant).count,
            "large timestamps are safe before the first stroke");
    std::cout << "Meteor scheduling, night gate, physical height, rate and "
                 "exposure pass; peak segments="
              << maxSegments << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
