////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/session/scene_session.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <vector>

namespace quader {
namespace {

/// Captures an interior texture sample independently of a face's loop anchor.
struct TransformUvSample {
  QVec3 position;
  QVec2 uv;
  bool transformed = false;
};

QuaderTransform3 half_turn_x() {
  return {.x_axis = {1, 0, 0}, .y_axis = {0, -1, 0}, .z_axis = {0, 0, -1}};
}

QVec3 half_turn_x(QVec3 point) { return {point.x, -point.y, -point.z}; }

quader_poly::Document transform_uv_document(bool reversed = false) {
  quader_poly::Document document;
  constexpr std::array<QVec3, 6> loop{{
      {0, 0, 0}, {3, 0, 0}, {4, 1, 0},
      {3, 3, 0}, {1, 4, 0}, {0, 2, 0}}};
  // The second disconnected face exercises untouched topology in the same mesh.
  for (const float offset : {0.0F, 10.0F}) {
    std::vector<quader_poly::ElementId> corners;
    for (const auto point : loop) {
      corners.push_back(quader_poly::add_vertex(
          document, point + QVec3{offset, 0, 0}));
    }
    if (reversed) std::ranges::reverse(corners);
    (void)quader_poly::add_face(document, corners);
    auto &face = document.faces.back();
    for (std::size_t index = 0; index < corners.size(); ++index) {
      face.uvs.push_back({0.31F * static_cast<float>(index % 3U),
                          0.27F * static_cast<float>((index * index) % 7U)});
    }
  }
  return document;
}

void expect_face_attributes(const quader_poly::Face &actual,
                            const quader_poly::Face &expected) {
  EXPECT_EQ(actual.id, expected.id);
  EXPECT_EQ(actual.vertices, expected.vertices);
  ASSERT_EQ(actual.uvs.size(), expected.uvs.size());
  for (std::size_t corner = 0; corner < actual.uvs.size(); ++corner) {
    EXPECT_FLOAT_EQ(actual.uvs[corner].x, expected.uvs[corner].x);
    EXPECT_FLOAT_EQ(actual.uvs[corner].y, expected.uvs[corner].y);
  }
  EXPECT_EQ(actual.material_slot, expected.material_slot);
  EXPECT_EQ(actual.normal_shading, expected.normal_shading);
  EXPECT_EQ(actual.texture_transform, expected.texture_transform);
}

void expect_document(const quader_poly::Document &actual,
                     const quader_poly::Document &expected) {
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  ASSERT_EQ(actual.faces.size(), expected.faces.size());
  for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
    EXPECT_EQ(actual.vertices[index].id, expected.vertices[index].id);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.x, expected.vertices[index].position.x);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.y, expected.vertices[index].position.y);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.z, expected.vertices[index].position.z);
  }
  for (std::size_t index = 0; index < actual.faces.size(); ++index) {
    expect_face_attributes(actual.faces[index], expected.faces[index]);
  }
  EXPECT_EQ(actual.hard_normal_edges, expected.hard_normal_edges);
  EXPECT_EQ(actual.soft_normal_edges, expected.soft_normal_edges);
  EXPECT_EQ(actual.next_vertex_id, expected.next_vertex_id);
  EXPECT_EQ(actual.next_face_id, expected.next_face_id);
}

std::optional<QVec2> triangle_uv_at(const MeshAsset &mesh, QVec3 point,
                                  std::span<const QVec3> positions = {}) {
  const auto &geometry = mesh.geometry();
  const auto position = [&](std::uint32_t index) {
    return positions.empty() ? geometry.vertices[index].position : positions[index];
  };
  for (std::size_t index = 0; index + 2U < geometry.indices.size(); index += 3U) {
    const auto ia = geometry.indices[index];
    const auto ib = geometry.indices[index + 1U];
    const auto ic = geometry.indices[index + 2U];
    const auto a = position(ia);
    const auto ab = position(ib) - a;
    const auto ac = position(ic) - a;
    const auto relative = point - a;
    const float aa = dot(ab, ab);
    const float cc = dot(ac, ac);
    const float mixed = dot(ab, ac);
    const float determinant = aa * cc - mixed * mixed;
    if (determinant <= 0.000001F) continue;
    const float wb = (dot(relative, ab) * cc - dot(relative, ac) * mixed) / determinant;
    const float wc = (dot(relative, ac) * aa - dot(relative, ab) * mixed) / determinant;
    const float wa = 1.0F - wb - wc;
    if (wa < -0.0001F || wb < -0.0001F || wc < -0.0001F ||
        length_squared(relative - ab * wb - ac * wc) > 0.000001F) continue;
    return geometry.vertices[ia].uv * wa + geometry.vertices[ib].uv * wb +
           geometry.vertices[ic].uv * wc;
  }
  return std::nullopt;
}

/// Verifies accepted transforms against the actual committed and preview triangles.
class UvTransformAcceptanceTests : public ::testing::Test {
protected:
  UvTransformAcceptanceTests()
      : editor(QuaderApiSessionInternalAccess::from_scene_session(session)) {}

  void create(bool reversed = false) {
    const auto created = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(transform_uv_document(reversed)),
        {.name = "Transform UV interiors"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    object = created.value();
    ASSERT_TRUE(session.scene().set_texture_mapping(object.entity, {QMat4{}}));
    select(QuaderSelectionMode::Face);
  }

  void select(QuaderSelectionMode mode) {
    ASSERT_TRUE(editor.selection().clear().ok());
    ASSERT_TRUE(editor.modeling().set_selection_mode(mode).ok());
    QuaderComponentSelectionArgs selected{.object = object};
    const auto source = document();
    const auto &face = source.faces.front();
    if (mode == QuaderSelectionMode::Face) {
      selected.kind = QuaderComponentKind::Face;
      selected.faces = {static_cast<QuaderElementId>(face.id)};
    } else if (mode == QuaderSelectionMode::Vertex) {
      selected.kind = QuaderComponentKind::Vertex;
      for (const auto vertex : face.vertices) {
        selected.vertices.push_back(static_cast<QuaderElementId>(vertex));
      }
    } else {
      selected.kind = QuaderComponentKind::Edge;
      for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        selected.edges.emplace_back(
            static_cast<QuaderElementId>(face.vertices[index]),
            static_cast<QuaderElementId>(face.vertices[(index + 1U) % face.vertices.size()]));
      }
    }
    ASSERT_TRUE(editor.selection().select_components(selected).ok());
    session.history().clear();
  }

  MeshAsset mesh() const {
    auto compiled = session.modeling().compile_object_mesh(object.modeling_object_id);
    EXPECT_TRUE(compiled.has_value());
    return compiled.value_or(MeshAsset{});
  }

  quader_poly::Document document() const {
    const auto compiled = mesh();
    EXPECT_NE(compiled.authored_polygon_document, nullptr);
    return compiled.authored_polygon_document != nullptr
               ? *compiled.authored_polygon_document : quader_poly::Document{};
  }

  std::vector<TransformUvSample> samples() const {
    auto compiled = mesh();
    MeshProjectionService::apply_mesh_texture_mapping(compiled, QMat4{});
    const auto &geometry = compiled.geometry();
    std::vector<TransformUvSample> result;
    for (std::size_t index = 0; index + 2U < geometry.indices.size(); index += 3U) {
      const auto &a = geometry.vertices[geometry.indices[index]];
      const auto &b = geometry.vertices[geometry.indices[index + 1U]];
      const auto &c = geometry.vertices[geometry.indices[index + 2U]];
      const auto point = a.position * 0.2F + b.position * 0.35F + c.position * 0.45F;
      result.push_back({point, a.uv * 0.2F + b.uv * 0.35F + c.uv * 0.45F,
                         point.x < 5.0F});
    }
    EXPECT_EQ(result.size(), 8U);
    return result;
  }

  void expect_samples(MeshAsset compiled, const std::vector<TransformUvSample> &expected,
                      bool rotated) const {
    MeshProjectionService::apply_mesh_texture_mapping(compiled, QMat4{});
    for (const auto &sample : expected) {
      const auto point = rotated && sample.transformed ? half_turn_x(sample.position)
                                                      : sample.position;
      const auto actual = triangle_uv_at(compiled, point);
      ASSERT_TRUE(actual.has_value()) << point.x << ", " << point.y << ", " << point.z;
      EXPECT_NEAR(actual->x, sample.uv.x, 0.00002F);
      EXPECT_NEAR(actual->y, sample.uv.y, 0.00002F);
    }
  }

  void expect_preview_samples(const ModelingToolPreviewMesh &preview,
                              const std::vector<TransformUvSample> &expected,
                              double angle_degrees) const {
    ASSERT_TRUE(preview.dynamic_positions);
    ASSERT_NE(preview.dynamic_position_payload, nullptr);
    ASSERT_EQ(preview.dynamic_position_payload->size(),
              preview.mesh.geometry().vertices.size());
    const double radians = angle_degrees * std::acos(-1.0) / 180.0;
    const float cosine = static_cast<float>(std::cos(radians));
    const float sine = static_cast<float>(std::sin(radians));
    for (const auto &sample : expected) {
      auto point = sample.position;
      if (sample.transformed) {
        point = {point.x, cosine * point.y - sine * point.z,
                 sine * point.y + cosine * point.z};
      }
      const auto actual = triangle_uv_at(preview.mesh, point,
                                          *preview.dynamic_position_payload);
      ASSERT_TRUE(actual.has_value()) << point.x << ", " << point.y << ", " << point.z;
      EXPECT_NEAR(actual->x, sample.uv.x, 0.00002F);
      EXPECT_NEAR(actual->y, sample.uv.y, 0.00002F);
    }
  }

  static QuaderTransformDragBeginRequest rotate_begin() {
    return {
        .tool = QuaderTransformTool::Rotate,
        .axis = QuaderTransformAxis::X,
        .screen_position = {120, 100},
        .screen_pivot = {100, 100},
        .screen_tip = {120, 100},
        .has_world_direction = true,
        .world_direction = {1, 0, 0},
        .ray = {.origin = {10, 0, 0}, .direction = {-1, 0, 0}},
        .camera = {.orthographic = true, .origin = {10, 0, 0},
                   .x_axis = {0, 0, -1}, .y_axis = {0, 1, 0}, .z_axis = {1, 0, 0},
                   .viewport_size = {800, 600}, .orthographic_size = 8},
        .vertex_snap_radius = 0,
        .grid_snap_enabled = false,
        .rotation_snap_degrees = 0};
  }

  static QuaderTransformDragUpdateRequest rotate_update() {
    const auto begin = rotate_begin();
    return {.screen_position = {80, 100}, .ray = begin.ray, .camera = begin.camera,
            .vertex_snap_radius = 0, .grid_snap_enabled = false,
            .rotation_snap_degrees = 0};
  }

  SceneSession session;
  QuaderEditorApi editor;
  QuaderModelingObjectRef object;
};

TEST_F(UvTransformAcceptanceTests, RotatePreviewStaysTransientAndAcceptancePreservesInteriors) {
  create();
  const auto before = document();
  const auto expected = samples();
  const auto scene_revision = session.scene().revision();
  const auto asset_revision = session.assets().revision();
  const auto publications = session.modeling().modeling_sdk_projection_stats_for_tests();
  ASSERT_TRUE(editor.modeling().begin_transform_drag(rotate_begin()).ok());
  const auto updated = editor.modeling().update_transform_drag(rotate_update());
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  EXPECT_NEAR(std::abs(updated.value().rotate_angle_degrees), 180.0, 0.00001);
  const auto previews = session.modeling().tool_preview_meshes();
  ASSERT_EQ(previews.size(), 1U);
  const auto &preview = previews.front();
  ASSERT_TRUE(preview.dynamic_positions);
  ASSERT_NE(preview.dynamic_position_payload, nullptr);
  ASSERT_NE(preview.mesh.authored_polygon_document, nullptr);
  expect_face_attributes(preview.mesh.authored_polygon_document->faces.back(), before.faces.back());
  // The derived preview may re-anchor its triangles; the committed document
  // remains unchanged until acceptance, including the original authored chart.
  for (const auto &vertex : preview.mesh.authored_polygon_document->vertices) {
    const auto original = std::ranges::find(before.vertices, vertex.id,
                                            &quader_poly::Vertex::id);
    ASSERT_NE(original, before.vertices.end());
    const auto expected_position = original->position.x < 5.0F
                                       ? half_turn_x(original->position)
                                       : original->position;
    EXPECT_NEAR(length_squared(vertex.position - expected_position), 0, 0.000001F);
  }
  expect_preview_samples(preview, expected, 180.0);
  expect_samples(mesh(), expected, false);
  expect_document(document(), before);
  EXPECT_EQ(session.scene().revision(), scene_revision);
  EXPECT_EQ(session.assets().revision(), asset_revision);
  EXPECT_EQ(session.history().undo_count(), 0U);
  const auto after_preview = session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_preview.mesh_compile_count, publications.mesh_compile_count);
  EXPECT_EQ(after_preview.asset_publication_count, publications.asset_publication_count);
  EXPECT_EQ(after_preview.scene_publication_count, publications.scene_publication_count);
  EXPECT_EQ(after_preview.preview_publication_count, publications.preview_publication_count);

  const auto preview_revision = session.modeling().transient_transform_preview_revision();
  const auto work = session.modeling().tool_preview_work_stats();
  for (int sample = 0; sample < 8; ++sample) {
    ASSERT_TRUE(editor.modeling().update_transform_drag(rotate_update()).ok());
  }
  EXPECT_EQ(session.modeling().transient_transform_preview_revision(), preview_revision);
  EXPECT_EQ(session.modeling().tool_preview_work_stats().signature_enumeration_count,
            work.signature_enumeration_count);
  EXPECT_EQ(session.modeling().tool_preview_work_stats().mesh_extraction_count,
            work.mesh_extraction_count);
  const auto after_unchanged = session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_unchanged.mesh_compile_count, after_preview.mesh_compile_count);
  EXPECT_EQ(after_unchanged.asset_publication_count, after_preview.asset_publication_count);
  EXPECT_EQ(after_unchanged.scene_publication_count, after_preview.scene_publication_count);
  EXPECT_EQ(after_unchanged.preview_publication_count, after_preview.preview_publication_count);
  const auto retained = session.modeling().tool_preview_meshes();
  ASSERT_EQ(retained.size(), 1U);
  EXPECT_EQ(retained.front().position_revision, preview.position_revision);
  EXPECT_EQ(retained.front().dynamic_position_payload, preview.dynamic_position_payload);

  // Continue past the first packing change: stale source-to-render index maps
  // can leave a later position stream paired with the wrong UV corners.
  auto next_update = rotate_update();
  next_update.screen_position = {80, 120};
  const auto next = editor.modeling().update_transform_drag(next_update);
  ASSERT_TRUE(next.ok()) << next.error_message();
  ASSERT_TRUE(next.changed());
  const auto next_previews = session.modeling().tool_preview_meshes();
  ASSERT_EQ(next_previews.size(), 1U);
  const auto &next_preview = next_previews.front();
  EXPECT_NE(next_preview.position_revision, preview.position_revision);
  expect_preview_samples(next_preview, expected, -135.0);
  // A retained earlier frame must keep its own packed stream and attributes.
  expect_preview_samples(preview, expected, 180.0);
  expect_document(document(), before);
  EXPECT_EQ(session.scene().revision(), scene_revision);
  EXPECT_EQ(session.assets().revision(), asset_revision);
  EXPECT_EQ(session.history().undo_count(), 0U);

  const auto next_revision = session.modeling().transient_transform_preview_revision();
  const auto next_work = session.modeling().tool_preview_work_stats();
  for (int sample = 0; sample < 8; ++sample) {
    ASSERT_TRUE(editor.modeling().update_transform_drag(next_update).ok());
  }
  EXPECT_EQ(session.modeling().transient_transform_preview_revision(), next_revision);
  EXPECT_EQ(session.modeling().tool_preview_work_stats().signature_enumeration_count,
            next_work.signature_enumeration_count);
  EXPECT_EQ(session.modeling().tool_preview_work_stats().mesh_extraction_count,
            next_work.mesh_extraction_count);
  const auto next_retained = session.modeling().tool_preview_meshes();
  ASSERT_EQ(next_retained.size(), 1U);
  EXPECT_EQ(next_retained.front().position_revision, next_preview.position_revision);
  EXPECT_EQ(next_retained.front().layout_key, next_preview.layout_key);
  EXPECT_EQ(next_retained.front().dynamic_position_payload,
            next_preview.dynamic_position_payload);

  ASSERT_TRUE(editor.modeling().update_transform_drag(rotate_update()).ok());
  const auto final_previews = session.modeling().tool_preview_meshes();
  ASSERT_EQ(final_previews.size(), 1U);
  expect_preview_samples(final_previews.front(), expected, 180.0);

  const auto accepted = editor.modeling().commit_transform_drag();
  ASSERT_TRUE(accepted.ok()) << accepted.error_message();
  ASSERT_TRUE(accepted.changed());
  const auto after = document();
  EXPECT_NE(after.faces.front().vertices, before.faces.front().vertices);
  expect_face_attributes(after.faces.back(), before.faces.back());
  expect_samples(mesh(), expected, true);
  EXPECT_EQ(session.history().undo_count(), 1U);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_document(document(), before);
  expect_samples(mesh(), expected, false);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_document(document(), after);
  expect_samples(mesh(), expected, true);

  ASSERT_TRUE(editor.history().repeat_last_action().ok());
  EXPECT_EQ(session.history().undo_count(), 2U);
  expect_samples(mesh(), expected, false);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_document(document(), after);
}

TEST_F(UvTransformAcceptanceTests, DirectAndPreviewAcceptancePreserveBothWindingCharts) {
  for (const bool reversed : {false, true}) {
    for (const auto mode : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Edge,
                            QuaderSelectionMode::Face}) {
      SCOPED_TRACE(::testing::Message() << reversed << "/" << static_cast<int>(mode));
      create(reversed);
      select(mode);
      const auto before = document();
      const auto expected = samples();
      const auto transformed = editor.modeling().transform_selection(half_turn_x());
      ASSERT_TRUE(transformed.ok()) << transformed.error_message();
      expect_samples(mesh(), expected, true);
      const auto direct = document();
      expect_face_attributes(direct.faces.back(), before.faces.back());
      EXPECT_EQ(session.history().undo_count(), 1U);
      ASSERT_TRUE(editor.history().undo().ok());
      expect_document(document(), before);
      ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
      ASSERT_TRUE(editor.modeling().update_transform_matrix(half_turn_x(), {}).ok());
      const auto accepted = editor.modeling().commit_transform_preview("Rotate Selection");
      ASSERT_TRUE(accepted.ok()) << accepted.error_message();
      expect_document(document(), direct);
      expect_samples(mesh(), expected, true);
      EXPECT_EQ(session.history().undo_count(), 1U);
      ASSERT_TRUE(editor.history().undo().ok());
      expect_document(document(), before);
      ASSERT_TRUE(editor.history().redo().ok());
      expect_document(document(), direct);
    }
  }
}

TEST_F(UvTransformAcceptanceTests, PartialFaceDeformationRetainsAuthoredCornerChart) {
  create();
  ASSERT_TRUE(editor.modeling().set_selection_mode(QuaderSelectionMode::Vertex).ok());
  const auto before = document();
  const auto vertex = before.faces.front().vertices[1];
  ASSERT_TRUE(editor.selection().select_vertex(object,
      static_cast<QuaderElementId>(vertex)).ok());
  session.history().clear();
  const auto moved = editor.modeling().transform_selection({.origin = {0.2F, 0.1F, 0.3F}});
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  ASSERT_TRUE(moved.changed());
  const auto after = document();
  for (std::size_t face = 0; face < before.faces.size(); ++face) {
    expect_face_attributes(after.faces[face], before.faces[face]);
  }
  for (std::size_t index = 0; index < before.vertices.size(); ++index) {
    const auto delta = before.vertices[index].id == vertex ? QVec3{0.2F, 0.1F, 0.3F} : QVec3{};
    EXPECT_NEAR(length_squared(after.vertices[index].position -
                               before.vertices[index].position - delta), 0, 0.000001F);
  }
  EXPECT_EQ(session.history().undo_count(), 1U);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_document(document(), before);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_document(document(), after);
}

TEST_F(UvTransformAcceptanceTests, ObjectGeometryPreviewPreservesChartsAndSelectionMode) {
  for (const auto mode : {QuaderSelectionMode::Object, QuaderSelectionMode::Group}) {
    SCOPED_TRACE(static_cast<int>(mode));
    create();
    ASSERT_TRUE(editor.selection().clear().ok());
    ASSERT_TRUE(editor.modeling().set_selection_mode(QuaderSelectionMode::Object).ok());
    ASSERT_TRUE(editor.selection().select_object(object).ok());
    if (mode == QuaderSelectionMode::Group) {
      const auto group = editor.scene().group_selection("Geometry preview group");
      ASSERT_TRUE(group.ok()) << group.error_message();
      ASSERT_TRUE(editor.modeling().set_selection_mode(mode).ok());
      ASSERT_TRUE(editor.selection().select_object(group.value().id).ok());
    }
    session.history().clear();
    const auto before = document();
    auto expected = samples();
    for (auto &sample : expected) sample.transformed = true;
    const auto world = session.scene().world_transform_matrix(object.entity);
    const auto scene_before = session.scene().revision();
    const auto assets_before = session.assets().revision();
    const auto selected_before = session.scene().selected_entities();
    const auto begun = editor.modeling().begin_transform_preview();
    if (mode == QuaderSelectionMode::Group) {
      // Group selection belongs to scene transforms and intentionally clears
      // SDK object selection; this geometry-only preview API cannot expand it.
      EXPECT_FALSE(begun.ok()) << begun.error_message();
      EXPECT_FALSE(begun.changed());
      EXPECT_EQ(begun.error_message(), "No active SDK modeling selection.");
      expect_document(document(), before);
      expect_samples(mesh(), expected, false);
      EXPECT_EQ(editor.modeling().selection_summary().mode, mode);
      EXPECT_EQ(session.scene().selected_entities(), selected_before);
      EXPECT_EQ(session.scene().revision(), scene_before);
      EXPECT_EQ(session.assets().revision(), assets_before);
      EXPECT_EQ(session.history().undo_count(), 0U);
      EXPECT_TRUE(session.modeling().tool_preview_meshes().empty());
      continue;
    }
    ASSERT_TRUE(begun.ok()) << begun.error_message();
    ASSERT_TRUE(editor.modeling().update_transform_matrix(half_turn_x(), {}).ok());
    EXPECT_EQ(editor.modeling().selection_summary().mode, mode);
    EXPECT_EQ(session.history().undo_count(), 0U);
    // This API renders its existing scene-bound preview asset, whereas the
    // component drag uses the transient tool-preview mesh collection.
    EXPECT_TRUE(session.modeling().tool_preview_meshes().empty());
    const auto preview = mesh();
    expect_samples(preview, expected, true);
    const auto scene_revision = session.scene().revision();
    const auto asset_revision = session.assets().revision();
    const auto work = session.modeling().tool_preview_work_stats();
    const auto publications = session.modeling().modeling_sdk_projection_stats_for_tests();
    for (int sample = 0; sample < 8; ++sample) {
      const auto unchanged = editor.modeling().update_transform_matrix(half_turn_x(), {});
      ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
      EXPECT_FALSE(unchanged.changed());
    }
    EXPECT_EQ(session.modeling().tool_preview_work_stats().signature_enumeration_count,
              work.signature_enumeration_count);
    EXPECT_EQ(session.modeling().tool_preview_work_stats().mesh_extraction_count,
              work.mesh_extraction_count);
    const auto retained = session.modeling().modeling_sdk_projection_stats_for_tests();
    EXPECT_EQ(retained.mesh_compile_count, publications.mesh_compile_count);
    EXPECT_EQ(retained.preview_publication_count, publications.preview_publication_count);
    EXPECT_EQ(retained.asset_publication_count, publications.asset_publication_count);
    EXPECT_EQ(retained.scene_publication_count, publications.scene_publication_count);
    EXPECT_EQ(session.scene().revision(), scene_revision);
    EXPECT_EQ(session.assets().revision(), asset_revision);
    EXPECT_EQ(mesh().geometry().content_id(), preview.geometry().content_id());
    const auto accepted = editor.modeling().commit_transform_preview("Rotate Geometry");
    ASSERT_TRUE(accepted.ok()) << accepted.error_message();
    ASSERT_TRUE(accepted.changed());
    const auto after = document();
    EXPECT_EQ(session.scene().world_transform_matrix(object.entity).values, world.values);
    EXPECT_EQ(editor.modeling().selection_summary().mode, mode);
    expect_samples(mesh(), expected, true);
    EXPECT_EQ(session.history().undo_count(), 1U);
    ASSERT_TRUE(editor.history().undo().ok());
    expect_document(document(), before);
    expect_samples(mesh(), expected, false);
    EXPECT_EQ(editor.modeling().selection_summary().mode, mode);
    ASSERT_TRUE(editor.history().redo().ok());
    expect_document(document(), after);
    expect_samples(mesh(), expected, true);
    EXPECT_EQ(editor.modeling().selection_summary().mode, mode);
  }
}

TEST_F(UvTransformAcceptanceTests, NegativeScalePreservesLoopOrientationAndInteriorUvs) {
  for (const bool reversed : {false, true}) {
    SCOPED_TRACE(reversed);
    create(reversed);
    const auto before = document();
    const auto expected = samples();
    const auto scaled = editor.modeling().scale_selection({1, -1, 1});
    ASSERT_TRUE(scaled.ok()) << scaled.error_message();
    const auto after = document();
    const auto &source = before.faces.front().vertices;
    const auto &result = after.faces.front().vertices;
    ASSERT_EQ(result.size(), source.size());
    const auto start = std::ranges::find(source, result.front());
    ASSERT_NE(start, source.end());
    const auto offset = static_cast<std::size_t>(start - source.begin());
    for (std::size_t index = 0; index < result.size(); ++index) {
      EXPECT_EQ(result[index], source[(index + offset) % source.size()]);
    }
    // These faces lie in XY, so negative Y scale and a half-turn around X
    // reach the same sample positions while exercising distinct transform paths.
    expect_samples(mesh(), expected, true);
    expect_face_attributes(after.faces.back(), before.faces.back());
    EXPECT_EQ(session.history().undo_count(), 1U);
    ASSERT_TRUE(editor.history().undo().ok());
    expect_document(document(), before);
    ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
    ASSERT_TRUE(editor.modeling().update_transform_matrix(
        {.y_axis = {0, -1, 0}}, {}).ok());
    const auto previews = session.modeling().tool_preview_meshes();
    ASSERT_EQ(previews.size(), 1U);
    expect_preview_samples(previews.front(), expected, 180.0);
    const auto accepted = editor.modeling().commit_transform_preview("Scale Selection");
    ASSERT_TRUE(accepted.ok()) << accepted.error_message();
    expect_document(document(), after);
    expect_samples(mesh(), expected, true);
    ASSERT_TRUE(editor.history().undo().ok());
    expect_document(document(), before);
    ASSERT_TRUE(editor.history().redo().ok());
    expect_document(document(), after);
  }
}

TEST_F(UvTransformAcceptanceTests, CancelAndZeroRotationLeaveChartsAndHistoryUntouched) {
  create();
  const auto before = document();
  const auto scene_revision = session.scene().revision();
  const auto asset_revision = session.assets().revision();
  ASSERT_TRUE(editor.modeling().begin_transform_drag(rotate_begin()).ok());
  ASSERT_TRUE(editor.modeling().update_transform_drag(rotate_update()).ok());
  ASSERT_TRUE(editor.modeling().cancel_transform_drag().ok());
  expect_document(document(), before);
  EXPECT_TRUE(session.modeling().tool_preview_meshes().empty());
  ASSERT_TRUE(editor.modeling().begin_transform_drag(rotate_begin()).ok());
  const auto zero = editor.modeling().commit_transform_drag();
  ASSERT_TRUE(zero.ok()) << zero.error_message();
  EXPECT_FALSE(zero.changed());
  const auto direct_zero = editor.modeling().transform_selection({});
  ASSERT_TRUE(direct_zero.ok()) << direct_zero.error_message();
  EXPECT_FALSE(direct_zero.changed());
  expect_document(document(), before);
  EXPECT_EQ(session.history().undo_count(), 0U);
  EXPECT_EQ(session.scene().revision(), scene_revision);
  EXPECT_EQ(session.assets().revision(), asset_revision);
}

TEST_F(UvTransformAcceptanceTests, FailedAcceptanceRollsBackPositionsChartsAndHistory) {
  create();
  const auto before = document();
  const auto expected = samples();
  const auto scene_revision = session.scene().revision();
  const auto asset_revision = session.assets().revision();
  const auto selected = session.scene().selected_entities();
  for (const auto point : {ModelingSdkProjectionFailurePoint::BeforeCommit,
                           ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
                           ModelingSdkProjectionFailurePoint::CommittedSceneNotify,
                           ModelingSdkProjectionFailurePoint::AfterSdkMutation}) {
    SCOPED_TRACE(static_cast<int>(point));
    ASSERT_TRUE(editor.modeling().begin_transform_drag(rotate_begin()).ok());
    ASSERT_TRUE(editor.modeling().update_transform_drag(rotate_update()).ok());
    session.modeling().set_modeling_sdk_projection_failure_for_tests(point);
    const auto failed = editor.modeling().commit_transform_drag();
    session.modeling().set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());
    expect_document(document(), before);
    expect_samples(mesh(), expected, false);
    EXPECT_TRUE(session.modeling().tool_preview_meshes().empty());
    EXPECT_EQ(session.scene().selected_entities(), selected);
    EXPECT_EQ(session.scene().revision(), scene_revision);
    EXPECT_EQ(session.assets().revision(), asset_revision);
    EXPECT_EQ(session.history().undo_count(), 0U);
    EXPECT_EQ(session.history().redo_count(), 0U);
  }
}

} // namespace
} // namespace quader
