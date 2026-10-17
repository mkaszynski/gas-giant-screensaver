#include "renderer.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <memory>

using namespace observatory;
namespace {
std::unique_ptr<Renderer> renderer;
double scene = 510, weather = 1790900000, lastTick = 0, lastRender = -1e6;
bool paused = false, synchronize = false, hidden = false, redraw = true;
int drawnWidth = 0, drawnHeight = 0;
int frames = 0;
GLsync submitted = nullptr;
struct SubmittedFrame {
  int count, width, height, builds;
  double time, cost;
  bool pending, paused;
} completed{};

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
void frame() {
  try {
    const double now = emscripten_get_now();
    const double elapsed = std::max(0., (now - lastTick) / 1000.);
    if (hidden) {
      lastTick = now;
      return;
    }
    // Keep at most one GPU frame outstanding. Never busy-wait on a slow GPU
    // or let rAF submissions accumulate behind shader compilation/rendering.
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
    lastTick = now;
    if (!paused) {
      scene += elapsed;
      weather += elapsed;
    }
    int width, height;
    emscripten_get_canvas_element_size("#scene", &width, &height);
    // A paused scene needs no repeated GPU work, but seeking/resizing redraws.
    if (paused && !synchronize && !redraw && width == drawnWidth && height == drawnHeight)
      return;
    // rAF follows the display; cap work at the desktop's 30 fps without drift.
    if (now + .1 < lastRender + 1000. / 30)
      return;
    lastRender = now - std::fmod(std::max(0., now - lastRender), 1000. / 30);
    if (synchronize)
      glFinish();
    const double start = emscripten_get_now();
    renderer->render(width, height, scene, 1800, 1, true, true,
                     {weather, 1. / 30, true, true, 5, 1000}, weather);
    if (synchronize)
      glFinish();
    const auto error = glGetError();
    if (error != GL_NO_ERROR)
      throw std::runtime_error("WebGL error " + std::to_string(error));
    redraw = false;
    drawnWidth = width;
    drawnHeight = height;
    completed = {++frames, width, height,
                 static_cast<int>(renderer->surfaceBuildsStarted()), scene,
                 emscripten_get_now() - start, renderer->surfaceBuildPending(), paused};
    submitted = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!submitted)
      throw std::runtime_error("Cannot track the rendered GPU frame.");
    glFlush();

  } catch (const std::exception &error) {
    emscripten_cancel_main_loop();
    reportError(error.what());
  }
}
} // namespace
extern "C" {
EMSCRIPTEN_KEEPALIVE void preview_pause(int value) {
  paused = value != 0;
  redraw = true;
}
EMSCRIPTEN_KEEPALIVE void preview_visibility(int value) {
  hidden = value != 0;
  lastTick = emscripten_get_now();
}
EMSCRIPTEN_KEEPALIVE void preview_seek(double seconds) {
  if (std::isfinite(seconds) && seconds >= 0 && seconds <= 1e10) {
    weather += seconds - scene;
    scene = seconds;
    redraw = true;
  }
}
}
int main() {
  try {
    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion = 2;
    attributes.alpha = false;
    attributes.antialias = false; // The shared terrain renderer owns MSAA.
    attributes.depth = false;
    const auto context = emscripten_webgl_create_context("#scene", &attributes);
    if (context <= 0 || emscripten_webgl_make_context_current(context) != EMSCRIPTEN_RESULT_SUCCESS)
      throw std::runtime_error("This preview requires WebGL 2.");
    if (!emscripten_webgl_enable_extension(context, "EXT_color_buffer_float"))
      throw std::runtime_error("This GPU does not support floating-point WebGL rendering.");
    emscripten_webgl_enable_extension(context, "OES_texture_float_linear");
    emscripten_webgl_enable_extension(context, "EXT_float_blend");
    scene = option("time", 510, 0, 1e10);
    weather = option("weather", 1790900000, 0, 1e10);
    paused = option("paused", 0, 0, 1) != 0;
    synchronize = option("benchmark", 0, 0, 1) != 0;
    SurfaceOptions surface{option("surface", 1, 0, 1) != 0};
    StarfieldOptions sky;
    sky.stars = option("stars", 1, 0, 1) != 0;
    sky.galaxy = option("galaxy", 1, 0, 1) != 0;
    surface.speed = option("speed", 1, 0, 10000);
    surface.offset = option("travel", 162000, 0, 1e10) - scene * surface.speed;
    renderer = std::make_unique<Renderer>("/", sky, surface);
    lastTick = emscripten_get_now();
    emscripten_set_main_loop(frame, 0, false);
    return 0;
  } catch (const std::exception &error) {
    reportError(error.what());
    return 1;
  }
}
