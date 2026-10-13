#include "core/physics/InitialConditions.h"
#include "core/physics/Invariants.h"
#include "core/physics/RK4Integrator.h"
#include <algorithm>
#include <iostream>
using namespace threebody;
int main() {
  auto a = makeAsymmetricDemo(), b = a;
  RK4Integrator<double> integrator;
  const auto initial = computeInvariants(a);
  double minDistance = 1e10, maxRadius = 0, maxDrift = 0, maxDifference = 0;
  for (int step = 0; step < 120000; ++step) {
    integrator.step(a, 1e-4);
    integrator.step(b, 5e-5);
    integrator.step(b, 5e-5);
    for (int i = 0; i < 3; ++i) {
      maxRadius = std::max(maxRadius, norm(a.body[i].position));
      maxDifference = std::max(maxDifference,
                               norm(a.body[i].position - b.body[i].position));
      for (int j = i + 1; j < 3; ++j)
        minDistance = std::min(minDistance,
                               norm(a.body[i].position - a.body[j].position));
    }
    if (step % 100 == 0) {
      double drift =
          std::abs((computeInvariants(a).totalEnergy - initial.totalEnergy) /
                   initial.totalEnergy);
      if (!std::isfinite(drift))
        return 1;
      maxDrift = std::max(maxDrift, drift);
    }
  }
  std::cout << "demo t=12 min_separation=" << minDistance
            << " max_radius=" << maxRadius << " max_energy_drift=" << maxDrift
            << " dt_halving_max_position_difference=" << maxDifference << "\n";
  return minDistance > .02 && maxDrift < 1e-6 && maxDifference < 1e-4 &&
                 maxRadius < 8
             ? 0
             : 2;
}
