#include "Observatory.hpp"
#include "../../core/Output.hpp"
#include "../../core/hyprlock.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/Log.hpp"
#include "../Renderer.hpp"
#include <algorithm>
#include <hyprlang.hpp>
#include <linux/input-event-codes.h>
CObservatory::~CObservatory() {
  if (timer)
    timer->cancel();
}
void CObservatory::configure(
    const std::unordered_map<std::string, std::any> &props,
    const SP<COutput> &p) {
  owner = p;
  viewport = p->getViewport();
  output = p->stringPort;
  fps = std::clamp<int>(std::any_cast<Hyprlang::INT>(props.at("fps")), 1, 60);
  framePacer = observatory::FramePacer(fps);
  day = std::clamp<int>(std::any_cast<Hyprlang::INT>(props.at("day_seconds")),
                        30, 86400);
  lightningBrightness =
      std::any_cast<Hyprlang::FLOAT>(props.at("lightning_brightness"));
  auroraBrightness =
      std::any_cast<Hyprlang::FLOAT>(props.at("aurora_brightness"));
  time = observatory::sceneClockNow();
  elapsed = 0;
  last = std::chrono::steady_clock::now();
}
CBox CObservatory::getBoundingBoxWl() const {
  // Upstream hit-testing uses framebuffer pixels. Pointer input is logical.
  if (!controls)
    return {};
  const auto p = owner.lock();
  if (!p || !p->m_sessionLockSurface)
    return {};
  const double scale = p->m_sessionLockSurface->fractionalScale;
  observatory::DebugLayout layout(viewport.x / scale, viewport.y / scale);
  auto rect = controls->menu.open() ? layout.panel
                                    : (controls->menu.promptVisible(realElapsed)
                                           ? layout.prompt(viewport.x / scale)
                                           : layout.corner(viewport.x / scale));
  return {rect.x * scale, rect.y * scale, rect.width * scale,
          rect.height * scale};
}
void CObservatory::onClick(uint32_t button, bool down, const Vector2D &pos) {
  if (button != BTN_LEFT || !controls)
    return;
  const auto p = owner.lock();
  if (!p || !p->m_sessionLockSurface)
    return;
  pointerDown = down;
  const double scale = p->m_sessionLockSurface->fractionalScale;
  controls->menu.pointer(viewport.x / scale, viewport.y / scale, pos.x, pos.y,
                         down, realElapsed);
  g_pHyprlock->renderOutput(output);
}
bool CObservatory::draw(const SRenderData &data) {
  const auto frameStart = std::chrono::steady_clock::now();
  if (!failed) {
    try {
      if (!renderer) {
        renderer = std::make_unique<observatory::Renderer>(
            observatory::Renderer::defaultDataDirectory(),
            observatory::StarfieldOptions{}, observatory::SurfaceOptions{true});
        controls =
            std::make_unique<observatory::DebugOverlay>(time, day, false);
        auto quality = observatory::GraphicsSettings{};
        quality.fps = fps;
        controls->menu.graphics.initialize(quality);
      }
      auto now = std::chrono::steady_clock::now();
      double dt = std::chrono::duration<double>(now - last).count();
      last = now;
      // Catch up after a suspended output; dropping elapsed time desynchronizes
      // the sky from a freshly opened preview or another monitor.
      // Accumulate a small elapsed value instead of rounding every frame
      // into a Unix-scale number.
      elapsed += dt * data.motionScale;
      realElapsed += dt;
      const auto p = owner.lock();
      const auto focused = g_pHyprlock->m_focusedOutput.lock();
      const double scale = p && p->m_sessionLockSurface
                               ? p->m_sessionLockSurface->fractionalScale
                               : 1.;
      const bool focusedHere = p && focused == p;
      if (!focusedHere)
        pointerDown = false;
      const auto pos = g_pHyprlock->m_vMouseLocation;
      controls->menu.pointer(viewport.x / scale, viewport.y / scale,
                             focusedHere ? pos.x : -1, focusedHere ? pos.y : -1,
                             pointerDown, realElapsed, focusedHere);
      // The debug clock follows normal lock motion. Unlock deceleration is
      // applied independently so neither debug controls nor pause affect PAM.
      const double seconds =
          controls->time(realElapsed) + elapsed - realElapsed;
      if (qualityRevision != controls->menu.graphics.revision) {
        glFinish();
        renderer.reset();
        renderer = std::make_unique<observatory::Renderer>(
            observatory::Renderer::defaultDataDirectory(),
            observatory::StarfieldOptions{}, observatory::SurfaceOptions{true},
            controls->menu.graphics.applied);
        qualityRevision = controls->menu.graphics.revision;
        fps = controls->menu.graphics.applied.fps;
        framePacer = observatory::FramePacer(fps);
      }
      renderer->setWeatherPreset(controls->menu.weather());
      renderer->setMeteorFrame(
          {controls->menu.meteorOptions.mode == observatory::MeteorMode::Preview
               ? controls->menu.meteorTime(realElapsed)
               : seconds,
           1. / fps, controls->menu.meteorOptions});
      renderer->render(viewport.x, viewport.y, seconds, day, data.opacity, true,
                       true,
                       {seconds, std::max(.001, double(data.motionScale) / fps),
                        true, true, lightningBrightness, auroraBrightness});
      controls->menu.meteorDark = renderer->meteorDarkEnough();
      controls->draw(viewport.x, viewport.y, viewport.x / scale,
                     viewport.y / scale, realElapsed, data.opacity);
    } catch (const std::exception &e) {
      Debug::log(ERR, "Observatory: {}; using opaque fallback", e.what());
      failed = true;
      controls.reset();
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
        framePacer.delay(frameStart, std::chrono::steady_clock::now()),
        [ref = weakSelf](auto, auto) {
          if (auto self = ref.lock()) {
            self->timer.reset();
            g_pHyprlock->renderOutput(self->output);
          }
        },
        nullptr);
  return false;
}
