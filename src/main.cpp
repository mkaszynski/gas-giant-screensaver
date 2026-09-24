#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
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
} // namespace
int main(int argc, char **argv) {
  try {
    int w = 1280, h = 720, fps = 30, benchmark = 0, recordFrames = 270;
    double day = 1800, time = -1, weatherTime = -1, timeout = 0,
           timeStep = 1.0 / 30;
    bool fullscreen = false, drawRings = true, drawEmissions = true;
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
               "render frames\n  --data-dir PATH        Assets and shaders "
               "directory\n  --record DIRECTORY     Write a numbered PNG "
               "sequence\n  --frames N             Recording length (default "
               "270)\n  --time-step SECONDS    Simulation step per recorded "
               "frame\n  --no-rings             Disable rings for comparison\n"
               "  --weather-time SECONDS  Starting weather clock (lightning "
               "follows orbital speed)\n"
               "  --no-emissions         Disable lightning and auroras for "
               "comparison\n"
               "Escape or Q closes the preview. This preview does "
               "not lock your session.\n";
        return 0;
      }
      if (a == "--fullscreen") {
        fullscreen = true;
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
        time = number(v, 0, 1e9);
      else if (a == "--weather-time")
        weatherTime = number(v, 0, 1e9);
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
    if (!capture.empty() && !record.empty())
      throw std::runtime_error("Use either --capture or --record");
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
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    std::cerr << "Renderer: " << glGetString(GL_RENDERER) << "\n";
    {
      Renderer renderer(data);
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
        time =
            std::fmod(std::chrono::duration<double>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count(),
                      day * 365.);
      if (weatherTime < 0)
        weatherTime = time;
      int frames = 0;
      while (!glfwWindowShouldClose(window) && !interrupted) {
        auto frameStart = Clock::now();
        double elapsed =
            std::chrono::duration<double>(frameStart - start).count();
        if (timeout > 0 && elapsed >= timeout)
          break;
        glfwPollEvents();
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
        const double weatherElapsed = !record.empty()
                                          ? frames * timeStep
                                          : (capture.empty() ? elapsed : 0);
        renderer.render(
            w, h,
            time + (!record.empty() ? frames * timeStep
                                    : (capture.empty() ? elapsed : 0)),
            day, 1, true, drawRings,
            {drawEmissions ? weatherTime + weatherElapsed : -1, 1. / fps});
        if (benchmark) {
          if (query)
            glEndQuery(0x88BF);
          glFinish();
          if (query) {
            GLuint ns = 0;
            glGetQueryObjectuiv(query, GL_QUERY_RESULT, &ns);
            gpuDurations.push_back(ns / 1e6);
          }
          durations.push_back(
              std::chrono::duration<double, std::milli>(Clock::now() - measure)
                  .count());
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
        if (benchmark && ++frames >= benchmark) {
          std::sort(durations.begin(), durations.end());
          std::cout << "Synchronized GPU+CPU render ms: median="
                    << durations[durations.size() / 2]
                    << " p95=" << durations[durations.size() * 95 / 100]
                    << " frames=" << frames << " resolution=" << w << 'x' << h
                    << '\n';
          if (!gpuDurations.empty()) {
            std::sort(gpuDurations.begin(), gpuDurations.end());
            std::cout << "GPU timer ms: median="
                      << gpuDurations[gpuDurations.size() / 2]
                      << " p95=" << gpuDurations[gpuDurations.size() * 95 / 100]
                      << '\n';
          }
          break;
        }
        std::this_thread::sleep_until(frameStart +
                                      std::chrono::microseconds(1000000 / fps));
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
