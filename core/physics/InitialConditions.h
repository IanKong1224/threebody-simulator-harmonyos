#pragma once
#include <cstdint>
#include "core/physics/ThreeBodyState.h"

namespace threebody {

ThreeBodyState<double> makeFigureEightDouble();
// Fixed asymmetric 3D demo, validated only over its finite demo window.
ThreeBodyState<double> makeAsymmetricDemo();
// Non-hierarchical spatial preset validated for a finite interval only.
ThreeBodyState<double> makeLongLivedTriple();
// Hierarchical spatial preset validated for a finite interval only.
ThreeBodyState<double> makeStableTriple();
ThreeBodyState<double> makeReturningTriple();
ThreeBodyState<double> makeRandomTriple(std::uint32_t seed);
ThreeBodyState<float> makeFigureEightFloat();

} // namespace threebody
