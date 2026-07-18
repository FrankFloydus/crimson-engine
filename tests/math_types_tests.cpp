////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "math/glm_bridge.hpp"
#include "math/types.hpp"

#include <gtest/gtest.h>

using namespace quader;

namespace {

void expect_q_vec3_near(QVec3 actual, QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, 0.001f);
  EXPECT_NEAR(actual.y, expected.y, 0.001f);
  EXPECT_NEAR(actual.z, expected.z, 0.001f);
}

} // namespace

TEST(MathTypesTests, QuaternionFromEulerMatchesExpectedAxisRotations) {
  constexpr float kHalfPi = 1.57079632679489661923f;

  expect_q_vec3_near(transform_vector(compose_transform(QTransform{
                                          .rotation = quat_from_euler_radians(
                                              {kHalfPi, 0.0f, 0.0f})}),
                                      {0.0f, 1.0f, 0.0f}),
                     {0.0f, 0.0f, 1.0f});
  expect_q_vec3_near(transform_vector(compose_transform(QTransform{
                                          .rotation = quat_from_euler_radians(
                                              {0.0f, kHalfPi, 0.0f})}),
                                      {0.0f, 0.0f, 1.0f}),
                     {1.0f, 0.0f, 0.0f});
  expect_q_vec3_near(transform_vector(compose_transform(QTransform{
                                          .rotation = quat_from_euler_radians(
                                              {0.0f, 0.0f, kHalfPi})}),
                                      {1.0f, 0.0f, 0.0f}),
                     {0.0f, 1.0f, 0.0f});
}

TEST(MathTypesTests, QuaternionProductAppliesRightRotationBeforeLeftRotation) {
  constexpr float kHalfPi = 1.57079632679489661923f;
  const QQuat pitch = quat_from_euler_radians({kHalfPi, 0.0f, 0.0f});
  const QQuat yaw = quat_from_euler_radians({0.0f, kHalfPi, 0.0f});
  const QMat4 composed =
      compose_transform(QTransform{.rotation = normalize(pitch * yaw)});

  expect_q_vec3_near(transform_vector(composed, {0.0f, 1.0f, 0.0f}),
                     {0.0f, 0.0f, 1.0f});
  expect_q_vec3_near(transform_vector(composed, {0.0f, 0.0f, 1.0f}),
                     {1.0f, 0.0f, 0.0f});
}

TEST(MathTypesTests, GlmBridgePreservesColumnMajorMat4Storage) {
  const QMat4 transform = compose_transform(QTransform{
      .position = {3.0f, 4.0f, 5.0f},
      .rotation = quat_from_euler_radians({0.0f, 0.0f, 0.0f}),
      .scale = {2.0f, 3.0f, 4.0f},
  });

  expect_q_vec3_near(transform_point(transform, {1.0f, 1.0f, 1.0f}),
                     {5.0f, 7.0f, 9.0f});
  expect_q_vec3_near(transform_point(inverse(transform), {5.0f, 7.0f, 9.0f}),
                     {1.0f, 1.0f, 1.0f});
}
