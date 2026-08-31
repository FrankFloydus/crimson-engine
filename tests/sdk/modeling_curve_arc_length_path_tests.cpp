////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/modeling.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

namespace quader::modeling {
namespace {

static_assert(std::is_same_v<
              decltype(std::declval<const CurveArcLengthPath &>().stations()),
              std::span<const CurveArcLengthStation>>);

void expect_vec_near(Vec3 actual, Vec3 expected, double tolerance = 1.0e-4) {
  EXPECT_NEAR(actual.x, expected.x, tolerance);
  EXPECT_NEAR(actual.y, expected.y, tolerance);
  EXPECT_NEAR(actual.z, expected.z, tolerance);
}

CurveDocument nonlinear_straight_curve() {
  const std::array points{
      CurvePointSpec{.position = {0.0F, 0.0F, 0.0F}},
      CurvePointSpec{.position = {10.0F, 0.0F, 0.0F}},
  };
  return CurveDocument::make_bezier(points).or_throw();
}

TEST(ModelingCurveArcLengthPathTests,
     DistanceLookupHandlesStraightCurvesWithNonlinearParameterSpeed) {
  const CurveDocument document = nonlinear_straight_curve();
  const CurveContentIdentity identity = document.content_identity();
  const std::uint64_t revision = document.content_revision();
  const auto built = CurveArcLengthPath::build(
      document, {}, {.tolerance = 1.0e-5, .max_depth = 20});
  ASSERT_TRUE(built.ok()) << built.error_message();
  const CurveArcLengthPath &path = built.value();
  EXPECT_NEAR(path.total_length(), 10.0, 1.0e-5);
  EXPECT_FALSE(path.closed());
  const std::span<const CurveArcLengthStation> stations = path.stations();
  ASSERT_GT(stations.size(), 2U)
      << "nonlinear progress must subdivide a geometrically straight path";
  EXPECT_DOUBLE_EQ(stations.front().distance, 0.0);
  EXPECT_NEAR(stations.back().distance, path.total_length(), 1.0e-8);
  for (std::size_t index = 0; index < stations.size(); ++index) {
    if (index > 0U) {
      EXPECT_GT(stations[index].distance, stations[index - 1U].distance);
    }
    const auto at_station =
        path.evaluate_at_distance(stations[index].distance);
    ASSERT_TRUE(at_station.ok()) << at_station.error_message();
    expect_vec_near(at_station.value().position,
                    stations[index].evaluation.position, 1.0e-6);
    expect_vec_near(at_station.value().tangent,
                    stations[index].evaluation.tangent, 1.0e-6);
    EXPECT_EQ(at_station.value().segment,
              stations[index].evaluation.segment);
    EXPECT_DOUBLE_EQ(at_station.value().parameter,
                     stations[index].evaluation.parameter);
  }

  const auto quarter = path.evaluate_at_distance(2.5);
  ASSERT_TRUE(quarter.ok()) << quarter.error_message();
  expect_vec_near(quarter.value().position, {2.5F, 0.0F, 0.0F}, 2.0e-4);
  expect_vec_near(quarter.value().tangent, {1.0F, 0.0F, 0.0F});
  EXPECT_EQ(quarter.value().segment, document.segments().front());
  EXPECT_GT(quarter.value().parameter, 0.25);
  const auto exact =
      document.evaluate(quarter.value().segment, quarter.value().parameter);
  ASSERT_TRUE(exact.ok());
  expect_vec_near(exact.value().position, quarter.value().position, 1.0e-6);
  EXPECT_EQ(document.content_identity(), identity);
  EXPECT_EQ(document.content_revision(), revision);
}

TEST(ModelingCurveArcLengthPathTests,
     CurvedPathUsesSymmetricArcLengthStations) {
  const std::array points{
      CurvePointSpec{.position = {0.0F, 0.0F, 0.0F},
                     .outgoing_handle = {0.0F, 4.0F, 0.0F}},
      CurvePointSpec{.position = {4.0F, 0.0F, 0.0F},
                     .incoming_handle = {0.0F, 4.0F, 0.0F}},
  };
  const CurveDocument document = CurveDocument::make_bezier(points).or_throw();
  const auto built = CurveArcLengthPath::build(
      document, {}, {.tolerance = 1.0e-5, .max_depth = 20});
  ASSERT_TRUE(built.ok()) << built.error_message();
  const auto middle =
      built.value().evaluate_at_distance(built.value().total_length() * 0.5);
  ASSERT_TRUE(middle.ok()) << middle.error_message();
  expect_vec_near(middle.value().position, {2.0F, 3.0F, 0.0F}, 2.0e-4);
  expect_vec_near(middle.value().tangent, {1.0F, 0.0F, 0.0F}, 2.0e-4);
  EXPECT_NEAR(middle.value().parameter, 0.5, 2.0e-4);
}

TEST(ModelingCurveArcLengthPathTests,
     WorldTransformDefinesDistanceAndNormalizedTangent) {
  const std::array points{
      CurvePointSpec{.position = {0.0F, 0.0F, 0.0F},
                     .outgoing_handle = {2.0F / 3.0F, 0.0F, 0.0F}},
      CurvePointSpec{.position = {2.0F, 0.0F, 0.0F},
                     .incoming_handle = {-2.0F / 3.0F, 0.0F, 0.0F}},
  };
  const CurveDocument document = CurveDocument::make_bezier(points).or_throw();
  const Transform3 world_from_curve{
      .x_axis = {2.0F, 1.0F, 0.0F},
      .y_axis = {0.5F, 3.0F, 0.0F},
      .z_axis = {0.0F, 0.0F, 4.0F},
      .origin = {3.0F, 4.0F, 5.0F},
  };
  const auto built = CurveArcLengthPath::build(
      document, world_from_curve, {.tolerance = 1.0e-5});
  ASSERT_TRUE(built.ok()) << built.error_message();
  const double expected_length = 2.0 * std::sqrt(5.0);
  EXPECT_NEAR(built.value().total_length(), expected_length, 2.0e-5);
  const auto middle = built.value().evaluate_at_distance(expected_length * 0.5);
  ASSERT_TRUE(middle.ok()) << middle.error_message();
  expect_vec_near(middle.value().position, {5.0F, 5.0F, 5.0F});
  expect_vec_near(middle.value().tangent,
                  {static_cast<float>(2.0 / std::sqrt(5.0)),
                   static_cast<float>(1.0 / std::sqrt(5.0)), 0.0F});
}

TEST(ModelingCurveArcLengthPathTests,
     RepeatedTrailingStationsPreserveFinalSegmentAndFallbackDirection) {
  const std::array points{
      CurvePointSpec{.position = {0.0F, 0.0F, 0.0F},
                     .outgoing_handle = {5.0F / 3.0F, 0.0F, 0.0F}},
      CurvePointSpec{.position = {5.0F, 0.0F, 0.0F},
                     .incoming_handle = {-5.0F / 3.0F, 0.0F, 0.0F}},
      CurvePointSpec{.position = {5.0F, 0.0F, 0.0F}},
  };
  const CurveDocument document = CurveDocument::make_bezier(points).or_throw();
  const auto built = CurveArcLengthPath::build(document);
  ASSERT_TRUE(built.ok()) << built.error_message();
  EXPECT_NEAR(built.value().total_length(), 5.0, 1.0e-5);
  const auto end = built.value().evaluate_at_distance(5.0);
  ASSERT_TRUE(end.ok()) << end.error_message();
  expect_vec_near(end.value().position, {5.0F, 0.0F, 0.0F});
  expect_vec_near(end.value().tangent, {1.0F, 0.0F, 0.0F});
  EXPECT_EQ(end.value().segment, document.segments().back());
  EXPECT_DOUBLE_EQ(end.value().parameter, 1.0);
  const auto stations = built.value().stations();
  ASSERT_GE(stations.size(), 4U);
  const auto boundary = std::adjacent_find(
      stations.begin(), stations.end(),
      [](const CurveArcLengthStation &left,
         const CurveArcLengthStation &right) {
        return left.distance == right.distance &&
               left.evaluation.segment != right.evaluation.segment;
      });
  ASSERT_NE(boundary, stations.end());
  EXPECT_DOUBLE_EQ(boundary->distance, 5.0);
  EXPECT_EQ(boundary->evaluation.segment, document.segments().front());
  EXPECT_EQ((boundary + 1)->evaluation.segment, document.segments().back());
  expect_vec_near(boundary->evaluation.position,
                  (boundary + 1)->evaluation.position);
}

TEST(ModelingCurveArcLengthPathTests,
     ClosedPathRetainsBothSidesOfItsSeam) {
  const std::array points{
      CurvePointSpec{.position = {0.0F, 0.0F, 0.0F},
                     .incoming_handle = {1.0F / 3.0F, 0.0F, 0.0F},
                     .outgoing_handle = {1.0F / 3.0F, 0.0F, 0.0F}},
      CurvePointSpec{.position = {1.0F, 0.0F, 0.0F},
                     .incoming_handle = {-1.0F / 3.0F, 0.0F, 0.0F},
                     .outgoing_handle = {-1.0F / 3.0F, 0.0F, 0.0F}},
  };
  const CurveDocument document =
      CurveDocument::make_bezier(points, true).or_throw();
  const auto built = CurveArcLengthPath::build(document);
  ASSERT_TRUE(built.ok()) << built.error_message();
  EXPECT_TRUE(built.value().closed());
  EXPECT_NEAR(built.value().total_length(), 2.0, 1.0e-5);
  const auto start = built.value().evaluate_at_distance(0.0);
  const auto seam = built.value().evaluate_at_distance(2.0);
  ASSERT_TRUE(start.ok());
  ASSERT_TRUE(seam.ok());
  expect_vec_near(start.value().position, seam.value().position);
  expect_vec_near(start.value().tangent, {1.0F, 0.0F, 0.0F});
  expect_vec_near(seam.value().tangent, {-1.0F, 0.0F, 0.0F});
  EXPECT_EQ(start.value().segment, document.segments().front());
  EXPECT_EQ(seam.value().segment, document.segments().back());
  EXPECT_DOUBLE_EQ(start.value().parameter, 0.0);
  EXPECT_DOUBLE_EQ(seam.value().parameter, 1.0);
  const auto stations = built.value().stations();
  ASSERT_GE(stations.size(), 4U);
  EXPECT_DOUBLE_EQ(stations.front().distance, 0.0);
  EXPECT_NEAR(stations.back().distance, built.value().total_length(),
              1.0e-8);
  EXPECT_EQ(stations.front().evaluation.segment,
            document.segments().front());
  EXPECT_EQ(stations.back().evaluation.segment,
            document.segments().back());
  EXPECT_DOUBLE_EQ(stations.front().evaluation.parameter, 0.0);
  EXPECT_DOUBLE_EQ(stations.back().evaluation.parameter, 1.0);
  expect_vec_near(stations.front().evaluation.position,
                  stations.back().evaluation.position);
}

TEST(ModelingCurveArcLengthPathTests,
     EmptySinglePointAndCoincidentPathsHaveStructuredDegenerateBehavior) {
  const CurveDocument empty;
  const auto empty_path = CurveArcLengthPath::build(empty);
  ASSERT_TRUE(empty_path.ok()) << empty_path.error_message();
  EXPECT_DOUBLE_EQ(empty_path.value().total_length(), 0.0);
  const auto empty_evaluation = empty_path.value().evaluate_at_distance(0.0);
  ASSERT_FALSE(empty_evaluation.ok());
  EXPECT_EQ(empty_evaluation.error().code, ErrorCode::ValidationFailed);

  CurveDocument point;
  ASSERT_TRUE(point.append_point({.position = {4.0F, 5.0F, 6.0F}}).ok());
  const auto point_path = CurveArcLengthPath::build(point);
  ASSERT_TRUE(point_path.ok()) << point_path.error_message();
  const auto point_evaluation = point_path.value().evaluate_at_distance(0.0);
  ASSERT_TRUE(point_evaluation.ok()) << point_evaluation.error_message();
  expect_vec_near(point_evaluation.value().position, {4.0F, 5.0F, 6.0F});
  expect_vec_near(point_evaluation.value().tangent, {});
  EXPECT_FALSE(point_evaluation.value().segment.valid());

  const std::array coincident{
      CurvePointSpec{.position = {2.0F, 3.0F, 4.0F}},
      CurvePointSpec{.position = {2.0F, 3.0F, 4.0F}},
  };
  const auto coincident_path = CurveArcLengthPath::build(
      CurveDocument::make_bezier(coincident).or_throw());
  ASSERT_TRUE(coincident_path.ok()) << coincident_path.error_message();
  EXPECT_DOUBLE_EQ(coincident_path.value().total_length(), 0.0);
  const auto coincident_evaluation =
      coincident_path.value().evaluate_at_distance(0.0);
  ASSERT_TRUE(coincident_evaluation.ok());
  expect_vec_near(coincident_evaluation.value().position,
                  {2.0F, 3.0F, 4.0F});
  expect_vec_near(coincident_evaluation.value().tangent, {});

  EXPECT_FALSE(point_path.value().evaluate_at_distance(-1.0).ok());
  EXPECT_FALSE(point_path.value().evaluate_at_distance(1.0).ok());
  EXPECT_FALSE(point_path.value()
                   .evaluate_at_distance(
                       std::numeric_limits<double>::quiet_NaN())
                   .ok());
}

TEST(ModelingCurveArcLengthPathTests,
     SamplingBudgetsFailAtomicallyWithoutChangingTheSource) {
  const CurveDocument document = nonlinear_straight_curve();
  const CurveContentIdentity identity = document.content_identity();
  const std::uint64_t revision = document.content_revision();
  const auto depth_error = CurveArcLengthPath::build(
      document, {}, {.tolerance = 1.0e-5, .max_depth = 0});
  ASSERT_FALSE(depth_error.ok());
  EXPECT_EQ(depth_error.error().code, ErrorCode::ValidationFailed);
  const auto budget_error = CurveArcLengthPath::build(
      document, {}, {.tolerance = 1.0e-5, .max_samples = 1});
  ASSERT_FALSE(budget_error.ok());
  EXPECT_EQ(budget_error.error().code, ErrorCode::ValidationFailed);
  EXPECT_EQ(document.content_identity(), identity);
  EXPECT_EQ(document.content_revision(), revision);
}

} // namespace
} // namespace quader::modeling
