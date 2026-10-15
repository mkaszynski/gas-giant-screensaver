#include "surface.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace observatory;
int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Invalid argument count\n";
    return 2;
  }
  using Clock = std::chrono::steady_clock;
  SurfaceWorld world(std::string(argv[1]) + "/assets/world/moon.ggworld");
  const auto epoch = Clock::now();
  auto stamp = []() {
    return std::chrono::duration<double>(Clock::now().time_since_epoch())
        .count();
  };
  std::cout.precision(15);
  for (int n = 0; n < 4; ++n) {
    std::this_thread::sleep_until(epoch + std::chrono::seconds(3 + 6 * n));
    std::cout << "{\"event\":\"start\",\"build\":" << n
              << ",\"monotonic_seconds\":" << stamp() << "}" << std::endl;
    const auto mesh = buildSurfaceMesh(world, world.location(162000));
    std::cout << "{\"event\":\"finish\",\"build\":" << n
              << ",\"monotonic_seconds\":" << stamp()
              << ",\"vertices\":" << mesh.vertices.size() << "}" << std::endl;
  }
}
