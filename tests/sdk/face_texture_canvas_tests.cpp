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
#include <limits>

namespace {

using namespace quader::modeling;

/// Gives the canvas a mirrored, non-square authored chart with exact world units.
AuthoredTopology canvas_topology() {
  AuthoredTopology topology;
  topology.vertices = {
      {make_id<VertexTag>(1U), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2U), {4.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3U), {4.0F, 3.0F, 0.0F}},
      {make_id<VertexTag>(4U), {0.0F, 3.0F, 0.0F}}};
  topology.faces = {{.id = make_id<FaceTag>(1U),
      .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                   make_id<VertexTag>(3U), make_id<VertexTag>(4U)},
      .uvs = {{0.25F, 0.0F}, {2.25F, 0.0F}, {2.25F, -1.0F}, {0.25F, -1.0F}},
      .material_slot = 7U}};
  topology.next_vertex_id = 5U;
  topology.next_face_id = 2U;
  return topology;
}

/// Associates a baseline publication with one independently placed occurrence.
FaceTextureCanvasInput canvas_input(const PolygonDocument &document) {
  return {.key = 42U, .document = &document, .face = make_id<FaceTag>(1U)};
}

/// Compares reconstructed effective coefficients against the requested chart.
void expect_canvas_map_near(const FaceTextureCanvasMap &actual,
                            const FaceTextureCanvasMap &expected) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    EXPECT_NEAR(actual.uv_from_plane[index], expected.uv_from_plane[index], 2.0e-5);
  }
  for (std::size_t index = 0U; index < 2U; ++index) {
    EXPECT_NEAR(actual.uv_offset[index], expected.uv_offset[index], 2.0e-5);
  }
}

TEST(FaceTextureCanvasTests, QueryUsesPhysicalProportionsAndNormalizedMirroredUv) {
  const auto document = PolygonDocument::from_authored_topology(canvas_topology()).or_throw();
  const auto chart = query_face_texture_canvas(canvas_input(document)).or_throw();
  ASSERT_TRUE(chart.mapping_usable) << chart.disabled_reason;
  const std::vector<std::array<double, 2>> outline{{0.0, 0.0}, {4.0, 0.0},
                                                {4.0, 3.0}, {0.0, 3.0}};
  EXPECT_EQ(chart.outline, outline);
  expect_canvas_map_near(chart.map, {.uv_from_plane = {0.5, 0.0, 0.0, -1.0 / 3.0},
                                    .uv_offset = {0.25, 0.0}});
  EXPECT_TRUE(chart.disabled_reason.empty());
}

TEST(FaceTextureCanvasTests, SlopedTriangleStaysUprightAcrossCornerOrderAndUvEdits) {
  auto topology = canvas_topology();
  topology.vertices.resize(3U);
  topology.vertices[2].position = {0.0F, 8.0F, -4.0F};
  auto &face = topology.faces.front();
  face.vertices.resize(3U);
  face.uvs = {{0.0F, 0.0F}, {2.0F, 0.0F}, {0.0F, -3.0F}};
  const double height = std::sqrt(80.0);
  const std::array<std::array<double, 2>, 3> physical{
      std::array{0.0, 0.0}, std::array{4.0, 0.0}, std::array{0.0, height}};
  for (std::size_t first = 0U; first < 3U; ++first) {
    SCOPED_TRACE(first);
    const auto document = PolygonDocument::from_authored_topology(topology).or_throw();
    const auto source_bytes = serialize_qdr_authored_topology(document).or_throw();
    const auto input = canvas_input(document);
    const auto chart = query_face_texture_canvas(input).or_throw();
    ASSERT_TRUE(chart.mapping_usable) << chart.disabled_reason;
    ASSERT_EQ(chart.outline.size(), 3U);
    for (std::size_t corner = 0U; corner < 3U; ++corner) {
      const auto &point = physical[(first + corner) % 3U];
      EXPECT_NEAR(chart.outline[corner][0], point[0] - physical[first][0], 1.0e-12);
      EXPECT_NEAR(chart.outline[corner][1], point[1] - physical[first][1], 1.0e-12);
    }
    auto changed_map = chart.map;
    changed_map.uv_from_plane = {0.125, 0.75, -0.25, 0.5};
    changed_map.uv_offset = {-0.25, 1.5};
    const auto edited = prepare_face_texture_canvas(input, changed_map).or_throw();
    ASSERT_TRUE(edited.receipt.changed);
    const auto changed_chart = query_face_texture_canvas(canvas_input(edited.document)).or_throw();
    EXPECT_EQ(chart.outline, changed_chart.outline);
    expect_canvas_map_near(changed_chart.map, changed_map);
    EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), source_bytes);
    std::rotate(face.vertices.begin(), face.vertices.begin() + 1, face.vertices.end());
    std::rotate(face.uvs.begin(), face.uvs.begin() + 1, face.uvs.end());
  }
}

TEST(FaceTextureCanvasTests, ScalarAndOccurrenceOffsetsDoNotQuantizeTextureGradients) {
  for (const bool generated : {false, true}) {
    for (const bool placed : {false, true}) {
      auto topology = canvas_topology();
      auto &face = topology.faces.front();
      if (generated) {
        face.uvs.clear();
      }
      face.texture_transform.scale = {32768.0F, -16384.0F};
      face.texture_transform.rotation_degrees = 37.0F;
      auto document = PolygonDocument::from_authored_topology(topology).or_throw();
      auto input = canvas_input(document);
      if (placed) {
        input.sampling_from_local.x_axis = {1.5F, 0.25F, 0.0F};
        input.sampling_from_local.y_axis = {0.5F, 0.75F, 0.0F};
      }
      const auto baseline = query_face_texture_canvas(input).or_throw();
      ASSERT_TRUE(baseline.mapping_usable) << baseline.disabled_reason;
      for (const float phase : {0.5F, 32.0F, 1024.0F}) {
        SCOPED_TRACE(phase);
        face.texture_transform.shift_uv = {phase, -phase};
        document = PolygonDocument::from_authored_topology(topology).or_throw();
        input.document = &document;
        input.sampling_from_local.origin = {phase, -phase, 0.0F};
        const auto shifted = query_face_texture_canvas(input).or_throw();
        ASSERT_TRUE(shifted.mapping_usable) << shifted.disabled_reason;
        EXPECT_EQ(shifted.outline, baseline.outline);
        EXPECT_EQ(shifted.map.uv_from_plane, baseline.map.uv_from_plane);
      }
    }
  }
}

TEST(FaceTextureCanvasTests, WorldPlacementDeterminesUprightWallAndHorizontalViews) {
  const auto document = PolygonDocument::from_authored_topology(canvas_topology()).or_throw();
  // Preserve world up on either wall side; floor/ceiling use world X as right.
  const std::array placements{
      Transform3{},
      Transform3{.x_axis = {-1.0F, 0.0F, 0.0F}, .z_axis = {0.0F, 0.0F, -1.0F}},
      Transform3{.x_axis = {0.0F, 0.0F, -1.0F}, .z_axis = {1.0F, 0.0F, 0.0F}},
      Transform3{.y_axis = {0.0F, 0.0F, -1.0F}, .z_axis = {0.0F, 1.0F, 0.0F}},
      Transform3{.y_axis = {0.0F, 0.0F, 1.0F}, .z_axis = {0.0F, -1.0F, 0.0F}}};
  const std::vector<std::array<double, 2>> expected{
      {0.0, 0.0}, {4.0, 0.0}, {4.0, 3.0}, {0.0, 3.0}};
  for (const auto &placement : placements) {
    auto input = canvas_input(document);
    input.world_from_local = placement;
    const auto chart = query_face_texture_canvas(input).or_throw();
    ASSERT_TRUE(chart.mapping_usable) << chart.disabled_reason;
    EXPECT_EQ(chart.outline, expected);
    EXPECT_FALSE(prepare_face_texture_canvas(input, chart.map).or_throw().receipt.changed);
  }
}

TEST(FaceTextureCanvasTests, TranslationKeepsOutlineGeometrySettingsAndSelection) {
  auto topology = canvas_topology();
  topology.faces.front().texture_transform.scale = {-2.0F, 0.5F};
  topology.faces.front().texture_transform.rotation_degrees = 37.0F;
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  const std::array selected{make_id<FaceTag>(1U)};
  ASSERT_TRUE(document.select_faces(selected, SelectionEdit::Replace).ok());
  const auto original_bytes = serialize_qdr_authored_topology(document).or_throw();
  const auto input = canvas_input(document);
  const auto before = query_face_texture_canvas(input).or_throw();
  ASSERT_TRUE(before.mapping_usable) << before.disabled_reason;
  auto moved = before.map;
  moved.uv_offset[0] += 0.75;
  moved.uv_offset[1] -= 0.5;
  const auto prepared = prepare_face_texture_canvas(input, moved).or_throw();
  EXPECT_EQ(prepared.key, input.key);
  ASSERT_TRUE(prepared.receipt.changed);
  EXPECT_TRUE(prepared.receipt.dirty.geometry);
  EXPECT_FALSE(prepared.receipt.dirty.topology);
  EXPECT_FALSE(prepared.receipt.dirty.materials);
  EXPECT_TRUE(prepared.document.has_same_geometry(document));
  EXPECT_TRUE(prepared.document.shares_selection_with(document));
  EXPECT_EQ(prepared.document.selection_revision(), document.selection_revision());
  const auto output = prepared.document.authored_topology().or_throw();
  EXPECT_EQ(output.faces.front().material_slot, 7U);
  EXPECT_EQ(output.faces.front().texture_transform, topology.faces.front().texture_transform);
  EXPECT_EQ(output.faces.front().vertices, topology.faces.front().vertices);
  const auto after = query_face_texture_canvas(canvas_input(prepared.document)).or_throw();
  ASSERT_TRUE(after.mapping_usable) << after.disabled_reason;
  EXPECT_EQ(before.outline, after.outline);
  expect_canvas_map_near(after.map, moved);
  EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), original_bytes);
}

TEST(FaceTextureCanvasTests, AbsoluteScaleAndRotationPreserveTheirPlanePivots) {
  const auto document = PolygonDocument::from_authored_topology(canvas_topology()).or_throw();
  const auto input = canvas_input(document);
  const auto before = query_face_texture_canvas(input).or_throw();
  const std::array<double, 2> pivot{2.0, 1.5};
  const std::array<double, 2> pivot_uv{1.25, -0.5};
  // Inverse 90-degree physical rotation and anisotropic scale, around the center.
  FaceTextureCanvasMap desired{.uv_from_plane = {0.0, 0.25, 2.0 / 3.0, 0.0}};
  desired.uv_offset = {pivot_uv[0] - desired.uv_from_plane[1] * pivot[1],
                       pivot_uv[1] - desired.uv_from_plane[2] * pivot[0]};
  const auto prepared = prepare_face_texture_canvas(input, desired).or_throw();
  ASSERT_TRUE(prepared.receipt.changed);
  const auto after = query_face_texture_canvas(canvas_input(prepared.document)).or_throw();
  ASSERT_TRUE(after.mapping_usable) << after.disabled_reason;
  EXPECT_EQ(after.outline, before.outline);
  expect_canvas_map_near(after.map, desired);
  EXPECT_NEAR(after.map.uv_from_plane[0] * pivot[0] +
              after.map.uv_from_plane[1] * pivot[1] + after.map.uv_offset[0],
              pivot_uv[0], 1.0e-6);
  EXPECT_NEAR(after.map.uv_from_plane[2] * pivot[0] +
              after.map.uv_from_plane[3] * pivot[1] + after.map.uv_offset[1],
              pivot_uv[1], 1.0e-6);
  EXPECT_LT(after.map.uv_from_plane[0] * after.map.uv_from_plane[3] -
             after.map.uv_from_plane[1] * after.map.uv_from_plane[2], 0.0);
}

TEST(FaceTextureCanvasTests, OccurrenceWorldAndSamplingTransformsRoundTripIndependently) {
  const auto document = PolygonDocument::from_authored_topology(canvas_topology()).or_throw();
  auto input = canvas_input(document);
  input.world_from_local = {.x_axis = {0.0F, 2.0F, 0.0F},
                            .y_axis = {-3.0F, 0.0F, 0.0F},
                            .z_axis = {0.0F, 0.0F, 1.0F},
                            .origin = {1.0e9F, -1.0e9F, 1.0e9F}};
  input.sampling_from_local = {.x_axis = {1.5F, 0.25F, 0.0F},
                               .y_axis = {0.5F, 0.75F, 0.0F},
                               .z_axis = {0.0F, 0.0F, 1.0F},
                               .origin = {10.0F, -8.0F, 0.0F}};
  const auto before = query_face_texture_canvas(input).or_throw();
  ASSERT_TRUE(before.mapping_usable) << before.disabled_reason;
  ASSERT_EQ(before.outline.size(), 4U);
  EXPECT_NEAR(std::hypot(before.outline[1][0] - before.outline[0][0],
                         before.outline[1][1] - before.outline[0][1]), 8.0, 1.0e-12);
  EXPECT_NEAR(std::hypot(before.outline[2][0] - before.outline[1][0],
                         before.outline[2][1] - before.outline[1][1]), 9.0, 1.0e-12);
  auto desired = before.map;
  desired.uv_from_plane[0] *= 1.5;
  desired.uv_from_plane[1] *= 1.5;
  desired.uv_offset[0] -= 0.75;
  desired.uv_offset[1] += 0.375;
  const auto prepared = prepare_face_texture_canvas(input, desired).or_throw();
  ASSERT_TRUE(prepared.receipt.changed);
  input.document = &prepared.document;
  const auto after = query_face_texture_canvas(input).or_throw();
  ASSERT_TRUE(after.mapping_usable) << after.disabled_reason;
  EXPECT_EQ(after.outline, before.outline);
  expect_canvas_map_near(after.map, desired);
}

TEST(FaceTextureCanvasTests, InvalidMappingsKeepOutlineAndNeverRewriteTheSource) {
  for (const auto defect : {0, 1, 2}) {
    SCOPED_TRACE(defect);
    auto topology = canvas_topology();
    if (defect == 0) {
      topology.faces.front().uvs.back().y -= 0.5F;
    } else if (defect == 1) {
      for (auto &uv : topology.faces.front().uvs) {
        uv.y = 0.0F;
      }
    } else {
      topology.vertices.back().position.z = 0.5F;
    }
    const auto document = PolygonDocument::from_authored_topology(topology).or_throw();
    const auto before = serialize_qdr_authored_topology(document).or_throw();
    const auto input = canvas_input(document);
    const auto chart = query_face_texture_canvas(input).or_throw();
    EXPECT_FALSE(chart.mapping_usable);
    EXPECT_EQ(chart.outline.size(), 4U);
    EXPECT_FALSE(chart.disabled_reason.empty());
    EXPECT_FALSE(prepare_face_texture_canvas(input, FaceTextureCanvasMap{}).ok());
    EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), before);
  }
}

TEST(FaceTextureCanvasTests, BaselineNoOpAndReturnToOriginShareTheOriginalPublication) {
  const auto document = PolygonDocument::from_authored_topology(canvas_topology()).or_throw();
  const auto input = canvas_input(document);
  const auto baseline = query_face_texture_canvas(input).or_throw();
  auto moved = baseline.map;
  moved.uv_offset[0] += 2.0;
  const auto first = prepare_face_texture_canvas(input, moved).or_throw();
  const auto repeated = prepare_face_texture_canvas(input, moved).or_throw();
  ASSERT_TRUE(first.receipt.changed);
  EXPECT_EQ(serialize_qdr_authored_topology(first.document).or_throw(),
            serialize_qdr_authored_topology(repeated.document).or_throw());
  const auto restored = prepare_face_texture_canvas(input, baseline.map).or_throw();
  EXPECT_FALSE(restored.receipt.changed);
  EXPECT_EQ(restored.document.content_identity(), document.content_identity());
  EXPECT_EQ(restored.document.content_revision(), document.content_revision());
  auto singular = baseline.map;
  singular.uv_from_plane[2] = 0.0;
  singular.uv_from_plane[3] = 0.0;
  EXPECT_FALSE(prepare_face_texture_canvas(input, singular).ok());
  auto nonfinite = baseline.map;
  nonfinite.uv_offset[0] = std::numeric_limits<double>::infinity();
  EXPECT_FALSE(prepare_face_texture_canvas(input, nonfinite).ok());
  auto stale = input;
  stale.face = make_id<FaceTag>(99U);
  EXPECT_FALSE(query_face_texture_canvas(stale).ok());
  EXPECT_FALSE(prepare_face_texture_canvas(stale, baseline.map).ok());
}

TEST(FaceTextureCanvasTests, ConcaveFacesRetainEveryAuthoredOutlineCorner) {
  auto topology = canvas_topology();
  topology.vertices.push_back({make_id<VertexTag>(5U), {2.0F, 1.5F, 0.0F}});
  auto &face = topology.faces.front();
  face.vertices.insert(face.vertices.begin() + 3, make_id<VertexTag>(5U));
  face.uvs.insert(face.uvs.begin() + 3, {1.25F, -0.5F});
  topology.next_vertex_id = 6U;
  const auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  const auto chart = query_face_texture_canvas(canvas_input(document)).or_throw();
  ASSERT_TRUE(chart.mapping_usable) << chart.disabled_reason;
  ASSERT_EQ(chart.outline.size(), 5U);
  EXPECT_DOUBLE_EQ(chart.outline[3][0], 2.0);
  EXPECT_DOUBLE_EQ(chart.outline[3][1], 1.5);
}

} // namespace
