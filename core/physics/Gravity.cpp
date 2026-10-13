#include "core/physics/Gravity.h"
#include <cmath>

namespace threebody {

template <typename T>
StateDerivative<T> evaluateDerivative(const ThreeBodyState<T>& state, T G) {
    StateDerivative<T> out{};
    for (std::size_t i = 0; i < 3; ++i) {
        out.dPosition[i] = state.body[i].velocity;
        out.dVelocity[i] = Vec3<T>{};
    }

    // Pair-symmetric force evaluation: one displacement calculation per pair.
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = i + 1; j < 3; ++j) {
            const Vec3<T> r = state.body[j].position - state.body[i].position;
            const T r2 = normSquared(r);
            // The simulator checks close approaches before integration. This last-resort
            // zero-distance guard avoids division by zero without adding softening.
            if (r2 == static_cast<T>(0)) {
                continue;
            }
            const T invR = static_cast<T>(1) / std::sqrt(r2);
            const T invR3 = invR * invR * invR;
            const Vec3<T> direction = r * (G * invR3);
            out.dVelocity[i] += direction * state.body[j].mass;
            out.dVelocity[j] -= direction * state.body[i].mass;
        }
    }
    return out;
}

template StateDerivative<float> evaluateDerivative(const ThreeBodyState<float>&, float);
template StateDerivative<double> evaluateDerivative(const ThreeBodyState<double>&, double);

} // namespace threebody
