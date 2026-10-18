#define GLFW_INCLUDE_ES3
#include "debug_overlay.hpp"
#include "frame_pacing.hpp"
#include "preview_statistics.hpp"
#include "renderer.hpp"
#include <GLES2/gl2ext.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace observatory;
namespace {
volatile std::sig_atomic_t interrupted = 0;
void stop(int) { interrupted = 1; }
double number(const char *text, double low, double high) {
  std::string s(text);
  size_t end = 0;
  double n = std::stod(s, &end);
  if (end != s.size() || !std::isfinite(n) || n < low || n > high)
    throw std::runtime_error("Numeric argument out of range: " + s);
  return n;
}
int integerArgument(const char *text, int low, int high) {
  const double n = number(text, low, high);
  if (std::floor(n) != n)
    throw std::runtime_error("Expected an integer: " + std::string(text));
  return static_cast<int>(n);
}
// Preserve each button edge and its cursor position between rendered frames.
// Polling only the final sticky state loses consecutive clicks on a slow GPU.
struct PreviewPointerInput {
  struct Event {
    double x, y;
    bool down;
  };
  std::array<Event, 64> events{};
  size_t count = 0;
  double x = 0, y = 0;
};
} // namespace
int main(int argc, char **argv) {
  try {
    int w = 1280, h = 720, fps = 30, benchmark = 0, recordFrames = 270;
    double day = 1800, time = -1, weatherTime = -1, timeout = 0,
           timeStep = 1.0 / 30, lightningBrightness = 5,
           auroraBrightness = 1000, travelTime = -1, travelSpeed = 1;
    SurfaceOptions surface{true};
    bool fullscreen = false, drawRings = true, drawEmissions = true,
         debug = false;
    StarfieldOptions starfield;
    MeteorOptions meteorOptions;
    double meteorPreviewStart = -1;
    std::string benchmarkOutput;
    std::string capture, record, data = Renderer::defaultDataDirectory();
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--help") {
        std::cout
            << "Gas Giant Screensaver\n  --fullscreen           Fill the "
               "current monitor\n  --timeout SECONDS      Exit automatically "
               "(0: unlimited)\n  --time SECONDS         Starting simulation "
               "instant for review\n  --day-seconds SECONDS  Solar day length "
               "(default 1800)\n  --fps N                Frame cap 1..60 "
               "(default 30)\n  --width N --height N   Window/capture size\n  "
               "--capture FILE.png     Render a hidden deterministic "
               "screenshot\n  --benchmark N          Measure N synchronized "
               "render frames\n  --benchmark-output FILE.csv  Save per-frame "
               "costs and cache events\n  --data-dir PATH        Assets and "
               "shaders "
               "directory\n  --record DIRECTORY     Write a numbered PNG "
               "sequence\n  --frames N             Recording length (default "
               "270)\n  --time-step SECONDS    Simulation step per recorded "
               "frame\n  --no-rings             Disable rings for comparison\n"
               "  --weather-time SECONDS  Independent cloud/storm clock\n"
               "  --no-weather           Previous thin-cloud comparison\n"
               "  --lightning-brightness N  Lightning multiplier, 0..1000000 "
               "(default 5)\n"
               "  --aurora-brightness N     Aurora multiplier, 0..1000000 "
               "(default 1000)\n"
               "  --no-galaxy            Disable diffuse Milky Way for "
               "comparison\n"
               "  --no-stars             Disable catalog stars for comparison\n"
               "  --uncompressed-galaxy  Expand HDR sky on GPU for format "
               "comparison\n"
               "  --meteor-mode auto|preview|off  Rare natural showers or "
               "preview\n"
               "  --meteor-rate N         Shower rate, .5..1800 meteors/min "
               "(default 3)\n"
               "  --meteor-interval HOURS  Mean shower gap .5..12 (default "
               "4.5)\n"
               "  --meteor-preview-start SECONDS  Fixed preview epoch for "
               "captures\n"
               "  --no-emissions         Disable giant lightning/auroras for "
               "comparison\n"
               "  --travel-time SECONDS   Start at this point of the airborne "
               "72-hour "
               "route\n"
               "  --travel-speed N        Preview travel rate (default 1)\n"
               "  --terrain-reflections  Enable optional coastal terrain "
               "reflections\n"
               "  --no-terrain-reflections  Disable coastal terrain "
               "reflections\n"
               "  --no-weather-cache      Trace every cloud ray for "
               "comparison\n"
               "  --no-weather-compute    Use fragment clouds for comparison\n"
               "  --fixed-view            Previous stationary landscape "
               "comparison\n"
               "  --debug                 Show FPS/native quality and open "
               "time and weather controls at "
               "launch; "
               "release slider to seek\n"
               "Space pauses; Home resets the slider; Left/Right step time.\n"
               "Move to the upper-right corner to reveal OPEN DEBUG MENU.\n"
               "Escape or Q closes the preview. This preview does "
               "not lock your session.\n";
        return 0;
      }
      if (a == "--terrain-reflections") {
        surface.reflections = true;
        continue;
      }
      if (a == "--no-terrain-reflections") {
        surface.reflections = false;
        continue;
      }
      if (a == "--no-weather") {
        surface.weather = false;
        continue;
      }
      if (a == "--no-weather-cache") {
        surface.weatherCache = false;
        continue;
      }
      if (a == "--no-weather-compute") {
        surface.weatherCompute = false;
        continue;
      }
      if (a == "--fixed-view") {
        surface.enabled = false;
        continue;
      }
      if (a == "--debug") {
        debug = true;
        continue;
      }
      if (a == "--fullscreen") {
        fullscreen = true;
        continue;
      }
      if (a == "--no-galaxy") {
        starfield.galaxy = false;
        continue;
      }
      if (a == "--no-stars") {
        starfield.stars = false;
        continue;
      }
      if (a == "--uncompressed-galaxy") {
        starfield.compressed = false;
        continue;
      }
      if (a == "--no-emissions") {
        drawEmissions = false;
        continue;
      }
      if (a == "--no-rings") {
        drawRings = false;
        continue;
      }
      if (i + 1 >= argc)
        throw std::runtime_error("Missing value: " + a);
      const char *v = argv[++i];
      if (a == "--timeout")
        timeout = number(v, 0, 86400);
      else if (a == "--time")
        time = number(v, 0, 1e10);
      else if (a == "--travel-time")
        travelTime = number(v, 0, 1e10);
      else if (a == "--travel-speed")
        travelSpeed = number(v, 0, 10000);
      else if (a == "--meteor-mode") {
        const std::string mode(v);
        if (mode == "auto")
          meteorOptions.mode = MeteorMode::Auto;
        else if (mode == "preview")
          meteorOptions.mode = MeteorMode::Preview;
        else if (mode == "off")
          meteorOptions.mode = MeteorMode::Off;
        else
          throw std::runtime_error("Invalid meteor mode: " + mode);
      } else if (a == "--meteor-rate")
        meteorOptions.rate = number(v, meteorMinRate, meteorMaxRate);
      else if (a == "--meteor-interval")
        meteorOptions.intervalHours = number(v, .5, 12);
      else if (a == "--meteor-preview-start")
        meteorPreviewStart = number(v, 0, 1.e10);
      else if (a == "--weather-time")
        weatherTime = number(v, 0, 1e10);
      else if (a == "--lightning-brightness")
        lightningBrightness = number(v, 0, 1e6);
      else if (a == "--aurora-brightness")
        auroraBrightness = number(v, 0, 1e6);
      else if (a == "--day-seconds")
        day = number(v, 30, 86400);
      else if (a == "--fps")
        fps = integerArgument(v, 1, 60);
      else if (a == "--width")
        w = integerArgument(v, 64, 7680);
      else if (a == "--height")
        h = integerArgument(v, 64, 4320);
      else if (a == "--benchmark")
        benchmark = integerArgument(v, 1, 10000);
      else if (a == "--benchmark-output")
        benchmarkOutput = v;
      else if (a == "--record")
        record = v;
      else if (a == "--frames")
        recordFrames = integerArgument(v, 1, 10000);
      else if (a == "--time-step")
        timeStep = number(v, 0.001, 1800);
      else if (a == "--capture")
        capture = v;
      else if (a == "--data-dir")
        data = v;
      else
        throw std::runtime_error("Unknown option: " + a);
    }
    if (!benchmarkOutput.empty() && !benchmark)
      throw std::runtime_error("--benchmark-output requires --benchmark");
    if (benchmark && (!capture.empty() || !record.empty()))
      throw std::runtime_error(
          "--benchmark cannot be combined with capture or record");
    if (!capture.empty() && !record.empty())
      throw std::runtime_error("Use either --capture or --record");
    if (debug && (!capture.empty() || !record.empty() || benchmark))
      throw std::runtime_error("--debug requires an interactive preview");
    if (!record.empty())
      std::filesystem::create_directories(record);
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    glfwSetErrorCallback(
        [](int, const char *msg) { std::cerr << msg << '\n'; });
    if (!glfwInit())
      throw std::runtime_error("Cannot initialize GLFW display");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, capture.empty() && record.empty() && !benchmark
                                     ? GLFW_TRUE
                                     : GLFW_FALSE);
    GLFWmonitor *monitor = fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    if (monitor) {
      auto mode = glfwGetVideoMode(monitor);
      w = mode->width;
      h = mode->height;
    }
    GLFWwindow *window =
        glfwCreateWindow(w, h, "Gas Giant Observatory", monitor, nullptr);
    if (!window)
      throw std::runtime_error("Cannot create GLES 3 window");
    const bool interactive = capture.empty() && record.empty() && !benchmark;
    PreviewPointerInput pointerInput;
    if (interactive) {
      glfwSetWindowUserPointer(window, &pointerInput);
      glfwGetCursorPos(window, &pointerInput.x, &pointerInput.y);
      glfwSetCursorPosCallback(window, [](GLFWwindow *win, double x, double y) {
        auto &input =
            *static_cast<PreviewPointerInput *>(glfwGetWindowUserPointer(win));
        input.x = x;
        input.y = y;
      });
      glfwSetMouseButtonCallback(window, [](GLFWwindow *win, int button,
                                            int action, int) {
        if (button != GLFW_MOUSE_BUTTON_LEFT)
          return;
        auto &input =
            *static_cast<PreviewPointerInput *>(glfwGetWindowUserPointer(win));
        if (input.count < input.events.size())
          input.events[input.count++] = {input.x, input.y,
                                         action == GLFW_PRESS};
      });
      glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_TRUE);
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    std::cerr << "Renderer: " << glGetString(GL_RENDERER) << "\n";
    {
      if (time < 0)
        time = sceneClockNow();
      surface.speed = travelSpeed;
      surface.offset =
          (travelTime < 0 ? time : travelTime) - time * travelSpeed;
      Renderer renderer(data, starfield, surface);
      using Clock = std::chrono::steady_clock;
      auto start = Clock::now();
      std::vector<double> durations, gpuDurations;
      GLuint query = 0;
      bool hasTimer = false;
      GLint extensionCount = 0;
      glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
      for (GLint i = 0; i < extensionCount; ++i)
        if (std::string(reinterpret_cast<const char *>(glGetStringi(
                GL_EXTENSIONS, i))) == "GL_EXT_disjoint_timer_query")
          hasTimer = true;
      if (benchmark && hasTimer)
        glGenQueries(1, &query);
      if (time < 0)
        time = sceneClockNow();
      if (weatherTime < 0)
        weatherTime = time;
      meteorOptions.previewStart = meteorPreviewStart >= 0
                                       ? meteorPreviewStart
                                       : std::max(0., time - .65);
      std::unique_ptr<DebugOverlay> controls;
      if (interactive)
        controls = std::make_unique<DebugOverlay>(time, day, debug);
      if (controls)
        controls->menu.meteorOptions = meteorOptions;
      // Shader/asset loading is not part of the interactive timeline.
      start = Clock::now();
      std::ofstream frameLog;
      if (!benchmarkOutput.empty()) {
        frameLog.open(benchmarkOutput);
        if (!frameLog)
          throw std::runtime_error("Cannot write benchmark output");
        frameLog
            << "frame,scene_seconds,render_ms,gpu_ms,mesh_revision,shadow_"
               "revision,upload_bytes,reflection_revision,submit_ms,wait_ms,"
               "monotonic_seconds,mesh_builds_started,mesh_build_pending\n";
        frameLog << std::setprecision(12);
      }
      int frames = 0;
      PreviewFrameRate frameRate;
      FramePacer framePacer(fps);
      while (!glfwWindowShouldClose(window) && !interrupted) {
        auto frameStart = Clock::now();
        double elapsed =
            std::chrono::duration<double>(frameStart - start).count();
        if (timeout > 0 && elapsed >= timeout)
          break;
        glfwPollEvents();
        if (controls) {
          int ww, wh;
          double x, y;
          glfwGetWindowSize(window, &ww, &wh);
          glfwGetCursorPos(window, &x, &y);
          // Poll only while the pointer belongs to this window. A lost focus
          // still releases a drag; stale cursor coordinates cannot reveal UI.
          bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED);
          bool hovered = glfwGetWindowAttrib(window, GLFW_HOVERED);
          bool down =
              glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
          if (ww > 0 && wh > 0) {
            for (size_t i = 0; i < pointerInput.count; ++i) {
              auto event = pointerInput.events[i];
              controls->menu.pointer(ww, wh, event.x, event.y, event.down,
                                     elapsed, focused);
            }
            controls->menu.pointer(
                ww, wh, x, y, focused && down, elapsed,
                focused && (hovered || controls->menu.timeline.dragging()));
          }
          pointerInput.count = 0;
          controls->menu.keys(
              {focused && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS,
               focused && glfwGetKey(window, GLFW_KEY_HOME) == GLFW_PRESS,
               focused && glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS,
               focused && glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS},
              elapsed);
          renderer.setWeatherPreset(controls->menu.weather());
        }
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
          break;
        glfwGetFramebufferSize(window, &w, &h);
        if (w == 0 || h == 0 || glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
          glfwWaitEventsTimeout(.25);
          continue;
        }
        if (benchmark)
          glFinish();
        auto measure = Clock::now();
        if (query)
          glBeginQuery(0x88BF, query);
        const double sceneTime =
            controls
                ? controls->time(elapsed)
                : time + (!record.empty() ? frames * timeStep
                                          : (capture.empty() ? elapsed : 0));
        const double weatherElapsed = controls ? sceneTime - time
                                      : !record.empty()
                                          ? frames * timeStep
                                          : (capture.empty() ? elapsed : 0);
        renderer.setMeteorFrame(
            {controls ? controls->menu.meteorTime(elapsed) : sceneTime,
             1. / fps,
             controls ? controls->menu.meteorOptions : meteorOptions});
        renderer.render(w, h, sceneTime, day, 1, true, drawRings,
                        {drawEmissions ? weatherTime + weatherElapsed : -1,
                         1. / fps, true, true, lightningBrightness,
                         auroraBrightness},
                        weatherTime + weatherElapsed);
        if (controls) {
          controls->menu.meteorDark = renderer.meteorDarkEnough();
          if (debug)
            controls->setFrameRate(frameRate.fps(), fps);
          int ww, wh;
          glfwGetWindowSize(window, &ww, &wh);
          controls->draw(w, h, ww, wh, elapsed);
        }
        if (benchmark) {
          const auto submitted = Clock::now();
          if (query)
            glEndQuery(0x88BF);
          glFinish();
          if (query) {
            GLint disjoint = 0;
            glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
            if (disjoint)
              throw std::runtime_error("Disjoint GPU timer; rerun benchmark");
            GLuint ns = 0;
            glGetQueryObjectuiv(query, GL_QUERY_RESULT, &ns);
            gpuDurations.push_back(ns / 1e6);
          }
          const auto finished = Clock::now();
          durations.push_back(
              std::chrono::duration<double, std::milli>(finished - measure)
                  .count());
          if (frameLog.is_open()) {
            frameLog << frames << "," << sceneTime << "," << durations.back()
                     << ",";
            if (query)
              frameLog << gpuDurations.back();
            frameLog << "," << renderer.surfaceMeshRevision() << ","
                     << renderer.surfaceShadowRevision() << ","
                     << renderer.surfaceUploadBytes() << ","
                     << renderer.surfaceReflectionRevision() << ","
                     << std::chrono::duration<double, std::milli>(submitted -
                                                                  measure)
                            .count()
                     << ","
                     << std::chrono::duration<double, std::milli>(finished -
                                                                  submitted)
                            .count()
                     << ","
                     << std::chrono::duration<double>(
                            measure.time_since_epoch())
                            .count()
                     << "," << renderer.surfaceBuildsStarted() << ","
                     << renderer.surfaceBuildPending() << "\n";
            if (!frameLog)
              throw std::runtime_error("Cannot write benchmark frame");
          }
        }
        GLenum error = glGetError();
        if (error != GL_NO_ERROR)
          throw std::runtime_error("OpenGL error " + std::to_string(error));
        if (!capture.empty()) {
          savePng(capture, w, h);
          std::cout << "Captured " << capture << '\n';
          break;
        }
        if (!record.empty()) {
          std::ostringstream name;
          name << record << "/frame-" << std::setw(5) << std::setfill('0')
               << frames << ".png";
          savePng(name.str(), w, h);
          if (++frames >= recordFrames)
            break;
          continue;
        }
        glfwSwapBuffers(window);
        if (interactive)
          frameRate.presented(
              std::chrono::duration<double>(Clock::now() - start).count());
        if (benchmark && ++frames >= benchmark) {
          std::sort(durations.begin(), durations.end());
          std::cout << "Synchronized GPU+CPU render ms: median="
                    << durations[durations.size() / 2]
                    << " p95=" << durations[durations.size() * 95 / 100]
                    << " p99=" << durations[durations.size() * 99 / 100]
                    << " max=" << durations.back() << " deadline_misses="
                    << std::count_if(
                           durations.begin(), durations.end(),
                           [fps](double ms) { return ms > 1000. / fps; })
                    << " frames=" << frames << " resolution=" << w << 'x' << h
                    << '\n';
          if (!gpuDurations.empty()) {
            std::sort(gpuDurations.begin(), gpuDurations.end());
            std::cout << "GPU timer ms: median="
                      << gpuDurations[gpuDurations.size() / 2]
                      << " p95=" << gpuDurations[gpuDurations.size() * 95 / 100]
                      << '\n';
          }
          auto cloudSize = renderer.weatherResolution();
          auto cacheSize = renderer.weatherSkyCacheResolution();
          if (cloudSize[0] > 0)
            std::cout << "Cloud buffer: " << cloudSize[0] << 'x' << cloudSize[1]
                      << "; noise "
                      << (renderer.floatingWeatherNoise() ? "RGBA16F" : "RGBA8")
                      << '\n';
          if (cloudSize[0] > 0)
            std::cout << "Cloud sky cache: " << cacheSize[0] << 'x'
                      << cacheSize[1] << '\n';
          if (cloudSize[0] > 0)
            std::cout << "Cloud backend: "
                      << (renderer.computingWeather() ? "OpenGL compute"
                                                      : "fragment shader")
                      << '\n';
          break;
        }
        std::this_thread::sleep_until(
            framePacer.deadline(frameStart, Clock::now()));
      }
      if (query)
        glDeleteQueries(1, &query);
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "gas-giant-screensaver: " << e.what() << '\n';
    glfwTerminate();
    return 1;
  }
}
