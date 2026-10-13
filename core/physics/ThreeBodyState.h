#pragma once
#include <array>
#include "core/physics/Vec3.h"

namespace threebody {

template <typename T>
struct BodyState {
    Vec3<T> position;
    Vec3<T> velocity;
    T mass{1};
};

template <typename T>
struct ThreeBodyState {
    std::array<BodyState<T>, 3> body;
    T simulationTime{0};
    // Optional massless tracer: ignored by all three-star force evaluations.
    bool planetEnabled{false};
    BodyState<T> planet{};
    T planetRadius{T(6371000.0 / 149597870700.0)};

};

template <typename T>
struct StateDerivative {
    std::array<Vec3<T>, 3> dPosition;
    std::array<Vec3<T>, 3> dVelocity;
};

} // namespace threebody
