#include "core/physics/RK4Integrator.h"
#include "core/physics/Gravity.h"

namespace threebody {
namespace {

template <typename T>
ThreeBodyState<T> addScaled(const ThreeBodyState<T>& s, const StateDerivative<T>& k, T scale) {
    ThreeBodyState<T> out = s;
    for (std::size_t i = 0; i < 3; ++i) {
        out.body[i].position += k.dPosition[i] * scale;
        out.body[i].velocity += k.dVelocity[i] * scale;
    }
    out.simulationTime = s.simulationTime + scale;
    return out;
}

} // namespace

template <typename T>
void RK4Integrator<T>::step(ThreeBodyState<T>& state, T dt) const {
    const auto k1 = evaluateDerivative(state, G_);
    const auto s2 = addScaled(state, k1, dt * static_cast<T>(0.5));
    const auto k2 = evaluateDerivative(s2, G_);
    const auto s3 = addScaled(state, k2, dt * static_cast<T>(0.5));
    const auto k3 = evaluateDerivative(s3, G_);
    const auto s4 = addScaled(state, k3, dt);
    const auto k4 = evaluateDerivative(s4, G_);

    const T sixth = static_cast<T>(1) / static_cast<T>(6);
    for (std::size_t i = 0; i < 3; ++i) {
        const Vec3<T> dp = (k1.dPosition[i] + static_cast<T>(2) * k2.dPosition[i] +
                            static_cast<T>(2) * k3.dPosition[i] + k4.dPosition[i]) * (dt * sixth);
        const Vec3<T> dv = (k1.dVelocity[i] + static_cast<T>(2) * k2.dVelocity[i] +
                            static_cast<T>(2) * k3.dVelocity[i] + k4.dVelocity[i]) * (dt * sixth);
        state.body[i].position += dp;
        state.body[i].velocity += dv;
    }
    state.simulationTime += dt;
}

template class RK4Integrator<float>;
template class RK4Integrator<double>;

} // namespace threebody
