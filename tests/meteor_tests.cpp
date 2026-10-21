#include "debug_menu.hpp"
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
    for (double hours : {.5, 1., 4.5, 12.}) {
      const double period = hours * 3600;
      for (uint64_t i = 0; i < 1000; ++i) {
        const double a = meteorShowerStart(i, hours),
                     b = meteorShowerStart(i + 1, hours);
        require(b - a >= period * 2. / 9. - 1.e-4 &&
                    b - a <= period * 16. / 9. + 1.e-4,
                "recurrence slider scales random shower gaps");
        auto window = meteorWindowAt(a + .01, hours);
        require(window.id == i && window.duration > 0 &&
                    a + window.duration < b,
                "adjusted recurrence remains queryable and non-overlapping");
      }
    }
    require(meteorPeriod(0) == 0 && meteorPeriod(13) == 0,
            "invalid encounter periods are rejected");
    for (int fps : {24, 30, 60}) {
      for (double peakFraction : {.32, .5, .7}) {
        double energy = 0;
        constexpr double duration = .731;
        for (int i = -2; i < fps * 2; ++i)
          energy += meteorLightIntegral(i / double(fps), (i + 1.) / fps,
                                        duration, peakFraction);
        require(std::abs(energy - duration * .5) < 1.e-12,
                "sub-frame meteor energy is frame-rate independent");
      }
    }
    auto scene = sceneAt(1200);
    scene.sunLocal.y = -.4;
    {
      int left = 0, right = 0;
      for (double aspect : {.6, 16. / 9., 32. / 9.}) {
        for (uint64_t id = 0; id < 512; ++id) {
          const auto radiant = meteorPreviewRadiant(scene, id, aspect);
          const auto local = scene.local(radiant);
          const double forward = dot(local, scene.forward);
          require(std::abs(length(radiant) - 1) < 1.e-12 &&
                      local.y >= std::sin(25 * pi / 180) - 1.e-12 &&
                      local.y <= std::sin(70 * pi / 180) + 1.e-12,
                  "manual radiants stay finite, normalized and above the horizon");
          require(forward <= 0 ||
                      std::abs(dot(local, scene.right)) >
                          forward * std::tan(29 * pi / 180) * aspect * 1.2 ||
                      std::abs(dot(local, scene.cameraUp)) >
                          forward * std::tan(29 * pi / 180) * 1.2,
                  "radiants stay outside the viewport, avoiding a central burst");
          require(length(radiant - meteorPreviewRadiant(scene, id, aspect)) <
                      1.e-12,
                  "a shower seed always reproduces the same incoming direction");
          (dot(local, scene.right) < 0 ? left : right)++;
        }
      }
      require(left > 400 && right > 400,
              "encounters choose both leftward and rightward streams");
      MeteorOptions options{MeteorMode::Preview, 60, 1200};
      const auto id = meteorPreviewId(options);
      const auto radiant = meteorPreviewRadiant(scene, id);
      options.rate = 1800;
      options.intervalHours = 12;
      require(meteorPreviewId(options) == id,
              "rate and recurrence changes cannot reroll an active shower");
      ++options.restart;
      require(length(radiant -
                     meteorPreviewRadiant(scene, meteorPreviewId(options))) > .01,
              "a fresh encounter selects a new stream direction");
      options.restart = 0;
      size_t tracks = 0;
      for (int i = 0; i < 120; ++i) {
        MeteorFrame f{1200 + i / 30., 1. / 30, options};
        const auto explicitStream =
            meteorsAt(f, scene, 16. / 9., 1800, radiant, &scene);
        const auto implicitStream =
            meteorsAt(f, scene, 16. / 9., 1800, {}, &scene);
        require(explicitStream.count == implicitStream.count,
                "the model and renderer share their radiant selection");
        for (size_t j = 0; j < explicitStream.count; ++j) {
          const auto &segment = explicitStream.segments[j];
          require(length(segment.head - implicitStream.segments[j].head) <
                      1.e-10,
                  "cached and directly sampled showers have identical tracks");
          const auto incoming = segment.tail - segment.head;
          if (length(incoming) < 1.e-6)
            continue;
          require(dot(normalized(incoming), scene.local(radiant)) >
                      std::cos(3 * pi / 180),
                  "meteors retain one incoming stream with only physical dispersion");
          ++tracks;
        }
      }
      require(tracks > 100, "direction consistency covers many actual meteors");
    }
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
    {
      size_t storm = 0, ordinary = 0, stormPeak = 0;
      auto early = frame;
      early.options.rate = 1800;
      early.seconds = early.options.previewStart + .1;
      require(!meteorsAt(early, scene, 16. / 9., 1800, radiant).count,
              "high rates retain the manual preview's initial delay");
      for (int i = 0; i < 60 * 30; ++i) {
        auto f = frame;
        f.seconds = 1200 + i / 30.;
        f.options.rate = 60;
        ordinary += meteorsAt(f, scene, 16. / 9., 1800, radiant).count;
        f.options.rate = 1800;
        auto batch = meteorsAt(f, scene, 16. / 9., 1800, radiant);
        storm += batch.count;
        stormPeak = std::max(stormPeak, batch.count);
        require(batch.count < batch.segments.size(),
                "maximum rate never truncates newer events behind old trains");
      }
      std::cout << "Maximum-rate population=" << storm << " versus " << ordinary
                << "; peak segments=" << stormPeak << '\n';
      require(storm > ordinary * 20,
              "1800/min produces the requested increase in event density");
      require(MeteorBatch{}.segments.size() >= 955,
              "capacity covers the worst-case candidate and train population");
    }
    {
      double minimumRed = 2, maximumRed = 0;
      for (uint64_t seed = 0; seed < 2048; ++seed) {
        for (bool wake : {false, true}) {
          const auto faint = meteor_detail::emissionColor(seed, 60, 2, wake),
                     bright = meteor_detail::emissionColor(seed, 60, -3, wake);
          require(std::abs(dot(bright, {.2126, .7152, .0722}) - 1) < 1.e-12,
                  "emission color does not change the magnitude calibration");
          require(std::abs(faint.x - 1) < .02 && std::abs(faint.y - 1) < .02 &&
                      std::abs(faint.z - 1) < .02,
                  "faint meteors stay nearly achromatic");
          minimumRed = std::min(minimumRed, bright.x);
          maximumRed = std::max(maximumRed, bright.x);
        }
      }
      require(maximumRed - minimumRed > .03,
              "composition varies color even at a shared entry speed");
    }
    {
      std::vector<double> powers, wakes;
      double faintWake = 0, brightWake = 0;
      int faintCount = 0, brightCount = 0;
      for (uint64_t seed = 0; seed < 4096; ++seed) {
        const auto a = meteor_detail::appearance(seed);
        powers.push_back(a.flux);
        wakes.push_back(a.wakeSeconds);
        require(a.endHeight >= 80 && a.endHeight <= 94,
                "ablation endpoints remain in the modeled luminous layer");
        if (a.magnitude > 1.5) {
          faintWake += a.wakeSeconds;
          ++faintCount;
        }
        if (a.magnitude < -1) {
          brightWake += a.wakeSeconds;
          ++brightCount;
        }
      }
      std::sort(powers.begin(), powers.end());
      std::sort(wakes.begin(), wakes.end());
      require(
          powers[3686] > powers[409] * 5 && wakes[3686] > wakes[409] * 1.5 &&
              faintCount && brightCount &&
              brightWake / brightCount > faintWake / faintCount * 1.5,
          "population has distinct peak brightness and mass-dependent tails");
    }
    {
      DebugMenu menu(1200, 1800, 1772928, 259200, true);
      DebugLayout layout(1280, 720);
      auto click = [&](DebugRect r, double real) {
        menu.pointer(1280, 720, r.x + r.width * .5, r.y + r.height * .5, true,
                     real);
        menu.pointer(1280, 720, r.x + r.width * .5, r.y + r.height * .5, false,
                     real);
      };
      click(layout.meteorMode(1), 0);
      click(layout.button(3),
            0); // Year range: large forward and backward seeks.
      menu.meteorOptions.rate = 1800;
      double real = 1;
      for (double fraction : {.9, .1, 1.}) {
        const auto serial = menu.meteorOptions.restart;
        const auto bar = layout.slider;
        menu.pointer(1280, 720, bar.x + bar.width * fraction, bar.y + 1, true,
                     real);
        const double held = menu.meteorTime(real);
        menu.pointer(1280, 720, bar.x + bar.width * fraction, bar.y + 1, false,
                     real + .1);
        require(menu.meteorOptions.restart == serial + 1 &&
                    menu.meteorOptions.previewStart == held &&
                    std::abs(menu.meteorTime(real + .9) - held - .8) < 1.e-6,
                "seeks re-anchor events without jumping or halting the "
                "activity clock");
        auto view = sceneAt(menu.time(real + .9));
        const auto aim = meteor_detail::world(
            view, normalized(view.forward * .65 + Vec3{0, .8, 0}));
        require(
            meteorsAt({menu.meteorTime(real + .9), 1. / 30, menu.meteorOptions},
                      view, 16. / 9., 1800, aim)
                    .count > 0,
            "meteors continue after forward/backward seeks and at the timeline "
            "endpoint");
        real += 2;
      }
      click(layout.button(0), real);
      const double paused = menu.meteorTime(real);
      require(menu.meteorTime(real + 20) == paused,
              "explicit pause still freezes meteor activity");
      const auto serial = menu.meteorOptions.restart;
      menu.keys({false, true, false, false}, real + 20); // Home while paused.
      require(
          menu.meteorOptions.restart == serial + 1 &&
              menu.meteorTime(real + 21) == paused,
          "paused keyboard seek refreshes the field without advancing events");
      menu.keys({}, real + 21);
      menu.keys({true, false, false, false}, real + 22);
      require(std::abs(menu.meteorTime(real + 23) - paused - 1) < 1.e-6,
              "resuming preserves real-time meteor speed");
    }
    frame.seconds = 1200.9;
    frame.options.rate = 60;
    const auto fixed = meteorsAt(frame, scene, 16. / 9., 1800, radiant, &scene);
    auto independent = frame;
    independent.options.intervalHours = 12;
    const auto samePreview =
        meteorsAt(independent, scene, 16. / 9., 1800, radiant, &scene);
    require(samePreview.count == fixed.count,
            "natural recurrence does not change an immediate preview");
    for (size_t i = 0; i < fixed.count; ++i)
      require(length(samePreview.segments[i].head - fixed.segments[i].head) <
                  1.e-10,
              "recurrence and preview geometry are independent");
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
    require(meteorsAt(frame, daylight, 16. / 9., 1800, radiant).count ==
                fixed.count,
            "manual trigger retains its meteor stream in daylight");
    frame.options.mode = MeteorMode::Auto;
    frame.seconds = meteorShowerStart(0) + 200;
    require(!meteorsAt(frame, daylight, 16. / 9., 1800, radiant).count,
            "automatic encounters remain night-only inside their window");
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
