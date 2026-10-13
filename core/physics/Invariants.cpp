#include "core/physics/Invariants.h"

namespace threebody {

template <typename T>
Invariants<T> computeInvariants(const ThreeBodyState<T>& state, T G) {
    Invariants<T> inv{};
    T totalMass = static_cast<T>(0);

    for (const auto& body : state.body) {
        totalMass += body.mass;
        inv.totalEnergy += static_cast<T>(0.5) * body.mass * normSquared(body.velocity);
        inv.linearMomentum += body.velocity * body.mass;
        inv.angularMomentum += cross(body.position, body.velocity * body.mass);
        inv.centerOfMass += body.position * body.mass;
    }

    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = i + 1; j < 3; ++j) {
            const T distance = norm(state.body[j].position - state.body[i].position);
            if (distance != static_cast<T>(0)) {
                inv.totalEnergy -= G * state.body[i].mass * state.body[j].mass / distance;
            }
        }
    }

    if (totalMass != static_cast<T>(0)) {
        inv.centerOfMass = inv.centerOfMass / totalMass;
    }
    return inv;
}

template Invariants<float> computeInvariants(const ThreeBodyState<float>&, float);
template Invariants<double> computeInvariants(const ThreeBodyState<double>&, double);

} // namespace threebody
