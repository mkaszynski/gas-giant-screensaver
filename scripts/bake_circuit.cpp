#include "surface.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
using namespace observatory;
namespace {
constexpr int count = 552960, columns = 157, middle = 78;
constexpr double step = 64., cell = travelPeriod / count;
void write32(std::ofstream &f, uint32_t value) {
  char b[4] = {char(value), char(value >> 8), char(value >> 16),
               char(value >> 24)};
  f.write(b, 4);
}
void writeRoute(const std::string &name, const std::vector<float> &offset) {
  std::ofstream f(name + ".tmp", std::ios::binary);
  f.write("GGROUTE1", 8);
  write32(f, offset.size());
  for (float v : offset) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    write32(f, bits);
  }
  f.close();
  if (!f)
    throw std::runtime_error("Circuit write failed");
  std::filesystem::rename(name + ".tmp", name);
}
} // namespace
int main(int argc, char **argv) {
  std::string candidateFile;
  try {
    const std::string root = argc > 1 ? argv[1] : "assets/world";
    candidateFile = root + "/circuit.ggroute.candidate";
    SurfaceWorld world(root + "/moon.ggworld", true);
    std::vector<float> costs(size_t(count) * columns);
    std::vector<uint8_t> forbidden(costs.size()), parents(costs.size());
    std::cout << "Sampling high-ground corridor: " << count << " rows, "
              << columns << " candidates\n"
              << std::flush;
    for (int i = 0; i < count; ++i) {
      double best = -1e30;
      for (int j = 0; j < columns; ++j) {
        auto p = travelLocation(i * cell, (j - middle) * step);
        double h = std::max(0., world.elevation(p.latitude, p.longitude));
        costs[size_t(i) * columns + j] = h;
        best = std::max(best, h);
      }
      for (int j = 0; j < columns; ++j) {
        double offset = (j - middle) * step;
        float &v = costs[size_t(i) * columns + j];
        v = (best - v) / 45. + .02 * offset * offset / 1000000.;
      }
    }
    std::vector<float> offset(count);
    std::vector<double> previous(columns), next(columns);
    auto ridgeAt = [&](TravelLocation p) {
      double altitude =
          std::max(0., world.elevation(p.latitude, p.longitude)) + 50.;
      Vec3 up = geographicUp(p);
      Vec3 toward = normalized(Vec3{1, 0, 0} - up * dot({1, 0, 0}, up));
      return world.horizon(p, toward, altitude, 48);
    };
    for (int attempt = 0; attempt < 48; ++attempt) {
      std::fill(previous.begin(), previous.end(), 1.e30);
      previous[middle] = 0;
      for (int i = 1; i < count; ++i) {
        for (int j = 0; j < columns; ++j) {
          double best = 1.e30;
          int parent = j;
          for (int k = std::max(0, j - 1); k <= std::min(columns - 1, j + 1);
               ++k) {
            // Cache rejected transitions independently of rejected points.
            // A cliff between two safe nodes must not ban both safe nodes.
            if (forbidden[size_t(i) * columns + j] & (1u << (k - j + 1)))
              continue;
            double v = previous[k] + .32 * (j - k) * (j - k);
            if (v < best) {
              best = v;
              parent = k;
            }
          }
          next[j] = (forbidden[size_t(i) * columns + j] & 8)
                        ? 1.e30
                        : best + costs[size_t(i) * columns + j];
          parents[size_t(i) * columns + j] = parent;
        }
        previous.swap(next);
      }
      if (previous[middle] >= 1.e29)
        throw std::runtime_error("No continuous closed high-ground route");
      int j = middle;
      for (int i = count - 1; i >= 0; --i) {
        offset[i] = (j - middle) * step;
        if (i)
          j = parents[size_t(i) * columns + j];
      }
      writeRoute(candidateFile, offset);
      SurfaceWorld candidate(root + "/moon.ggworld", false, candidateFile);
      int unsafe = 0;
      double worst = -90;
      std::set<int> badRows, badEdges;
      // Dense route sampling includes interpolated points between cached keys.
      for (int i = 0; i < count * 2; ++i) {
        double time = i * cell * .5;
        auto p = candidate.location(time);
        // Jupiter's upper limb remains above 35 degrees along this mean
        // bearing throughout orbital libration. Preserve a clear ray below
        // 28 degrees; other parts of the disk may be covered by mountains.
        double ridge = ridgeAt(p);
        worst = std::max(worst, ridge * 180 / pi);
        if (ridge > 28 * pi / 180) {
          ++unsafe;
          int row = i / 2;
          int col = std::clamp(int(std::round(offset[row] / step)) + middle, 0,
                               columns - 1);
          if (!(i % 2)) {
            forbidden[size_t(row) * columns + col] |= 8;
            badRows.insert(row);
          } else {
            int nextRow = (row + 1) % count;
            int nextCol =
                std::clamp(int(std::round(offset[nextRow] / step)) + middle, 0,
                           columns - 1);
            forbidden[size_t(nextRow) * columns + nextCol] |=
                1u << (col - nextCol + 1);
            badEdges.insert(nextRow);
            badRows.insert(row);
            badRows.insert(nextRow);
          }
        }
      }
      std::cout << "Iteration " << attempt + 1 << ": " << unsafe
                << " unsafe samples; maximum ridge " << worst << " deg\n"
                << std::flush;
      if (!unsafe) {
        std::filesystem::rename(candidateFile, root + "/circuit.ggroute");
        return 0;
      }
      // Learn all alternatives at failing rows rather than stepping sideways
      // one candidate per complete sweep. Reject points and connecting edges
      // separately, so a cliff between safe nodes does not erase those nodes.
      for (int row : badRows)
        for (int col = 0; col < columns; ++col)
          if (ridgeAt(travelLocation(row * cell, (col - middle) * step)) >
              28 * pi / 180)
            forbidden[size_t(row) * columns + col] |= 8;
      for (int row : badEdges)
        for (int col = 0; col < columns; ++col)
          for (int from = std::max(0, col - 1);
               from <= std::min(columns - 1, col + 1); ++from)
            if (ridgeAt(travelLocation((row - .5) * cell,
                                       (.5 * (col + from) - middle) * step)) >
                28 * pi / 180)
              forbidden[size_t(row) * columns + col] |= 1u << (from - col + 1);
    }
    throw std::runtime_error(
        "Circuit needs more route optimization iterations");
  } catch (const std::exception &e) {
    if (!candidateFile.empty()) {
      std::error_code ignored;
      std::filesystem::remove(candidateFile, ignored);
      std::filesystem::remove(candidateFile + ".tmp", ignored);
    }
    std::cerr << e.what() << '\n';
    return 1;
  }
}
