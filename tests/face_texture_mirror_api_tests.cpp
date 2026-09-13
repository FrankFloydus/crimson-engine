////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/session/scene_session.hpp"
#include "scene/scene_texture_mapping.hpp"
#include "scene/scene_transform.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace quader {
namespace {

/// Captures authored invariants and the same effective corner UVs rendering uses.
struct MirrorApiSnapshot final {
  quader_poly::Document document;
  MeshAsset mesh;
  QMat4 sampling;
  std::map<quader_poly::ElementId, std::vector<QVec2>> face_uvs;
};

void expect_vector_equal(QVec3 actual, QVec3 expected) {
  EXPECT_FLOAT_EQ(actual.x, expected.x);
  EXPECT_FLOAT_EQ(actual.y, expected.y);
  EXPECT_FLOAT_EQ(actual.z, expected.z);
}

void expect_uvs_equal(const std::vector<QVec2> &actual,
                     const std::vector<QVec2> &expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t index = 0; index < actual.size(); ++index) {
    EXPECT_NEAR(actual[index].x, expected[index].x, 0.0001f) << index;
    EXPECT_NEAR(actual[index].y, expected[index].y, 0.0001f) << index;
  }
}

void expect_geometry_and_settings_equal(const MirrorApiSnapshot &actual,
                                        const MirrorApiSnapshot &expected) {
  const auto &a = actual.document;
  const auto &b = expected.document;
  ASSERT_EQ(a.vertices.size(), b.vertices.size());
  for (std::size_t index = 0; index < a.vertices.size(); ++index) {
    EXPECT_EQ(a.vertices[index].id, b.vertices[index].id);
    expect_vector_equal(a.vertices[index].position, b.vertices[index].position);
  }
  ASSERT_EQ(a.faces.size(), b.faces.size());
  for (std::size_t index = 0; index < a.faces.size(); ++index) {
    EXPECT_EQ(a.faces[index].id, b.faces[index].id);
    EXPECT_EQ(a.faces[index].vertices, b.faces[index].vertices);
    EXPECT_EQ(a.faces[index].material_slot, b.faces[index].material_slot);
    EXPECT_EQ(a.faces[index].normal_shading, b.faces[index].normal_shading);
    EXPECT_EQ(a.faces[index].texture_transform, b.faces[index].texture_transform);
  }
  EXPECT_EQ(a.hard_normal_edges, b.hard_normal_edges);
  EXPECT_EQ(a.soft_normal_edges, b.soft_normal_edges);
  EXPECT_EQ(a.next_vertex_id, b.next_vertex_id);
  EXPECT_EQ(a.next_face_id, b.next_face_id);
  EXPECT_EQ(actual.sampling.values, expected.sampling.values);
  const auto &mesh = actual.mesh.geometry();
  const auto &reference = expected.mesh.geometry();
  EXPECT_EQ(mesh.indices, reference.indices);
  EXPECT_EQ(mesh.primitives, reference.primitives);
  ASSERT_EQ(mesh.vertices.size(), reference.vertices.size());
  for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
    expect_vector_equal(mesh.vertices[index].position, reference.vertices[index].position);
    expect_vector_equal(mesh.vertices[index].normal, reference.vertices[index].normal);
    expect_vector_equal(mesh.vertices[index].uv_geometric_normal,
                        reference.vertices[index].uv_geometric_normal);
    EXPECT_EQ(mesh.vertices[index].source_vertex_id, reference.vertices[index].source_vertex_id);
    EXPECT_EQ(mesh.vertices[index].rgba, reference.vertices[index].rgba);
  }
}

void expect_snapshot_equal(const MirrorApiSnapshot &actual,
                           const MirrorApiSnapshot &expected) {
  expect_geometry_and_settings_equal(actual, expected);
  ASSERT_EQ(actual.face_uvs.size(), expected.face_uvs.size());
  for (const auto &[face, uvs] : expected.face_uvs) {
    ASSERT_TRUE(actual.face_uvs.contains(face));
    expect_uvs_equal(actual.face_uvs.at(face), uvs);
  }
}

std::array<float, 2> axis_bounds(const std::vector<QVec2> &uvs, bool horizontal) {
  std::array<float, 2> bounds{std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::lowest()};
  for (const auto uv : uvs) {
    const float coordinate = horizontal ? uv.x : uv.y;
    bounds[0] = std::min(bounds[0], coordinate);
    bounds[1] = std::max(bounds[1], coordinate);
  }
  return bounds;
}

void expect_mirrored(const std::vector<QVec2> &actual,
                     const std::vector<QVec2> &before, bool horizontal,
                     const std::array<float, 2> &bounds) {
  auto expected = before;
  for (auto &uv : expected) {
    float &coordinate = horizontal ? uv.x : uv.y;
    coordinate = bounds[0] + bounds[1] - coordinate;
  }
  expect_uvs_equal(actual, expected);
}

/// Editing, selection and history use public APIs. Native fixture access captures
/// authored winding, corner UVs/normals and occurrence sampling, and injects
/// publication failures; BehaviorScenario does not expose these observations.
class FaceTextureMirrorApiTests : public ::testing::Test {
protected:
  QuaderModelingObjectRef box() {
    const auto created = editor_.modeling().create_box(
        {.min = {0, 0, 0}, .max = {128, 64, 32}}, "Mirror face UV box");
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value() : QuaderModelingObjectRef{};
  }

  QuaderModelingObjectRef wall(float u_offset, bool collapsed = false) {
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {0, 0, 0}),
        quader_poly::add_vertex(document, {3, 0, 0}),
        quader_poly::add_vertex(document, {3, 2, 0}),
        quader_poly::add_vertex(document, {0, 2, 0})};
    (void)quader_poly::add_face(document, vertices);
    document.faces.front().uvs = {{u_offset, 0.2f}, {u_offset + 2, 0.4f},
                                 {u_offset + 1.7f, 2.8f}, {u_offset + 0.1f, 1.9f}};
    if (collapsed) {
      for (auto &uv : document.faces.front().uvs) uv.x = u_offset;
    }
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Mirror face UV wall"});
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value() : QuaderModelingObjectRef{};
  }

  void select(QuaderModelingObjectRef object, int face = 1) {
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor_.selection().select_face(object, face).ok());
  }

  QuaderApiVoidResult mirror(bool horizontal) {
    return editor_.modeling().apply_face_texture_action({
        .action = horizontal ? QuaderFaceTextureAction::FlipFaceHorizontal
                             : QuaderFaceTextureAction::FlipFaceVertical,
        .selection_token = editor_.modeling().face_texture_settings().selection_token});
  }

  MirrorApiSnapshot snapshot(QuaderModelingObjectRef object) {
    auto mesh = session_.modeling().compile_object_mesh(object.modeling_object_id);
    const auto sampling = effective_texture_sampling_from_local(session_.scene(), object.entity);
    EXPECT_TRUE(mesh.has_value());
    EXPECT_TRUE(sampling.has_value());
    if (!mesh.has_value() || !sampling.has_value()) return {};
    EXPECT_NE(mesh->authored_polygon_document, nullptr);
    if (!mesh->authored_polygon_document) return {};
    MirrorApiSnapshot result{.document = *mesh->authored_polygon_document,
                             .mesh = *mesh, .sampling = *sampling};
    MeshProjectionService::apply_mesh_texture_mapping(*mesh, *sampling);
    const auto &compiled = mesh->geometry().vertices;
    for (const auto &face : result.document.faces) {
      const auto normal = quader_poly::face_normal(result.document, face);
      auto &uvs = result.face_uvs[face.id];
      for (const auto vertex : face.vertices) {
        const auto found = std::ranges::find_if(compiled, [&](const auto &candidate) {
          return candidate.source_vertex_id == vertex &&
                 length_squared(candidate.uv_geometric_normal - normal) < 0.000001f;
        });
        EXPECT_NE(found, compiled.end()) << "Missing compiled face corner " << face.id;
        if (found != compiled.end()) uvs.push_back(found->uv);
      }
    }
    return result;
  }

  SceneSession session_;
  QuaderEditorApi editor_{QuaderApiSessionInternalAccess::from_scene_session(session_)};
};

TEST_F(FaceTextureMirrorApiTests, SingleBoxFaceMirrorsUvsWithoutMovingGeometryOrNeighbors) {
  const auto object = box();
  select(object);
  const auto before = snapshot(object);
  const auto selection = editor_.selection().summary();
  const auto selection_revision = editor_.revisions().modeling_selection_revision;
  for (const bool horizontal : {true, false}) {
    SCOPED_TRACE(horizontal);
    session_.history().clear();
    // Undo/redo restores the numeric edit context and issues a fresh token.
    // Each mirror must preserve the token presented immediately before it.
    const auto token = editor_.modeling().face_texture_settings().selection_token;
    const auto edited = mirror(horizontal);
    ASSERT_TRUE(edited.ok()) << edited.error_message();
    ASSERT_TRUE(edited.changed());
    const auto after = snapshot(object);
    expect_geometry_and_settings_equal(after, before);
    expect_mirrored(after.face_uvs.at(1), before.face_uvs.at(1), horizontal,
                    axis_bounds(before.face_uvs.at(1), horizontal));
    for (const auto &[face, uvs] : before.face_uvs) {
      if (face != 1) expect_uvs_equal(after.face_uvs.at(face), uvs);
    }
    EXPECT_EQ(editor_.modeling().face_texture_settings().selection_token, token);
    EXPECT_EQ(editor_.selection().summary().active_face_id, selection.active_face_id);
    EXPECT_EQ(editor_.selection().summary().selected_face_count, 1u);
    EXPECT_EQ(editor_.revisions().modeling_selection_revision, selection_revision);
    EXPECT_EQ(session_.history().undo_count(), 1u);
    EXPECT_EQ(editor_.history().snapshot().undo_label,
              horizontal ? "Mirror Face UVs Horizontally" : "Mirror Face UVs Vertically");
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_snapshot_equal(snapshot(object), before);
    ASSERT_TRUE(editor_.history().redo().ok());
    expect_snapshot_equal(snapshot(object), after);
    const auto repeated = editor_.history().repeat_last_action();
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    ASSERT_TRUE(repeated.changed());
    EXPECT_EQ(session_.history().undo_count(), 2u);
    expect_snapshot_equal(snapshot(object), before);
  }
}

TEST_F(FaceTextureMirrorApiTests, MirroringPreservesSignedScalarsAndOccurrenceSampling) {
  const auto object = wall(0.3f);
  select(object);
  for (const auto &[field, value] : std::array{
           std::pair{QuaderFaceTextureField::ScaleX, -0.7},
           std::pair{QuaderFaceTextureField::ScaleY, 1.3},
           std::pair{QuaderFaceTextureField::ShiftX, 11.0},
           std::pair{QuaderFaceTextureField::ShiftY, -9.0},
           std::pair{QuaderFaceTextureField::Rotate, 37.0}}) {
    ASSERT_TRUE(editor_.modeling().set_face_texture_setting({
        .field = field, .value = value,
        .selection_token = editor_.modeling().face_texture_settings().selection_token}).ok());
  }
  ASSERT_TRUE(session_.scene().set_texture_mapping(object.entity,
      {compose_transform(QTransform{.position = {0.4f, -0.3f, 0},
                                    .scale = {0.8f, 1.2f, 1}})}));
  const auto before = snapshot(object);
  for (const bool horizontal : {true, false}) {
    const auto edited = mirror(horizontal);
    ASSERT_TRUE(edited.ok()) << edited.error_message();
    ASSERT_TRUE(edited.changed());
    const auto after = snapshot(object);
    expect_geometry_and_settings_equal(after, before);
    expect_mirrored(after.face_uvs.at(1), before.face_uvs.at(1), horizontal,
                    axis_bounds(before.face_uvs.at(1), horizontal));
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_snapshot_equal(snapshot(object), before);
  }
}

TEST_F(FaceTextureMirrorApiTests, TreatAsOneMirrorsExistingChartsAcrossObjectsAtomically) {
  const std::array objects{wall(-2.0f), wall(5.0f)};
  select(objects[0]);
  ASSERT_TRUE(editor_.selection().select_face(objects[1], 1, QuaderSelectionEdit::Add).ok());
  const std::array before{snapshot(objects[0]), snapshot(objects[1])};
  for (const bool together : {false, true}) {
    ASSERT_TRUE(editor_.modeling().set_face_texture_treat_as_one(together).ok());
    session_.history().clear();
    const auto edited = mirror(true);
    ASSERT_TRUE(edited.ok()) << edited.error_message();
    ASSERT_TRUE(edited.changed());
    const auto first_bounds = axis_bounds(before[0].face_uvs.at(1), true);
    const auto second_bounds = axis_bounds(before[1].face_uvs.at(1), true);
    const std::array union_bounds{std::min(first_bounds[0], second_bounds[0]),
                                  std::max(first_bounds[1], second_bounds[1])};
    for (std::size_t index = 0; index < objects.size(); ++index) {
      const auto after = snapshot(objects[index]);
      expect_geometry_and_settings_equal(after, before[index]);
      expect_mirrored(after.face_uvs.at(1), before[index].face_uvs.at(1), true,
                      together ? union_bounds : axis_bounds(before[index].face_uvs.at(1), true));
    }
    EXPECT_EQ(session_.history().undo_count(), 1u);
    EXPECT_EQ(editor_.selection().summary().selected_face_count, 2u);
    ASSERT_TRUE(editor_.history().undo().ok());
    for (std::size_t index = 0; index < objects.size(); ++index) {
      expect_snapshot_equal(snapshot(objects[index]), before[index]);
    }
  }
}

TEST_F(FaceTextureMirrorApiTests, StaleSelectionRejectsWithoutChangingEitherFaceOrRedo) {
  const auto object = box();
  select(object);
  const auto stale_token = editor_.modeling().face_texture_settings().selection_token;
  select(object, 2);
  ASSERT_TRUE(mirror(true).ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  const auto before = snapshot(object);
  const auto revision = editor_.modeling().face_texture_settings_revision();
  const auto history = editor_.history().snapshot();
  const auto rejected = editor_.modeling().apply_face_texture_action({
      .action = QuaderFaceTextureAction::FlipFaceHorizontal, .selection_token = stale_token});
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  expect_snapshot_equal(snapshot(object), before);
  EXPECT_EQ(editor_.modeling().face_texture_settings_revision(), revision);
  EXPECT_EQ(editor_.history().snapshot().undo_label, history.undo_label);
  EXPECT_EQ(editor_.history().snapshot().redo_label, history.redo_label);
  EXPECT_TRUE(editor_.history().can_redo());
  EXPECT_EQ(editor_.selection().summary().active_face_id, 2);
}

TEST_F(FaceTextureMirrorApiTests, PublicationFailuresRestoreUvsBindingsRevisionsAndHistory) {
  const auto object = box();
  select(object);
  ASSERT_TRUE(mirror(false).ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  session_.mark_clean();
  const auto before = snapshot(object);
  const auto revision = editor_.modeling().face_texture_settings_revision();
  const auto history = editor_.history().snapshot();
  const auto before_entity = editor_.query().entity(object.entity);
  ASSERT_TRUE(before_entity.has_value());
  const auto mesh_id = before_entity->mesh;
  for (const auto point : {ModelingSdkProjectionFailurePoint::AfterSdkMutation,
                           ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
                           ModelingSdkProjectionFailurePoint::CommittedSceneNotify,
                           ModelingSdkProjectionFailurePoint::BeforeCommit}) {
    SCOPED_TRACE(static_cast<int>(point));
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(point);
    const auto failed = mirror(true);
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());
    expect_snapshot_equal(snapshot(object), before);
    const auto after_entity = editor_.query().entity(object.entity);
    ASSERT_TRUE(after_entity.has_value());
    EXPECT_EQ(after_entity->mesh, mesh_id);
    EXPECT_EQ(editor_.modeling().face_texture_settings_revision(), revision);
    EXPECT_EQ(editor_.history().snapshot().undo_label, history.undo_label);
    EXPECT_EQ(editor_.history().snapshot().redo_label, history.redo_label);
    EXPECT_TRUE(editor_.history().can_redo());
    EXPECT_FALSE(editor_.project().dirty());
  }
  ASSERT_TRUE(mirror(true).ok());
}

TEST_F(FaceTextureMirrorApiTests, CollapsedAxisIsANoOpThatKeepsRedo) {
  const auto object = wall(2.5f, true);
  select(object);
  ASSERT_TRUE(mirror(false).ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  session_.mark_clean();
  const auto before = snapshot(object);
  const auto revision = editor_.modeling().face_texture_settings_revision();
  const auto history = editor_.history().snapshot();
  const auto unchanged = mirror(true);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  expect_snapshot_equal(snapshot(object), before);
  EXPECT_EQ(editor_.modeling().face_texture_settings_revision(), revision);
  EXPECT_EQ(editor_.history().snapshot().undo_label, history.undo_label);
  EXPECT_EQ(editor_.history().snapshot().redo_label, history.redo_label);
  EXPECT_TRUE(editor_.history().can_redo());
  EXPECT_FALSE(editor_.project().dirty());
}

TEST_F(FaceTextureMirrorApiTests, TextureFlipStillChangesSignedScaleAndKeepsAuthoredChart) {
  const auto object = wall(0.3f);
  select(object);
  const auto before = snapshot(object);
  const auto &original_face = before.document.faces.front();
  for (const bool horizontal : {true, false}) {
    const auto flipped = editor_.modeling().apply_face_texture_action({
        .action = horizontal ? QuaderFaceTextureAction::FlipTextureHorizontal
                             : QuaderFaceTextureAction::FlipTextureVertical,
        .selection_token = editor_.modeling().face_texture_settings().selection_token});
    ASSERT_TRUE(flipped.ok()) << flipped.error_message();
    ASSERT_TRUE(flipped.changed());
    const auto after = snapshot(object);
    const auto &face = after.document.faces.front();
    auto expected_transform = original_face.texture_transform;
    if (horizontal) expected_transform.scale.x = -expected_transform.scale.x;
    else expected_transform.scale.y = -expected_transform.scale.y;
    EXPECT_EQ(face.texture_transform, expected_transform);
    expect_uvs_equal(face.uvs, original_face.uvs);
    EXPECT_EQ(face.vertices, original_face.vertices);
    EXPECT_EQ(after.sampling.values, before.sampling.values);
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_snapshot_equal(snapshot(object), before);
  }
}

} // namespace
} // namespace quader
