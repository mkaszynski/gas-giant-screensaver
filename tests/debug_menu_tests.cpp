#include "debug_menu.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace observatory;
void require(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
void near(double a, double b, const char *why) {
  require(std::abs(a - b) < 1.e-6, why);
}
int main() {
  try {
    for (auto size :
         {std::array<double, 2>{1280, 720}, {640, 360}, {320, 180}}) {
      double w = size[0], h = size[1];
      DebugLayout layout(w, h);
      DebugMenu menu(1790900000, 1800, 1772928, 259200);
      require(!menu.open() && !menu.promptVisible(0), "normal launch is clean");
      menu.keys({true, false, false, false}, 0);
      near(menu.time(300000), 1791200000,
           "hidden clock continues beyond three days");
      menu.pointer(w, h, w - 1, 1, false, 300000, false);
      require(!menu.promptVisible(300000),
              "stale pointer on another output cannot reveal prompt");
      menu.pointer(w, h, w - 1, 1, false, 300000);
      require(menu.promptVisible(300000) && !menu.open(),
              "corner reveals invitation, not menu");
      auto prompt = layout.prompt(w);
      menu.pointer(w, h, prompt.x + 10 * layout.scale,
                   prompt.y + 10 * layout.scale, false, 300001);
      require(menu.promptVisible(300002),
              "invitation stays reachable away from corner");
      menu.pointer(w, h, w / 2, h - 1, false, 300003);
      require(!menu.promptVisible(300003), "unused invitation expires");
      menu.pointer(w, h, w - 1, 1, false, 300004);
      menu.pointer(w, h, prompt.x + 10 * layout.scale,
                   prompt.y + 10 * layout.scale, true, 300004);
      menu.pointer(w, h, prompt.x + 10 * layout.scale,
                   prompt.y + 10 * layout.scale, false, 300004);
      require(menu.open(), "invitation opens controls");
      near(menu.time(300004), 1791200004,
           "opening preserves live clock exactly");
      auto click = [&](DebugRect r, double t) {
        menu.pointer(w, h, r.x + r.width / 2, r.y + r.height / 2, true, t);
        menu.pointer(w, h, r.x + r.width / 2, r.y + r.height / 2, false, t);
      };
      for (int i = 1; i < 6; ++i) {
        click(layout.weather(i), 300005 + i);
        require(int(menu.weather()) == i,
                "every weather button selects its named preset");
      }
      click(layout.weather(0), 300012);
      require(menu.weather() == WeatherPreset::Auto,
              "automatic weather can be restored");
      click(layout.button(0), 300013);
      double paused = menu.time(300013);
      near(menu.time(300020), paused, "pause works through shared menu");
      menu.pointer(w, h, layout.slider.x + layout.slider.width * .25,
                   layout.slider.y + 1, true, 300021);
      menu.pointer(w, h, layout.slider.x + layout.slider.width * .75,
                   layout.slider.y + 1, true, 300022);
      near(menu.time(300022), paused,
           "drag does not rebuild the scene until release");
      menu.pointer(w, h, -1, -1, false, 300023, false);
      near(menu.timeline.fraction(300023), .75,
           "focus loss commits the last drag target, not zero");
      require(!menu.timeline.dragging(), "focus loss releases capture");
      click(layout.meteorMode(1), 300024);
      require(menu.meteorOptions.mode == MeteorMode::Preview &&
                  menu.meteorOptions.previewStart == menu.meteorTime(300024),
              "preview starts at the current activity epoch");
      auto slider = layout.meteorSlider();
      menu.pointer(w, h, slider.x, slider.y + 1, true, 300024);
      menu.pointer(w, h, slider.x + slider.width + 100, slider.y + 1, true,
                   300024);
      menu.pointer(w, h, slider.x + slider.width + 100, slider.y + 1, false,
                   300024);
      near(menu.meteorOptions.rate, 1800,
           "frequency slider clamps to its upper limit");
      auto gap = layout.meteorIntervalSlider();
      menu.pointer(w, h, gap.x, gap.y + 1, true, 300024);
      menu.pointer(w, h, gap.x + gap.width + 100, gap.y + 1, true, 300024);
      menu.pointer(w, h, gap.x + gap.width + 100, gap.y + 1, false, 300024);
      near(menu.meteorOptions.intervalHours, 12,
           "shower frequency slider controls the mean encounter gap");
      near(menu.meteorOptions.rate, 1800,
           "encounter frequency is independent of meteor rate");
      click(layout.meteorMode(2), 300024);
      require(menu.meteorOptions.mode == MeteorMode::Off,
              "showers can be disabled");
      click(layout.meteorMode(0), 300024);
      require(menu.meteorOptions.mode == MeteorMode::Auto,
              "natural rarity can be restored");
      click(layout.close(), 300024);
      require(!menu.open(), "close hides controls");
      near(menu.time(300030), menu.time(300024),
           "closing preserves the selected clock state");
      menu.keys({}, 300030);
      menu.keys({true, false, false, false}, 300031);
      require(!menu.timeline.playing(),
              "hidden shortcuts do not control scene");
      // A slow frame can keep an invitation on screen beyond its timer. Moving
      // directly onto the displayed button must still allow the click.
      DebugMenu stalled(900, 1800, 1772928, 259200);
      stalled.pointer(w, h, w - 1, 1, false, 0);
      stalled.pointer(w, h, prompt.x + 10 * layout.scale,
                      prompt.y + 10 * layout.scale, true, 10);
      require(stalled.open(),
              "visible invitation remains clickable after a long frame");
      DebugMenu immediate(900, 1800, 1772928, 259200, true);
      require(immediate.open(), "--debug opens directly");
    }
    std::cout << "Debug menu: hover, expiry, open/close, scaled input, "
                 "weather, clock and focus pass\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
