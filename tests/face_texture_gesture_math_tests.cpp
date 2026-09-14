////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "editor/modeling/modeling_face_texture_canvas.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace quader {
namespace {

using Map = modeling::FaceTextureCanvasMap;
using Kind = ModelingFaceTextureGestureKind;

/// Evaluates a public affine chart at one physical point for observable assertions.
std::array<double, 2> mapped_point(const Map &map, QVec2 point) {
  const auto &matrix = map.uv_from_plane;
  return {matrix[0] * point.x + matrix[1] * point.y + map.uv_offset[0],
          matrix[2] * point.x + matrix[3] * point.y + map.uv_offset[1]};
}

/// Supplies known texture-repeat dimensions with mirrored V and nonzero phase.
ModelingFaceTextureCanvas canvas_baseline(double width = 10.0, double height = 20.0) {
  ModelingFaceTextureCanvas canvas;
  canvas.editable = true;
  canvas.chart.mapping_usable = true;
  canvas.chart.map = {.uv_from_plane = {1.0 / width, 0.0, 0.0, -1.0 / height},
                      .uv_offset = {0.25, 0.75}};
  canvas.texture_extent = {128.0F, 64.0F};
  measure_face_texture_canvas(canvas);
  return canvas;
}

/// Resolves the displayed repeat dimensions of a completed candidate.
ModelingFaceTextureCanvas measured_candidate(const Map &map) {
  auto result = canvas_baseline();
  result.chart.map = map;
  measure_face_texture_canvas(result);
  return result;
}

/// Compares the UV phase of an anchored point before and after a gesture.
void expect_fixed_point(const Map &before, const Map &after, QVec2 point) {
  const auto first = mapped_point(before, point);
  const auto second = mapped_point(after, point);
  EXPECT_NEAR(first[0], second[0], 1.0e-10);
  EXPECT_NEAR(first[1], second[1], 1.0e-10);
}

/// Keeps matrix comparison tolerances explicit for material round-trip checks.
void expect_same_map(const Map &actual, const Map &expected) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    EXPECT_NEAR(actual.uv_from_plane[index], expected.uv_from_plane[index], 1.0e-12);
  }
  for (std::size_t index = 0U; index < 2U; ++index) {
    EXPECT_NEAR(actual.uv_offset[index], expected.uv_offset[index], 1.0e-12);
  }
}

TEST(FaceTextureGestureMathTests, TranslationMovesTextureUnderFixedPhysicalPoints) {
  const auto baseline = canvas_baseline(4.0, 8.0);
  const ModelingFaceTextureGestureBegin begin{
      .kind = Kind::Translate, .start = {1.0F, 2.0F}};
  const auto result = prepare_face_texture_gesture_map(
      baseline, begin, {.current = {3.5F, -1.0F}}).or_throw();
  EXPECT_EQ(result.uv_from_plane, baseline.chart.map.uv_from_plane);
  EXPECT_DOUBLE_EQ(result.uv_offset[0], -0.375);
  EXPECT_DOUBLE_EQ(result.uv_offset[1], 0.375);
  const auto original_uv = mapped_point(baseline.chart.map, begin.start);
  const auto dragged_uv = mapped_point(result, {3.5F, -1.0F});
  EXPECT_EQ(dragged_uv, original_uv);
}

TEST(FaceTextureGestureMathTests, PrecisionTranslationUsesTexelsAndShiftTakesPrecedence) {
  auto baseline = canvas_baseline(4.0, 8.0);
  baseline.texture_extent = {8.0F, 16.0F};
  const ModelingFaceTextureGestureBegin begin{.kind = Kind::Translate};
  const ModelingFaceTextureGestureUpdate plain{.current = {0.3F, 0.9F}};
  auto precise = plain;
  precise.precision = true;
  const auto quantized = prepare_face_texture_gesture_map(baseline, begin, precise).or_throw();
  EXPECT_DOUBLE_EQ(quantized.uv_offset[0] - baseline.chart.map.uv_offset[0], -1.0 / 8.0);
  EXPECT_DOUBLE_EQ(quantized.uv_offset[1] - baseline.chart.map.uv_offset[1], 2.0 / 16.0);
  auto both = precise;
  both.snap = true;
  both.grid_step = 4.0;
  const auto unsnapped = prepare_face_texture_gesture_map(baseline, begin, plain).or_throw();
  const auto shifted = prepare_face_texture_gesture_map(baseline, begin, both).or_throw();
  expect_same_map(shifted, unsnapped);
  EXPECT_NEAR(unsnapped.uv_offset[0] - baseline.chart.map.uv_offset[0], -0.075, 1.0e-8);
  EXPECT_NEAR(unsnapped.uv_offset[1] - baseline.chart.map.uv_offset[1], 0.1125, 1.0e-8);
}

TEST(FaceTextureGestureMathTests, WidthAndHeightScalingKeepTheOppositeEdgeFixed) {
  const auto baseline = canvas_baseline();
  const ModelingFaceTextureGestureBegin width_begin{
      .kind = Kind::ScaleWidth, .start = {10.0F, 2.0F}, .pivot = {0.0F, 2.0F}};
  const auto width = prepare_face_texture_gesture_map(
      baseline, width_begin, {.current = {13.5F, 99.0F}}).or_throw();
  const auto width_measurement = measured_candidate(width);
  EXPECT_NEAR(width_measurement.repeat_width, 13.5, 1.0e-10);
  EXPECT_NEAR(width_measurement.repeat_height, 20.0, 1.0e-10);
  expect_fixed_point(baseline.chart.map, width, {0.0F, -3.0F});
  expect_fixed_point(baseline.chart.map, width, {0.0F, 7.0F});

  const ModelingFaceTextureGestureBegin height_begin{
      .kind = Kind::ScaleHeight, .start = {3.0F, 10.0F}, .pivot = {3.0F, 0.0F}};
  const auto height = prepare_face_texture_gesture_map(
      baseline, height_begin, {.current = {99.0F, 14.0F}}).or_throw();
  const auto height_measurement = measured_candidate(height);
  EXPECT_NEAR(height_measurement.repeat_width, 10.0, 1.0e-10);
  EXPECT_NEAR(height_measurement.repeat_height, 28.0, 1.0e-10);
  expect_fixed_point(baseline.chart.map, height, {-3.0F, 0.0F});
  expect_fixed_point(baseline.chart.map, height, {7.0F, 0.0F});
}

TEST(FaceTextureGestureMathTests, ScaleModifiersQuantizeDisplayedRepeatDimensions) {
  const auto baseline = canvas_baseline();
  const ModelingFaceTextureGestureBegin width_begin{
      .kind = Kind::ScaleWidth, .start = {10.0F, 0.0F}};
  const auto width_precise = prepare_face_texture_gesture_map(
      baseline, width_begin, {.current = {13.7F, 0.0F}, .precision = true}).or_throw();
  EXPECT_NEAR(measured_candidate(width_precise).repeat_width, 14.0, 1.0e-10);
  const auto width_grid = prepare_face_texture_gesture_map(
      baseline, width_begin,
      {.current = {13.7F, 0.0F}, .precision = true, .snap = true, .grid_step = 4.0}).or_throw();
  EXPECT_NEAR(measured_candidate(width_grid).repeat_width, 12.0, 1.0e-10);

  const ModelingFaceTextureGestureBegin height_begin{
      .kind = Kind::ScaleHeight, .start = {0.0F, 10.0F}};
  const auto height_precise = prepare_face_texture_gesture_map(
      baseline, height_begin, {.current = {0.0F, 13.7F}, .precision = true}).or_throw();
  EXPECT_NEAR(measured_candidate(height_precise).repeat_height, 27.0, 1.0e-10);
  const auto height_grid = prepare_face_texture_gesture_map(
      baseline, height_begin,
      {.current = {0.0F, 13.7F}, .precision = true, .snap = true, .grid_step = 8.0}).or_throw();
  EXPECT_NEAR(measured_candidate(height_grid).repeat_height, 24.0, 1.0e-10);
}

TEST(FaceTextureGestureMathTests, SnappingBackToOriginalScaleRetainsExactPhase) {
  auto baseline = canvas_baseline();
  baseline.chart.map.uv_offset = {0.1, 0.3};
  for (const auto kind : {Kind::ScaleWidth, Kind::ScaleHeight}) {
    const bool width = kind == Kind::ScaleWidth;
    const ModelingFaceTextureGestureBegin begin{
        .kind = kind,
        .start = width ? QVec2{10010.0F, 10000.0F} : QVec2{10000.0F, 10010.0F},
        .pivot = {10000.0F, 10000.0F}};
    for (const bool grid : {false, true}) {
      const auto candidate = prepare_face_texture_gesture_map(baseline, begin,
          {.current = width ? QVec2{10010.1F, 10000.0F} : QVec2{10000.0F, 10010.1F},
           .precision = true, .snap = grid, .grid_step = 1.0}).or_throw();
      EXPECT_EQ(candidate, baseline.chart.map);
    }
  }
}

/// Places handles along the image axes, which are columns of the inverse chart.
QVec2 texture_axis_point(const Map &map, QVec2 pivot, double u, double v) {
  const auto &m = map.uv_from_plane;
  const double determinant = m[0] * m[3] - m[1] * m[2];
  return {static_cast<float>(pivot.x + (m[3] * u - m[1] * v) / determinant),
          static_cast<float>(pivot.y + (-m[2] * u + m[0] * v) / determinant)};
}

TEST(FaceTextureGestureMathTests, RotatedTextureScaleChangesOnlyTheRequestedRepeat) {
  auto baseline = canvas_baseline();
  constexpr double kCosine = std::numbers::sqrt2 / 2.0;
  baseline.chart.map.uv_from_plane = {kCosine / 10.0, kCosine / 10.0,
                                     kCosine / 20.0, -kCosine / 20.0};
  measure_face_texture_canvas(baseline);
  ASSERT_NEAR(baseline.repeat_width, 10.0, 1.0e-10);
  ASSERT_NEAR(baseline.repeat_height, 20.0, 1.0e-10);
  for (const auto kind : {Kind::ScaleWidth, Kind::ScaleHeight}) {
    SCOPED_TRACE(static_cast<int>(kind));
    const bool width = kind == Kind::ScaleWidth;
    const ModelingFaceTextureGestureBegin begin{
        .kind = kind,
        .start = texture_axis_point(baseline.chart.map, {3.0F, 2.0F},
                                   width ? 1.0 : 0.0, width ? 0.0 : 0.5),
        .pivot = {3.0F, 2.0F}};
    for (const auto modifiers : {0, 1, 2, 3}) {
      SCOPED_TRACE(modifiers);
      const bool precision = (modifiers & 1) != 0;
      const bool snap = (modifiers & 2) != 0;
      const auto prepared = prepare_face_texture_gesture_map(
          baseline, begin,
          {.current = texture_axis_point(baseline.chart.map, begin.pivot,
                                         width ? 1.32 : 0.25,
                                         width ? 0.25 : 0.66),
           .precision = precision, .snap = snap, .grid_step = 4.0});
      ASSERT_TRUE(prepared.ok()) << prepared.error_message();
      const auto measured = measured_candidate(prepared.value());
      const double repeat = width ? measured.repeat_width : measured.repeat_height;
      const double expected = width ? (snap ? 12.0 : precision ? 13.0 : 13.2)
                                     : (snap ? 28.0 : precision ? 26.0 : 26.4);
      EXPECT_NEAR(repeat, expected, precision || snap ? 1.0e-10 : 1.0e-5);
      EXPECT_NEAR(width ? measured.repeat_height : measured.repeat_width,
                   width ? baseline.repeat_height : baseline.repeat_width, 1.0e-10);
      expect_fixed_point(baseline.chart.map, prepared.value(), begin.pivot);
      const auto &matrix = prepared.value().uv_from_plane;
      EXPECT_LT(matrix[0] * matrix[3] - matrix[1] * matrix[2], 0.0);
      for (const std::size_t row : {0U, 2U}) {
        EXPECT_NEAR(std::atan2(matrix[row + 1U], matrix[row]),
                     std::atan2(baseline.chart.map.uv_from_plane[row + 1U],
                                  baseline.chart.map.uv_from_plane[row]), 1.0e-12);
      }
    }
  }
}

TEST(FaceTextureGestureMathTests, RotationModifiersPreservePivotAndRepeatLengths) {
  const auto baseline = canvas_baseline();
  const QVec2 pivot{3.0F, 7.0F};
  const ModelingFaceTextureGestureBegin begin{
      .kind = Kind::Rotate, .start = {4.0F, 7.0F}, .pivot = pivot};
  const double radians = 17.4 * std::numbers::pi / 180.0;
  const QVec2 current{static_cast<float>(pivot.x + std::cos(radians)),
                      static_cast<float>(pivot.y + std::sin(radians))};
  for (const auto modifiers : {0, 1, 2, 3}) {
    SCOPED_TRACE(modifiers);
    const bool precision = (modifiers & 1) != 0;
    const bool snap = (modifiers & 2) != 0;
    const auto result = prepare_face_texture_gesture_map(
        baseline, begin, {.current = current, .precision = precision,
                           .snap = snap, .rotation_step = 15.0}).or_throw();
    const double angle = std::atan2(result.uv_from_plane[1], result.uv_from_plane[0]) *
                          180.0 / std::numbers::pi;
    EXPECT_NEAR(angle, snap ? 15.0 : precision ? 17.0 : 17.4, 3.0e-5);
    expect_fixed_point(baseline.chart.map, result, pivot);
    const auto measured = measured_candidate(result);
    EXPECT_NEAR(measured.repeat_width, baseline.repeat_width, 1.0e-10);
    EXPECT_NEAR(measured.repeat_height, baseline.repeat_height, 1.0e-10);
  }
}

TEST(FaceTextureGestureMathTests, MaterialTransformMatchesEffectiveShaderSampling) {
  const Map map{.uv_from_plane = {0.25, 0.05, -0.125, 0.375},
                .uv_offset = {-0.2, 1.1}};
  const StandardMaterialTextureTransform transform{
      .center = {0.2F, 0.7F}, .scale = {2.0F, 0.5F},
      .offset = {0.3F, -0.4F}, .rotation_degrees = 90.0F};
  const auto displayed = transform_face_texture_canvas_map(map, transform);
  const auto uv = mapped_point(displayed, {2.0F, -4.0F});
  // The shader's flipUV option and explicit V flip cancel. Source UV=(0.1,-0.65)
  // therefore enters the material directly, giving centered/scaled=(-0.2,-0.675).
  EXPECT_NEAR(uv[0], 1.175, 1.0e-7);
  EXPECT_NEAR(uv[1], 0.1, 1.0e-7);
  expect_same_map(transform_face_texture_canvas_map(displayed, transform, true), map);
  const auto identity_material = transform_face_texture_canvas_map(map, {});
  const auto original = mapped_point(map, {2.0F, -4.0F});
  const auto image_uv = mapped_point(identity_material, {2.0F, -4.0F});
  EXPECT_DOUBLE_EQ(image_uv[0], original[0]);
  EXPECT_DOUBLE_EQ(image_uv[1], original[1]);
}

TEST(FaceTextureGestureMathTests, ZeroCrossingAndInvalidGestureCoordinatesAreRejected) {
  const auto baseline = canvas_baseline();
  const ModelingFaceTextureGestureBegin width{
      .kind = Kind::ScaleWidth, .start = {10.0F, 0.0F}};
  EXPECT_FALSE(prepare_face_texture_gesture_map(baseline, width, {.current = {0.0F, 0.0F}}).ok());
  EXPECT_FALSE(prepare_face_texture_gesture_map(baseline, width, {.current = {-1.0F, 0.0F}}).ok());
  auto at_pivot = width;
  at_pivot.start = {};
  EXPECT_FALSE(prepare_face_texture_gesture_map(baseline, at_pivot, {.current = {1.0F, 0.0F}}).ok());
  at_pivot.kind = Kind::Rotate;
  EXPECT_FALSE(prepare_face_texture_gesture_map(baseline, at_pivot, {.current = {1.0F, 0.0F}}).ok());
  auto rotation = width;
  rotation.kind = Kind::Rotate;
  EXPECT_FALSE(prepare_face_texture_gesture_map(baseline, rotation, {}).ok());
  const float invalid = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(prepare_face_texture_gesture_map(baseline, width, {.current = {invalid, 0.0F}}).ok());
  auto bad_begin = width;
  bad_begin.start.x = invalid;
  EXPECT_FALSE(prepare_face_texture_gesture_map(baseline, bad_begin, {}).ok());
  bad_begin = width;
  bad_begin.pivot.y = invalid;
  EXPECT_FALSE(prepare_face_texture_gesture_map(baseline, bad_begin, {}).ok());
  auto singular = baseline;
  singular.chart.map.uv_from_plane = {};
  EXPECT_FALSE(prepare_face_texture_gesture_map(singular, width, {}).ok());
  auto nonfinite = baseline;
  nonfinite.chart.map.uv_offset[0] = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(prepare_face_texture_gesture_map(nonfinite, width, {}).ok());
}

TEST(FaceTextureGestureMathTests, TranslationRejectsInvalidTexelExtentAndNonfiniteCandidates) {
  const ModelingFaceTextureGestureBegin begin{.kind = Kind::Translate};
  auto baseline = canvas_baseline();
  baseline.texture_extent.x = 0.0F;
  EXPECT_FALSE(prepare_face_texture_gesture_map(
      baseline, begin, {.current = {1.0F, 1.0F}, .precision = true}).ok());
  baseline = canvas_baseline();
  baseline.texture_extent.y = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(prepare_face_texture_gesture_map(
      baseline, begin, {.current = {1.0F, 1.0F}, .precision = true}).ok());
  baseline.chart.map.uv_from_plane = {1.0e308, 0.0, 0.0, 1.0e308};
  EXPECT_FALSE(prepare_face_texture_gesture_map(
      baseline, begin, {.current = {10.0F, 10.0F}}).ok());
}

TEST(FaceTextureGestureMathTests, LowDensityIsNotMistakenForASingularMapping) {
  auto baseline = canvas_baseline();
  baseline.chart.map.uv_from_plane = {1.0e-12, 0.0, 0.0, -2.0e-12};
  measure_face_texture_canvas(baseline);
  EXPECT_TRUE(baseline.editable);
  EXPECT_NEAR(baseline.repeat_width, 1.0e12, 1.0);
  EXPECT_NEAR(baseline.repeat_height, 5.0e11, 1.0);
  const auto translated = prepare_face_texture_gesture_map(
      baseline, {.kind = Kind::Translate}, {.current = {1024.0F, 1024.0F}});
  ASSERT_TRUE(translated.ok()) << translated.error_message();
  EXPECT_NEAR(translated.value().uv_offset[0], baseline.chart.map.uv_offset[0] - 1.024e-9, 1.0e-15);
  EXPECT_NEAR(translated.value().uv_offset[1], baseline.chart.map.uv_offset[1] + 2.048e-9, 1.0e-15);
}

TEST(FaceTextureGestureMathTests, BaselineRelativeUpdatesDoNotAccumulateAndReturnExactly) {
  const auto baseline = canvas_baseline();
  const ModelingFaceTextureGestureBegin begin{
      .kind = Kind::ScaleWidth, .start = {10.0F, 4.0F}, .pivot = {0.0F, 4.0F}};
  const ModelingFaceTextureGestureUpdate update{.current = {14.0F, 4.0F}, .precision = true};
  const auto first = prepare_face_texture_gesture_map(baseline, begin, update).or_throw();
  const auto repeated = prepare_face_texture_gesture_map(baseline, begin, update).or_throw();
  EXPECT_EQ(first, repeated);
  const auto restored = prepare_face_texture_gesture_map(
      baseline, begin, {.current = begin.start}).or_throw();
  EXPECT_EQ(restored, baseline.chart.map);
}

TEST(FaceTextureGestureMathTests, ReturningToOriginPreservesUnsnappedBaselineWithModifiers) {
  const auto baseline = canvas_baseline(10.4, 20.7);
  for (const auto kind : {Kind::ScaleWidth, Kind::ScaleHeight}) {
    SCOPED_TRACE(static_cast<int>(kind));
    const ModelingFaceTextureGestureBegin begin{
        .kind = kind, .start = {10.0F, 10.0F}, .pivot = {0.0F, 0.0F}};
    for (const bool snap : {false, true}) {
      SCOPED_TRACE(snap);
      const auto result = prepare_face_texture_gesture_map(
          baseline, begin, {.current = begin.start, .precision = true,
                             .snap = snap, .grid_step = 8.0});
      ASSERT_TRUE(result.ok()) << result.error_message();
      EXPECT_EQ(result.value(), baseline.chart.map);
    }
  }
}

TEST(FaceTextureGestureMathTests, QuarterTurnTextureSupportsBothHandlesAndAllModifiers) {
  auto baseline = canvas_baseline();
  baseline.chart.map.uv_from_plane = {0.0, 0.1, 0.05, 0.0};
  measure_face_texture_canvas(baseline);
  for (const auto kind : {Kind::ScaleWidth, Kind::ScaleHeight}) {
    SCOPED_TRACE(static_cast<int>(kind));
    const bool width = kind == Kind::ScaleWidth;
    const ModelingFaceTextureGestureBegin begin{
        .kind = kind,
        .start = width ? QVec2{0.0F, 10.0F} : QVec2{10.0F, 0.0F},
        .pivot = {0.0F, 0.0F}};
    for (const auto modifiers : {0, 1, 2, 3}) {
      SCOPED_TRACE(modifiers);
      const bool precision = (modifiers & 1) != 0;
      const bool snap = (modifiers & 2) != 0;
      const auto result = prepare_face_texture_gesture_map(
          baseline, begin,
          {.current = width ? QVec2{4.0F, 13.2F} : QVec2{13.2F, 4.0F},
           .precision = precision,
                             .snap = snap, .grid_step = 4.0});
      ASSERT_TRUE(result.ok()) << result.error_message();
      const auto measured = measured_candidate(result.value());
      const double expected_width = width ? (snap ? 12.0 : precision ? 13.0 : 13.2) : 10.0;
      const double expected_height = width ? 20.0 : (snap ? 28.0 : precision ? 26.0 : 26.4);
      EXPECT_NEAR(measured.repeat_width, expected_width, precision || snap ? 1.0e-10 : 2.0e-6);
      EXPECT_NEAR(measured.repeat_height, expected_height, precision || snap ? 1.0e-10 : 2.0e-6);
      expect_fixed_point(baseline.chart.map, result.value(), begin.pivot);
      EXPECT_DOUBLE_EQ(result.value().uv_from_plane[0], 0.0);
      EXPECT_DOUBLE_EQ(result.value().uv_from_plane[3], 0.0);
      EXPECT_GT(result.value().uv_from_plane[1], 0.0);
      EXPECT_GT(result.value().uv_from_plane[2], 0.0);
    }
    const auto zero = prepare_face_texture_gesture_map(
        baseline, begin,
        {.current = width ? QVec2{10.0F, 0.0F} : QVec2{0.0F, 10.0F}});
    EXPECT_FALSE(zero.ok());
  }
}

TEST(FaceTextureGestureMathTests, TextureAxisHandlesPreserveMirrorsShearAndOppositeRepeat) {
  for (const auto matrix : {
           std::array{0.0, 0.1, 0.05, 0.0},
           std::array{0.06, 0.08, 0.04, -0.03},
           std::array{-0.06, -0.08, 0.04, -0.03},
           std::array{0.06, 0.08, -0.04, 0.03},
           std::array{-0.06, -0.08, -0.04, 0.03},
           std::array{0.1, 0.05, -0.025, 0.05}}) {
    auto baseline = canvas_baseline();
    baseline.chart.map.uv_from_plane = matrix;
    measure_face_texture_canvas(baseline);
    for (const auto kind : {Kind::ScaleWidth, Kind::ScaleHeight}) {
      SCOPED_TRACE(static_cast<int>(kind));
      SCOPED_TRACE(::testing::PrintToString(matrix));
      const bool width = kind == Kind::ScaleWidth;
      const QVec2 pivot{3.0F, -2.0F};
      const ModelingFaceTextureGestureBegin begin{
          .kind = kind,
          .start = texture_axis_point(baseline.chart.map, pivot,
                                     width ? 1.0 : 0.0, width ? 0.0 : 1.0),
          .pivot = pivot};
      const auto current = texture_axis_point(baseline.chart.map, pivot,
                                             width ? 1.5 : 2.0,
                                             width ? 2.0 : 1.5);
      const auto prepared = prepare_face_texture_gesture_map(
          baseline, begin, {.current = current});
      ASSERT_TRUE(prepared.ok()) << prepared.error_message();
      const auto &candidate = prepared.value();
      const auto measured = measured_candidate(candidate);
      EXPECT_NEAR(measured.repeat_width,
                  baseline.repeat_width * (width ? 1.5 : 1.0), 1.0e-5);
      EXPECT_NEAR(measured.repeat_height,
                  baseline.repeat_height * (width ? 1.0 : 1.5), 1.0e-5);
      expect_fixed_point(baseline.chart.map, candidate, pivot);
      const std::size_t selected_row = width ? 0 : 2;
      const std::size_t opposite_row = width ? 2 : 0;
      EXPECT_DOUBLE_EQ(candidate.uv_from_plane[opposite_row], matrix[opposite_row]);
      EXPECT_DOUBLE_EQ(candidate.uv_from_plane[opposite_row + 1], matrix[opposite_row + 1]);
      EXPECT_NEAR(candidate.uv_from_plane[selected_row], matrix[selected_row] / 1.5, 1.0e-8);
      EXPECT_NEAR(candidate.uv_from_plane[selected_row + 1], matrix[selected_row + 1] / 1.5, 1.0e-8);
      const double original_determinant = matrix[0] * matrix[3] - matrix[1] * matrix[2];
      const auto &changed = candidate.uv_from_plane;
      EXPECT_EQ(std::signbit(changed[0] * changed[3] - changed[1] * changed[2]),
                std::signbit(original_determinant));
      EXPECT_EQ(prepare_face_texture_gesture_map(
                    baseline, begin, {.current = current}).or_throw(), candidate);
      const auto opposite_motion = texture_axis_point(
          baseline.chart.map, pivot, width ? 1.0 : 2.0, width ? 2.0 : 1.0);
      EXPECT_EQ(prepare_face_texture_gesture_map(
                    baseline, begin, {.current = opposite_motion}).or_throw(),
                baseline.chart.map);
      EXPECT_EQ(prepare_face_texture_gesture_map(
                    baseline, begin, {.current = begin.start,
                                      .precision = true, .snap = true,
                                      .grid_step = 8.0}).or_throw(), baseline.chart.map);
      for (const double extent : {0.0, -0.5}) {
        const auto crossed = texture_axis_point(baseline.chart.map, pivot,
                                               width ? extent : 1.0,
                                               width ? 1.0 : extent);
        EXPECT_FALSE(prepare_face_texture_gesture_map(
            baseline, begin, {.current = crossed}).ok());
      }
    }
  }
}

TEST(FaceTextureGestureMathTests, ScalePivotToleranceUsesPhysicalUnitsAtAnyTextureDensity) {
  for (const double density : {1.0e-12, 1.0e12}) {
    auto baseline = canvas_baseline();
    baseline.chart.map.uv_from_plane = {0.0, density, -density, 0.0};
    measure_face_texture_canvas(baseline);
    for (const auto kind : {Kind::ScaleWidth, Kind::ScaleHeight}) {
      SCOPED_TRACE(static_cast<int>(kind));
      SCOPED_TRACE(density);
      const bool width = kind == Kind::ScaleWidth;
      ModelingFaceTextureGestureBegin begin{
          .kind = kind,
          .start = width ? QVec2{0.0F, 10.0F} : QVec2{10.0F, 0.0F}};
      const auto scaled = prepare_face_texture_gesture_map(
          baseline, begin,
          {.current = width ? QVec2{0.0F, 15.0F} : QVec2{15.0F, 0.0F}});
      ASSERT_TRUE(scaled.ok()) << scaled.error_message();
      const auto measured = measured_candidate(scaled.value());
      EXPECT_NEAR(measured.repeat_width / baseline.repeat_width, width ? 1.5 : 1.0, 1.0e-12);
      EXPECT_NEAR(measured.repeat_height / baseline.repeat_height, width ? 1.0 : 1.5, 1.0e-12);
      begin.start = width ? QVec2{0.0F, 1.0e-9F} : QVec2{1.0e-9F, 0.0F};
      EXPECT_FALSE(prepare_face_texture_gesture_map(
          baseline, begin,
          {.current = width ? QVec2{0.0F, 1.0F} : QVec2{1.0F, 0.0F}}).ok());
    }
  }
}

} // namespace
} // namespace quader
