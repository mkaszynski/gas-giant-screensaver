#include "ocean_foam.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace observatory;
void require(bool b, const char *message) {
  if (!b)
    throw std::runtime_error(message);
}
int main() {
  try {
    require(whitecapFraction(3.7) == 0. && whitecapFraction(-1.) == 0.,
            "calm water has no breaking whitecaps");
    require(whitecapFraction(std::numeric_limits<double>::quiet_NaN()) == 0.,
            "invalid wind cannot create luminous foam");
    require(std::abs(whitecapFraction(10.) - 3.18e-5 * 6.3 * 6.3 * 6.3) < 1e-12,
            "published coverage is converted from percent to fraction");
    require(whitecapFraction(100.) == whitecapFraction(23.09),
            "no empirical extrapolation beyond measured storm winds");
    double previous = 0.;
    for (int i = 0; i <= 2400; ++i) {
      double coverage = whitecapFraction(i / 100.);
      require(coverage >= previous && coverage <= .12,
              "whitecap coverage grows continuously with marine wind");
      require(coverage - previous < .001,
              "piecewise wind fit has no transition jump");
      previous = coverage;
    }
    require(marineWind(.94, 1., 0.) > marineWind(.36, 0., 0.),
            "storm fronts produce rougher seas than fair weather");
    require(marineWind(.3, 0., .45) <= 3.7,
            "quiet fog does not force storm whitecaps");
    // Measure independent phase samples against the baked inverse-CDF. The
    // distant shader uses the same target area when crests are unresolved.
    constexpr int samples = 524288;
    constexpr double tau = 6.2831853071795864769;
    for (double coverage : {.001, .01, .05}) {
      const double threshold = whitecapThreshold(coverage);
      int count = 0;
      for (int i = 0; i < samples; ++i) {
        auto phase = [&](double ratio) {
          double q = (i + .5) * ratio;
          return tau * (q - std::floor(q));
        };
        double score = .58 * std::cos(phase(.6180339887498948)) +
                       .27 * std::cos(phase(.4142135623730950)) +
                       .15 * std::cos(phase(.7320508075688772));
        count += score > threshold;
      }
      const double measured = double(count) / samples;
      std::cout << "Whitecap target=" << coverage << " measured=" << measured
                << '\n';
      require(std::abs(measured - coverage) < coverage * .12 + .00005,
              "crest masks retain empirical area coverage");
    }
    std::cout
        << "Marine wind, whitecap coverage and crest filtering model PASS\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
