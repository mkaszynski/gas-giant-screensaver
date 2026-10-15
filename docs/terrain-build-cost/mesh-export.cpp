#include "surface.hpp"
#include <chrono>
#include <iostream>
using namespace observatory;
template <class T> void value(const T &v) {
  std::cout.write(reinterpret_cast<const char *>(&v), sizeof(v));
}
int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Invalid argument count\n";
    return 2;
  }
  SurfaceWorld world(std::string(argv[1]) + "/assets/world/moon.ggworld");
  auto start = std::chrono::steady_clock::now();
  const auto mesh = buildSurfaceMesh(world, world.location(std::stod(argv[2])));
  auto end = std::chrono::steady_clock::now();
  std::cerr << std::chrono::duration<double, std::milli>(end - start).count()
            << std::endl;
  for (auto size : {mesh.vertices.size(), mesh.indices.size(),
                    mesh.waterIndices.size(), mesh.bounds.size()})
    value(uint64_t(size));
  value(mesh.center.latitude);
  value(mesh.center.longitude);
  value(mesh.hasLand);
  value(mesh.hasWater);
  std::cout.write(reinterpret_cast<const char *>(mesh.vertices.data()),
                  mesh.vertices.size() * sizeof(SurfaceVertex));
  for (const auto *ids : {&mesh.indices, &mesh.waterIndices})
    std::cout.write(reinterpret_cast<const char *>(ids->data()),
                    ids->size() * sizeof(uint32_t));
  for (const auto &b : mesh.bounds) {
    for (auto x : {b.minimum.x, b.minimum.y, b.minimum.z, b.maximum.x,
                   b.maximum.y, b.maximum.z})
      value(x);
    value(b.firstIndex);
    value(b.indexCount);
    value(b.waterFirstIndex);
    value(b.waterIndexCount);
    value(b.deepWater);
  }
}
