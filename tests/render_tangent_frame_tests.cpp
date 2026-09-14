////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_utils.hpp"

#include <gtest/gtest.h>

#include <math/half.h>
#include <math/mat3.h>
#include <math/quat.h>
#include <math/vec3.h>

#include <array>
#include <cmath>
#include <cstddef>

namespace quader {
namespace {

using filament::math::float3;
using filament::math::float4;
using filament::math::mat3f;
using filament::math::quatf;

/// Checks the frame consumed after the tangent handedness crosses an FP16
/// varying, including implementations that discard subnormal FP16 values.
void expect_surviving_tangent_frame(const mat3f &authored) {
  const float4 packed = pack_filament_tangent_frame(authored);
  const filament::math::half varying_w(packed.w);
  const auto varying_bits = getBits(varying_w);
  ASSERT_NE(varying_bits & 0x7c00u, 0u)
      << "Handedness must survive GPUs that flush FP16 subnormals";
  ASSERT_NE(varying_bits & 0x7c00u, 0x7c00u);
  const float handedness = static_cast<float>(varying_w) < 0.0f ? -1.0f : 1.0f;
  const quatf rotation{packed.w, packed.x, packed.y, packed.z};
  const mat3f unpacked(rotation);
  const float3 tangent = unpacked[0];
  const float3 normal = unpacked[2];
  const float3 bitangent = cross(normal, tangent) * handedness;

  EXPECT_NEAR(dot(packed, packed), 1.0f, 0.000001f);
  EXPECT_LT(length(tangent - authored[0]), 0.00013f);
  EXPECT_LT(length(normal - authored[2]), 0.00013f);
  // Filament packs the input UV bitangent with the opposite sign to its
  // sampled-normal bitangent. Preserve that convention for mirrored UVs too.
  EXPECT_LT(length(bitangent + authored[1]), 0.00013f);

  const float3 texel = normalize(float3{0.2f, 0.7f, 0.68f});
  const float3 sampled = normalize(tangent * texel.x + bitangent * texel.y +
                                   normal * texel.z);
  const float3 expected = normalize(authored[0] * texel.x -
                                    authored[1] * texel.y +
                                    authored[2] * texel.z);
  EXPECT_LT(length(sampled - expected), 0.00013f);
}

TEST(RenderTangentFrameTests, AxisAlignedAndMirroredFramesRetainNormalMapDetail) {
  constexpr std::array<float3, 6> kNormals{
      float3{1.0f, 0.0f, 0.0f}, float3{-1.0f, 0.0f, 0.0f},
      float3{0.0f, 1.0f, 0.0f}, float3{0.0f, -1.0f, 0.0f},
      float3{0.0f, 0.0f, 1.0f}, float3{0.0f, 0.0f, -1.0f}};
  for (std::size_t axis = 0u; axis < kNormals.size(); ++axis) {
    const float3 normal = kNormals[axis];
    const float3 reference = std::fabs(normal.y) < 0.5f
                                 ? float3{0.0f, 1.0f, 0.0f}
                                 : float3{1.0f, 0.0f, 0.0f};
    float3 tangent = normalize(cross(reference, normal));
    for (int quarter_turn = 0; quarter_turn < 4; ++quarter_turn) {
      for (const float reflection : {1.0f, -1.0f}) {
        SCOPED_TRACE(::testing::Message()
                     << "axis=" << axis << " quarter_turn=" << quarter_turn
                     << " reflection=" << reflection);
        expect_surviving_tangent_frame(
            {tangent, cross(normal, tangent) * reflection, normal});
      }
      tangent = cross(normal, tangent);
    }
  }
}

TEST(RenderTangentFrameTests, GeneralOrthonormalFramesRetainTheirDirections) {
  const float3 normal = normalize(float3{1.0f, 2.0f, -3.0f});
  const float3 tangent = normalize(cross(float3{0.0f, 1.0f, 0.0f}, normal));
  for (const float reflection : {1.0f, -1.0f}) {
    expect_surviving_tangent_frame(
        {tangent, cross(normal, tangent) * reflection, normal});
  }
}

} // namespace
} // namespace quader
