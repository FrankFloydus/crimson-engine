////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/array/path_array_layout.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>

namespace quader {
namespace {

constexpr float kTolerance = 0.0001F;

static_assert(!std::is_default_constructible_v<PathArrayLayoutBackbone>);

void expect_vector_near(QVec3 actual, QVec3 expected,
                        float tolerance = kTolerance) {
  EXPECT_NEAR(actual.x, expected.x, tolerance);
  EXPECT_NEAR(actual.y, expected.y, tolerance);
  EXPECT_NEAR(actual.z, expected.z, tolerance);
}

void expect_matrix_near(const QMat4 &actual, const QMat4 &expected,
                        float tolerance = kTolerance) {
  for (std::size_t index = 0U; index < actual.values.size(); ++index) {
    EXPECT_NEAR(actual.values[index], expected.values[index], tolerance)
        << "matrix component " << index;
  }
}

QVec3 translation(const QMat4 &matrix) {
  return {matrix.values[12U], matrix.values[13U], matrix.values[14U]};
}

QVec3 normalized_axis(const QMat4 &matrix, QVec3 local_axis) {
  return normalize_or_zero(transform_vector(matrix, local_axis));
}

float axis_length(const QMat4 &matrix, QVec3 local_axis) {
  return length(transform_vector(matrix, local_axis));
}

std::vector<PathArrayPathStation> straight_path(double length) {
  return {
      {.distance = 0.0,
       .position_world = {0.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
      {.distance = length,
       .position_world = {static_cast<float>(length), 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
  };
}

TEST(PathArrayLayoutTests,
     OpenPathKeepsExactSpacingAndExtrapolatesPastAuthoredEnd) {
  const std::vector<PathArrayPathStation> path = straight_path(10.0);
  const QMat4 source = compose_transform(QTransform{
      .position = {50.0F, 3.0F, -2.0F},
      .rotation = quat_from_euler_radians({0.3F, -0.4F, 0.2F}),
      .scale = {2.0F, 3.0F, 4.0F},
  });

  const PathArrayLayoutResult result = build_path_array_layout({
      .path_stations = path,
      .source_world_transform = source,
      .source_pivot_world = {50.0F, 3.0F, -2.0F},
      .count = 4U,
      .distance = 8.0,
  });

  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_EQ(result.world_transforms.size(), 4U);
  EXPECT_DOUBLE_EQ(result.effective_distance, 8.0);
  EXPECT_DOUBLE_EQ(result.total_path_length, 10.0);
  for (std::size_t index = 0U; index < result.world_transforms.size(); ++index) {
    expect_vector_near(translation(result.world_transforms[index]),
                       {static_cast<float>(index) * 8.0F, 0.0F, 0.0F});
  }
  for (const QMat4 &world : result.world_transforms) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      for (std::size_t row = 0U; row < 4U; ++row) {
        EXPECT_FLOAT_EQ(world.values[column * 4U + row],
                        source.values[column * 4U + row]);
      }
    }
  }
}

TEST(PathArrayLayoutTests, ClosedPathKeepsExactSpacingAndWrapsAtTheSeam) {
  const std::array<PathArrayPathStation, 5U> path = {{
      {.distance = 0.0,
       .position_world = {0.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
      {.distance = 1.0,
       .position_world = {1.0F, 0.0F, 0.0F},
       .tangent_world = {0.0F, 1.0F, 0.0F}},
      {.distance = 2.0,
       .position_world = {1.0F, 1.0F, 0.0F},
       .tangent_world = {-1.0F, 0.0F, 0.0F}},
      {.distance = 3.0,
       .position_world = {0.0F, 1.0F, 0.0F},
       .tangent_world = {0.0F, -1.0F, 0.0F}},
      {.distance = 4.0,
       .position_world = {0.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
  }};

  const PathArrayLayoutResult result = build_path_array_layout({
      .path_stations = path,
      .count = 4U,
      .distance = 5.0,
      .closed = true,
  });

  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_EQ(result.world_transforms.size(), 4U);
  EXPECT_DOUBLE_EQ(result.effective_distance, 5.0);
  expect_vector_near(translation(result.world_transforms[0U]),
                     {0.0F, 0.0F, 0.0F});
  expect_vector_near(translation(result.world_transforms[1U]),
                     {1.0F, 0.0F, 0.0F});
  expect_vector_near(translation(result.world_transforms[2U]),
                     {1.0F, 1.0F, 0.0F});
  expect_vector_near(translation(result.world_transforms[3U]),
                     {0.0F, 1.0F, 0.0F});
}

TEST(PathArrayLayoutTests, ClosedPathRetainsEveryIntentionalSeamOverlap) {
  const std::array<PathArrayPathStation, 5U> path = {{
      {.distance = 0.0,
       .position_world = {0.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
      {.distance = 1.0,
       .position_world = {1.0F, 0.0F, 0.0F},
       .tangent_world = {0.0F, 1.0F, 0.0F}},
      {.distance = 2.0,
       .position_world = {1.0F, 1.0F, 0.0F},
       .tangent_world = {-1.0F, 0.0F, 0.0F}},
      {.distance = 3.0,
       .position_world = {0.0F, 1.0F, 0.0F},
       .tangent_world = {0.0F, -1.0F, 0.0F}},
      {.distance = 4.0,
       .position_world = {0.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
  }};

  const PathArrayLayoutResult result = build_path_array_layout({
      .path_stations = path,
      .count = 5U,
      .distance = 4.0,
      .follow_path = true,
      .closed = true,
  });

  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_EQ(result.world_transforms.size(), 5U);
  EXPECT_DOUBLE_EQ(result.effective_distance, 4.0);
  for (std::size_t index = 1U; index < result.world_transforms.size(); ++index) {
    EXPECT_EQ(result.world_transforms[index].values,
              result.world_transforms.front().values)
        << "overlapping station " << index << " was omitted or altered";
  }
}

TEST(PathArrayLayoutTests,
     IncreasingCountPreservesExactOpenAndClosedPrefixTransforms) {
  const std::array<PathArrayPathStation, 5U> closed_path = {{
      {.distance = 0.0,
       .position_world = {0.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
      {.distance = 1.0,
       .position_world = {1.0F, 0.0F, 0.0F},
       .tangent_world = {0.0F, 1.0F, 0.0F}},
      {.distance = 2.0,
       .position_world = {1.0F, 1.0F, 0.0F},
       .tangent_world = {-1.0F, 0.0F, 0.0F}},
      {.distance = 3.0,
       .position_world = {0.0F, 1.0F, 0.0F},
       .tangent_world = {0.0F, -1.0F, 0.0F}},
      {.distance = 4.0,
       .position_world = {0.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
  }};
  const std::vector<PathArrayPathStation> open_path = straight_path(2.0);

  const auto expect_prefix_stable = [](auto path, bool closed,
                                       double distance) {
    const PathArrayLayoutResult before = build_path_array_layout({
        .path_stations = path,
        .count = 4U,
        .distance = distance,
        .follow_path = true,
        .closed = closed,
    });
    const PathArrayLayoutResult after = build_path_array_layout({
        .path_stations = path,
        .count = 12U,
        .distance = distance,
        .follow_path = true,
        .closed = closed,
    });
    ASSERT_TRUE(before.ok) << before.message;
    ASSERT_TRUE(after.ok) << after.message;
    ASSERT_EQ(before.world_transforms.size(), 4U);
    ASSERT_EQ(after.world_transforms.size(), 12U);
    for (std::size_t index = 0U; index < before.world_transforms.size();
         ++index) {
      EXPECT_EQ(after.world_transforms[index].values,
                before.world_transforms[index].values)
          << "prefix placement " << index << " changed";
    }
  };

  expect_prefix_stable(std::span<const PathArrayPathStation>{open_path}, false,
                       1.5);
  expect_prefix_stable(std::span<const PathArrayPathStation>{closed_path}, true,
                       1.5);
}

TEST(PathArrayLayoutTests, FollowPathAlignsTheSelectedSignedAxis) {
  const std::array<PathArrayPathStation, 3U> path = {{
      {.distance = 0.0,
       .position_world = {0.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
      {.distance = 2.0,
       .position_world = {2.0F, 0.0F, 0.0F},
       .tangent_world = {0.0F, 0.0F, 1.0F}},
      {.distance = 4.0,
       .position_world = {2.0F, 0.0F, 2.0F},
       .tangent_world = {0.0F, 0.0F, 1.0F}},
  }};
  const QMat4 source = compose_transform(QTransform{
      .position = {8.0F, -3.0F, 6.0F},
      .rotation = quat_from_euler_radians({0.2F, 0.4F, -0.1F}),
      .scale = {2.0F, 3.0F, 4.0F},
  });

  const PathArrayLayoutResult result = build_path_array_layout({
      .path_stations = path,
      .source_world_transform = source,
      .source_pivot_world = {8.0F, -3.0F, 6.0F},
      .count = 3U,
      .distance = 2.0,
      .follow_path = true,
      .signed_axis = PathArraySignedAxis::NegativeZ,
  });

  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_EQ(result.world_transforms.size(), 3U);
  expect_vector_near(
      normalized_axis(result.world_transforms[0U], {0.0F, 0.0F, -1.0F}),
      {1.0F, 0.0F, 0.0F});
  expect_vector_near(
      normalized_axis(result.world_transforms[1U], {0.0F, 0.0F, -1.0F}),
      {0.0F, 0.0F, 1.0F});
  expect_vector_near(
      normalized_axis(result.world_transforms[2U], {0.0F, 0.0F, -1.0F}),
      {0.0F, 0.0F, 1.0F});
  expect_vector_near(translation(result.world_transforms[0U]), {});
  expect_vector_near(translation(result.world_transforms[2U]),
                     {2.0F, 0.0F, 2.0F});
}

TEST(PathArrayLayoutTests, FollowPathPlacesTheRequestedVisiblePivot) {
  const std::vector<PathArrayPathStation> path = straight_path(6.0);
  const QMat4 source = compose_transform(QTransform{
      .position = {4.0F, -2.0F, 3.0F},
      .rotation = quat_from_euler_radians({0.2F, 0.5F, -0.35F}),
      .scale = {2.0F, 3.0F, 0.5F},
  });
  const QVec3 pivot_world{5.0F, 1.0F, 2.0F};
  const QVec3 local_pivot = transform_point(inverse(source), pivot_world);

  const PathArrayLayoutResult result = build_path_array_layout({
      .path_stations = path,
      .source_world_transform = source,
      .source_pivot_world = pivot_world,
      .count = 3U,
      .distance = 3.0,
      .follow_path = true,
      .signed_axis = PathArraySignedAxis::PositiveY,
  });

  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_EQ(result.world_transforms.size(), 3U);
  expect_vector_near(transform_point(result.world_transforms[0U], local_pivot),
                     {0.0F, 0.0F, 0.0F});
  expect_vector_near(transform_point(result.world_transforms[1U], local_pivot),
                     {3.0F, 0.0F, 0.0F});
  expect_vector_near(transform_point(result.world_transforms[2U], local_pivot),
                     {6.0F, 0.0F, 0.0F});
}

TEST(PathArrayLayoutTests, RotationMinimizingFramesKeepPlanarRollStable) {
  constexpr float kRootHalf = 0.70710678F;
  const std::array<PathArrayPathStation, 3U> path = {{
      {.distance = 0.0,
       .position_world = {0.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
      {.distance = 1.0,
       .position_world = {kRootHalf, kRootHalf, 0.0F},
       .tangent_world = {kRootHalf, kRootHalf, 0.0F}},
      {.distance = 2.0,
       .position_world = {1.0F, 1.0F, 0.0F},
       .tangent_world = {0.0F, 1.0F, 0.0F}},
  }};

  const PathArrayLayoutResult result = build_path_array_layout({
      .path_stations = path,
      .count = 3U,
      .distance = 1.0,
      .follow_path = true,
      .signed_axis = PathArraySignedAxis::PositiveX,
  });

  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_EQ(result.world_transforms.size(), 3U);
  for (const QMat4 &world : result.world_transforms) {
    expect_vector_near(normalized_axis(world, {0.0F, 0.0F, 1.0F}),
                       {0.0F, 0.0F, 1.0F});
  }
}

TEST(PathArrayLayoutTests, RepeatedCornerStationUsesTheOutgoingTangent) {
  const std::array<PathArrayPathStation, 4U> path = {{
      {.distance = 0.0,
       .position_world = {0.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
      {.distance = 1.0,
       .position_world = {1.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
      {.distance = 1.0,
       .position_world = {1.0F, 0.0F, 0.0F},
       .tangent_world = {0.0F, 1.0F, 0.0F}},
      {.distance = 2.0,
       .position_world = {1.0F, 1.0F, 0.0F},
       .tangent_world = {0.0F, 1.0F, 0.0F}},
  }};

  const PathArrayLayoutResult result = build_path_array_layout({
      .path_stations = path,
      .count = 3U,
      .distance = 1.0,
      .follow_path = true,
  });

  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_EQ(result.world_transforms.size(), 3U);
  expect_vector_near(
      normalized_axis(result.world_transforms[0U], {1.0F, 0.0F, 0.0F}),
      {1.0F, 0.0F, 0.0F});
  expect_vector_near(
      normalized_axis(result.world_transforms[1U], {1.0F, 0.0F, 0.0F}),
      {0.0F, 1.0F, 0.0F});
  expect_vector_near(
      normalized_axis(result.world_transforms[2U], {1.0F, 0.0F, 0.0F}),
      {0.0F, 1.0F, 0.0F});
}

TEST(PathArrayLayoutTests,
     FinalOpenEndpointAndOverflowUseTheIncomingTerminalDirection) {
  const std::array<PathArrayPathStation, 3U> path = {{
      {.distance = 0.0,
       .position_world = {0.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
      {.distance = 1.0,
       .position_world = {1.0F, 0.0F, 0.0F},
       .tangent_world = {0.0F, 1.0F, 0.0F}},
      {.distance = 2.0,
       .position_world = {1.0F, 1.0F, 0.0F},
       .tangent_world = {}},
  }};

  const PathArrayLayoutResult result = build_path_array_layout({
      .path_stations = path,
      .count = 4U,
      .distance = 1.0,
      .follow_path = true,
  });

  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_EQ(result.world_transforms.size(), 4U);
  expect_vector_near(
      normalized_axis(result.world_transforms[2U], {1.0F, 0.0F, 0.0F}),
      {0.0F, 1.0F, 0.0F});
  expect_vector_near(
      normalized_axis(result.world_transforms[3U], {1.0F, 0.0F, 0.0F}),
      {0.0F, 1.0F, 0.0F});
  expect_vector_near(translation(result.world_transforms[3U]),
                     {1.0F, 2.0F, 0.0F});
}

TEST(PathArrayLayoutTests,
     VariationKeepsStationZeroExactAndComposesCumulativeLocalSteps) {
  const std::vector<PathArrayPathStation> path = straight_path(8.0);
  const QVec3 pivot{1.0F, 2.0F, 0.0F};
  PathArrayLayoutRequest request{
      .path_stations = path,
      .source_pivot_world = pivot,
      .count = 3U,
      .distance = 2.0,
  };
  request.variation.rotation_offset_degrees = {10.0F, 20.0F, 30.0F};
  request.variation.scale_offset_factors = {2.0F, 0.5F, 1.0F};

  const PathArrayLayoutResult varied = build_path_array_layout(request);
  ASSERT_TRUE(varied.ok) << varied.message;
  ASSERT_EQ(varied.world_transforms.size(), 3U);

  request.variation = {};
  const PathArrayLayoutResult neutral = build_path_array_layout(request);
  ASSERT_TRUE(neutral.ok) << neutral.message;
  EXPECT_EQ(varied.world_transforms.front().values,
            neutral.world_transforms.front().values)
      << "the reused source at station zero must be byte-exact";

  const glm::quat step = to_glm(quat_from_euler_radians(
      {10.0F * std::numbers::pi_v<float> / 180.0F,
       20.0F * std::numbers::pi_v<float> / 180.0F,
       30.0F * std::numbers::pi_v<float> / 180.0F}));
  const glm::quat twice = glm::normalize(step * step);
  expect_vector_near(normalized_axis(varied.world_transforms[1U],
                                     {1.0F, 0.0F, 0.0F}),
                     from_glm(step * glm::vec3{1.0F, 0.0F, 0.0F}));
  expect_vector_near(normalized_axis(varied.world_transforms[2U],
                                     {1.0F, 0.0F, 0.0F}),
                     from_glm(twice * glm::vec3{1.0F, 0.0F, 0.0F}));
  EXPECT_NEAR(axis_length(varied.world_transforms[1U], {1.0F, 0.0F, 0.0F}),
              2.0F, kTolerance);
  EXPECT_NEAR(axis_length(varied.world_transforms[1U], {0.0F, 1.0F, 0.0F}),
              0.5F, kTolerance);
  EXPECT_NEAR(axis_length(varied.world_transforms[2U], {1.0F, 0.0F, 0.0F}),
              4.0F, kTolerance);
  EXPECT_NEAR(axis_length(varied.world_transforms[2U], {0.0F, 1.0F, 0.0F}),
              0.25F, kTolerance);
  for (std::size_t index = 0U; index < varied.world_transforms.size(); ++index) {
    expect_vector_near(transform_point(varied.world_transforms[index], pivot),
                       {static_cast<float>(index) * 2.0F, 0.0F, 0.0F});
  }
}

TEST(PathArrayLayoutTests,
     ExplicitVariationFramePreservesMirroredSourceLocalRotation) {
  const std::vector<PathArrayPathStation> path = straight_path(4.0);
  const QQuat source_rotation =
      quat_from_euler_radians({0.25F, -0.15F, 0.4F});
  const QVec3 source_scale{-2.0F, 0.75F, 1.25F};
  const QMat4 source = compose_transform(QTransform{
      .rotation = source_rotation,
      .scale = source_scale,
  });
  const QMat4 variation_frame =
      from_glm(glm::mat4_cast(to_glm(source_rotation)));
  PathArrayLayoutRequest request{
      .path_stations = path,
      .source_world_transform = source,
      .source_variation_frame_world = variation_frame,
      .count = 2U,
      .distance = 1.0,
  };
  request.variation.rotation_offset_degrees = {0.0F, 0.0F, 30.0F};

  const PathArrayLayoutResult result = build_path_array_layout(request);

  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_EQ(result.world_transforms.size(), 2U);
  EXPECT_EQ(result.world_transforms.front().values, source.values)
      << "station zero must retain the exact mirrored source matrix";
  const QQuat rotation_step = quat_from_euler_radians(
      {0.0F, 0.0F, 30.0F * std::numbers::pi_v<float> / 180.0F});
  const QMat4 expected = compose_transform(QTransform{
      .position = {1.0F, 0.0F, 0.0F},
      .rotation = normalize(source_rotation * rotation_step),
      .scale = source_scale,
  });
  expect_matrix_near(result.world_transforms[1U], expected);
}

TEST(PathArrayLayoutTests,
     RandomVariationIsDeterministicBoundedAndCountPrefixStable) {
  const std::vector<PathArrayPathStation> path = straight_path(16.0);
  PathArrayLayoutRequest request{
      .path_stations = path,
      .count = 4U,
      .distance = 1.0,
  };
  request.variation.rotation_random_min_degrees = {0.0F, 0.0F, 10.0F};
  request.variation.rotation_random_max_degrees = {0.0F, 0.0F, 20.0F};
  request.variation.scale_random_min_factors = {0.5F, 0.75F, 1.25F};
  request.variation.scale_random_max_factors = {1.5F, 1.25F, 2.0F};
  request.variation.seed = 0x123456789abcdef0ULL;

  const PathArrayLayoutResult first = build_path_array_layout(request);
  const PathArrayLayoutResult repeated = build_path_array_layout(request);
  request.count = 8U;
  const PathArrayLayoutResult grown = build_path_array_layout(request);
  ASSERT_TRUE(first.ok) << first.message;
  ASSERT_TRUE(repeated.ok) << repeated.message;
  ASSERT_TRUE(grown.ok) << grown.message;
  ASSERT_EQ(first.world_transforms.size(), 4U);
  ASSERT_EQ(grown.world_transforms.size(), 8U);
  EXPECT_EQ(first.world_transforms.front().values, QMat4{}.values)
      << "random ranges must not modify the reused source at station zero";
  for (std::size_t index = 0U; index < first.world_transforms.size(); ++index) {
    EXPECT_EQ(first.world_transforms[index].values,
              repeated.world_transforms[index].values);
    EXPECT_EQ(first.world_transforms[index].values,
              grown.world_transforms[index].values)
        << "Count growth rerolled station " << index;
  }
  EXPECT_FLOAT_EQ(axis_length(first.world_transforms[0U],
                              {1.0F, 0.0F, 0.0F}),
                  1.0F);
  for (std::size_t index = 1U; index < first.world_transforms.size(); ++index) {
    const float x_scale =
        axis_length(first.world_transforms[index], {1.0F, 0.0F, 0.0F});
    const float y_scale =
        axis_length(first.world_transforms[index], {0.0F, 1.0F, 0.0F});
    const float z_scale =
        axis_length(first.world_transforms[index], {0.0F, 0.0F, 1.0F});
    EXPECT_GE(x_scale, 0.5F);
    EXPECT_LE(x_scale, 1.5F);
    EXPECT_GE(y_scale, 0.75F);
    EXPECT_LE(y_scale, 1.25F);
    EXPECT_GE(z_scale, 1.25F);
    EXPECT_LE(z_scale, 2.0F);
    const QVec3 x_axis =
        normalized_axis(first.world_transforms[index], {1.0F, 0.0F, 0.0F});
    const float degrees =
        std::atan2(x_axis.y, x_axis.x) * 180.0F / std::numbers::pi_v<float>;
    EXPECT_GE(degrees, 10.0F - kTolerance);
    EXPECT_LE(degrees, 20.0F + kTolerance);
  }

  request.count = 4U;
  request.variation.scale_random_min_factors.y = 0.25F;
  request.variation.scale_random_max_factors.y = 3.0F;
  const PathArrayLayoutResult changed_y = build_path_array_layout(request);
  ASSERT_TRUE(changed_y.ok) << changed_y.message;
  for (std::size_t index = 1U; index < first.world_transforms.size(); ++index) {
    EXPECT_FLOAT_EQ(
        axis_length(first.world_transforms[index], {1.0F, 0.0F, 0.0F}),
        axis_length(changed_y.world_transforms[index], {1.0F, 0.0F, 0.0F}))
        << "editing the Y range rerolled the X channel";
    expect_vector_near(
        normalized_axis(first.world_transforms[index], {1.0F, 0.0F, 0.0F}),
        normalized_axis(changed_y.world_transforms[index],
                        {1.0F, 0.0F, 0.0F}));
  }
}

TEST(PathArrayLayoutTests, VariationKeepsTheVisiblePivotOnFollowedStations) {
  const std::array<PathArrayPathStation, 3U> path = {{
      {.distance = 0.0,
       .position_world = {0.0F, 0.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, 0.0F}},
      {.distance = 2.0,
       .position_world = {2.0F, 0.0F, 0.0F},
       .tangent_world = {0.0F, 1.0F, 0.0F}},
      {.distance = 4.0,
       .position_world = {2.0F, 2.0F, 0.0F},
       .tangent_world = {0.0F, 1.0F, 0.0F}},
  }};
  const QMat4 source = compose_transform(QTransform{
      .position = {5.0F, -2.0F, 1.0F},
      .rotation = quat_from_euler_radians({0.2F, -0.1F, 0.4F}),
      .scale = {1.5F, 0.75F, 2.0F},
  });
  const QVec3 pivot_world{6.0F, -1.0F, 2.0F};
  const QVec3 pivot_local = transform_point(inverse(source), pivot_world);
  PathArrayLayoutRequest request{
      .path_stations = path,
      .source_world_transform = source,
      .source_pivot_world = pivot_world,
      .count = 3U,
      .distance = 2.0,
      .follow_path = true,
  };
  request.variation.rotation_offset_degrees = {5.0F, 12.0F, -8.0F};
  request.variation.rotation_random_min_degrees = {-3.0F, -4.0F, -5.0F};
  request.variation.rotation_random_max_degrees = {3.0F, 4.0F, 5.0F};
  request.variation.scale_offset_factors = {1.1F, 0.9F, 1.2F};
  request.variation.scale_random_min_factors = {0.8F, 0.85F, 0.9F};
  request.variation.scale_random_max_factors = {1.2F, 1.15F, 1.1F};
  request.variation.seed = 42U;

  const PathArrayLayoutResult result = build_path_array_layout(request);
  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_EQ(result.world_transforms.size(), 3U);
  expect_vector_near(transform_point(result.world_transforms[0U], pivot_local),
                     {0.0F, 0.0F, 0.0F});
  expect_vector_near(transform_point(result.world_transforms[1U], pivot_local),
                     {2.0F, 0.0F, 0.0F});
  expect_vector_near(transform_point(result.world_transforms[2U], pivot_local),
                     {2.0F, 2.0F, 0.0F});
}

TEST(PathArrayLayoutTests, RejectsInvalidAndOverflowingVariation) {
  const std::vector<PathArrayPathStation> path = straight_path(4.0);
  PathArrayLayoutRequest request{
      .path_stations = path,
      .count = 3U,
      .distance = 1.0,
  };

  request.variation.rotation_offset_degrees.x =
      std::numeric_limits<float>::infinity();
  EXPECT_EQ(build_path_array_layout(request).message,
            "Path Array rotation variation must be finite.");
  request.variation = {};
  request.variation.rotation_random_min_degrees.x = 1.0F;
  EXPECT_EQ(build_path_array_layout(request).message,
            "Path Array random rotation minimum must not exceed maximum.");
  request.variation = {};
  request.variation.scale_offset_factors.x = 0.0F;
  EXPECT_EQ(build_path_array_layout(request).message,
            "Path Array scale variation must be finite and strictly positive.");
  request.variation = {};
  request.variation.scale_random_min_factors.x = 2.0F;
  EXPECT_EQ(build_path_array_layout(request).message,
            "Path Array random scale minimum must not exceed maximum.");
  request.variation = {};
  request.variation.scale_offset_factors.x =
      std::numeric_limits<float>::max();
  const PathArrayLayoutResult overflow = build_path_array_layout(request);
  EXPECT_FALSE(overflow.ok);
  EXPECT_EQ(overflow.message,
             "Path Array generated a non-finite or singular scale.");
}

TEST(PathArrayLayoutTests,
     RejectsUiLegalCumulativeScaleUnderflowAndOverflowAfterConversion) {
  const std::vector<PathArrayPathStation> path = straight_path(128.0);
  PathArrayLayoutRequest request{
      .path_stations = path,
      .count = 17U,
      .distance = 0.0,
  };
  request.variation.scale_offset_factors.x = 0.001F;

  const PathArrayLayoutResult underflow = build_path_array_layout(request);
  EXPECT_FALSE(underflow.ok);
  EXPECT_EQ(underflow.message,
            "Path Array generated a non-finite or singular scale.");

  request.count = 40U;
  request.variation.scale_offset_factors.x = 10.0F;
  const PathArrayLayoutResult overflow = build_path_array_layout(request);
  EXPECT_FALSE(overflow.ok);
  EXPECT_EQ(overflow.message,
            "Path Array generated a non-finite or singular scale.");
}

TEST(PathArrayLayoutTests, RejectsInvalidEvaluatorAndSourceInputs) {
  std::vector<PathArrayPathStation> path = straight_path(2.0);
  PathArrayLayoutRequest request{
      .path_stations = path,
      .count = 2U,
      .distance = 1.0,
  };

  request.path_stations = {};
  request.count = 1U;
  request.distance = -1.0;
  const PathArrayLayoutResult invalid_count = build_path_array_layout(request);
  EXPECT_FALSE(invalid_count.ok);
  EXPECT_EQ(invalid_count.message,
            "Path Array count must be between 2 and 128.");
  request.count = 2U;
  const PathArrayLayoutResult invalid_distance =
      build_path_array_layout(request);
  EXPECT_FALSE(invalid_distance.ok);
  EXPECT_EQ(invalid_distance.message,
            "Path Array distance must be finite and nonnegative.");
  request.path_stations = path;
  request.distance = 1.0;
  path[1U].distance = -1.0;
  EXPECT_FALSE(build_path_array_layout(request).ok);
  path = straight_path(2.0);
  path[1U].position_world.x = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(build_path_array_layout(request).ok);
  path = straight_path(2.0);
  request.follow_path = true;
  request.signed_axis = PathArraySignedAxis::PositiveX;
  request.source_world_transform.values[0U] = 0.0F;
  request.source_world_transform.values[1U] = 0.0F;
  request.source_world_transform.values[2U] = 0.0F;
  EXPECT_FALSE(build_path_array_layout(request).ok);

  request = {
      .path_stations = path,
      .count = 3U,
      .distance = std::numeric_limits<double>::max(),
  };
  EXPECT_FALSE(build_path_array_layout(request).ok)
      << "station-distance multiplication overflow must be rejected";
}

TEST(PathArrayLayoutTests, SharpClosedSeamKeepsOneSidedTangentsAndClosesRoll) {
  constexpr float kRootHalf = 0.70710678F;
  const std::array<PathArrayPathStation, 9U> path = {{
      {.distance = 0.0,
       .position_world = {1.0F, 0.0F, 0.0F},
       .tangent_world = {0.0F, 1.0F, kRootHalf}},
      {.distance = 1.0,
       .position_world = {kRootHalf, kRootHalf, 0.5F},
       .tangent_world = {-kRootHalf, kRootHalf, 0.0F}},
      {.distance = 2.0,
       .position_world = {0.0F, 1.0F, 0.0F},
       .tangent_world = {-1.0F, 0.0F, -kRootHalf}},
      {.distance = 3.0,
       .position_world = {-kRootHalf, kRootHalf, -0.5F},
       .tangent_world = {-kRootHalf, -kRootHalf, 0.0F}},
      {.distance = 4.0,
       .position_world = {-1.0F, 0.0F, 0.0F},
       .tangent_world = {0.0F, -1.0F, kRootHalf}},
      {.distance = 5.0,
       .position_world = {-kRootHalf, -kRootHalf, 0.5F},
       .tangent_world = {kRootHalf, -kRootHalf, 0.0F}},
      {.distance = 6.0,
       .position_world = {0.0F, -1.0F, 0.0F},
       .tangent_world = {1.0F, 0.0F, -kRootHalf}},
      {.distance = 7.0,
       .position_world = {kRootHalf, -kRootHalf, -0.5F},
       .tangent_world = {kRootHalf, kRootHalf, 0.0F}},
      {.distance = 8.0,
       .position_world = {1.0F, 0.0F, 0.0F},
       .tangent_world = {0.0F, -1.0F, 0.0F}},
  }};

  const PathArrayLayoutBackbone backbone =
      prepare_path_array_layout_backbone({
          .path_stations = path,
          .follow_path = true,
          .signed_axis = PathArraySignedAxis::PositiveX,
          .closed = true,
      });
  const PathArrayLayoutResult result = build_path_array_layout(
      backbone, {.count = 128U, .distance = 8.0 / 128.0});
  const PathArrayLayoutResult coarser = build_path_array_layout(
      backbone, {.count = 64U, .distance = 8.0 / 64.0});

  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_TRUE(coarser.ok) << coarser.message;
  ASSERT_EQ(result.world_transforms.size(), 128U);
  ASSERT_EQ(coarser.world_transforms.size(), 64U);
  for (std::size_t index = 0U; index < coarser.world_transforms.size();
       ++index) {
    EXPECT_EQ(coarser.world_transforms[index].values,
              result.world_transforms[index * 2U].values)
        << "prepared closed-loop frame changed at station " << index;
  }
  const QVec3 first_tangent =
      normalized_axis(result.world_transforms.front(), {1.0F, 0.0F, 0.0F});
  expect_vector_near(first_tangent,
                     normalize_or_zero(QVec3{0.0F, 1.0F, kRootHalf}));
  const QVec3 last_tangent =
      normalized_axis(result.world_transforms.back(), {1.0F, 0.0F, 0.0F});
  EXPECT_GT(dot(last_tangent, QVec3{0.0F, -1.0F, 0.0F}), 0.9F);
  const QVec3 first_normal =
      normalized_axis(result.world_transforms.front(), {0.0F, 1.0F, 0.0F});
  const QVec3 last_normal =
      normalized_axis(result.world_transforms.back(), {0.0F, 1.0F, 0.0F});
  const glm::quat seam_transport = path_array_layout_detail::shortest_arc(
      to_glm(last_tangent), to_glm(first_tangent), to_glm(last_normal));
  const QVec3 wrapped_last_normal =
      from_glm(seam_transport * to_glm(last_normal));
  EXPECT_GT(dot(first_normal, wrapped_last_normal), 0.98F);
}

} // namespace
} // namespace quader
