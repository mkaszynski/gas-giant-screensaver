#include "system.hpp"
#include <iostream>
#include <stdexcept>
using namespace observatory;
void require(bool b, const char *msg) {
  if (!b)
    throw std::runtime_error(msg);
}
int main() {
  Orbit o{homeA, homeE, 0, 0, 0, 0};
  require(std::abs(length(orbitPosition(o, .5)) / length(orbitPosition(o, 0)) -
                   1.25) < 1e-12,
          "apo/peri ratio");
  require(length(orbitPosition(o, 1) - orbitPosition(o, 0)) < 1e-12,
          "orbit closure");
  require(diskVisibility(0, .004, .01) == 0, "total eclipse");
  require(diskVisibility(.1, .004, .01) == 1, "no eclipse");
  require(std::abs(diskVisibility(0, .01, .005) - .75) < 1e-12,
          "annular eclipse");
  require(diskVisibility(.01, .004, .01) > 0 &&
              diskVisibility(.01, .004, .01) < 1,
          "penumbra");
  const auto orbits = makeOrbits();
  const auto initial = sceneAt(0);
  require(std::abs(initial.bodies[1].radius * 71492.0 - 6371.0) < 1e-9,
          "observer moon has Earth's 6371 km mean radius");
  require(std::abs(initial.bodies[2].radius - 0.0039) < 1e-12 &&
              std::abs(initial.bodies[8].radius - 0.258) < 1e-12,
          "other small and large moons retain tripled radii");
  require(homeA == 3.0, "observer orbit halved");
  const Vec3 pole = giantPole(), equator = normalized(cross(pole, {0, 0, 1}));
  require(std::abs(length(pole) - 1) < 1e-12, "unit ring/giant shared pole");
  require(std::abs(dot(pole, equator)) < 1e-12, "rings lie in giant equator");
  require(length(initial.bodies[0].pole - pole) < 1e-12,
          "rendered giant and rings share the authoritative pole");
  require(std::abs(dot(orbitNormal(orbits[0]), pole) -
                   std::cos(10 * pi / 180)) < 1e-12,
          "observer orbit is exactly 10 degrees from giant equator");
  for (const auto &orbit : orbits) {
    const auto normal = orbitNormal(orbit);
    require(std::abs(dot(normal, pole)) >= std::cos(10 * pi / 180) - 1e-12,
            "all moon orbital planes stay within 10 degrees of the equator");
    for (double phase : {0., .13, .37, .61, .89})
      require(std::abs(dot(normal, orbitPosition(orbit, phase))) < 1e-10,
              "actual moon trajectory lies in its declared inclined plane");
  }
  const auto north = normalized(orbitPosition(orbits[0], .5));
  const auto south = normalized(orbitPosition(orbits[0], 0));
  require(std::abs(dot(north, pole) - std::sin(10 * pi / 180)) < 1e-12 &&
              std::abs(dot(south, pole) + std::sin(10 * pi / 180)) < 1e-12,
          "observer travels above and below fixed equatorial rings");
  require(homeA * (1 - homeE) - initial.bodies[1].radius > ringOuter,
          "observer stays outside the ring system");
  require(ringOpticalDepth(1.1) == 0 && ringOpticalDepth(2.4) == 0,
          "finite ring annulus");
  require(ringOpticalDepth(1.52) > ringOpticalDepth(1.30) * 4 &&
              ringOpticalDepth(1.6075) < ringOpticalDepth(1.67) * .1,
          "dense B ring, translucent C ring and Cassini division");
  double denseTransmission =
      ringSunVisibility(equator * 1.52 - pole * .2, pole);
  double thinTransmission = ringSunVisibility(equator * 1.30 - pole * .2, pole);
  require(denseTransmission > 0 && denseTransmission < thinTransmission &&
              thinTransmission < 1,
          "rings cast partial density-dependent shadows");
  require(ringSunVisibility(equator * 1.52 + pole * .2, pole) == 1 &&
              ringSunVisibility(equator * 1.52 - pole * .2, equator) == 1,
          "rings cannot shadow a source on the same side or a parallel ray");
  require(ringSunVisibility(equator * .8660254038 - pole * .5,
                            normalized(equator * .78 + pole * .625)) < .7,
          "ring shadow reaches the giant's lit surface");
  std::array<Body, 21> eclipseBodies{};
  eclipseBodies[2] = {{0, 0, 10}, 1, 0, 0};
  require(visibility({0, 0, 0}, {0, 0, 1}, eclipseBodies, 1) == 0,
          "moon casts shadow on another body");
  eclipseBodies[2].position = {10, 0, 10};
  require(visibility({0, 0, 0}, {0, 0, 1}, eclipseBodies, 1) == 1,
          "displaced moon no longer eclipses");
  for (int i = 0; i < 20; ++i)
    for (int j = i + 1; j < 20; ++j) {
      const auto a = orbits[i].a < orbits[j].a ? orbits[i] : orbits[j];
      const auto b = orbits[i].a < orbits[j].a ? orbits[j] : orbits[i];
      require(a.a * (1 + a.e) +
                      initial.bodies[orbits[i].a < orbits[j].a ? i + 1 : j + 1]
                          .radius <
                  b.a * (1 - b.e) -
                      initial.bodies[orbits[i].a < orbits[j].a ? j + 1 : i + 1]
                          .radius,
              "non-crossing radial orbital envelopes");
    }
  for (int t = 0; t < 3600; t += 7) {
    auto s = sceneAt(t);
    require(s.bodies.size() == 21, "20 moons plus giant");
    require(std::abs(dot(s.up, s.east)) < 1e-12, "orthogonal local frame");
    require(std::abs(dot(s.up, orbitNormal(orbits[0])) -
                     std::sin(38 * pi / 180)) < 1e-12,
            "tidally locked ground frame follows the inclined observer orbit");
    require(std::abs(length(s.forward) - 1) < 1e-12, "normalized camera");
    require(s.sunVisibility >= 0 && s.sunVisibility <= 1, "bounded visibility");
    require(s.local(-s.observer).y > 0, "giant remains above horizon");
    for (auto b : s.bodies)
      require(std::isfinite(length(b.position)), "finite ephemerides");
  }
  std::cout << "Orbital geometry, eccentricity, frames and eclipses passed\n";
}
