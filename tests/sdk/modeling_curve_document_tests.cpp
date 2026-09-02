////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace {

using namespace quader::modeling;

[[nodiscard]] CurveDocument make_arch() {
  const std::array points{
      CurvePointSpec{.position = {0, 0, 0}, .outgoing_handle = {0, 4, 0}},
      CurvePointSpec{.position = {4, 0, 0}, .incoming_handle = {0, 4, 0}},
  };
  return CurveDocument::make_bezier(points).or_throw();
}

[[nodiscard]] CurveDocument make_corner_path(bool closed = false) {
  const std::array points{
      CurvePointSpec{.position = {0, 0, 0}},
      CurvePointSpec{.position = {4, 0, 0}},
      CurvePointSpec{.position = {4, 0, 4}},
  };
  return CurveDocument::make_bezier(points, closed).or_throw();
}

void expect_vec_near(Vec3 actual, Vec3 expected, float tolerance = 1.0e-5F) {
  EXPECT_NEAR(actual.x, expected.x, tolerance);
  EXPECT_NEAR(actual.y, expected.y, tolerance);
  EXPECT_NEAR(actual.z, expected.z, tolerance);
}

[[nodiscard]] CurvePointSpec spec(const CurveControlPoint &point) {
  return {point.position, point.incoming_handle, point.outgoing_handle};
}

void write_u32(std::vector<std::byte> &bytes, std::size_t offset,
               std::uint32_t value) {
  for (unsigned int i = 0; i < 4; ++i) {
    bytes[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
  }
}

[[nodiscard]] double signed_anchor_area_twice(const CurveDocument &document) {
  double area = 0.0;
  const auto points = document.points();
  for (std::size_t index = 0; index < points.size(); ++index) {
    const Vec3 current = points[index].position;
    const Vec3 next = points[(index + 1U) % points.size()].position;
    area += static_cast<double>(current.x) * next.y -
            static_cast<double>(current.y) * next.x;
  }
  return area;
}

TEST(ModelingCurveDocumentTests, CircleFactoryCreatesCanonicalCounterclockwiseBezier) {
  constexpr double kRadius = 2.0;
  constexpr float kHandle =
      static_cast<float>(kRadius *
                         (4.0 * (1.4142135623730950488 - 1.0) / 3.0));
  const auto created = make_xy_circle_curve(kRadius);
  ASSERT_TRUE(created.ok());
  const CurveDocument &circle = created.value();
  EXPECT_TRUE(circle.closed());
  EXPECT_EQ(circle.point_count(), 4U);
  EXPECT_EQ(circle.segment_count(), 4U);
  EXPECT_TRUE(circle.validate().ok());
  EXPECT_GT(signed_anchor_area_twice(circle), 0.0);

  const std::array expected{
      CurvePointSpec{.position = {2, 0, 0},
                     .incoming_handle = {0, -kHandle, 0},
                     .outgoing_handle = {0, kHandle, 0}},
      CurvePointSpec{.position = {0, 2, 0},
                     .incoming_handle = {kHandle, 0, 0},
                     .outgoing_handle = {-kHandle, 0, 0}},
      CurvePointSpec{.position = {-2, 0, 0},
                     .incoming_handle = {0, kHandle, 0},
                     .outgoing_handle = {0, -kHandle, 0}},
      CurvePointSpec{.position = {0, -2, 0},
                     .incoming_handle = {-kHandle, 0, 0},
                     .outgoing_handle = {kHandle, 0, 0}},
  };
  for (std::size_t index = 0; index < expected.size(); ++index) {
    expect_vec_near(circle.points()[index].position, expected[index].position, 0);
    expect_vec_near(circle.points()[index].incoming_handle,
                    expected[index].incoming_handle, 0);
    expect_vec_near(circle.points()[index].outgoing_handle,
                    expected[index].outgoing_handle, 0);
  }
  const CurveBounds bounds = circle.bounds();
  ASSERT_TRUE(bounds.valid);
  expect_vec_near(bounds.min, {-2, -2, 0});
  expect_vec_near(bounds.max, {2, 2, 0});

  const auto start = circle.evaluate(circle.segments()[0], 0.0);
  const auto end = circle.evaluate(circle.segments()[0], 1.0);
  ASSERT_TRUE(start.ok());
  ASSERT_TRUE(end.ok());
  EXPECT_GT(start.value().tangent.y, 0);
  EXPECT_FLOAT_EQ(start.value().tangent.x, 0);
  EXPECT_LT(end.value().tangent.x, 0);
  EXPECT_FLOAT_EQ(end.value().tangent.y, 0);
}

TEST(ModelingCurveDocumentTests, StadiumFactoryCreatesFourArcsAndTwoExactLines) {
  constexpr double kRadius = 2.0;
  constexpr double kTotalLength = 10.0;
  constexpr float kHalfLength = 5.0F;
  constexpr float kOffset = 3.0F;
  constexpr float kStraightHandle = 2.0F;
  constexpr float kArcHandle =
      static_cast<float>(kRadius *
                         (4.0 * (1.4142135623730950488 - 1.0) / 3.0));
  const auto created = make_xy_stadium_curve(kRadius, kTotalLength);
  ASSERT_TRUE(created.ok());
  const CurveDocument &stadium = created.value();
  EXPECT_TRUE(stadium.closed());
  EXPECT_EQ(stadium.point_count(), 6U);
  EXPECT_EQ(stadium.segment_count(), 6U);
  EXPECT_TRUE(stadium.validate().ok());
  EXPECT_GT(signed_anchor_area_twice(stadium), 0.0);

  const std::array expected{
      CurvePointSpec{.position = {kHalfLength, 0, 0},
                     .incoming_handle = {0, -kArcHandle, 0},
                     .outgoing_handle = {0, kArcHandle, 0}},
      CurvePointSpec{.position = {kOffset, 2, 0},
                     .incoming_handle = {kArcHandle, 0, 0},
                     .outgoing_handle = {-kStraightHandle, 0, 0}},
      CurvePointSpec{.position = {-kOffset, 2, 0},
                     .incoming_handle = {kStraightHandle, 0, 0},
                     .outgoing_handle = {-kArcHandle, 0, 0}},
      CurvePointSpec{.position = {-kHalfLength, 0, 0},
                     .incoming_handle = {0, kArcHandle, 0},
                     .outgoing_handle = {0, -kArcHandle, 0}},
      CurvePointSpec{.position = {-kOffset, -2, 0},
                     .incoming_handle = {-kArcHandle, 0, 0},
                     .outgoing_handle = {kStraightHandle, 0, 0}},
      CurvePointSpec{.position = {kOffset, -2, 0},
                     .incoming_handle = {-kStraightHandle, 0, 0},
                     .outgoing_handle = {kArcHandle, 0, 0}},
  };
  for (std::size_t index = 0; index < expected.size(); ++index) {
    expect_vec_near(stadium.points()[index].position, expected[index].position, 0);
    expect_vec_near(stadium.points()[index].incoming_handle,
                    expected[index].incoming_handle, 0);
    expect_vec_near(stadium.points()[index].outgoing_handle,
                    expected[index].outgoing_handle, 0);
  }

  const CurveBounds bounds = stadium.bounds();
  ASSERT_TRUE(bounds.valid);
  expect_vec_near(bounds.min, {-5, -2, 0});
  expect_vec_near(bounds.max, {5, 2, 0});
  const auto top_middle = stadium.evaluate(stadium.segments()[1], 0.5);
  const auto bottom_middle = stadium.evaluate(stadium.segments()[4], 0.5);
  ASSERT_TRUE(top_middle.ok());
  ASSERT_TRUE(bottom_middle.ok());
  expect_vec_near(top_middle.value().position, {0, 2, 0});
  expect_vec_near(bottom_middle.value().position, {0, -2, 0});
  EXPECT_LT(top_middle.value().tangent.x, 0);
  EXPECT_FLOAT_EQ(top_middle.value().tangent.y, 0);
  EXPECT_GT(bottom_middle.value().tangent.x, 0);
  EXPECT_FLOAT_EQ(bottom_middle.value().tangent.y, 0);
}

TEST(ModelingCurveDocumentTests, StadiumDiameterBoundaryUsesCanonicalCircle) {
  const auto circle = make_xy_circle_curve(3.25);
  const auto stadium = make_xy_stadium_curve(3.25, 6.5);
  ASSERT_TRUE(circle.ok());
  ASSERT_TRUE(stadium.ok());
  EXPECT_TRUE(stadium.value().has_same_geometry(circle.value()));
  EXPECT_EQ(stadium.value().point_count(), 4U);
}

TEST(ModelingCurveDocumentTests, ShapeFactoriesRejectInvalidOrUnrepresentableDimensions) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  for (const double radius : {0.0, -1.0, nan, infinity,
                              std::numeric_limits<double>::max(),
                              std::numeric_limits<double>::denorm_min()}) {
    const auto circle = make_xy_circle_curve(radius);
    ASSERT_FALSE(circle.ok());
    EXPECT_EQ(circle.error().code, ErrorCode::InvalidArgument);
  }
  for (const auto [radius, total_length] :
       std::array<std::array<double, 2>, 8>{
           std::array{0.0, 4.0}, std::array{-1.0, 4.0},
           std::array{nan, 4.0}, std::array{1.0, nan},
           std::array{1.0, infinity}, std::array{2.0, 3.999},
           std::array{std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max()},
           std::array{1.0, std::numeric_limits<double>::max()}}) {
    const auto stadium = make_xy_stadium_curve(radius, total_length);
    ASSERT_FALSE(stadium.ok());
    EXPECT_EQ(stadium.error().code, ErrorCode::InvalidArgument);
  }
}

TEST(ModelingCurveDocumentTests, ShapeFactoryDocumentsTransformAndSerializeNormally) {
  auto stadium = make_xy_stadium_curve(2.0, 10.0).or_throw();
  const auto transformed = stadium.transform({.x_axis = {0, 1, 0},
                                              .y_axis = {-1, 0, 0},
                                              .z_axis = {0, 0, 1},
                                              .origin = {10, 20, 30}});
  ASSERT_TRUE(transformed.ok());
  EXPECT_TRUE(transformed.value().changed);
  expect_vec_near(stadium.points()[0].position, {10, 25, 30});
  expect_vec_near(stadium.points()[1].position, {8, 23, 30});
  const CurveBounds bounds = stadium.bounds();
  ASSERT_TRUE(bounds.valid);
  expect_vec_near(bounds.min, {8, 15, 30});
  expect_vec_near(bounds.max, {12, 25, 30});

  const auto encoded = serialize_curve_document(stadium);
  ASSERT_TRUE(encoded.ok());
  const auto decoded = deserialize_curve_document(encoded.value());
  ASSERT_TRUE(decoded.ok());
  EXPECT_TRUE(decoded.value().has_same_geometry(stadium));
  EXPECT_EQ(serialize_curve_document(decoded.value()).or_throw(),
            encoded.value());
}

TEST(ModelingCurveDocumentTests, EmptyAndIsolatedPointAreValidDrafts) {
  CurveDocument document;
  EXPECT_TRUE(document.validate().ok());
  EXPECT_EQ(document.point_count(), 0U);
  EXPECT_FALSE(document.bounds().valid);
  const auto empty_samples = document.sample();
  ASSERT_TRUE(empty_samples.ok());
  EXPECT_TRUE(empty_samples.value().empty());

  const auto append = document.append_point({.position = {2, 3, 4}});
  ASSERT_TRUE(append.ok());
  EXPECT_TRUE(append.value().changed);
  ASSERT_EQ(document.point_count(), 1U);
  EXPECT_TRUE(document.points().front().id.valid());
  EXPECT_EQ(document.segment_count(), 0U);
  ASSERT_TRUE(document.bounds().valid);
  expect_vec_near(document.bounds().min, {2, 3, 4});
  expect_vec_near(document.bounds().max, {2, 3, 4});
  const auto samples = document.sample();
  ASSERT_TRUE(samples.ok());
  ASSERT_EQ(samples.value().size(), 1U);
  EXPECT_FALSE(samples.value().front().segment.valid());
  expect_vec_near(samples.value().front().position, {2, 3, 4});

  const auto identity = document.content_identity();
  EXPECT_FALSE(document.set_closed(true).ok());
  EXPECT_EQ(document.content_identity(), identity);
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingCurveDocumentTests, CubicEvaluationUsesAnchorRelativeHandles) {
  const auto document = make_arch();
  const auto segment = document.segments().front();
  const auto start = document.evaluate(segment, 0);
  const auto middle = document.evaluate(segment, 0.5);
  const auto end = document.evaluate(segment, 1);
  ASSERT_TRUE(start.ok());
  ASSERT_TRUE(middle.ok());
  ASSERT_TRUE(end.ok());
  expect_vec_near(start.value().position, {0, 0, 0});
  expect_vec_near(start.value().tangent, {0, 12, 0});
  expect_vec_near(middle.value().position, {2, 3, 0});
  expect_vec_near(middle.value().tangent, {6, 0, 0});
  expect_vec_near(end.value().position, {4, 0, 0});
  expect_vec_near(end.value().tangent, {0, -12, 0});
}

TEST(ModelingCurveDocumentTests, BoundsUseCubicExtremaInsteadOfControlHull) {
  const auto document = make_arch();
  const auto bounds = document.bounds();
  ASSERT_TRUE(bounds.valid);
  expect_vec_near(bounds.min, {0, 0, 0});
  expect_vec_near(bounds.max, {4, 3, 0});
  for (int i = 0; i <= 100; ++i) {
    const auto value = document.evaluate(document.segments().front(), i / 100.0);
    ASSERT_TRUE(value.ok());
    EXPECT_GE(value.value().position.x, bounds.min.x);
    EXPECT_GE(value.value().position.y, bounds.min.y);
    EXPECT_LE(value.value().position.x, bounds.max.x);
    EXPECT_LE(value.value().position.y, bounds.max.y);
  }
}

TEST(ModelingCurveDocumentTests, SmoothnessToggleKeepsAnchorsAndStableIdentities) {
  auto document = make_corner_path();
  const auto before = document;
  const auto id = document.points()[1].id;
  const auto result = document.toggle_point_smoothness(id);
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().changed);
  EXPECT_TRUE(result.value().dirty.geometry);
  EXPECT_TRUE(result.value().dirty.overlays);
  EXPECT_FALSE(result.value().dirty.topology);
  ASSERT_EQ(document.point_count(), before.point_count());
  EXPECT_TRUE(std::equal(document.segments().begin(), document.segments().end(),
                         before.segments().begin(), before.segments().end()));
  EXPECT_EQ(document.identity_high_water(), before.identity_high_water());
  for (std::size_t i = 0; i < document.point_count(); ++i) {
    EXPECT_EQ(document.points()[i].id, before.points()[i].id);
    expect_vec_near(document.points()[i].position, before.points()[i].position, 0);
  }
  const float component = 4.0f / (3.0f * std::sqrt(2.0f));
  expect_vec_near(document.points()[1].incoming_handle, {-component, 0, -component});
  expect_vec_near(document.points()[1].outgoing_handle, {component, 0, component});
  expect_vec_near(before.points()[1].outgoing_handle, {});
  const auto encoded = serialize_curve_document(document).or_throw();
  EXPECT_TRUE(deserialize_curve_document(encoded).or_throw().has_same_geometry(document));

  ASSERT_TRUE(document.toggle_point_smoothness(id).ok());
  EXPECT_TRUE(document.has_same_geometry(before));
}

TEST(ModelingCurveDocumentTests, SmoothnessUsesAdjacentLengthsAndWrapsClosedPaths) {
  const std::array points{
      CurvePointSpec{.position = {0, 0, 0}},
      CurvePointSpec{.position = {3, 0, 0}},
      CurvePointSpec{.position = {3, 0, 6}},
  };
  auto document = CurveDocument::make_bezier(points).or_throw();
  for (const auto index : {0U, 1U, 2U}) {
    ASSERT_TRUE(document.toggle_point_smoothness(document.points()[index].id).ok());
  }
  expect_vec_near(document.points()[0].incoming_handle, {-1, 0, 0});
  expect_vec_near(document.points()[0].outgoing_handle, {1, 0, 0});
  const float component = 1.0f / std::sqrt(2.0f);
  expect_vec_near(document.points()[1].incoming_handle, {-component, 0, -component});
  expect_vec_near(document.points()[1].outgoing_handle, {2 * component, 0, 2 * component});
  expect_vec_near(document.points()[2].incoming_handle, {0, 0, -2});
  expect_vec_near(document.points()[2].outgoing_handle, {0, 0, 2});

  auto closed = CurveDocument::make_bezier(points, true).or_throw();
  ASSERT_TRUE(closed.toggle_point_smoothness(closed.points()[0].id).ok());
  const auto point = closed.points()[0];
  EXPECT_GT(point.outgoing_handle.x, 0);
  EXPECT_LT(point.outgoing_handle.z, 0);
  EXPECT_GT(point.incoming_handle.z, 0);
  const float length_ratio = std::sqrt(5.0f);
  expect_vec_near(point.incoming_handle,
                  {-length_ratio * point.outgoing_handle.x, 0,
                   -length_ratio * point.outgoing_handle.z});
  EXPECT_TRUE(closed.closed());
  EXPECT_TRUE(closed.validate().ok());
}

TEST(ModelingCurveDocumentTests, SmoothnessHandlesCoincidentAndReversingChords) {
  const std::array points{
      CurvePointSpec{.position = {0, 0, 0}},
      CurvePointSpec{.position = {0, 0, 0}},
      CurvePointSpec{.position = {3, 0, 0}},
  };
  auto document = CurveDocument::make_bezier(points).or_throw();
  ASSERT_TRUE(document.toggle_point_smoothness(document.points()[1].id).ok());
  expect_vec_near(document.points()[1].incoming_handle, {});
  expect_vec_near(document.points()[1].outgoing_handle, {1, 0, 0});
  ASSERT_TRUE(document.toggle_point_smoothness(document.points()[1].id).ok());
  expect_vec_near(document.points()[1].outgoing_handle, {});

  const std::array reversing{
      CurvePointSpec{.position = {0, 0, 0}},
      CurvePointSpec{.position = {3, 0, 0}},
      CurvePointSpec{.position = {0, 0, 0}},
  };
  document = CurveDocument::make_bezier(reversing).or_throw();
  ASSERT_TRUE(document.toggle_point_smoothness(document.points()[1].id).ok());
  expect_vec_near(document.points()[1].incoming_handle, {1, 0, 0});
  expect_vec_near(document.points()[1].outgoing_handle, {-1, 0, 0});
  document = CurveDocument::make_bezier(std::span(reversing).first(2), true).or_throw();
  ASSERT_TRUE(document.toggle_point_smoothness(document.points()[0].id).ok());
  expect_vec_near(document.points()[0].incoming_handle, {-1, 0, 0});
  expect_vec_near(document.points()[0].outgoing_handle, {1, 0, 0});
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingCurveDocumentTests, DegenerateAndInvalidSmoothnessEditsAreAtomic) {
  const std::array points{
      CurvePointSpec{.position = {2, 3, 4}},
      CurvePointSpec{.position = {2, 3, 4}},
  };
  for (const auto count : {1U, 2U}) {
    auto document = CurveDocument::make_bezier(std::span(points).first(count)).or_throw();
    const auto before = document;
    const auto result = document.toggle_point_smoothness(document.points()[0].id);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().changed);
    EXPECT_FALSE(document.toggle_point_smoothness({}).ok());
    EXPECT_FALSE(document.toggle_point_smoothness({1, 2}).ok());
    EXPECT_EQ(document.content_identity(), before.content_identity());
    EXPECT_EQ(document.content_revision(), before.content_revision());
    EXPECT_EQ(document.identity_high_water(), before.identity_high_water());
    EXPECT_TRUE(document.has_same_geometry(before));
  }
  const std::array extreme{
      CurvePointSpec{.position = {std::numeric_limits<float>::max(), 0, 0}},
      CurvePointSpec{.position = {0, 0, 0}},
  };
  auto document = CurveDocument::make_bezier(extreme).or_throw();
  const auto before = document;
  EXPECT_FALSE(document.toggle_point_smoothness(document.points()[0].id).ok());
  EXPECT_EQ(document.content_identity(), before.content_identity());
  EXPECT_TRUE(document.has_same_geometry(before));
}

TEST(ModelingCurveDocumentTests, CopiesPinAuthoredContentAndDetachOnlyForRealEdits) {
  const auto source = make_arch();
  auto edited = source;
  const auto identity = source.content_identity();
  const auto revision = source.content_revision();
  EXPECT_EQ(source.points().data(), edited.points().data());

  const auto no_op = edited.set_point(edited.points().front().id,
                                      spec(edited.points().front()));
  ASSERT_TRUE(no_op.ok());
  EXPECT_FALSE(no_op.value().changed);
  EXPECT_EQ(edited.content_identity(), identity);
  EXPECT_EQ(edited.content_revision(), revision);
  EXPECT_EQ(source.points().data(), edited.points().data());
  ASSERT_TRUE(edited.transform({}).ok());
  ASSERT_TRUE(edited.set_closed(false).ok());
  EXPECT_EQ(edited.content_identity(), identity);

  auto point = spec(edited.points().front());
  point.position.z = 2;
  const auto change = edited.set_point(edited.points().front().id, point);
  ASSERT_TRUE(change.ok());
  EXPECT_TRUE(change.value().changed);
  EXPECT_TRUE(change.value().dirty.geometry);
  EXPECT_FALSE(change.value().dirty.topology);
  EXPECT_NE(edited.content_identity(), identity);
  EXPECT_GT(edited.content_revision(), revision);
  EXPECT_NE(source.points().data(), edited.points().data());
  EXPECT_EQ(source.content_identity(), identity);
  EXPECT_EQ(source.points().front().position.z, 0);
  EXPECT_FALSE(source.has_same_geometry(edited));
}

TEST(ModelingCurveDocumentTests, BatchPointEditsPublishAllReplacementsOnce) {
  auto document = make_corner_path();
  const auto before = document;
  const auto identity = document.content_identity();
  const auto revision = document.content_revision();
  auto first = spec(document.points().front());
  first.position = {-2, 1, 3};
  first.outgoing_handle = {1, 2, 3};
  auto last = spec(document.points().back());
  last.position = {8, 5, 2};
  last.incoming_handle = {-3, -2, -1};
  const std::array edits{
      CurvePointEdit{.id = document.points().front().id, .point = first},
      CurvePointEdit{.id = document.points().back().id, .point = last},
  };

  const auto result = document.set_points(edits);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().changed);
  EXPECT_TRUE(result.value().dirty.geometry);
  EXPECT_TRUE(result.value().dirty.overlays);
  EXPECT_FALSE(result.value().dirty.topology);
  EXPECT_NE(document.content_identity(), identity);
  EXPECT_EQ(document.content_revision(), revision + 1U);
  EXPECT_NE(document.points().data(), before.points().data());
  EXPECT_EQ(document.points().front().id, before.points().front().id);
  EXPECT_EQ(document.points().back().id, before.points().back().id);
  expect_vec_near(document.points().front().position, first.position, 0);
  expect_vec_near(document.points().front().outgoing_handle,
                  first.outgoing_handle, 0);
  expect_vec_near(document.points()[1].position, before.points()[1].position, 0);
  expect_vec_near(document.points().back().position, last.position, 0);
  expect_vec_near(document.points().back().incoming_handle,
                  last.incoming_handle, 0);
}

TEST(ModelingCurveDocumentTests, InvalidBatchPointEditsRollBackEveryReplacement) {
  auto document = make_corner_path();
  const auto before = document;
  const auto identity = document.content_identity();
  const auto revision = document.content_revision();
  const auto high_water = document.identity_high_water();
  const auto *storage = document.points().data();
  auto changed = spec(document.points().front());
  changed.position.z = 9;

  const std::array invalid_id{
      CurvePointEdit{.id = document.points().front().id, .point = changed},
      CurvePointEdit{.id = {999, 1}, .point = {}},
  };
  const auto invalid_id_result = document.set_points(invalid_id);
  ASSERT_FALSE(invalid_id_result.ok());
  EXPECT_EQ(invalid_id_result.error().code, ErrorCode::InvalidId);

  auto invalid_controls = spec(document.points().back());
  invalid_controls.outgoing_handle.x = std::numeric_limits<float>::infinity();
  const std::array invalid_value{
      CurvePointEdit{.id = document.points().front().id, .point = changed},
      CurvePointEdit{.id = document.points().back().id,
                     .point = invalid_controls},
  };
  const auto invalid_value_result = document.set_points(invalid_value);
  ASSERT_FALSE(invalid_value_result.ok());
  EXPECT_EQ(invalid_value_result.error().code, ErrorCode::InvalidArgument);

  auto second_replacement = changed;
  second_replacement.position.y = 12;
  const std::array duplicate{
      CurvePointEdit{.id = document.points().front().id, .point = changed},
      CurvePointEdit{.id = document.points().front().id,
                     .point = second_replacement},
  };
  const auto duplicate_result = document.set_points(duplicate);
  ASSERT_FALSE(duplicate_result.ok());
  EXPECT_EQ(duplicate_result.error().code, ErrorCode::InvalidArgument);

  EXPECT_EQ(document.content_identity(), identity);
  EXPECT_EQ(document.content_revision(), revision);
  EXPECT_EQ(document.identity_high_water(), high_water);
  EXPECT_EQ(document.points().data(), storage);
  EXPECT_TRUE(document.has_same_geometry(before));
}

TEST(ModelingCurveDocumentTests, NoOpBatchPointEditsRetainSharedContent) {
  auto document = make_corner_path();
  const auto snapshot = document;
  const auto identity = document.content_identity();
  const auto revision = document.content_revision();
  const auto *storage = document.points().data();

  const auto empty = document.set_points({});
  ASSERT_TRUE(empty.ok());
  EXPECT_FALSE(empty.value().changed);
  const std::array unchanged{
      CurvePointEdit{.id = document.points()[2].id,
                     .point = spec(document.points()[2])},
      CurvePointEdit{.id = document.points()[0].id,
                     .point = spec(document.points()[0])},
  };
  const auto exact = document.set_points(unchanged);
  ASSERT_TRUE(exact.ok());
  EXPECT_FALSE(exact.value().changed);
  EXPECT_EQ(document.content_identity(), identity);
  EXPECT_EQ(document.content_revision(), revision);
  EXPECT_EQ(document.points().data(), storage);
  EXPECT_TRUE(document.has_same_geometry(snapshot));
}

TEST(ModelingCurveDocumentTests, InvalidEditsPreserveContentRevisionAndAllocation) {
  auto document = make_arch();
  const auto before = document;
  const auto high_water = document.identity_high_water();
  const auto identity = document.content_identity();
  const auto revision = document.content_revision();
  const float infinity = std::numeric_limits<float>::infinity();
  const float nan = std::numeric_limits<float>::quiet_NaN();

  EXPECT_FALSE(document.append_point({.position = {infinity, 0, 0}}).ok());
  EXPECT_FALSE(document.set_point(document.points().back().id,
                                  {.incoming_handle = {nan, 0, 0}}).ok());
  EXPECT_FALSE(document.set_point({}, {}).ok());
  EXPECT_FALSE(document.remove_point({42, 1}).ok());
  EXPECT_FALSE(document.split_segment(document.segments().front(), nan).ok());
  EXPECT_FALSE(document.split_segment(document.segments().front(), 0).ok());
  EXPECT_FALSE(document.split_segment(document.segments().front(), 1).ok());
  EXPECT_FALSE(document.transform({.origin = {0, infinity, 0}}).ok());
  auto stale = document.points().front().id;
  stale.generation = 2;
  const auto stale_result = document.set_point(stale, {});
  ASSERT_FALSE(stale_result.ok());
  EXPECT_EQ(stale_result.error().code, ErrorCode::StaleId);

  EXPECT_TRUE(before.has_same_geometry(document));
  EXPECT_EQ(document.content_identity(), identity);
  EXPECT_EQ(document.content_revision(), revision);
  EXPECT_EQ(document.identity_high_water(), high_water);
}

TEST(ModelingCurveDocumentTests, AuthoredValidationRejectsInvalidIdentityAndControls) {
  const auto document = make_corner_path();
  const auto authored = document.authored_curve();
  auto invalid = authored;
  invalid.points[1].id = invalid.points[0].id;
  EXPECT_FALSE(CurveDocument::from_authored_curve(invalid).ok());
  invalid = authored;
  invalid.points.front().id.generation = 2;
  EXPECT_FALSE(CurveDocument::from_authored_curve(invalid).ok());
  invalid = authored;
  invalid.identity_high_water.next_point_index = invalid.points.back().id.index;
  EXPECT_FALSE(CurveDocument::from_authored_curve(invalid).ok());
  invalid = authored;
  invalid.segments[1] = invalid.segments[0];
  EXPECT_FALSE(CurveDocument::from_authored_curve(invalid).ok());
  invalid = authored;
  invalid.segments.pop_back();
  EXPECT_FALSE(CurveDocument::from_authored_curve(invalid).ok());
  invalid = authored;
  invalid.closed = true;
  EXPECT_FALSE(CurveDocument::from_authored_curve(invalid).ok());
  invalid = authored;
  invalid.points.front().position.x = std::numeric_limits<float>::max();
  invalid.points.front().outgoing_handle.x = std::numeric_limits<float>::max();
  EXPECT_FALSE(CurveDocument::from_authored_curve(invalid).ok());
  invalid = authored;
  invalid.identity_high_water.next_segment_index = 0;
  EXPECT_FALSE(CurveDocument::from_authored_curve(invalid).ok());
}

TEST(ModelingCurveDocumentTests, RemovingAnchorsPreservesSurvivorsAndRetiresIds) {
  for (const bool closed : {false, true}) {
    for (std::size_t removed_index = 0; removed_index < 3; ++removed_index) {
      auto document = make_corner_path(closed);
      const auto before = document.authored_curve();
      const auto removed = before.points[removed_index].id;
      const auto removal = document.remove_point(removed);
      ASSERT_TRUE(removal.ok());
      EXPECT_TRUE(removal.value().dirty.topology);
      ASSERT_EQ(document.point_count(), 2U);
      EXPECT_EQ(document.closed(), closed);
      EXPECT_EQ(document.segment_count(), closed ? 2U : 1U);
      EXPECT_TRUE(document.validate().ok());
      EXPECT_TRUE(std::none_of(document.points().begin(), document.points().end(),
                                [removed](const auto &point) { return point.id == removed; }));
      for (const auto &point : document.points()) {
        EXPECT_TRUE(std::any_of(before.points.begin(), before.points.end(),
                                 [&](const auto &old) { return old.id == point.id; }));
      }
      ASSERT_TRUE(document.append_point({.position = {8, 0, 4}}).ok());
      EXPECT_GT(document.points().back().id.index, before.points.back().id.index);
      EXPECT_TRUE(document.validate().ok());
      const auto stale = document.remove_point(removed);
      ASSERT_FALSE(stale.ok());
      EXPECT_EQ(stale.error().code, ErrorCode::StaleId);
    }
  }
}

TEST(ModelingCurveDocumentTests, ClosingAndReopeningRetiresOnlyTheClosingSegment) {
  auto document = make_corner_path();
  const auto original = document.authored_curve();
  ASSERT_TRUE(document.set_closed(true).ok());
  ASSERT_EQ(document.segment_count(), 3U);
  const auto closing_id = document.segments().back();
  const auto closing = document.evaluate(closing_id, 1);
  ASSERT_TRUE(closing.ok());
  expect_vec_near(closing.value().position, document.points().front().position);
  ASSERT_TRUE(document.set_closed(false).ok());
  EXPECT_EQ(document.segment_count(), 2U);
  EXPECT_TRUE(std::equal(document.segments().begin(), document.segments().end(),
                         original.segments.begin(), original.segments.end()));
  ASSERT_TRUE(document.set_closed(true).ok());
  EXPECT_GT(document.segments().back().index, closing_id.index);
  ASSERT_TRUE(document.remove_point(document.points().back().id).ok());
  ASSERT_TRUE(document.remove_point(document.points().back().id).ok());
  EXPECT_FALSE(document.closed());
  EXPECT_EQ(document.segment_count(), 0U);
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingCurveDocumentTests, SplittingPreservesShapeAndStableAdjacentIdentities) {
  auto document = make_arch();
  const auto before = document;
  const auto segment = document.segments().front();
  const auto first_id = document.points().front().id;
  const auto last_id = document.points().back().id;
  constexpr double kSplit = 0.37;
  const auto result = document.split_segment(segment, kSplit);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.value().changed);
  ASSERT_EQ(document.point_count(), 3U);
  ASSERT_EQ(document.segment_count(), 2U);
  EXPECT_EQ(document.points().front().id, first_id);
  EXPECT_EQ(document.points().back().id, last_id);
  EXPECT_EQ(document.segments().front(), segment);
  EXPECT_NE(document.points()[1].id, first_id);
  EXPECT_NE(document.points()[1].id, last_id);
  EXPECT_TRUE(document.validate().ok());
  for (int i = 0; i <= 100; ++i) {
    const double t = i / 100.0;
    const auto expected = before.evaluate(segment, t);
    const auto actual = t <= kSplit
                            ? document.evaluate(document.segments()[0], t / kSplit)
                            : document.evaluate(document.segments()[1], (t - kSplit) / (1.0 - kSplit));
    ASSERT_TRUE(expected.ok());
    ASSERT_TRUE(actual.ok());
    expect_vec_near(actual.value().position, expected.value().position, 1.0e-5F);
  }
  expect_vec_near(document.bounds().min, before.bounds().min);
  expect_vec_near(document.bounds().max, before.bounds().max);
}

TEST(ModelingCurveDocumentTests, SplittingClosingSegmentAppendsAnAnchorAndPreservesSeam) {
  auto document = make_corner_path(true);
  const auto before = document;
  const auto closing = document.segments().back();
  ASSERT_TRUE(document.split_segment(closing, 0.5).ok());
  ASSERT_EQ(document.point_count(), 4U);
  EXPECT_EQ(document.points().front().id, before.points().front().id);
  EXPECT_EQ(document.segments()[2], closing);
  EXPECT_TRUE(document.closed());
  EXPECT_TRUE(document.validate().ok());
  for (int i = 0; i <= 20; ++i) {
    const double t = i / 20.0;
    const auto expected = before.evaluate(closing, t);
    const auto actual = t <= 0.5
                            ? document.evaluate(document.segments()[2], t * 2)
                            : document.evaluate(document.segments()[3], (t - 0.5) * 2);
    ASSERT_TRUE(expected.ok());
    ASSERT_TRUE(actual.ok());
    expect_vec_near(actual.value().position, expected.value().position);
  }
}

TEST(ModelingCurveDocumentTests, SplitOffsetsReconstructControlsRelativeToTheStoredAnchor) {
  constexpr float kOrigin = 128.0F;
  const std::array points{
      CurvePointSpec{.position = {kOrigin, 0, 0}, .outgoing_handle = {1, 0, 0}},
      CurvePointSpec{.position = {kOrigin, 1, 0}, .incoming_handle = {1, 0, 0}},
  };
  auto document = CurveDocument::make_bezier(points).or_throw();
  const auto result = document.split_segment(document.segments().front(), 0.37);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.value().changed);
  ASSERT_EQ(document.point_count(), 3U);
  const auto &inserted = document.points()[1];
  const double stored_x = static_cast<double>(inserted.position.x) - kOrigin;
  const double stored_y = inserted.position.y;
  // Ideal de Casteljau controls around t=.37 are (.6031,.1369) and
  // (.8631,.6031), relative to the original first anchor. The stored anchor
  // rounds slightly; offsets compensate that rounding instead of shifting
  // both controls by the same error.
  EXPECT_NEAR(stored_x + inserted.incoming_handle.x, 0.6031, 1.0e-7);
  EXPECT_NEAR(stored_y + inserted.incoming_handle.y, 0.1369, 1.0e-7);
  EXPECT_NEAR(stored_x + inserted.outgoing_handle.x, 0.8631, 1.0e-7);
  EXPECT_NEAR(stored_y + inserted.outgoing_handle.y, 0.6031, 1.0e-7);
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingCurveDocumentTests, SplitPrecisionFailureIsAtomicAndIndependentOfWorldOrigin) {
  for (const bool closed : {false, true}) {
    for (const float origin : {0.0F, 16777216.0F}) {
      const std::array points{
          CurvePointSpec{.position = {origin, 0, 0},
                          .incoming_handle = {1, 0, 0},
                          .outgoing_handle = {1, 0, 0}},
          CurvePointSpec{.position = {origin, 1, 0},
                          .incoming_handle = {1, 0, 0},
                          .outgoing_handle = {1, 0, 0}},
      };
      auto document = CurveDocument::make_bezier(points, closed).or_throw();
      const auto before = document;
      const auto before_bytes = serialize_curve_document(document);
      ASSERT_TRUE(before_bytes.ok());
      const auto result = document.split_segment(document.segments().back(), 0.5);
      if (origin == 0.0F) {
        ASSERT_TRUE(result.ok());
        EXPECT_TRUE(result.value().changed);
        EXPECT_FLOAT_EQ(document.points()[closed ? 2 : 1].position.x, 0.75F);
        continue;
      }
      // A midpoint at origin+.75 cannot round-trip through a float anchor
      // here. Growing the tolerance with the world origin would accept a
      // visibly different curve; instead the local-hull budget rejects it.
      ASSERT_FALSE(result.ok());
      EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
      EXPECT_EQ(document.content_identity(), before.content_identity());
      EXPECT_EQ(document.content_revision(), before.content_revision());
      EXPECT_EQ(document.identity_high_water(), before.identity_high_water());
      EXPECT_EQ(document.points().data(), before.points().data());
      EXPECT_TRUE(document.has_same_geometry(before));
      const auto after_bytes = serialize_curve_document(document);
      ASSERT_TRUE(after_bytes.ok());
      EXPECT_EQ(after_bytes.value(), before_bytes.value());
    }
  }
}

TEST(ModelingCurveDocumentTests, AffineTransformTranslatesAnchorsButNotHandleOffsets) {
  auto document = make_arch();
  const auto before = document;
  Transform3 transform;
  transform.x_axis = {0, 2, 0};
  transform.y_axis = {-3, 0, 0};
  transform.z_axis = {0, 0, -1};
  transform.origin = {10, 20, 30};
  const auto result = document.transform(transform);
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().changed);
  expect_vec_near(document.points().front().position, {10, 20, 30});
  expect_vec_near(document.points().front().outgoing_handle, {-12, 0, 0});
  expect_vec_near(document.points().back().position, {10, 28, 30});
  EXPECT_TRUE(std::equal(document.segments().begin(), document.segments().end(),
                         before.segments().begin(), before.segments().end()));
  for (int i = 0; i <= 20; ++i) {
    const auto old_value = before.evaluate(before.segments().front(), i / 20.0);
    const auto value = document.evaluate(document.segments().front(), i / 20.0);
    ASSERT_TRUE(old_value.ok());
    ASSERT_TRUE(value.ok());
    const auto p = old_value.value().position;
    expect_vec_near(value.value().position, {10 - 3 * p.y, 20 + 2 * p.x, 30 - p.z});
  }
  expect_vec_near(document.bounds().min, {1, 20, 30});
  expect_vec_near(document.bounds().max, {10, 28, 30});
}

TEST(ModelingCurveDocumentTests, OverflowingTransformIsAtomicEvenAfterValidEarlierPoints) {
  auto document = make_arch();
  const auto before = document;
  Transform3 transform;
  transform.x_axis.x = std::numeric_limits<float>::max();
  transform.origin = {1, 2, 3};
  const auto result = document.transform(transform);
  ASSERT_FALSE(result.ok());
  EXPECT_TRUE(document.has_same_geometry(before));
  EXPECT_EQ(document.content_identity(), before.content_identity());
  EXPECT_EQ(document.content_revision(), before.content_revision());
  EXPECT_EQ(document.identity_high_water(), before.identity_high_water());
}

TEST(ModelingCurveDocumentTests, AdaptiveSamplesPreserveSegmentProvenanceWithoutAuthoredChanges) {
  auto document = make_arch();
  ASSERT_TRUE(document.append_point({.position = {8, 0, 0}}).ok());
  const auto identity = document.content_identity();
  const auto revision = document.content_revision();
  const auto coarse = document.sample({.tolerance = 0.1});
  const auto fine = document.sample({.tolerance = 0.001});
  ASSERT_TRUE(coarse.ok());
  ASSERT_TRUE(fine.ok());
  EXPECT_GT(fine.value().size(), coarse.value().size());
  for (const auto &sample : fine.value()) {
    const auto expected = document.evaluate(sample.segment, sample.parameter);
    ASSERT_TRUE(expected.ok());
    expect_vec_near(sample.position, expected.value().position);
    expect_vec_near(sample.tangent, expected.value().tangent);
  }
  const auto second = std::find_if(fine.value().begin(), fine.value().end(),
                                   [&](const auto &sample) {
                                     return sample.segment == document.segments()[1];
                                   });
  ASSERT_NE(second, fine.value().begin());
  ASSERT_NE(second, fine.value().end());
  EXPECT_EQ(second->parameter, 0.0);
  EXPECT_EQ((second - 1)->parameter, 1.0);
  expect_vec_near(second->position, (second - 1)->position);
  EXPECT_EQ(document.content_identity(), identity);
  EXPECT_EQ(document.content_revision(), revision);
  EXPECT_EQ(document.point_count(), 3U);
}

TEST(ModelingCurveDocumentTests, CornerSegmentsNeedOnlyTheirEndpointsAndClosedSamplesMeet) {
  const auto open = make_corner_path();
  const auto samples = open.sample();
  ASSERT_TRUE(samples.ok());
  ASSERT_EQ(samples.value().size(), 4U);
  const auto closed = make_corner_path(true);
  const auto loop = closed.sample();
  ASSERT_TRUE(loop.ok());
  ASSERT_EQ(loop.value().size(), 6U);
  expect_vec_near(loop.value().front().position, loop.value().back().position);
  EXPECT_NE(loop.value().front().segment, loop.value().back().segment);
}

TEST(ModelingCurveDocumentTests, AdaptiveOutputChordsRespectTheRequestedTolerance) {
  const auto document = make_arch();
  constexpr double kTolerance = 0.002;
  const auto samples = document.sample({.tolerance = kTolerance});
  ASSERT_TRUE(samples.ok());
  for (std::size_t i = 1; i < samples.value().size(); ++i) {
    const auto &start = samples.value()[i - 1];
    const auto &end = samples.value()[i];
    const double dx = static_cast<double>(end.position.x) - start.position.x;
    const double dy = static_cast<double>(end.position.y) - start.position.y;
    const double length_squared = dx * dx + dy * dy;
    for (int step = 1; step < 8; ++step) {
      const double parameter = start.parameter +
                               (end.parameter - start.parameter) * step / 8.0;
      const auto point = document.evaluate(start.segment, parameter);
      ASSERT_TRUE(point.ok());
      const double px = static_cast<double>(point.value().position.x) - start.position.x;
      const double py = static_cast<double>(point.value().position.y) - start.position.y;
      const double factor = length_squared > 0
                                ? std::clamp((px * dx + py * dy) / length_squared, 0.0, 1.0)
                                : 0;
      EXPECT_LE(std::hypot(px - factor * dx, py - factor * dy), kTolerance + 1.0e-6);
    }
  }
}

TEST(ModelingCurveDocumentTests, UnrepresentableDisplayPrecisionFailsInsteadOfLosingABend) {
  constexpr float kOrigin = 16777216.0F;
  const std::array points{
      CurvePointSpec{.position = {kOrigin, 0, 0}, .outgoing_handle = {1, 0, 0}},
      CurvePointSpec{.position = {kOrigin, 1, 0}, .incoming_handle = {1, 0, 0}},
  };
  const auto created = CurveDocument::make_bezier(points);
  ASSERT_TRUE(created.ok());
  const auto identity = created.value().content_identity();
  // The mathematical midpoint is origin + .75, but adjacent float X values
  // are two units apart here. A .01 error bound cannot be satisfied.
  const auto fine = created.value().sample({.tolerance = 0.01});
  ASSERT_FALSE(fine.ok());
  EXPECT_EQ(fine.error().code, ErrorCode::ValidationFailed);
  const auto coarse = created.value().sample({.tolerance = 1.0});
  ASSERT_TRUE(coarse.ok());
  EXPECT_EQ(coarse.value().size(), 2U);
  EXPECT_EQ(created.value().content_identity(), identity);
  EXPECT_GT(created.value().bounds().max.x, kOrigin);
}

TEST(ModelingCurveDocumentTests, CollinearOvershootingHandlesStillNeedSubdivision) {
  const std::array points{
      CurvePointSpec{.position = {0, 0, 0}, .outgoing_handle = {8, 0, 0}},
      CurvePointSpec{.position = {1, 0, 0}, .incoming_handle = {-8, 0, 0}},
  };
  const auto created = CurveDocument::make_bezier(points);
  ASSERT_TRUE(created.ok());
  const auto samples = created.value().sample({.tolerance = 0.001});
  ASSERT_TRUE(samples.ok());
  EXPECT_GT(samples.value().size(), 2U);
  EXPECT_TRUE(std::any_of(samples.value().begin(), samples.value().end(),
                           [](const auto &sample) { return sample.position.x > 1; }));
  EXPECT_TRUE(std::any_of(samples.value().begin(), samples.value().end(),
                           [](const auto &sample) { return sample.position.x < 0; }));
  EXPECT_LT(created.value().bounds().min.x, 0);
  EXPECT_GT(created.value().bounds().max.x, 1);
}

TEST(ModelingCurveDocumentTests, SamplingAndEvaluationRejectInvalidRequestsWithoutMutation) {
  const auto document = make_arch();
  const auto identity = document.content_identity();
  EXPECT_FALSE(document.sample({.tolerance = 0}).ok());
  EXPECT_FALSE(document.sample({.tolerance = -1}).ok());
  EXPECT_FALSE(document.sample({.tolerance = std::numeric_limits<double>::infinity()}).ok());
  EXPECT_FALSE(document.sample({.max_depth = 25}).ok());
  EXPECT_FALSE(document.sample({.max_samples = 0}).ok());
  EXPECT_FALSE(document.sample({.max_samples = 2}).ok());
  EXPECT_FALSE(document.sample({.max_depth = 0}).ok());
  EXPECT_FALSE(document.evaluate({}, 0.5).ok());
  EXPECT_FALSE(document.evaluate(document.segments().front(), -0.1).ok());
  EXPECT_FALSE(document.evaluate(document.segments().front(), 1.1).ok());
  EXPECT_FALSE(document.evaluate(document.segments().front(),
                                  std::numeric_limits<double>::quiet_NaN()).ok());
  EXPECT_EQ(document.content_identity(), identity);
}

TEST(ModelingCurveDocumentTests, RestoringAStateCanRetainIdentityHighWaterWithoutDetaching) {
  auto document = make_arch();
  const auto snapshot = document;
  ASSERT_TRUE(document.append_point({.position = {8, 0, 0}}).ok());
  const auto retired_point = document.points().back().id;
  const auto retired_segment = document.segments().back();
  const auto observed = document.identity_high_water();
  document = snapshot;
  document.preserve_identity_high_water(observed);
  EXPECT_EQ(document.content_identity(), snapshot.content_identity());
  EXPECT_EQ(document.content_revision(), snapshot.content_revision());
  EXPECT_EQ(document.points().data(), snapshot.points().data());
  EXPECT_TRUE(document.has_same_geometry(snapshot));
  ASSERT_TRUE(document.append_point({.position = {4, 0, 4}}).ok());
  EXPECT_GT(document.points().back().id.index, retired_point.index);
  EXPECT_GT(document.segments().back().index, retired_segment.index);
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingCurveDocumentTests, ExhaustedIdentitySpaceNeverWrapsToRetiredIds) {
  auto document = make_arch();
  const auto identity = document.content_identity();
  constexpr std::uint64_t kExhausted =
      std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1U;
  document.preserve_identity_high_water({kExhausted, kExhausted});
  EXPECT_TRUE(document.validate().ok());
  EXPECT_FALSE(document.append_point({}).ok());
  EXPECT_FALSE(document.split_segment(document.segments().front()).ok());
  EXPECT_FALSE(document.set_closed(true).ok());
  EXPECT_EQ(document.content_identity(), identity);
  auto point = spec(document.points().front());
  point.position.z = 1;
  EXPECT_TRUE(document.set_point(document.points().front().id, point).ok());
}

TEST(ModelingCurveDocumentTests, BinaryRoundTripPreservesAuthoredDataAndRetiredAllocation) {
  auto document = make_arch();
  ASSERT_TRUE(document.append_point({.position = {8, 0, 0}}).ok());
  const auto retired_id = document.points().back().id;
  ASSERT_TRUE(document.remove_point(retired_id).ok());
  ASSERT_TRUE(document.split_segment(document.segments().front(), 0.37).ok());
  ASSERT_TRUE(document.set_closed(true).ok());
  const auto encoded = serialize_curve_document(document);
  ASSERT_TRUE(encoded.ok());
  const auto decoded = deserialize_curve_document(encoded.value());
  ASSERT_TRUE(decoded.ok());
  EXPECT_TRUE(decoded.value().has_same_geometry(document));
  EXPECT_EQ(decoded.value().identity_high_water(), document.identity_high_water());
  EXPECT_EQ(decoded.value().content_revision(), 0U);
  EXPECT_NE(decoded.value().content_identity(), document.content_identity());
  const auto again = serialize_curve_document(decoded.value());
  ASSERT_TRUE(again.ok());
  EXPECT_EQ(again.value(), encoded.value());
  expect_vec_near(decoded.value().bounds().min, document.bounds().min);
  expect_vec_near(decoded.value().bounds().max, document.bounds().max);
  auto restored = decoded.value();
  ASSERT_TRUE(restored.append_point({.position = {9, 2, 1}}).ok());
  EXPECT_GT(restored.points().back().id.index, retired_id.index);
}

TEST(ModelingCurveDocumentTests, BinaryDecoderRejectsMalformedAndUnsupportedPayloads) {
  const auto encoded = serialize_curve_document(make_arch());
  ASSERT_TRUE(encoded.ok());
  const auto &valid = encoded.value();
  for (std::size_t length = 0; length < valid.size(); ++length) {
    EXPECT_FALSE(deserialize_curve_document(std::span(valid).first(length)).ok());
  }
  auto corrupted = valid;
  corrupted.push_back(std::byte{0});
  EXPECT_FALSE(deserialize_curve_document(corrupted).ok());
  corrupted = valid;
  corrupted[0] = std::byte{'X'};
  EXPECT_FALSE(deserialize_curve_document(corrupted).ok());
  corrupted = valid;
  corrupted[4] = std::byte{2};
  const auto future = deserialize_curve_document(corrupted);
  ASSERT_FALSE(future.ok());
  EXPECT_EQ(future.error().code, ErrorCode::UnsupportedOperation);
  corrupted = valid;
  write_u32(corrupted, 8, 2);
  EXPECT_FALSE(deserialize_curve_document(corrupted).ok());
  corrupted = valid;
  write_u32(corrupted, 12, std::numeric_limits<std::uint32_t>::max());
  EXPECT_FALSE(deserialize_curve_document(corrupted).ok());
  corrupted = valid;
  write_u32(corrupted, 20, 1);
  EXPECT_FALSE(deserialize_curve_document(corrupted).ok());
  corrupted = valid;
  write_u32(corrupted, 24, 1);
  EXPECT_FALSE(deserialize_curve_document(corrupted).ok());
  corrupted = valid;
  write_u32(corrupted, 44, 2);
  EXPECT_FALSE(deserialize_curve_document(corrupted).ok());
  corrupted = valid;
  write_u32(corrupted, 48, 0x7fc00000U);
  EXPECT_FALSE(deserialize_curve_document(corrupted).ok());
}

} // namespace
