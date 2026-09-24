#include "Observatory.hpp"
#include "../../core/Output.hpp"
#include "../../core/hyprlock.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/Log.hpp"
#include "../Renderer.hpp"
#include <algorithm>
#include <hyprlang.hpp>
CObservatory::~CObservatory() {
  if (timer)
    timer->cancel();
}
void CObservatory::configure(
    const std::unordered_map<std::string, std::any> &props,
    const SP<COutput> &p) {
  viewport = p->getViewport();
  output = p->stringPort;
  fps = std::clamp<int>(std::any_cast<Hyprlang::INT>(props.at("fps")), 1, 60);
  day = std::clamp<int>(std::any_cast<Hyprlang::INT>(props.at("day_seconds")),
                        30, 86400);
  lightningBrightness =
      std::any_cast<Hyprlang::FLOAT>(props.at("lightning_brightness"));
  auroraBrightness =
      std::any_cast<Hyprlang::FLOAT>(props.at("aurora_brightness"));
  time = std::fmod(std::chrono::duration<double>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count(),
                   day * 365.);
  last = std::chrono::steady_clock::now();
}
bool CObservatory::draw(const SRenderData &data) {
  if (!failed) {
    try {
      if (!renderer)
        renderer = std::make_unique<observatory::Renderer>(
            observatory::Renderer::defaultDataDirectory());
      auto now = std::chrono::steady_clock::now();
      double dt = std::chrono::duration<double>(now - last).count();
      last = now;
      time += std::min(dt, .25) * data.motionScale;
      renderer->render(viewport.x, viewport.y, time, day, data.opacity, true,
                       true,
                       {time, std::max(.001, double(data.motionScale) / fps),
                        true, true, lightningBrightness, auroraBrightness});
    } catch (const std::exception &e) {
      Debug::log(ERR, "Observatory: {}; using opaque fallback", e.what());
      failed = true;
      renderer.reset();
    }
  }
  if (failed)
    g_pRenderer->renderRect(CBox{0, 0, viewport.x, viewport.y},
                            CHyprColor(0.F, 0.F, 0.F, 1.F), 0);
  // One pending timer per output. Draw is driven by compositor frame callbacks;
  // when an output is off, it cannot perpetually schedule new render work.
  if (!failed && !timer)
    timer = g_pHyprlock->addTimer(
        std::chrono::milliseconds(1000 / fps),
        [ref = weakSelf](auto, auto) {
          if (auto self = ref.lock()) {
            self->timer.reset();
            g_pHyprlock->renderOutput(self->output);
          }
        },
        nullptr);
  return false;
}
