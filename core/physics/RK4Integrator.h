#pragma once
#include "core/physics/ThreeBodyState.h"

namespace threebody {

template <typename T>
class RK4Integrator {
public:
    explicit RK4Integrator(T gravitationalConstant = static_cast<T>(1)) : G_(gravitationalConstant) {}
    void step(ThreeBodyState<T>& state, T dt) const;
private:
    T G_;
};

extern template class RK4Integrator<float>;
extern template class RK4Integrator<double>;

} // namespace threebody
