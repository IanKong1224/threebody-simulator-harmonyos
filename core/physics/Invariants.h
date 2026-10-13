#pragma once
#include "core/physics/ThreeBodyState.h"

namespace threebody {

template <typename T>
struct Invariants {
    T totalEnergy{};
    Vec3<T> linearMomentum{};
    Vec3<T> angularMomentum{};
    Vec3<T> centerOfMass{};
};

template <typename T>
Invariants<T> computeInvariants(const ThreeBodyState<T>& state, T gravitationalConstant = static_cast<T>(1));

extern template Invariants<float> computeInvariants(const ThreeBodyState<float>&, float);
extern template Invariants<double> computeInvariants(const ThreeBodyState<double>&, double);

} // namespace threebody
