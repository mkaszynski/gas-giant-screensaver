#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace observatory {
// Preview-only clock. A drag previews a target; release commits a single seek
// so scrubbing cannot trigger expensive terrain rebuilds for every mouse move.
class DebugTimeline {
public:
  explicit DebugTimeline(double origin, double span)
      : origin_(origin), span_(span) {
    if (!std::isfinite(origin) || !std::isfinite(span) || span <= 0)
      throw std::invalid_argument("Invalid debug timeline range");
  }
  double offset(double real) const {
    return std::clamp(offset_ + (playing_ && !dragging_ ? real - anchor_ : 0.),
                      0., span_);
  }
  double time(double real) const { return origin_ + offset(real); }
  double target(double real) const {
    return origin_ + (dragging_ ? pending_ : offset(real));
  }
  double fraction(double real) const {
    return (target(real) - origin_) / span_;
  }
  double span() const { return span_; }
  bool playing() const { return playing_; }
  bool dragging() const { return dragging_; }
  void play(bool on, double real) {
    hold(real);
    playing_ = on;
  }
  void seek(double fraction, double real) {
    offset_ = span_ * std::clamp(fraction, 0., 1.);
    anchor_ = real;
  }
  void range(double span, double real) {
    if (!std::isfinite(span) || span <= 0)
      throw std::invalid_argument("Invalid debug timeline range");
    origin_ = time(real);
    offset_ = 0.;
    anchor_ = real;
    span_ = span;
    dragging_ = false;
  }
  void beginDrag(double fraction, double real) {
    hold(real);
    dragging_ = true;
    dragTo(fraction);
  }
  void dragTo(double fraction) {
    pending_ = span_ * std::clamp(fraction, 0., 1.);
  }
  void endDrag(double real) {
    seek(pending_ / span_, real);
    dragging_ = false;
  }

private:
  void hold(double real) {
    offset_ = offset(real);
    anchor_ = real;
  }
  double origin_, span_, offset_ = 0, anchor_ = 0, pending_ = 0;
  bool playing_ = true, dragging_ = false;
};

struct DebugRect {
  double x, y, width, height;
  bool contains(double px, double py) const {
    return px >= x && px <= x + width && py >= y && py <= y + height;
  }
};
struct DebugLayout {
  double scale;
  DebugRect panel, slider;
  explicit DebugLayout(double width, double height) {
    scale = std::min({1., width / 640., height / 400.});
    panel = {16 * scale, 16 * scale, std::min(width - 32 * scale, 560 * scale),
             360 * scale};
    slider = {panel.x + 8 * scale, panel.y + 71 * scale,
              panel.width - 16 * scale, 18 * scale};
  }
  DebugRect button(int index) const {
    return {panel.x + (8 + index * 78) * scale, panel.y + 10 * scale,
            70 * scale, 24 * scale};
  }
  DebugRect close() const {
    return {panel.x + panel.width - 38 * scale, panel.y + 10 * scale,
            30 * scale, 24 * scale};
  }
  DebugRect graphicsButton() const {
    return {panel.x + 406 * scale, panel.y + 10 * scale, 108 * scale,
            24 * scale};
  }
  DebugRect graphicsBack() const {
    return {panel.x + 8 * scale, panel.y + 10 * scale, 110 * scale, 24 * scale};
  }
  DebugRect qualitySlider() const {
    return {panel.x + 20 * scale, panel.y + 70 * scale,
            panel.width - 40 * scale, 20 * scale};
  }
  DebugRect feature(int i) const {
    return {panel.x + (8 + (i % 2) * 276) * scale,
            panel.y + (126 + (i / 2) * 34) * scale, 268 * scale, 26 * scale};
  }
  DebugRect resolution() const {
    return {panel.x + 8 * scale, panel.y + 290 * scale, 250 * scale,
            18 * scale};
  }
  DebugRect frameCap() const {
    return {panel.x + 280 * scale, panel.y + 280 * scale, 85 * scale,
            30 * scale};
  }
  DebugRect cloudQuality() const {
    return {panel.x + 373 * scale, panel.y + 280 * scale, 175 * scale,
            30 * scale};
  }
  DebugRect graphicsApply() const {
    return {panel.x + 8 * scale, panel.y + 326 * scale, 110 * scale,
            26 * scale};
  }
  DebugRect weather(int index) const {
    return {panel.x + (8 + (index % 3) * 181) * scale,
            panel.y + (120 + (index / 3) * 34) * scale, 173 * scale,
            26 * scale};
  }
  DebugRect meteorMode(int index) const {
    return {panel.x + (8 + index * 181) * scale, panel.y + 244 * scale,
            173 * scale, 26 * scale};
  }
  DebugRect meteorSlider() const {
    return {panel.x + panel.width * .5 + 8 * scale, panel.y + 305 * scale,
            (panel.width - 32 * scale) * .5, 18 * scale};
  }
  DebugRect meteorIntervalSlider() const {
    return {panel.x + 8 * scale, panel.y + 305 * scale,
            (panel.width - 32 * scale) * .5, 18 * scale};
  }
  double meteorIntervalFraction(double x) const {
    const auto bar = meteorIntervalSlider();
    return std::clamp((x - bar.x) / bar.width, 0., 1.);
  }
  double meteorFraction(double x) const {
    const auto bar = meteorSlider();
    return std::clamp((x - bar.x) / bar.width, 0., 1.);
  }
  DebugRect prompt(double width) const {
    return {width - 236 * scale, 12 * scale, 224 * scale, 32 * scale};
  }
  DebugRect corner(double width) const {
    return {width - 48 * scale, 0, 48 * scale, 48 * scale};
  }
  double fraction(double x) const {
    return std::clamp((x - slider.x) / slider.width, 0., 1.);
  }
};
} // namespace observatory
