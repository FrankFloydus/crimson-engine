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
#include <vector>

namespace quader {
namespace {

constexpr float kTolerance = 0.0001F;

void expect_vector_near(QVec3 actual, QVec3 expected,
                        float tolerance = kTolerance) {
  EXPECT_NEAR(actual.x, expected.x, tolerance);
  EXPECT_NEAR(actual.y, expected.y, tolerance);
  EXPECT_NEAR(actual.z, expected.z, tolerance);
}

QVec3 translation(const QMat4 &matrix) {
  return {matrix.values[12U], matrix.values[13U], matrix.values[14U]};
}

QVec3 normalized_axis(const QMat4 &matrix, QVec3 local_axis) {
  return normalize_or_zero(transform_vector(matrix, local_axis));
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

TEST(PathArrayLayoutTests, RejectsInvalidEvaluatorAndSourceInputs) {
  std::vector<PathArrayPathStation> path = straight_path(2.0);
  PathArrayLayoutRequest request{
      .path_stations = path,
      .count = 2U,
      .distance = 1.0,
  };

  request.count = 1U;
  EXPECT_FALSE(build_path_array_layout(request).ok);
  request.count = 2U;
  request.distance = -1.0;
  EXPECT_FALSE(build_path_array_layout(request).ok);
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

  const PathArrayLayoutResult result = build_path_array_layout({
      .path_stations = path,
      .count = 128U,
      .distance = 8.0 / 128.0,
      .follow_path = true,
      .signed_axis = PathArraySignedAxis::PositiveX,
      .closed = true,
  });

  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_EQ(result.world_transforms.size(), 128U);
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
