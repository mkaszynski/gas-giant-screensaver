#include "surface_work.hpp"
#include <atomic>
#include <future>
#include <iostream>
#include <limits>
#include <thread>
using namespace observatory::detail;
using namespace std::chrono_literals;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
int main() {
  try {
    require(surfaceWorkSeconds(1. / 30.) == (1. / 30.) * .16,
            "normal frames reserve sixteen percent for terrain work");
    require(surfaceWorkSeconds(.05) == .0055 &&
                surfaceWorkSeconds(1.e-6) == .0005,
            "supported intervals retain bounded CPU slices");
    for (double period : {.051, .1, 1., 30.})
      require(surfaceWorkSeconds(period) == 0.,
              "slow frames and resume select independent terrain progress");
    for (double period : {0., -1., std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()})
      require(surfaceWorkSeconds(period) == surfaceWorkSeconds(1. / 30.),
              "invalid frame intervals use the nominal budget");
    for (auto renderer : {"", "llvmpipe (LLVM 20.1.8, 256 bits)", "softpipe",
                           "Software Rasterizer", "Google SwiftShader",
                           "SWR (LLVM)"})
      require(!hardwareSurfaceWork(renderer),
              "CPU rendering retains independent terrain progress");
    for (auto renderer : {"AMD Radeon Graphics (radeonsi, renoir)",
                           "Mesa Intel(R) UHD Graphics", "NVIDIA GeForce"})
      require(hardwareSurfaceWork(renderer),
              "hardware renderers can use GPU-separated CPU windows");
    SurfaceWork work;
    work.reset();
    std::atomic<bool> stop{false}, gpuActive{false};
    std::atomic<size_t> steps{0}, overlap{0};
    std::promise<void> started;
    auto future = std::async(std::launch::async, [&] {
      started.set_value();
      try {
        while (!stop) {
          work.checkpoint();
          if (gpuActive)
            ++overlap;
          ++steps;
        }
        work.complete();
      } catch (...) {
        work.complete();
        throw;
      }
    });
    started.get_future().wait();
    bool resetRejected = false, stayedPaused = true;
    try {
      work.reset();
    } catch (const std::logic_error &) {
      resetRejected = true;
    }
    work.runFor(0ns);
    std::this_thread::sleep_for(2ms);
    bool zeroPaused = steps == 0;
    for (int i = 0; i < 3; ++i) {
      work.runFor(2ms);
      auto before = steps.load();
      gpuActive = true;
      std::this_thread::sleep_for(3ms);
      stayedPaused &= steps == before;
      gpuActive = false;
    }
    stop = true;
    work.release();
    future.get();
    require(resetRejected, "cannot discard an active worker generation");
    require(zeroPaused, "zero budget never starts terrain work");
    require(steps > 0, "explicit windows make progress");
    require(stayedPaused && overlap == 0,
            "returning from a window leaves no CPU work overlapping GPU draws");

    work.reset();
    auto shortJob = std::async(std::launch::async, [&] {
      work.checkpoint();
      work.complete();
    });
    work.runFor(100ms);
    const bool shortReady = shortJob.wait_for(1s) == std::future_status::ready;
    work.release();
    shortJob.get();
    require(shortReady,
            "completion wakes the render owner before its full budget");

    work.reset();
    auto noFrame = std::async(std::launch::async, [&] {
      work.checkpoint();
      work.complete();
    });
    work.release();
    require(noFrame.wait_for(1s) == std::future_status::ready,
            "seeks and shutdown do not require another frame");
    noFrame.get();

    work.reset();
    auto failed = std::async(std::launch::async, [&] {
      try {
        work.checkpoint();
        throw std::runtime_error("deliberate build failure");
      } catch (...) {
        work.complete();
        throw;
      }
    });
    work.runFor(100ms);
    work.release();
    bool propagated = false;
    try {
      failed.get();
    } catch (const std::runtime_error &) {
      propagated = true;
    }
    require(propagated,
            "build failure releases the owner and preserves errors");
    std::cout << "Terrain work windows, GPU exclusion, completion and shutdown "
                 "passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
