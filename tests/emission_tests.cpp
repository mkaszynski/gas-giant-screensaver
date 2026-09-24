#include "emissions.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace observatory;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main() {
  try {
    for (int strokes = 1; strokes <= 3; ++strokes) {
      require(std::abs(lightningEnergyFraction(-1, 1, strokes) - 1) < 1.e-12,
              "pulse energy normalization");
      require(lightningEnergyFraction(-1, 0, strokes) == 0,
              "no pulse before onset");
      require(lightningEnergyFraction(.5, 1, strokes) == 0,
              "no long synthetic afterglow");
      for (int fps : {15, 30, 60}) {
        double sum = 0;
        for (int i = 0; i < fps; ++i)
          sum += lightningEnergyFraction(double(i) / fps, double(i + 1) / fps,
                                         strokes);
        require(std::abs(sum - 1) < 1.e-12,
                "exposure integration conserves milliseconds-long pulses");
      }
    }
    double reference = 0;
    for (int fps : {15, 30, 60}) {
      double energy = 0;
      int peak = 0;
      for (int i = 1; i <= 120 * fps; ++i) {
        auto f = lightningAt({double(i) / fps, 1. / fps});
        peak = std::max(peak, int(f.size()));
        for (const auto &flash : f) {
          energy += flash.powerWatts / fps;
          require(std::abs(length(flash.normal) - 1) < 1.e-12,
                  "storms on sphere");
          require(flash.sigmaKm > 14 && flash.sigmaKm < 69,
                  "physical cloud footprint");
          require(std::isfinite(flash.powerWatts) && flash.powerWatts > 0,
                  "finite positive emission");
        }
      }
      std::cout << fps << " fps energy " << energy << " peak active " << peak
                << '\n';
      if (reference)
        require(std::abs(energy / reference - 1) < 1.e-8,
                "same events/energy independent of fps");
      reference = energy;
    }
    require(lightningTimeScale(1800) > 17.7 && lightningTimeScale(1800) < 17.9,
            "lightning uses orbital compression");
    require(std::abs(lightningTimeScale(900) / lightningTimeScale(1800) - 2) <
                1.e-12,
            "shorter day accelerates flash rate and duration equally");
    for (double day : {30., 1800., 86400.}) {
      double previous = 0;
      for (int fps : {15, 30, 60}) {
        double energy = 0;
        for (int i = 1; i <= 20 * fps; ++i) {
          const auto frame =
              acceleratedLightning({1000. + double(i) / fps, 1. / fps}, day);
          auto flashes = lightningAt(frame);
          require(flashes.size() < 500, "accelerated frame work is bounded");
          for (auto f : flashes)
            energy += f.powerWatts * frame.exposure;
        }
        if (previous)
          require(
              std::abs(energy / previous - 1) < 1.e-7,
              "accelerated shutter retains complete event energy at all fps");
        previous = energy;
      }
    }
    require(lightningAt({-1, .033}).empty(), "disabled frame");
    require(lightningAt({900, 0}).empty(), "invalid exposure");
    for (int t = 0; t < 10000; t++)
      require(auroraRayleighs(t) >= 80000 && auroraRayleighs(t) <= 300000,
              "measured visible aurora brightness range");
    Body b{{}, 1, 0, 0};
    b.pole = {0, 1, 0};
    auto a = bodyDirection({1, 0, 0}, b);
    b.spin = pi / 2;
    auto c = bodyDirection({1, 0, 0}, b);
    require(std::abs(dot(a, c)) < 1.e-12,
            "emission corotates with cloud texture");
    std::cout << "Emission physics and uncompressed pulse timing passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
