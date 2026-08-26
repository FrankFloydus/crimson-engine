////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/scene_transform.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

constexpr float kEpsilon = 0.0001F;

void expect_orthonormal_right_handed(const glm::mat3 &basis) {
  for (glm::length_t column = 0; column < 3; ++column) {
    EXPECT_NEAR(glm::length(basis[column]), 1.0F, kEpsilon);
  }
  EXPECT_NEAR(glm::dot(basis[0], basis[1]), 0.0F, kEpsilon);
  EXPECT_NEAR(glm::dot(basis[0], basis[2]), 0.0F, kEpsilon);
  EXPECT_NEAR(glm::dot(basis[1], basis[2]), 0.0F, kEpsilon);
  EXPECT_NEAR(glm::determinant(basis), 1.0F, kEpsilon);
}

TEST(SceneTransformTests,
     PureWorldRotationRemovesPositiveNonuniformScale) {
  const quader::QVec3 euler{0.37F, -0.61F, 1.12F};
  const quader::QTransform transform{
      .position = {4.0F, -2.0F, 9.0F},
      .rotation = quader::quat_from_euler_radians(euler),
      .scale = {2.0F, 0.25F, 7.0F},
  };

  const std::optional<glm::mat3> actual =
      quader::try_scene_pure_world_rotation_basis(
          quader::compose_transform(transform));

  ASSERT_TRUE(actual.has_value());
  const glm::mat3 expected =
      glm::mat3_cast(quader::to_glm(transform.rotation));
  for (glm::length_t column = 0; column < 3; ++column) {
    for (glm::length_t row = 0; row < 3; ++row) {
      EXPECT_NEAR((*actual)[column][row], expected[column][row], kEpsilon);
    }
  }
  expect_orthonormal_right_handed(*actual);
}

TEST(SceneTransformTests,
     PureWorldRotationMakesMirroredNonuniformBasisRightHanded) {
  const quader::QTransform transform{
      .rotation =
          quader::quat_from_euler_radians({-0.43F, 0.89F, 0.21F}),
      .scale = {-0.125F, 3.0F, 11.0F},
  };

  const std::optional<glm::mat3> actual =
      quader::try_scene_pure_world_rotation_basis(
          quader::compose_transform(transform));

  ASSERT_TRUE(actual.has_value());
  expect_orthonormal_right_handed(*actual);
}

TEST(SceneTransformTests, PureWorldRotationRejectsInvalidOrSingularMatrices) {
  quader::QTransform singular;
  singular.scale = {1.0F, 0.0F, 1.0F};
  EXPECT_FALSE(quader::try_scene_pure_world_rotation_basis(
                   quader::compose_transform(singular))
                   .has_value());

  quader::QMat4 nonfinite;
  nonfinite.values[5] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(
      quader::try_scene_pure_world_rotation_basis(nonfinite).has_value());
}

} // namespace
