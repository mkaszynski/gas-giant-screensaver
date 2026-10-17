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
    scale = std::min({1., width / 640., height / 180.});
    panel = {16 * scale, 16 * scale, std::min(width - 32 * scale, 560 * scale),
             220 * scale};
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
  DebugRect weather(int index) const {
    return {panel.x + (8 + (index % 3) * 181) * scale,
            panel.y + (120 + (index / 3) * 34) * scale, 173 * scale,
            26 * scale};
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
