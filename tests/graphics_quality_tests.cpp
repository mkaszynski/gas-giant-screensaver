#include "debug_menu.hpp"
#include "texture_quality.hpp"
#include <iostream>
#include <stdexcept>
using namespace observatory;
void require(bool yes, const char *message) {
  if (!yes)
    throw std::runtime_error(message);
}
int main() {
  try {
    require(recommendedGraphics(0, 0, false) == GraphicsPreset::Low,
            "unknown machines start low");
    require(recommendedGraphics(4, 16, false) == GraphicsPreset::Low,
            "constrained memory starts low");
    require(recommendedGraphics(32, 32, true) == GraphicsPreset::Low,
            "software renderer starts low");
    require(recommendedGraphics(8, 8, false) == GraphicsPreset::Medium,
            "coarse browser RAM hint stays conservative");
    require(recommendedGraphics(32, 16, false) == GraphicsPreset::High,
            "large native machine may start high");
    int previous = 0;
    const auto low = GraphicsSettings::forPreset(GraphicsPreset::Low);
    require(low.presentationSize(1280, 720, false) == std::array{640, 360} &&
                low.presentationSize(1280, 720, true) ==
                    std::array{1280, 720} &&
                low.size(1280, 720) == std::array{640, 360},
            "readable menu presentation preserves the low scene budget");
    for (int i = 0; i < 3; ++i) {
      auto q = GraphicsSettings::forPreset(GraphicsPreset(i));
      for (auto wh : {std::array{3840, 2160}, std::array{390, 844},
                      std::array{30000, 600}}) {
        auto size = q.size(wh[0], wh[1]);
        require(size[0] * size[1] <= q.height * q.height * 16 / 9,
                "pixel budget bounded at all aspects");
        require(size[0] <= 4096 && size[1] <= 4096,
                "texture dimensions bounded");
        require(std::abs(double(size[0]) / size[1] - double(wh[0]) / wh[1]) <
                    2. / size[1],
                "aspect preserved");
      }
      require(q.height > previous, "presets increase resolution monotonically");
      previous = q.height;
    }
    unsigned w = 2, h = 2;
    std::vector<unsigned char> distance{0, 255, 0, 255, 1, 0, 0, 255,
                                        0, 255, 0, 255, 1, 0, 0, 255};
    reduceTexture(distance, w, h, 1, true);
    require(w == 1 && h == 1 && distance[0] * 256 + distance[1] == 256,
            "packed depth survives a byte carry when reducing textures");
    w = h = 2;
    std::vector<unsigned char> color(16, 0);
    for (int c = 0; c < 4; ++c)
      color[c] = 200;
    reduceTexture(color, w, h, 1, false);
    require(color[0] == 50, "color texels use a filtered average");
    w = 2;
    h = 8;
    color.assign(w * h * 4, 127);
    reduceTexture(color, w, h, 2, false);
    require(w == 1 && h == 2 && color[0] == 127,
            "portrait textures respect the same maximum dimension");
    GraphicsSettings stripped;
    stripped.features.fill(false);
    require(stripped.assetGroups() == "[\"base\",\"terrain\"]",
            "disabled world and sky never requested");
    DebugMenu menu(510, 1800, 10000, 259200, true);
    DebugLayout layout(1280, 720);
    double now = 0;
    auto click = [&](DebugRect r) {
      menu.pointer(1280, 720, r.x + r.width / 2, r.y + r.height / 2, true,
                   now += .01);
      menu.pointer(1280, 720, r.x + r.width / 2, r.y + r.height / 2, false,
                   now += .01);
    };
    click(layout.graphicsButton());
    require(menu.graphics.open, "graphics opens from shared menu");
    click(layout.feature(0));
    require(!menu.graphics.draft.features[0], "custom switch changes draft");
    require(menu.graphics.applied.features[0] && menu.graphics.revision == 0,
            "edits cannot trigger allocation before Apply");
    click(layout.graphicsApply());
    require(!menu.graphics.applied.features[0] && menu.graphics.revision == 1,
            "Apply commits once");
    click(layout.graphicsApply());
    require(menu.graphics.revision == 1, "unchanged Apply does not rebuild");
    auto slider = layout.qualitySlider();
    menu.pointer(1280, 720, slider.x, slider.y + 4, true, now += .01);
    menu.pointer(1280, 720, slider.x + slider.width, slider.y + 4, true,
                 now += .01);
    menu.pointer(1280, 720, slider.x + slider.width, slider.y + 4, false,
                 now += .01);
    require(menu.graphics.draft.preset == GraphicsPreset::High &&
                menu.graphics.revision == 1,
            "drag selects preset without rebuild storms");
    click(layout.graphicsBack());
    require(!menu.graphics.open && menu.open(),
            "back preserves timeline panel");
    std::cout << "Quality policy, aspect budgets, custom edits and staged "
                 "Apply passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
