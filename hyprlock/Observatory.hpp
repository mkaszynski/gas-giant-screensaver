#pragma once
#include "../../core/Timer.hpp"
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

private:
  AWP<CObservatory> weakSelf;
  ASP<CTimer> timer;
  std::unique_ptr<observatory::Renderer> renderer;
  Vector2D viewport;
  std::string output;
  int fps = 30, day = 1800;
  bool failed = false;
  double time = 0, lightningBrightness = 5, auroraBrightness = 1000;
  std::chrono::steady_clock::time_point last;
};
