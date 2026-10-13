#pragma once
#include "renderer/common/IRenderBackend.h"
#include "renderer/common/StellarHalo.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <vector>
namespace threebody {
struct SceneVertex {
  float position[3];
  float normal[3];
  float color[4];
};
class SceneMesh {
  struct TrailSample {
    Vec3<float> position;
    double time;
    uint64_t id;
  };
  std::array<std::deque<TrailSample>, 3> trails_;
  uint64_t nextSample_ = 0;
  double lastTime_ = -1;
  uint64_t lastEpoch_ = 0;
  std::vector<SceneVertex> vertices_;
  void vertex(Vec3<float> p, Vec3<float> n, const std::array<float, 4> &c) {
    vertices_.push_back(
        {{p.x, p.y, p.z}, {n.x, n.y, n.z}, {c[0], c[1], c[2], c[3]}});
  }
  void triangle(Vec3<float> a, Vec3<float> b, Vec3<float> c,
                const std::array<float, 4> &color) {
    vertex(a, {0, 0, 0}, color);
    vertex(b, {0, 0, 0}, color);
    vertex(c, {0, 0, 0}, color);
  }

public:
  // Worst case: 3 * 1023 segments * 18 particles * 6 vertices +
  // 3 * 48 * 72 * 6 sphere vertices = 393660, bounded below this capacity.
  static constexpr size_t maxVertices = 524288;
  const std::vector<SceneVertex> &build(const RenderFrameInput &input) {
    if (input.sceneEpoch != lastEpoch_ ||
        input.state.simulationTime < lastTime_) {
      for (auto &t : trails_)
        t.clear();
      nextSample_ = 0;
    }
    if (input.sceneEpoch != lastEpoch_) {
      lastTime_ = -1;
      lastEpoch_ = input.sceneEpoch;
    }
    auto sample = [&](const ThreeBodyState<double> &state) {
      if (state.simulationTime <= lastTime_)
        return;
      for (int i = 0; i < 3; ++i) {
        const auto p = state.body[i].position;
        trails_[i].push_back({{float(p.x), float(p.y), float(p.z)},
                              state.simulationTime,
                              nextSample_});
        if (trails_[i].size() > 1024)
          trails_[i].pop_front();
      }
      ++nextSample_;
      lastTime_ = state.simulationTime;
    };
    if (input.history) {
      for (const auto &state : *input.history)
        sample(state);
    } else
      sample(input.state);
    vertices_.clear();
    vertices_.reserve(maxVertices);
    const std::array<float, 4> colors[3] = {
        {{1.f, .36f, .12f, 1}}, {{.16f, .65f, 1.f, 1}}, {{1.f, .82f, .2f, 1}}};
    const auto camera = boundedCamera(input.camera);
    const Vec3<float> right{-std::sin(camera.yaw), std::cos(camera.yaw), 0};
    const Vec3<float> up{-std::cos(camera.yaw) * std::sin(camera.pitch),
                         -std::sin(camera.yaw) * std::sin(camera.pitch),
                         std::cos(camera.pitch)};
    // Empty space has zero radiance; no decorative ground/grid.
    for (int i = 0; i < 3; ++i) {
      auto &t = trails_[i];
      // Particles follow recorded positions, not a second physics simulation.
      // Stable sample IDs prevent sparkling when old samples leave the deque.
      const unsigned particleTier = std::min(input.workloadLevel, uint32_t(2));
      const int density = 6 + 6 * int(particleTier);
      for (size_t j = 1; input.showTrails && j < t.size(); ++j) {
        const auto delta = t[j].position - t[j - 1].position;
        if (norm(delta) < 1e-8f)
          continue;
        for (int k = 0; k < density; ++k) {
          const float f = (k + .5f) / density;
          const double birth = t[j - 1].time + (t[j].time - t[j - 1].time) * f;
          const float age =
              float(std::max(0.0, input.state.simulationTime - birth));
          if (age >= input.trailDuration)
            continue;
          const float life = 1.f - age / float(input.trailDuration);
          // Fade the oldest retained samples as well as the physical lifetime.
          const float history = std::min(1.f, float(j) / 96.f);
          uint32_t seed =
              uint32_t(t[j].id) * 747796405u + uint32_t(i * 101 + k * 7919);
          auto random = [&]() {
            seed ^= seed >> 16;
            seed *= 2246822519u;
            seed ^= seed >> 13;
            return float(seed & 65535u) / 65535.f;
          };
          Vec3<float> drift{random() - .5f, random() - .5f, random() - .5f};
          const auto pos = t[j - 1].position + delta * f +
                           drift * (.04f + std::min(age, 8.f) * .008f);
          const float size = (.0025f + random() * .003f) * (.35f + .65f * life);
          auto color = colors[i];
          color[3] = 3.f;
          const float brightness =
              (.65f + .55f * random()) * life * std::sqrt(life) * history;
          for (int c = 0; c < 3; ++c)
            color[c] *= brightness;
          // Camera-facing disks: six vertices per small round grain, with
          // local coordinates consumed by the shared circular fragment mask.
          const auto x = right * size, y = up * size;
          vertex(pos - x - y, {-1, -1, 0}, color);
          vertex(pos + x - y, {1, -1, 0}, color);
          vertex(pos + x + y, {1, 1, 0}, color);
          vertex(pos - x - y, {-1, -1, 0}, color);
          vertex(pos + x + y, {1, 1, 0}, color);
          vertex(pos - x + y, {-1, 1, 0}, color);
        }
      }
      const auto p = input.state.body[i].position;
      Vec3<float> center{float(p.x), float(p.y), float(p.z)};
      unsigned tier = std::min(input.workloadLevel, uint32_t(2));
      int latitudes = tier == 0 ? 16 : (tier == 1 ? 32 : 48);
      int longitudes = latitudes * 3 / 2;
      float radius = stellarDisplayRadius(input.state.body[i].mass, tier);
      auto normal = [=](int lat, int lon) {
        float a = lat * 3.14159265f / latitudes,
              b = lon * 6.28318531f / longitudes;
        return Vec3<float>{std::sin(a) * std::cos(b), std::sin(a) * std::sin(b),
                           std::cos(a)};
      };
      for (int lat = 0; lat < latitudes; ++lat)
        for (int lon = 0; lon < longitudes; ++lon) {
          auto a = normal(lat, lon), b = normal(lat + 1, lon),
               c = normal(lat + 1, lon + 1), d = normal(lat, lon + 1);
          for (auto n : {a, b, c, a, c, d})
            vertex(center + n * radius, n, colors[i]);
        }
    }
    if(input.state.planetEnabled){
      const auto p=input.state.planet.position;
      Vec3<float> center{float(p.x),float(p.y),float(p.z)};
      const int latitudes=32,longitudes=48;
      auto normal=[&](int lat,int lon){float a=lat*3.14159265f/latitudes,b=lon*6.28318531f/longitudes;return Vec3<float>{std::sin(a)*std::cos(b),std::sin(a)*std::sin(b),std::cos(a)};};
      for(int lat=0;lat<latitudes;++lat)for(int lon=0;lon<longitudes;++lon){
        auto a=normal(lat,lon),b=normal(lat+1,lon),c=normal(lat+1,lon+1),d=normal(lat,lon+1);
        for(auto n:{a,b,c,a,c,d})vertex(center+n*.07f,n,{{.38f,.30f,.22f,-1.f}});
      }
      // A subdued green path, based on the same recorded physical checkpoints.
      if(input.showTrails && input.history){
        const auto& history=*input.history;
        for(size_t i=1;i<history.size();++i){
          if(!history[i].planetEnabled || !history[i-1].planetEnabled)continue;
          const float age=float(input.state.simulationTime-history[i].simulationTime);
          if(age>input.trailDuration)continue;
          const auto a=history[i-1].planet.position,b=history[i].planet.position;
          Vec3<float> x{float(a.x),float(a.y),float(a.z)},y{float(b.x),float(b.y),float(b.z)};
          const auto w=right*.001f;
          const float fade=std::max(0.f,1.f-age/float(input.trailDuration));
          const std::array<float,4> tint{{.12f*fade,.6f*fade,.25f*fade,0.f}};
          triangle(x-w,x+w,y+w,tint);triangle(x-w,y+w,y-w,tint);
        }
      }
    }
    return vertices_;
  }
};
} // namespace threebody
