#pragma once
#include "debug_timeline.hpp"
#include "graphics_quality.hpp"
#include "meteor_control.hpp"
#include "weather_control.hpp"
#include <array>
namespace observatory {
// Input/state only: shared by the GLFW preview and native Wayland lock widget.
// All coordinates are logical window pixels. No input is sent to
// authentication.
class DebugMenu {
public:
  DebugMenu(double start, double daySeconds, double yearSeconds,
            double routeSeconds, bool open = false)
      : timeline(start, open ? routeSeconds : 1.e12), day(daySeconds),
        year(yearSeconds), route(routeSeconds), meteorTimeline(start, 1.e12),
        open_(open), initialized(open) {}
  void pointer(double width, double height, double x, double y, bool down,
               double real, bool present = true) {
    const double previous = time(real);
    DebugLayout layout(width, height);
    down = down && present;
    if (present && (layout.corner(width).contains(x, y) ||
                    (promptUntil >= 0 && layout.prompt(width).contains(x, y))))
      promptUntil = real + 1.5;
    else if (real >= promptUntil)
      promptUntil = -1;
    const bool clicked = down && !mouseDown;
    if (!open_) {
      if (clicked && promptVisible(real) &&
          layout.prompt(width).contains(x, y)) {
        show(true, real);
      }
    } else if (graphics.open) {
      if (clicked) {
        if (layout.close().contains(x, y))
          open_ = false;
        else if (layout.graphicsBack().contains(x, y))
          graphics.open = false;
        else if (layout.qualitySlider().contains(x, y))
          graphics.dragging = true;
        else if (layout.graphicsApply().contains(x, y))
          graphics.apply();
        else if (layout.graphicsButton().contains(x, y)) {
          auto &q = graphics.draft;
          q.textureSize = q.textureSize == 1024   ? 2048
                          : q.textureSize == 2048 ? 4096
                                                  : 1024;
          q.preset = GraphicsPreset::Custom;
        } else if (layout.frameCap().contains(x, y)) {
          graphics.draft.fps = graphics.draft.fps == 15   ? 30
                               : graphics.draft.fps == 30 ? 60
                                                          : 15;
          graphics.draft.preset = GraphicsPreset::Custom;
        } else if (layout.cloudQuality().contains(x, y)) {
          graphics.draft.clouds = graphics.draft.clouds == 1 ? .5 : 1;
          graphics.draft.preset = GraphicsPreset::Custom;
        } else if (layout.resolution().contains(x, y)) {
          const auto r = layout.resolution();
          graphics.draft.height =
              360 + 120 * std::clamp(int((x - r.x) / r.width * 6 + .5), 0, 6);
          graphics.draft.preset = GraphicsPreset::Custom;
        } else
          for (int i = 0; i < 8; ++i)
            if (layout.feature(i).contains(x, y))
              graphics.toggle(i);
      }
      if (graphics.dragging) {
        auto r = layout.qualitySlider();
        if (present)
          graphics.select(std::clamp((x - r.x) / r.width, 0., 1.));
        if (!down)
          graphics.dragging = false;
      }
    } else if (clicked) {
      if (layout.close().contains(x, y))
        open_ = false;
      else if (layout.graphicsButton().contains(x, y))
        graphics.open = true;
      else if (layout.slider.contains(x, y))
        timeline.beginDrag(layout.fraction(x), real);
      else if (layout.button(0).contains(x, y))
        timeline.play(!timeline.playing(), real);
      else if (layout.button(1).contains(x, y))
        timeline.range(day, real);
      else if (layout.button(2).contains(x, y))
        timeline.range(route, real);
      else if (layout.button(3).contains(x, y))
        timeline.range(year, real);
      else if (layout.button(4).contains(x, y))
        timeline.seek(0, real);
      else if (layout.meteorMode(0).contains(x, y))
        meteorOptions.mode = MeteorMode::Auto;
      else if (layout.meteorMode(1).contains(x, y)) {
        meteorOptions.mode = MeteorMode::Preview;
        restartMeteors(real);
      } else if (layout.meteorMode(2).contains(x, y))
        meteorOptions.mode = MeteorMode::Off;
      else if (meteorOptions.mode != MeteorMode::Off &&
               layout.meteorSlider().contains(x, y))
        meteorDragging = true;
      else if (meteorOptions.mode != MeteorMode::Off &&
               layout.meteorIntervalSlider().contains(x, y))
        meteorIntervalDragging = true;
      else
        for (int i = 0; i < int(weatherPresetNames.size()); ++i)
          if (layout.weather(i).contains(x, y))
            weather_ = static_cast<WeatherPreset>(i);
    }
    if (timeline.dragging()) {
      if (present)
        timeline.dragTo(layout.fraction(x));
      if (!down)
        timeline.endDrag(real);
    }
    if (meteorDragging) {
      if (present)
        meteorOptions.rate =
            meteorMinRate *
            std::pow(meteorMaxRate / meteorMinRate, layout.meteorFraction(x));
      if (!down)
        meteorDragging = false;
    }
    if (meteorIntervalDragging) {
      if (present)
        meteorOptions.intervalHours =
            .5 * std::pow(24., layout.meteorIntervalFraction(x));
      if (!down)
        meteorIntervalDragging = false;
    }
    mouseDown = down;
    syncMeteors(previous, real);
  }
  // Preview shortcuts only. The lock screen keeps keyboard input for PAM.
  void keys(std::array<bool, 4> pressed, double real) {
    const double previous = time(real);
    for (int i = 0; i < 4; ++i) {
      if (open_ && !graphics.open && pressed[i] && !previousKeys[i] &&
          !timeline.dragging() && !meteorDragging && !meteorIntervalDragging) {
        switch (i) {
        case 0:
          timeline.play(!timeline.playing(), real);
          break;
        case 1:
          timeline.seek(0, real);
          break;
        case 2:
          timeline.seek(timeline.fraction(real) - .001, real);
          break;
        case 3:
          timeline.seek(timeline.fraction(real) + .001, real);
          break;
        }
      }
      previousKeys[i] = pressed[i];
    }
    syncMeteors(previous, real);
  }
  void show(bool open, double real) {
    if (open && !initialized) {
      timeline.range(route, real);
      initialized = true;
    }
    open_ = open;
  }
  bool open() const { return open_; }
  bool promptVisible(double real) const { return !open_ && real < promptUntil; }
  WeatherPreset weather() const { return weather_; }
  double time(double real) const { return timeline.time(real); }
  double meteorTime(double real) const {
    return meteorOptions.mode == MeteorMode::Preview ? meteorTimeline.time(real)
                                                     : time(real);
  }
  GraphicsControl graphics;
  MeteorOptions meteorOptions;
  bool meteorDark = false;
  DebugTimeline timeline;
  const double day, year, route;

private:
  void restartMeteors(double real) {
    meteorOptions.previewStart = meteorTimeline.time(real);
    ++meteorOptions.restart;
  }
  void syncMeteors(double previous, double real) {
    // Manual events retain real speed across arbitrary sky/route seeks and
    // timeline endpoints. Explicit pause and scrubbing still freeze them.
    meteorTimeline.play(timeline.playing() && !timeline.dragging(), real);
    if (meteorOptions.mode == MeteorMode::Preview &&
        std::abs(time(real) - previous) > 1.e-6)
      restartMeteors(real);
  }
  DebugTimeline meteorTimeline;
  bool open_, initialized, mouseDown = false, meteorDragging = false,
                           meteorIntervalDragging = false;
  double promptUntil = -1;
  WeatherPreset weather_ = WeatherPreset::Auto;
  std::array<bool, 4> previousKeys{};
};
} // namespace observatory
