#include "debug_overlay.hpp"
#include "renderer.hpp"
#include <algorithm>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <exception>
#include <memory>
#include <sstream>

using namespace observatory;
namespace {
std::unique_ptr<Renderer> renderer;
std::unique_ptr<DebugOverlay> controls;
double sceneStart = 510, weatherOffset = 0, activeTime = 0, lastTick = 0;
double lastRender = -1e6;
bool synchronize = false, hidden = false, redraw = true, drawnPrompt = false;
int drawnWidth = 0, drawnHeight = 0, logicalWidth = 1280, logicalHeight = 720;
int frames = 0, keyMask = 0;
GLsync submitted = nullptr;
struct SubmittedFrame {
  int count, width, height, builds;
  double time, cost;
  bool pending, paused;
} completed{};

// clang-format off: EM_JS bodies contain JavaScript operators, not C++ tokens.
EM_JS(double, option, (const char *name, double fallback, double low, double high), {
  const text = Module.previewOptions[UTF8ToString(name)] ?? null;
  const value = text === null || text.trim() === "" ? NaN : Number(text);
  return Number.isFinite(value) && value >= low && value <= high ? value : fallback;
});
EM_JS(void, reportError, (const char *message), {
  globalThis.previewError(UTF8ToString(message));
});
EM_JS(void, reportFrame, (int count, double time, double cost, int width, int height,
                         int builds, int pending, int paused), {
  globalThis.previewFrame({count, time, cost, width, height, builds, pending: !!pending, paused: !!paused});
});
EM_JS(void, reportControls, (const char *json), {
  globalThis.previewControls(JSON.parse(UTF8ToString(json)));
});
// clang-format on
void advanceClock() {
  const double now = emscripten_get_now();
  if (lastTick > 0 && !hidden)
    activeTime += std::max(0., (now - lastTick) / 1000.);
  lastTick = now;
}
void publishControls() {
  if (!controls)
    return;
  const auto &menu = controls->menu;
  const DebugLayout layout(logicalWidth, logicalHeight);
  auto rect = [](DebugRect r) {
    std::ostringstream out;
    out << "{\"x\":" << r.x << ",\"y\":" << r.y << ",\"width\":" << r.width
        << ",\"height\":" << r.height << "}";
    return out.str();
  };
  auto rects = [&](int count, auto get) {
    std::string result = "[";
    for (int i = 0; i < count; ++i)
      result += (i ? "," : "") + rect(get(i));
    return result + "]";
  };
  std::ostringstream out;
  out << "{\"open\":" << (menu.open() ? "true" : "false")
      << ",\"prompt\":" << (menu.promptVisible(activeTime) ? "true" : "false")
      << ",\"playing\":" << (menu.timeline.playing() ? "true" : "false")
      << ",\"weather\":" << int(menu.weather())
      << ",\"meteorMode\":" << int(menu.meteorOptions.mode)
      << ",\"meteorRate\":" << menu.meteorOptions.rate
      << ",\"meteorInterval\":" << menu.meteorOptions.intervalHours
      << ",\"fraction\":" << menu.timeline.fraction(activeTime)
      << ",\"span\":" << menu.timeline.span()
      << ",\"layout\":{\"panel\":" << rect(layout.panel)
      << ",\"prompt\":" << rect(layout.prompt(logicalWidth))
      << ",\"close\":" << rect(layout.close())
      << ",\"slider\":" << rect(layout.slider)
      << ",\"buttons\":" << rects(5, [&](int i) { return layout.button(i); })
      << ",\"weather\":" << rects(6, [&](int i) { return layout.weather(i); })
      << ",\"meteorMode\":"
      << rects(3, [&](int i) { return layout.meteorMode(i); })
      << ",\"meteorSlider\":" << rect(layout.meteorSlider())
      << ",\"meteorIntervalSlider\":" << rect(layout.meteorIntervalSlider())
      << "}}";
  reportControls(out.str().c_str());
}
void frame() {
  try {
    advanceClock();
    if (hidden)
      return;
    if (submitted) {
      const GLenum state = glClientWaitSync(submitted, 0, 0);
      if (state == GL_WAIT_FAILED)
        throw std::runtime_error("Cannot track GPU frame completion.");
      if (state == GL_TIMEOUT_EXPIRED)
        return;
      glDeleteSync(submitted);
      submitted = nullptr;
      reportFrame(completed.count, completed.time, completed.cost,
                  completed.width, completed.height, completed.builds,
                  completed.pending, completed.paused);
    }
    auto &menu = controls->menu;
    menu.keys({bool(keyMask & 1), bool(keyMask & 2), bool(keyMask & 4),
               bool(keyMask & 8)},
              activeTime);
    int width, height;
    emscripten_get_canvas_element_size("#scene", &width, &height);
    const bool prompt = menu.promptVisible(activeTime);
    if (!menu.timeline.playing() && !synchronize && !redraw &&
        width == drawnWidth && height == drawnHeight && prompt == drawnPrompt)
      return;
    const double now = emscripten_get_now();
    if (now + .1 < lastRender + 1000. / 30)
      return;
    lastRender = now - std::fmod(std::max(0., now - lastRender), 1000. / 30);
    if (synchronize)
      glFinish();
    const double start = emscripten_get_now();
    const double scene = menu.time(activeTime), weather = scene + weatherOffset;
    renderer->setWeatherPreset(menu.weather());
    renderer->setMeteorFrame(
        {menu.meteorTime(activeTime), 1. / 30, menu.meteorOptions});
    renderer->render(width, height, scene, 1800, 1, true, true,
                     {weather, 1. / 30, true, true, 5, 1000}, weather);
    menu.meteorDark = renderer->meteorDarkEnough();
    controls->draw(width, height, logicalWidth, logicalHeight, activeTime);
    if (synchronize)
      glFinish();
    const auto error = glGetError();
    if (error != GL_NO_ERROR)
      throw std::runtime_error("WebGL error " + std::to_string(error));
    redraw = false;
    drawnWidth = width;
    drawnHeight = height;
    drawnPrompt = prompt;
    completed = {++frames,
                 width,
                 height,
                 static_cast<int>(renderer->surfaceBuildsStarted()),
                 scene,
                 emscripten_get_now() - start,
                 renderer->surfaceBuildPending(),
                 !menu.timeline.playing()};
    // At most one outstanding frame, including debug draws. Never spin-wait.
    submitted = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!submitted)
      throw std::runtime_error("Cannot track the rendered GPU frame.");
    glFlush();
    publishControls();
  } catch (const std::exception &error) {
    emscripten_cancel_main_loop();
    reportError(error.what());
  }
}
} // namespace
extern "C" {
EMSCRIPTEN_KEEPALIVE void preview_pause(int value) {
  if (!controls)
    return;
  advanceClock();
  controls->menu.timeline.play(value == 0, activeTime);
  redraw = true;
}
EMSCRIPTEN_KEEPALIVE void preview_visibility(int value) {
  advanceClock();
  hidden = value != 0;
}
EMSCRIPTEN_KEEPALIVE void preview_seek(double seconds) {
  if (controls && std::isfinite(seconds) && seconds >= 0 && seconds <= 1e10) {
    advanceClock();
    auto &menu = controls->menu;
    menu.timeline.seek(menu.timeline.fraction(activeTime) +
                           (seconds - menu.time(activeTime)) /
                               menu.timeline.span(),
                       activeTime);
    redraw = true;
  }
}
EMSCRIPTEN_KEEPALIVE void preview_viewport(int width, int height) {
  if (width <= 0 || height <= 0 || width > 32768 || height > 32768)
    return;
  logicalWidth = width;
  logicalHeight = height;
  redraw = true;
  publishControls();
}
EMSCRIPTEN_KEEPALIVE void preview_pointer(double x, double y, int down,
                                          int present) {
  if (!controls)
    return;
  advanceClock();
  auto &menu = controls->menu;
  const bool before = menu.promptVisible(activeTime);
  const bool wasOpen = menu.open();
  menu.pointer(logicalWidth, logicalHeight, x, y, down != 0, activeTime,
               present != 0);
  redraw |= wasOpen || menu.open() || before != menu.promptVisible(activeTime);
  publishControls();
}
EMSCRIPTEN_KEEPALIVE void preview_keys(int mask) {
  if (!controls)
    return;
  advanceClock();
  auto &menu = controls->menu;
  keyMask = mask;
  menu.keys({bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8)},
            activeTime);
  redraw |= menu.open();
  publishControls();
}
EMSCRIPTEN_KEEPALIVE void preview_menu(int open) {
  if (!controls)
    return;
  advanceClock();
  controls->menu.show(open != 0, activeTime);
  redraw = true;
  publishControls();
}
}
int main() {
  try {
    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion = 2;
    attributes.alpha = false;
    attributes.antialias = false;
    attributes.depth = false;
    const auto context = emscripten_webgl_create_context("#scene", &attributes);
    if (context <= 0 || emscripten_webgl_make_context_current(context) !=
                            EMSCRIPTEN_RESULT_SUCCESS)
      throw std::runtime_error("This preview requires WebGL 2.");
    if (!emscripten_webgl_enable_extension(context, "EXT_color_buffer_float"))
      throw std::runtime_error(
          "This GPU does not support floating-point WebGL rendering.");
    emscripten_webgl_enable_extension(context, "OES_texture_float_linear");
    emscripten_webgl_enable_extension(context, "EXT_float_blend");
    sceneStart = option("time", 510, 0, 1e10);
    weatherOffset = option("weather", 1790900000, 0, 1e10) - sceneStart;
    synchronize = option("benchmark", 0, 0, 1) != 0;
    SurfaceOptions surface{option("surface", 1, 0, 1) != 0};
    surface.weather = option("volumetric", 1, 0, 1) != 0;
    StarfieldOptions sky;
    sky.stars = option("stars", 1, 0, 1) != 0;
    sky.galaxy = option("galaxy", 1, 0, 1) != 0;
    surface.speed = option("speed", 1, 0, 10000);
    surface.offset =
        option("travel", 162000, 0, 1e10) - sceneStart * surface.speed;
    renderer = std::make_unique<Renderer>("/", sky, surface);
    controls = std::make_unique<DebugOverlay>(sceneStart, 1800, false, "/");
    controls->menu.timeline.play(option("paused", 0, 0, 1) == 0, 0);
    // Runtime-initialization messages arrive before main. Start the scene's
    // clock here, after asset loading, independently of the worker clock epoch.
    activeTime = 0;
    lastTick = emscripten_get_now();
    publishControls();
    emscripten_set_main_loop(frame, 0, false);
    return 0;
  } catch (const std::exception &error) {
    reportError(error.what());
    return 1;
  }
}
