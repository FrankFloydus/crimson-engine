////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_utils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace quader {

filament::math::float4 pack_filament_tangent_frame(
    const filament::math::mat3f &frame) {
  filament::math::quatf packed =
      filament::math::mat3f::packTangentFrame(frame);
  // The quaternion's W sign becomes the bitangent sign in Filament. Keep it
  // above the normal FP16 floor: subnormals may be flushed to zero by the GPU.
  constexpr float kMinimumHandednessMagnitude = 0x1p-14f;
  if (std::fabs(packed.w) < kMinimumHandednessMagnitude) {
    const float squared_w = packed.w * packed.w;
    const float squared_minimum =
        kMinimumHandednessMagnitude * kMinimumHandednessMagnitude;
    packed.xyz *= std::sqrt((1.0f - squared_minimum) / (1.0f - squared_w));
    packed.w = std::copysign(kMinimumHandednessMagnitude, packed.w);
  }
  return {packed.x, packed.y, packed.z, packed.w};
}

filament::Box compute_bounds(const MeshAsset &mesh) {
  const MeshGeometryPayload &geometry = mesh.geometry();
  if (geometry.vertices.empty()) {
    return {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
  }

  QVec3 min_value{
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
  };
  QVec3 max_value{
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
  };

  for (const MeshVertex &vertex : geometry.vertices) {
    min_value.x = std::min(min_value.x, vertex.position.x);
    min_value.y = std::min(min_value.y, vertex.position.y);
    min_value.z = std::min(min_value.z, vertex.position.z);
    max_value.x = std::max(max_value.x, vertex.position.x);
    max_value.y = std::max(max_value.y, vertex.position.y);
    max_value.z = std::max(max_value.z, vertex.position.z);
  }

  filament::Box box;
  box.set(to_filament(min_value), to_filament(max_value));
  return box;
}

} // namespace quader
