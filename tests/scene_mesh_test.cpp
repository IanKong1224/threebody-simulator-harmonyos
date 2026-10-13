#include "core/physics/InitialConditions.h"
#include "core/physics/RK4Integrator.h"
#include "renderer/common/SceneMesh.h"
#include <iostream>
#include <limits>
using namespace threebody;
std::array<float, 4> project(const Matrix4 &m, Vec3<float> p) {
  return {{m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
           m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
           m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14],
           m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15]}};
}
int main() {
  auto m = cameraMatrix({}, 1);
  auto center = project(m, {0, 0, 0});
  if (std::abs(center[0]) > 1e-5 || std::abs(center[1]) > 1e-5 ||
      center[3] <= 0 || std::abs(center[2] / center[3]) > 1)
    return 1;
  auto p1 = project(m, {.5, 0, 0}),
       p2 = project(cameraMatrix({}, 2), {.5, 0, 0});
  if (std::abs(p1[0] - 2 * p2[0]) > 1e-5)
    return 2;
  auto bounded = boundedCamera({100, 100, -100});
  if (bounded.distance != 2 || bounded.pitch > 1.35f)
    return 3;
  auto invalid = boundedCamera({std::numeric_limits<float>::quiet_NaN(), 0, 0});
  if (!std::isfinite(invalid.yaw) || invalid.distance <= 0)
    return 4;
  RenderFrameInput haloInput{0, makeAsymmetricDemo()};
  haloInput.state.body[0].position = {0, 0, 0};
  auto nearHalo = stellarHalos(haloInput, cameraMatrix(haloInput.camera, 1), false);
  haloInput.camera.distance *= 2;
  auto farHalo = stellarHalos(haloInput, cameraMatrix(haloInput.camera, 1), false);
  if (std::abs(nearHalo[2] - 2 * farHalo[2]) > 1e-6 || nearHalo[3] != 1)
    return 11;
  auto flipped = stellarHalos(haloInput, cameraMatrix(haloInput.camera, 1), true);
  if (std::abs(flipped[5] + farHalo[5] - 1) > 1e-6)
    return 12;
  SceneMesh mesh;
  RenderFrameInput input{1, makeAsymmetricDemo()};
  auto first = mesh.build(input).size();
  if (first < 6000)
    return 5;
  RK4Integrator<double> integrator;
  size_t longest = 0;
  for (int frame = 0; frame < 1100; ++frame) {
    for (int step = 0; step < 10; ++step)
      integrator.step(input.state, 1e-4);
    auto &vertices = mesh.build(input);
    longest = std::max(longest, vertices.size());
    if (vertices.size() > SceneMesh::maxVertices || vertices.size() % 3)
      return 6;
    for (auto &v : vertices) {
      for (float p : v.position)
        if (!std::isfinite(p))
          return 7;
      float n = 0;
      for (float c : v.normal)
        n += c * c;
      if (v.color[3] < 2.5f && n > .001 && std::abs(n - 1) > .001)
        return 8;
    }
  }
  input.workloadLevel=2;
  if(mesh.build(input).size()>SceneMesh::maxVertices)return 10;
  input.workloadLevel=0;
  input.state = makeAsymmetricDemo();
  if (mesh.build(input).size() != first)
    return 9;
  std::cout << "PASS: camera projection/aspect/clamps, finite 3D mesh, unit "
               "normals, bounded trails, restart reset; max vertices="
            << longest << "\n";
}
