#pragma once
#include "../../core/Timer.hpp"
#include "../../observatory/debug_overlay.hpp"
#include "../../observatory/frame_pacing.hpp"
#include "../../observatory/renderer.hpp"
#include "IWidget.hpp"
#include <memory>
class CObservatory : public IWidget {
public:
  ~CObservatory();
  void registerSelf(const ASP<CObservatory> &self) { weakSelf = self; }
  void configure(const std::unordered_map<std::string, std::any> &,
                 const SP<COutput> &) override;
  bool draw(const SRenderData &) override;
  CBox getBoundingBoxWl() const override;
  void onClick(uint32_t button, bool down, const Vector2D &pos) override;
  bool wantsPointerRelease() const override { return true; }

private:
  AWP<CObservatory> weakSelf;
  ASP<CTimer> timer;
  std::unique_ptr<observatory::Renderer> renderer;
  std::unique_ptr<observatory::DebugOverlay> controls;
  WP<COutput> owner;
  bool pointerDown = false;
  double realElapsed = 0;
  Vector2D viewport;
  std::string output;
  unsigned qualityRevision = 0;
  int fps = 30, day = 1800;
  observatory::FramePacer framePacer;
  bool failed = false;
  double time = 0, elapsed = 0, lightningBrightness = 5,
         auroraBrightness = 1000;
  std::chrono::steady_clock::time_point last;
};
