#pragma once
#include "renderer/common/IRenderBackend.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <vector>
namespace threebody {
struct ColorRect {
  int x, y, w, h;
  float r, g, b;
};
// Shared deterministic preview geometry; no platform or graphics API types.
class PreviewGeometry {
  std::array<std::deque<Vec3<double>>, 3> trails_;
  double lastTime_{-1};

public:
  std::vector<ColorRect> build(const RenderFrameInput &in, int width,
                               int height) {
    if (in.state.simulationTime < lastTime_)
      for (auto &t : trails_)
        t.clear();
    if (in.state.simulationTime != lastTime_) {
      for (int i = 0; i < 3; ++i) {
        trails_[i].push_back(in.state.body[i].position);
        if (trails_[i].size() > 384)
          trails_[i].pop_front();
      }
      lastTime_ = in.state.simulationTime;
    }
    const float colors[3][3] = {
        {1.f, .35f, .15f}, {.18f, .7f, 1.f}, {1.f, .85f, .25f}};
    const double scale = std::min(width, height) * .34;
    std::vector<ColorRect> out;
    auto rect = [&](int x, int y, int w, int h, int i, float fade) {
      int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(width, x + w),
          y1 = std::min(height, y + h);
      if (x1 > x0 && y1 > y0)
        out.push_back({x0, y0, x1 - x0, y1 - y0, colors[i][0] * fade,
                       colors[i][1] * fade, colors[i][2] * fade});
    };
    auto px = [&](double x) { return int(width * .5 + x * scale); };
    auto py = [&](double y) { return int(height * .5 - y * scale); };
    for (int i = 0; i < 3; ++i) {
      auto &t = trails_[i];
      for (size_t j = 1; j < t.size(); ++j) {
        int x0 = px(t[j - 1].x), y0 = py(t[j - 1].y), x1 = px(t[j].x),
            y1 = py(t[j].y);
        int n =
            std::min(2048, std::max(std::abs(x1 - x0), std::abs(y1 - y0)) + 1);
        float fade = .12f + .55f * float(j) / float(t.size());
        for (int k = 0; k <= n; ++k)
          rect(x0 + (x1 - x0) * k / n - 1, y0 + (y1 - y0) * k / n - 1, 3, 3, i,
               fade);
      }
      int x = px(in.state.body[i].position.x),
          y = py(in.state.body[i].position.y);
      int radius = std::max(5, int(std::min(width, height) * .017));
      for (int dy = -radius; dy <= radius; ++dy) {
        int dx = int(std::sqrt(double(radius * radius - dy * dy)));
        rect(x - dx, y + dy, 2 * dx + 1, 1, i, 1.f);
      }
    }
    return out;
  }
};
} // namespace threebody
