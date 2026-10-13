#pragma once
#include "renderer/common/SceneMesh.h"
namespace threebody {
// LDR, screen-sized markers and thin ribbons. No sphere/particle geometry.
class BasicMesh {
  std::vector<SceneVertex> vertices_;

public:
  static constexpr size_t maxVertices = 3 * 2048 * 6 + 18;
  const std::vector<SceneVertex> &build(const RenderFrameInput &in,
                                        uint32_t width, uint32_t height) {
    vertices_.clear();
    vertices_.reserve(maxVertices);
    const auto m = cameraMatrix(in.camera, float(width) / height);
    auto project = [&](Vec3<double> p, Vec3<float> &out) {
      const double w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
      if (w < .051)
        return false;
      out = {float((m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) / w),
             float((m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) / w),
             float((m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]) / w)};
      return out.z < 1;
    };
    const std::array<float, 4> colors[3] = {
        {{1, .42f, .18f, 1}}, {{.2f, .7f, 1, 1}}, {{1, .86f, .25f, 1}}};
    auto vertex = [&](Vec3<float> p, float u, float v, std::array<float, 4> c) {
      vertices_.push_back(
          {{p.x, p.y, p.z}, {u, v, 0}, {c[0], c[1], c[2], c[3]}});
    };
    if (in.showTrails && in.history)
      for (int i = 0; i < 3; ++i) {
        const auto &h = *in.history;
        for (size_t j = 1; j < h.size(); ++j) {
          if (in.state.simulationTime - h[j - 1].simulationTime >
              in.trailDuration)
            continue;
          Vec3<float> a, b;
          if (!project(h[j - 1].body[i].position, a) ||
              !project(h[j].body[i].position, b))
            continue;
          const float dx = (b.x - a.x) * width, dy = (b.y - a.y) * height;
          const float len = std::sqrt(dx * dx + dy * dy);
          if (len < 1e-5f)
            continue;
          // 1.5 pixel line: same thickness at every zoom, no decorative glow.
          Vec3<float> n{-dy / len * 1.5f / width, dx / len * 1.5f / height, 0};
          auto c = colors[i];
          for (int k = 0; k < 3; ++k)
            c[k] *= .65f;
          for (auto p : {a - n, a + n, b + n, a - n, b + n, b - n})
            vertex(p, 0, 0, c);
        }
      }
    for (int i = 0; i < 3; ++i) {
      Vec3<float> p;
      if (!project(in.state.body[i].position, p))
        continue;
      const Vec3<float> x{10.f / width, 0, 0}, y{0, 10.f / height, 0};
      auto c = colors[i];
      c[3] = 2;
      vertex(p - x - y, -1, -1, c);
      vertex(p + x - y, 1, -1, c);
      vertex(p + x + y, 1, 1, c);
      vertex(p - x - y, -1, -1, c);
      vertex(p + x + y, 1, 1, c);
      vertex(p - x + y, -1, 1, c);
    }
    return vertices_;
  }
};
} // namespace threebody
