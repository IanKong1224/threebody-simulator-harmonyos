#pragma once
#include "core/physics/Vec3.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace threebody {
struct CameraState {
  float yaw{-1.5707963f};
  float pitch{0.70f};
  float distance{5.0f};
  Vec3<float> target{};
  float minimumDistance{2.f};
};

using Matrix4 = std::array<float, 16>;
inline CameraState boundedCamera(CameraState c) {
  if (!std::isfinite(c.yaw) || !std::isfinite(c.pitch) ||
      !std::isfinite(c.distance))
    return {};
  c.yaw = std::remainder(c.yaw, 6.28318530718f);
  c.pitch = std::max(-1.35f, std::min(1.35f, c.pitch));
  c.minimumDistance = std::isfinite(c.minimumDistance) ? std::max(.18f,std::min(2.f,c.minimumDistance)) : 2.f;
  c.distance = std::max(c.minimumDistance, std::min(500.f, c.distance));
  return c;
}
inline Matrix4 cameraMatrix(CameraState camera, float aspect) {
  camera = boundedCamera(camera);
  if (!(aspect > 0) || !std::isfinite(aspect))
    aspect = 1;
  const Vec3<float> offset{
      std::cos(camera.yaw) * std::cos(camera.pitch) * camera.distance,
      std::sin(camera.yaw) * std::cos(camera.pitch) * camera.distance,
      std::sin(camera.pitch) * camera.distance};
  const auto eye = offset + camera.target;
  auto f = offset * (-1.f / norm(offset));
  auto side = cross(f, Vec3<float>{0, 0, 1});
  side = side / norm(side);
  auto up = cross(side, f);
  Matrix4 view{{side.x, up.x, -f.x, 0, side.y, up.y, -f.y, 0, side.z, up.z,
                -f.z, 0, -dot(side, eye), -dot(up, eye), dot(f, eye), 1}};
  const float n = .05f, z = 2000.f, k = 2.41421356f;
  Matrix4 projection{{k / aspect, 0, 0, 0, 0, k, 0, 0, 0, 0, -(z + n) / (z - n),
                      -1, 0, 0, -2 * z * n / (z - n), 0}};
  Matrix4 out{};
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      for (int a = 0; a < 4; ++a)
        out[col * 4 + row] += projection[a * 4 + row] * view[col * 4 + a];
  return out;
}
} // namespace threebody
