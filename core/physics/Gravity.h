#pragma once
#include "core/physics/ThreeBodyState.h"

namespace threebody {

template <typename T>
StateDerivative<T> evaluateDerivative(const ThreeBodyState<T>& state, T gravitationalConstant = static_cast<T>(1));

extern template StateDerivative<float> evaluateDerivative(const ThreeBodyState<float>&, float);
extern template StateDerivative<double> evaluateDerivative(const ThreeBodyState<double>&, double);

} // namespace threebody
