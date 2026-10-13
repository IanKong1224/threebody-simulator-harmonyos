#pragma once
#include "renderer/common/IRenderBackend.h"
#include <array>
namespace threebody {
inline float stellarDisplayRadius(double mass, uint32_t tier) {
  return (tier == 0 ? .10f : (tier == 1 ? .16f : .22f)) * std::cbrt(float(mass));
}
// Projected center and radius in viewport-height units. Post-processing needs
// only three small records; no extra render target, geometry or blur pass.
inline std::array<float, 12> stellarHalos(const RenderFrameInput &in,
                                        const Matrix4 &m, bool flipY) {
  std::array<float, 12> out{};
  for (int i = 0; i < 3; ++i) {
    const auto p = in.state.body[i].position;
    const float w = m[3]*p.x + m[7]*p.y + m[11]*p.z + m[15];
    const float r = stellarDisplayRadius(in.state.body[i].mass,
                                        std::min(in.workloadLevel, uint32_t(2)));
    if (w <= r + .05f) continue; // no halo for clipped/behind-camera stars
    const float x = (m[0]*p.x + m[4]*p.y + m[8]*p.z + m[12])/w;
    const float y = (m[1]*p.x + m[5]*p.y + m[9]*p.z + m[13])/w;
    out[i*4] = .5f + .5f*x;
    out[i*4+1] = .5f + (flipY ? -.5f : .5f)*y;
    out[i*4+2] = 1.20710678f*r/w;
    out[i*4+3] = 1.f;
  }
  return out;
}
}
