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
#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/session/scene_session.hpp"
#include "math/glm_bridge.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "quader/modeling/neutral/polygon/document_texture.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_transform.hpp"
#include "support/command_observable_state.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr float kEpsilon = 0.002f;

/// Owns an isolated native editor session for copy-pivot behavior tests.
struct CopyPivotHarness {
  quader::SceneSession session;
  quader::QuaderEditorApi editor;

  CopyPivotHarness()
      : editor(quader::QuaderApiSessionInternalAccess::from_scene_session(
            session)) {}
};

quader_poly::Document asymmetric_document(float size_multiplier = 1.0f) {
  quader_poly::Document document;
  const quader_poly::ElementId origin =
      quader_poly::add_vertex(document, {});
  const quader_poly::ElementId x = quader_poly::add_vertex(
      document, {4.0f * size_multiplier, 0.0f, 0.0f});
  const quader_poly::ElementId y = quader_poly::add_vertex(
      document, {0.0f, 2.0f * size_multiplier, 0.0f});
  const quader_poly::ElementId z = quader_poly::add_vertex(
      document, {0.0f, 0.0f, 3.0f * size_multiplier});
  const std::array first_face{origin, x, y};
  const std::array second_face{origin, z, x};
  EXPECT_NE(quader_poly::add_face(document, first_face),
            quader_poly::kInvalidElementId);
  EXPECT_NE(quader_poly::add_face(document, second_face),
            quader_poly::kInvalidElementId);
  return document;
}

quader::QuaderModelingObjectRef
create_asymmetric_mesh(CopyPivotHarness &harness, std::string name,
                       float size_multiplier = 1.0f) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      harness.editor.modeling().create_mesh(
          quader::QuaderPolygonDocumentInternalAccess::from_backend(
              asymmetric_document(size_multiplier)),
          {.name = std::move(name)});
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value();
}

quader::QuaderModelingObjectRef create_box(CopyPivotHarness &harness,
                                            std::string name) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      harness.editor.modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          std::move(name));
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value();
}

quader::QuaderModelingObjectRef
create_box_with_bounds(CopyPivotHarness &harness, std::string name,
                       quader::QVec3 min, quader::QVec3 max) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      harness.editor.modeling().create_box(
          {.min = min, .max = max}, std::move(name));
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value();
}

quader::EntityId create_plain_cpu_mesh(CopyPivotHarness &harness,
                                       std::string name) {
  quader::MeshAsset mesh;
  mesh.name = name + " Mesh";
  quader::MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices = {
      {.position = {}, .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {4.0f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {0.0f, 2.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {0.0f, 0.0f, 3.0f}, .normal = {0.0f, 1.0f, 0.0f}},
  };
  geometry.indices = {0u, 1u, 2u, 0u, 3u, 1u};
  const quader::AssetId mesh_id =
      harness.session.assets().add_mesh(std::move(mesh));
  const quader::EntityId entity = harness.session.scene().create_entity(
      std::move(name), quader::EntityKind::Mesh);
  harness.session.scene().set_mesh(entity,
                                   quader::MeshComponent{.mesh = mesh_id});
  return entity;
}

void set_transform(CopyPivotHarness &harness, quader::EntityId entity,
                   quader::TransformComponent transform) {
  harness.session.scene().set_transform(entity, transform);
}

quader::QVec3 world_origin(const CopyPivotHarness &harness,
                           quader::EntityId entity) {
  return quader::transform_point(
      harness.session.scene().world_transform_matrix(entity), {});
}

void expect_vec3_near(quader::QVec3 actual, quader::QVec3 expected,
                      float epsilon = kEpsilon) {
  EXPECT_NEAR(actual.x, expected.x, epsilon);
  EXPECT_NEAR(actual.y, expected.y, epsilon);
  EXPECT_NEAR(actual.z, expected.z, epsilon);
}

void expect_matrix_near(const quader::QMat4 &actual,
                        const quader::QMat4 &expected) {
  for (std::size_t index = 0; index < actual.values.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_NEAR(actual.values[index], expected.values[index], kEpsilon);
  }
}

void expect_transform_near(const quader::TransformComponent &actual,
                           const quader::TransformComponent &expected) {
  expect_vec3_near(actual.position, expected.position);
  expect_vec3_near(actual.rotation_euler, expected.rotation_euler);
  expect_vec3_near(actual.scale, expected.scale);
  EXPECT_EQ(actual.custom_pivot, expected.custom_pivot);
}

std::vector<quader::QVec3>
world_vertices(const CopyPivotHarness &harness, quader::EntityId entity) {
  const quader::MeshComponent *component =
      harness.session.scene().mesh(entity);
  EXPECT_NE(component, nullptr);
  if (component == nullptr) {
    return {};
  }
  const quader::MeshAsset *mesh =
      harness.session.assets().mesh(component->mesh);
  EXPECT_NE(mesh, nullptr);
  if (mesh == nullptr) {
    return {};
  }

  const quader::QMat4 world =
      harness.session.scene().world_transform_matrix(entity);
  std::vector<quader::QVec3> result;
  result.reserve(mesh->geometry().vertices.size());
  for (const quader::MeshVertex &vertex : mesh->geometry().vertices) {
    result.push_back(quader::transform_point(world, vertex.position));
  }
  return result;
}

std::vector<quader::QVec3>
authored_world_vertices(const CopyPivotHarness &harness,
                        quader::EntityId entity) {
  const quader::MeshComponent *component =
      harness.session.scene().mesh(entity);
  EXPECT_NE(component, nullptr);
  if (component == nullptr) {
    return {};
  }
  const quader::MeshAsset *mesh =
      harness.session.assets().mesh(component->mesh);
  EXPECT_NE(mesh, nullptr);
  if (mesh == nullptr || mesh->authored_polygon_document == nullptr) {
    return {};
  }

  const quader::QMat4 world =
      harness.session.scene().world_transform_matrix(entity);
  std::vector<quader::QVec3> result;
  result.reserve(mesh->authored_polygon_document->vertices.size());
  for (const quader_poly::Vertex &vertex :
       mesh->authored_polygon_document->vertices) {
    result.push_back(quader::transform_point(world, vertex.position));
  }
  return result;
}

void expect_vertices_near(const std::vector<quader::QVec3> &actual,
                          const std::vector<quader::QVec3> &expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t index = 0; index < actual.size(); ++index) {
    SCOPED_TRACE(index);
    expect_vec3_near(actual[index], expected[index]);
  }
}

quader::SceneBounds bounds(const CopyPivotHarness &harness,
                           quader::EntityId entity) {
  const std::optional<quader::SceneBounds> result =
      quader::entity_world_bounds(harness.session.scene(),
                                  harness.session.assets(), entity);
  EXPECT_TRUE(result.has_value());
  return result.value_or(quader::SceneBounds{});
}

void expect_bounds_near(const quader::SceneBounds &actual,
                        const quader::SceneBounds &expected) {
  expect_vec3_near(actual.min, expected.min);
  expect_vec3_near(actual.max, expected.max);
}

quader::QVec3 local_mean(const quader::MeshAsset &mesh) {
  quader::QVec3 sum;
  std::size_t count = 0u;
  if (mesh.authored_polygon_document != nullptr &&
      !mesh.authored_polygon_document->vertices.empty()) {
    for (const quader_poly::Vertex &vertex :
         mesh.authored_polygon_document->vertices) {
      sum += vertex.position;
    }
    count = mesh.authored_polygon_document->vertices.size();
  } else {
    for (const quader::MeshVertex &vertex : mesh.geometry().vertices) {
      sum += vertex.position;
    }
    count = mesh.geometry().vertices.size();
  }
  EXPECT_GT(count, 0u);
  return count == 0u ? quader::QVec3{}
                     : sum / static_cast<float>(count);
}

quader::QVec3 hybrid_center(const CopyPivotHarness &harness,
                            quader::EntityId entity) {
  const quader::MeshComponent *component =
      harness.session.scene().mesh(entity);
  EXPECT_NE(component, nullptr);
  if (component == nullptr) {
    return {};
  }
  const quader::MeshAsset *mesh =
      harness.session.assets().mesh(component->mesh);
  EXPECT_NE(mesh, nullptr);
  if (mesh == nullptr) {
    return {};
  }
  const quader::QMat4 world =
      harness.session.scene().world_transform_matrix(entity);
  const quader::QVec3 mean_world =
      quader::transform_point(world, local_mean(*mesh));
  const std::optional<quader::SceneBounds> world_bounds =
      quader::world_bounds_from_local_mesh_bounds(
          world, quader::local_mesh_bounds(*mesh));
  EXPECT_TRUE(world_bounds.has_value());
  return world_bounds.has_value()
             ? (quader::bounds_center(*world_bounds) + mean_world) * 0.5f
             : quader::QVec3{};
}

quader::QMat4 rotation_only(const quader::TransformComponent &transform) {
  return quader::compose_transform(quader::QTransform{
      .rotation = quader::quat_from_euler_radians(transform.rotation_euler),
  });
}

quader::QVec3 expected_top_level_target_pivot(
    const CopyPivotHarness &harness, quader::EntityId source,
    quader::EntityId target) {
  const quader::TransformComponent &source_transform =
      *harness.session.scene().transform(source);
  const quader::TransformComponent &target_transform =
      *harness.session.scene().transform(target);
  const quader::QVec3 source_pivot =
      source_transform.custom_pivot
          ? world_origin(harness, source)
          : quader::bounds_center(bounds(harness, source));
  const quader::QVec3 source_offset =
      source_pivot - hybrid_center(harness, source);
  const quader::QVec3 source_local_offset = quader::transform_vector(
      quader::inverse(rotation_only(source_transform)), source_offset);
  return hybrid_center(harness, target) +
         quader::transform_vector(rotation_only(target_transform),
                                  source_local_offset);
}

quader::AssetId mesh_id(const CopyPivotHarness &harness,
                        quader::EntityId entity) {
  const quader::MeshComponent *mesh = harness.session.scene().mesh(entity);
  EXPECT_NE(mesh, nullptr);
  return mesh == nullptr ? quader::kInvalidAssetId : mesh->mesh;
}

struct PivotRollbackState {
  quader::AssetId mesh = quader::kInvalidAssetId;
  quader::MeshAsset mesh_asset;
  quader::TransformComponent transform;
  std::optional<quader::TextureMappingComponent> texture_mapping;
  std::vector<quader::QVec3> world_vertices;
  std::vector<quader::AssetId> mesh_ids;
  std::vector<quader::EntityId> entities;
  std::vector<quader::EntityId> selected_entities;
  quader::EntityId primary_selection = quader::kInvalidEntityId;
  std::uint64_t scene_revision = 0u;
  std::uint64_t scene_content_revision = 0u;
  std::uint64_t scene_selection_revision = 0u;
  std::uint64_t scene_transform_revision = 0u;
  std::uint64_t scene_texture_mapping_revision = 0u;
  std::uint64_t scene_mesh_binding_revision = 0u;
  std::uint64_t scene_render_component_revision = 0u;
  std::uint64_t asset_revision = 0u;
  std::uint64_t modeling_content_revision = 0u;
  std::uint64_t modeling_selection_revision = 0u;
  quader::QuaderHistorySnapshot history;
  quader::ModelingSdkProjectionStats projection;
};

PivotRollbackState capture_pivot_rollback_state(
    const CopyPivotHarness &harness, quader::EntityId target) {
  PivotRollbackState state;
  state.mesh = mesh_id(harness, target);
  if (const quader::MeshAsset *asset = harness.session.assets().mesh(state.mesh)) {
    state.mesh_asset = quader::clone_mesh_asset_payload(*asset);
  }
  if (const quader::TransformComponent *transform =
          harness.session.scene().transform(target)) {
    state.transform = *transform;
  }
  if (const quader::TextureMappingComponent *mapping =
          harness.session.scene().texture_mapping(target)) {
    state.texture_mapping = *mapping;
  }
  state.world_vertices = world_vertices(harness, target);
  state.mesh_ids = harness.session.assets().mesh_ids();
  state.entities = harness.session.scene().entities();
  state.selected_entities = harness.session.scene().selected_entities();
  state.primary_selection = harness.session.scene().primary_selection();
  state.scene_revision = harness.session.scene().revision();
  state.scene_content_revision = harness.session.scene().content_revision();
  state.scene_selection_revision = harness.session.scene().selection_revision();
  state.scene_transform_revision = harness.session.scene().transform_revision();
  state.scene_texture_mapping_revision =
      harness.session.scene().texture_mapping_revision();
  state.scene_mesh_binding_revision =
      harness.session.scene().mesh_binding_revision();
  state.scene_render_component_revision =
      harness.session.scene().render_component_revision();
  state.asset_revision = harness.session.assets().revision();
  state.modeling_content_revision = harness.session.modeling().content_revision();
  state.modeling_selection_revision =
      harness.session.modeling().selection_revision();
  state.history = harness.editor.query().history();
  state.projection =
      harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  return state;
}

void expect_pivot_rollback_state(const CopyPivotHarness &harness,
                                 quader::EntityId target,
                                 const PivotRollbackState &expected) {
  EXPECT_EQ(mesh_id(harness, target), expected.mesh);
  const quader::MeshAsset *asset =
      harness.session.assets().mesh(expected.mesh);
  ASSERT_NE(asset, nullptr);
  EXPECT_TRUE(quader::mesh_asset_payload_equal(*asset, expected.mesh_asset));
  const quader::TransformComponent *transform =
      harness.session.scene().transform(target);
  ASSERT_NE(transform, nullptr);
  expect_transform_near(*transform, expected.transform);
  const quader::TextureMappingComponent *mapping =
      harness.session.scene().texture_mapping(target);
  if (expected.texture_mapping.has_value()) {
    ASSERT_NE(mapping, nullptr);
    EXPECT_EQ(*mapping, *expected.texture_mapping);
  } else {
    EXPECT_EQ(mapping, nullptr);
  }
  expect_vertices_near(world_vertices(harness, target),
                       expected.world_vertices);
  EXPECT_EQ(harness.session.assets().mesh_ids(), expected.mesh_ids);
  EXPECT_EQ(harness.session.scene().entities(), expected.entities);
  EXPECT_EQ(harness.session.scene().selected_entities(),
            expected.selected_entities);
  EXPECT_EQ(harness.session.scene().primary_selection(),
            expected.primary_selection);
  EXPECT_EQ(harness.session.scene().revision(), expected.scene_revision);
  EXPECT_EQ(harness.session.scene().content_revision(),
            expected.scene_content_revision);
  EXPECT_EQ(harness.session.scene().selection_revision(),
            expected.scene_selection_revision);
  EXPECT_EQ(harness.session.scene().transform_revision(),
            expected.scene_transform_revision);
  EXPECT_EQ(harness.session.scene().texture_mapping_revision(),
            expected.scene_texture_mapping_revision);
  EXPECT_EQ(harness.session.scene().mesh_binding_revision(),
            expected.scene_mesh_binding_revision);
  EXPECT_EQ(harness.session.scene().render_component_revision(),
            expected.scene_render_component_revision);
  EXPECT_EQ(harness.session.assets().revision(), expected.asset_revision);
  EXPECT_EQ(harness.session.modeling().content_revision(),
            expected.modeling_content_revision);
  EXPECT_EQ(harness.session.modeling().selection_revision(),
            expected.modeling_selection_revision);
  const quader::QuaderHistorySnapshot history =
      harness.editor.query().history();
  EXPECT_EQ(history.can_undo, expected.history.can_undo);
  EXPECT_EQ(history.can_redo, expected.history.can_redo);
  EXPECT_EQ(history.undo_label, expected.history.undo_label);
  EXPECT_EQ(history.redo_label, expected.history.redo_label);
  const quader::ModelingSdkProjectionStats projection =
      harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(projection.full_scene_reset_count,
            expected.projection.full_scene_reset_count);
  EXPECT_EQ(projection.scene_import_document_count,
            expected.projection.scene_import_document_count);
  EXPECT_EQ(projection.targeted_scene_sync_count,
            expected.projection.targeted_scene_sync_count);
  EXPECT_EQ(projection.targeted_scene_sync_document_count,
            expected.projection.targeted_scene_sync_document_count);
}

std::vector<quader::QVec2>
effective_uvs(const quader::MeshAsset &mesh, const quader::QMat4 &mapping) {
  const auto sampling =
      quader::MeshProjectionService::texture_mapping_transform(mapping);
  std::vector<quader::QVec2> result;
  result.reserve(mesh.geometry().vertices.size());
  for (const quader::MeshVertex &vertex : mesh.geometry().vertices) {
    result.push_back(quader_poly::evaluate_texture_mapping(
        vertex.uv, vertex.position, vertex.uv_geometric_normal,
        vertex.uv_carrier, sampling));
  }
  return result;
}

void expect_uvs_near(const std::vector<quader::QVec2> &actual,
                     const std::vector<quader::QVec2> &expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t index = 0; index < actual.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_NEAR(actual[index].x, expected[index].x, kEpsilon);
    EXPECT_NEAR(actual[index].y, expected[index].y, kEpsilon);
  }
}

quader::QuaderModelingObjectRef
create_textured_asymmetric_mesh(CopyPivotHarness &harness,
                                std::uint32_t material_slot = 0u) {
  quader_poly::Document document = asymmetric_document();
  document.faces.front().material_slot = material_slot;
  document.faces.front().texture_transform = {
      .scale = {2.0f, -0.5f},
      .shift_uv = {0.125f, -0.25f},
      .rotation_degrees = 35.0f,
  };
  const auto created = harness.editor.modeling().create_mesh(
      quader::QuaderPolygonDocumentInternalAccess::from_backend(document),
      {.name = "Textured Pivot Target"});
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value();
}

TEST(PivotToolSettingsTests,
     SharedSnapPreferencesSurviveCancellationWithoutSharingPlacement) {
  quader::PivotSessionDefaults remembered;
  CopyPivotHarness first_harness;
  CopyPivotHarness second_harness;
  const auto first_box = create_box(first_harness, "First Pivot");
  const auto second_box = create_box(second_harness, "Second Pivot");
  ASSERT_TRUE(first_harness.editor.selection().select_object(first_box).ok());
  ASSERT_TRUE(second_harness.editor.selection().select_object(second_box).ok());
  quader::PivotToolService first(
      first_harness.session.scene(), first_harness.session.assets(),
      first_harness.session.history(), first_harness.session.modeling(),
      &remembered);
  quader::PivotToolService second(
      second_harness.session.scene(), second_harness.session.assets(),
      second_harness.session.history(), second_harness.session.modeling(),
      &remembered);

  ASSERT_TRUE(first.activate_persistent(first_harness.session.selection()).ok);
  ASSERT_TRUE(first.set_snap_enabled(quader::kPivotSnapGridSetting, false).changed);
  ASSERT_TRUE(first.set_snap_enabled(quader::kPivotSnapVertexSetting, false).changed);
  first.set_persistent_preview_position({7.0f, 8.0f, 9.0f});
  ASSERT_TRUE(first.cancel_persistent().changed);
  EXPECT_FALSE(first.persistent_preview_position().has_value());

  ASSERT_TRUE(second.activate_persistent(second_harness.session.selection()).ok);
  EXPECT_FALSE(second.snap_settings().grid);
  EXPECT_FALSE(second.snap_settings().vertex);
  EXPECT_TRUE(second.snap_settings().edge_midpoint);
  EXPECT_TRUE(second.snap_settings().face_center);
  ASSERT_TRUE(second.persistent_preview_position().has_value());
  EXPECT_FLOAT_EQ(second.persistent_preview_position()->x, 0.0f);
  EXPECT_FLOAT_EQ(second.persistent_preview_position()->y, 0.0f);
  EXPECT_FLOAT_EQ(second.persistent_preview_position()->z, 0.0f);
  ASSERT_TRUE(second.set_snap_enabled(quader::kPivotSnapFaceCenterSetting, false)
                  .changed);
  ASSERT_TRUE(second.cancel_persistent().changed);

  first.begin_quick({1.0f, 2.0f, 3.0f});
  EXPECT_TRUE(first.quick_edit_active());
  EXPECT_FALSE(first.snap_settings().face_center);
  EXPECT_FALSE(first.snap_settings().grid);
  ASSERT_TRUE(first.set_snap_enabled(quader::kPivotSnapGridSetting, true).changed);
  first.end_quick_edit();
  first.clear_quick_override();
  ASSERT_TRUE(second.activate_persistent(second_harness.session.selection()).ok);
  EXPECT_TRUE(second.snap_settings().grid);
  EXPECT_FALSE(second.snap_settings().face_center);

  quader::PivotToolService fresh(
      first_harness.session.scene(), first_harness.session.assets(),
      first_harness.session.history(), first_harness.session.modeling());
  EXPECT_TRUE(fresh.snap_settings().grid);
  EXPECT_TRUE(fresh.snap_settings().vertex);
  EXPECT_TRUE(fresh.snap_settings().edge_midpoint);
  EXPECT_TRUE(fresh.snap_settings().face_center);
  EXPECT_FALSE(fresh.persistent_preview_position().has_value());
  EXPECT_FALSE(fresh.selection_pivot_override().valid);
}

TEST(CopyPivotFromActiveTests,
     PivotRebasePreservesGeneratedUvsAndFaceSettingsThroughHistory) {
  CopyPivotHarness harness;
  const auto target = create_textured_asymmetric_mesh(harness);
  const quader::MeshAsset before =
      *harness.session.assets().mesh(mesh_id(harness, target.entity));
  ASSERT_NE(before.authored_polygon_document, nullptr);
  ASSERT_TRUE(before.authored_polygon_document->faces.front().uvs.empty());
  const auto before_uvs = effective_uvs(before, {});
  const auto before_vertices = world_vertices(harness, target.entity);
  ASSERT_TRUE(harness.editor.selection().select_object(target).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  ASSERT_TRUE(harness.editor.tools()
                  .execute_action("pivot", "pivot.align.top_right_back")
                  .ok());
  harness.session.history().clear();
  const auto applied = harness.editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());

  const auto expect_preserved = [&] {
    const auto *after =
        harness.session.assets().mesh(mesh_id(harness, target.entity));
    ASSERT_NE(after, nullptr);
    ASSERT_NE(after->authored_polygon_document, nullptr);
    ASSERT_EQ(after->authored_polygon_document->faces.size(),
              before.authored_polygon_document->faces.size());
    for (std::size_t index = 0;
         index < after->authored_polygon_document->faces.size(); ++index) {
      const auto &face = after->authored_polygon_document->faces[index];
      EXPECT_EQ(face.uvs.size(), face.vertices.size());
      EXPECT_EQ(face.texture_transform,
                before.authored_polygon_document->faces[index].texture_transform);
    }
    expect_uvs_near(effective_uvs(*after, {}), before_uvs);
    const auto recompiled =
        quader::MeshProjectionService::compile_polygon_document_mesh(
            *after->authored_polygon_document);
    expect_uvs_near(effective_uvs(recompiled, {}), before_uvs);
    expect_vertices_near(world_vertices(harness, target.entity), before_vertices);
    EXPECT_EQ(harness.session.scene().texture_mapping(target.entity), nullptr);
  };
  expect_preserved();
  ASSERT_TRUE(harness.editor.history().undo().ok());
  const auto *undone =
      harness.session.assets().mesh(mesh_id(harness, target.entity));
  ASSERT_NE(undone, nullptr);
  ASSERT_NE(undone->authored_polygon_document, nullptr);
  EXPECT_TRUE(undone->authored_polygon_document->faces.front().uvs.empty());
  expect_uvs_near(effective_uvs(*undone, {}), before_uvs);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_preserved();
}

TEST(CopyPivotFromActiveTests,
     ConjugatesTextureMappingWithoutUvDriftThroughUndoAndRedo) {
  CopyPivotHarness harness;
  const auto source = create_asymmetric_mesh(harness, "Custom Pivot Source");
  const auto target = create_textured_asymmetric_mesh(harness);
  set_transform(harness, source.entity,
                {.position = {3.0f, -2.0f, 5.0f}, .custom_pivot = true});
  set_transform(harness, target.entity,
                {.position = {10.0f, 3.0f, -1.0f},
                 .rotation_euler = {0.1f, 0.2f, 0.3f},
                 .scale = {1.2f, 0.7f, 1.4f}});
  const quader::TransformComponent sampling_transform{
      .position = {0.3f, -1.0f, 0.5f},
      .rotation_euler = {0.3f, 1.2f, -0.4f},
      .scale = {1.3f, 0.7f, -1.1f},
  };
  const quader::QMat4 mapping_before =
      quader::compose_transform(sampling_transform.spatial_transform());
  ASSERT_TRUE(harness.session.scene().set_texture_mapping(
      target.entity, {.sampling_from_local = mapping_before}));
  const quader::MeshAsset before =
      *harness.session.assets().mesh(mesh_id(harness, target.entity));
  const auto before_uvs = effective_uvs(before, mapping_before);
  const auto before_vertices = world_vertices(harness, target.entity);
  const quader::QMat4 world_before =
      harness.session.scene().world_transform_matrix(target.entity);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  harness.session.history().clear();
  const auto copied = harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());

  const quader::QMat4 old_local_from_new = quader::scene_transform_multiply(
      quader::inverse(world_before),
      harness.session.scene().world_transform_matrix(target.entity));
  const quader::QMat4 mapping_after = quader::scene_transform_multiply(
      quader::inverse(old_local_from_new),
      quader::scene_transform_multiply(mapping_before, old_local_from_new));
  const auto expect_preserved = [&] {
    const auto *mapping = harness.session.scene().texture_mapping(target.entity);
    ASSERT_NE(mapping, nullptr);
    expect_matrix_near(mapping->sampling_from_local, mapping_after);
    const auto *after =
        harness.session.assets().mesh(mesh_id(harness, target.entity));
    ASSERT_NE(after, nullptr);
    ASSERT_NE(after->authored_polygon_document, nullptr);
    expect_uvs_near(effective_uvs(*after, mapping->sampling_from_local), before_uvs);
    const auto recompiled =
        quader::MeshProjectionService::compile_polygon_document_mesh(
            *after->authored_polygon_document);
    expect_uvs_near(effective_uvs(recompiled, mapping->sampling_from_local),
                    before_uvs);
    EXPECT_EQ(after->authored_polygon_document->faces.front().texture_transform,
              before.authored_polygon_document->faces.front().texture_transform);
    expect_vertices_near(world_vertices(harness, target.entity), before_vertices);
  };
  expect_preserved();
  ASSERT_TRUE(harness.editor.history().undo().ok());
  const auto *undone_mapping =
      harness.session.scene().texture_mapping(target.entity);
  ASSERT_NE(undone_mapping, nullptr);
  EXPECT_EQ(undone_mapping->sampling_from_local.values, mapping_before.values);
  expect_uvs_near(effective_uvs(
                      *harness.session.assets().mesh(mesh_id(harness, target.entity)),
                      mapping_before),
                  before_uvs);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_preserved();
}

TEST(CopyPivotFromActiveTests, GroupPivotRebaseDoesNotChangeChildTextureMapping) {
  CopyPivotHarness harness;
  const auto child = create_textured_asymmetric_mesh(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(child).ok());
  const auto grouped = harness.editor.scene().group_selection("Textured Group");
  ASSERT_TRUE(grouped.ok()) << grouped.error_message();
  const quader::TransformComponent sampling{
      .position = {1.0f, -2.0f, 3.0f},
      .rotation_euler = {0.5f, 0.3f, 0.7f},
  };
  const quader::QMat4 mapping_before =
      quader::compose_transform(sampling.spatial_transform());
  ASSERT_TRUE(harness.session.scene().set_texture_mapping(
      child.entity, {.sampling_from_local = mapping_before}));
  const auto world_before =
      harness.session.scene().world_transform_matrix(child.entity);
  ASSERT_TRUE(harness.editor.selection().select_object(grouped.value().id).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  ASSERT_TRUE(harness.editor.tools()
                  .execute_action("pivot", "pivot.align.top_right_back")
                  .ok());
  harness.session.history().clear();
  const auto applied = harness.editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  const auto expect_preserved = [&] {
    const auto *mapping = harness.session.scene().texture_mapping(child.entity);
    ASSERT_NE(mapping, nullptr);
    EXPECT_EQ(mapping->sampling_from_local.values, mapping_before.values);
    expect_matrix_near(harness.session.scene().world_transform_matrix(child.entity),
                       world_before);
    EXPECT_EQ(harness.session.scene().texture_mapping(grouped.value().id), nullptr);
  };
  expect_preserved();
  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_preserved();
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_preserved();
}

const quader::QuaderActionPanel *
find_panel(const quader::QuaderActionSet &actions, std::string_view panel_id) {
  const auto found = std::find_if(
      actions.panels.begin(), actions.panels.end(),
      [&](const quader::QuaderActionPanel &panel) {
        return panel.panel_id == panel_id;
      });
  return found == actions.panels.end() ? nullptr : &*found;
}

const quader::QuaderActionItem *
find_action(const quader::QuaderActionPanel &panel,
            std::string_view action_id) {
  const auto found = std::find_if(
      panel.actions.begin(), panel.actions.end(),
      [&](const quader::QuaderActionItem &action) {
        return action.action_id == action_id;
      });
  return found == panel.actions.end() ? nullptr : &*found;
}

std::optional<quader::QuaderCommandDescriptor>
command_descriptor(const CopyPivotHarness &harness) {
  const std::vector<quader::QuaderCommandDescriptor> commands =
      harness.editor.commands().available();
  const auto found = std::find_if(
      commands.begin(), commands.end(),
      [](const quader::QuaderCommandDescriptor &command) {
        return command.id == quader::quader_command::kModelingCopyPivotFromActive
                                 .string();
      });
  return found == commands.end()
             ? std::nullopt
             : std::optional<quader::QuaderCommandDescriptor>{*found};
}

TEST(CopyPivotFromActiveTests,
     AppliedLowerLeftPivotTransfersToCongruentTargetLowerLeft) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source = create_box_with_bounds(
      harness, "Lower Left Source", {2.0f, 1.0f, -4.0f},
      {4.0f, 3.0f, -2.0f});
  const quader::QuaderModelingObjectRef target = create_box_with_bounds(
      harness, "Lower Left Target", {-8.0f, 5.0f, 1.0f},
      {-6.0f, 7.0f, 3.0f});

  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  const std::optional<quader::QVec3> source_move_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(source_move_pivot.has_value());
  expect_vec3_near(*source_move_pivot, {3.0f, 2.0f, -3.0f});
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  expect_vec3_near(harness.session.pivot().handle_state().world_position,
                   *source_move_pivot);
  ASSERT_TRUE(harness.editor.tools()
                  .execute_action("pivot", "pivot.align.bottom_left_front")
                  .ok());
  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());

  const quader::SceneBounds source_bounds = bounds(harness, source.entity);
  expect_vec3_near(world_origin(harness, source.entity), source_bounds.min);
  ASSERT_TRUE(harness.session.scene().transform(source.entity)->custom_pivot);

  const quader::SceneBounds target_bounds_before =
      bounds(harness, target.entity);
  const std::vector<quader::QVec3> source_vertices_before =
      world_vertices(harness, source.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  const std::vector<quader::QVec3> target_authored_vertices_before =
      authored_world_vertices(harness, target.entity);
  ASSERT_FALSE(target_authored_vertices_before.empty());
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  EXPECT_TRUE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_vec3_near(world_origin(harness, target.entity),
                   target_bounds_before.min);
  expect_vertices_near(world_vertices(harness, source.entity),
                       source_vertices_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_vertices_near(authored_world_vertices(harness, target.entity),
                       target_authored_vertices_before);
  expect_bounds_near(bounds(harness, target.entity), target_bounds_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), source.entity);
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(),
            "Copy Pivot From Active");

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_vertices_near(authored_world_vertices(harness, target.entity),
                       target_authored_vertices_before);
  expect_bounds_near(bounds(harness, target.entity), target_bounds_before);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_vec3_near(world_origin(harness, target.entity),
                   target_bounds_before.min);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_vertices_near(authored_world_vertices(harness, target.entity),
                       target_authored_vertices_before);
}

TEST(CopyPivotFromActiveTests,
     DefaultSourceWithChildCopiesThePivotShownByTransformGizmo) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_box(harness, "Hierarchy Pivot Source");
  const quader::QuaderModelingObjectRef source_child =
      create_box(harness, "Off-Center Source Child");
  const quader::QuaderModelingObjectRef target =
      create_box(harness, "Hierarchy Pivot Target");
  set_transform(harness, source_child.entity, {.position = {10.0f, 0.0f, 0.0f}});
  set_transform(harness, target.entity, {.position = {30.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(source_child.entity, source.entity, false)
                  .ok());

  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  const std::optional<quader::QVec3> displayed_source_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(displayed_source_pivot.has_value());
  expect_vec3_near(*displayed_source_pivot, {5.0f, 0.0f, 0.0f});
  expect_vec3_near(quader::bounds_center(bounds(harness, source.entity)), {});

  const quader::QMat4 source_child_world_before =
      harness.session.scene().world_transform_matrix(source_child.entity);
  const std::vector<quader::QVec3> source_vertices_before =
      world_vertices(harness, source.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_vec3_near(world_origin(harness, target.entity),
                   {35.0f, 0.0f, 0.0f});
  EXPECT_TRUE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_vertices_near(world_vertices(harness, source.entity),
                       source_vertices_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_matrix_near(
      harness.session.scene().world_transform_matrix(source_child.entity),
      source_child_world_before);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_vec3_near(world_origin(harness, target.entity),
                   {35.0f, 0.0f, 0.0f});
}

TEST(CopyPivotFromActiveTests,
     DefaultTargetWithChildIsComparedAgainstItsDisplayedHierarchyPivot) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_box(harness, "Centered Source");
  const quader::QuaderModelingObjectRef target =
      create_box(harness, "Child-Bearing Target");
  const quader::QuaderModelingObjectRef target_child =
      create_box(harness, "Off-Center Target Child");
  set_transform(harness, target.entity, {.position = {30.0f, 0.0f, 0.0f}});
  set_transform(harness, target_child.entity, {.position = {10.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(target_child.entity, target.entity, false)
                  .ok());

  ASSERT_TRUE(harness.editor.selection().select_object(target).ok());
  const std::optional<quader::QVec3> displayed_target_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(displayed_target_pivot.has_value());
  expect_vec3_near(*displayed_target_pivot, {35.0f, 0.0f, 0.0f});
  expect_vec3_near(quader::bounds_center(bounds(harness, target.entity)),
                   {30.0f, 0.0f, 0.0f});

  const quader::QMat4 target_child_world_before =
      harness.session.scene().world_transform_matrix(target_child.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_vec3_near(world_origin(harness, target.entity),
                   {30.0f, 0.0f, 0.0f});
  EXPECT_TRUE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_matrix_near(
      harness.session.scene().world_transform_matrix(target_child.entity),
      target_child_world_before);
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(),
            "Copy Pivot From Active");

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_matrix_near(
      harness.session.scene().world_transform_matrix(target_child.entity),
      target_child_world_before);
}

TEST(CopyPivotFromActiveTests,
     UsesAuthoredHybridCenterVisibleBoundsPivotAndRotationOnlyTransfer) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Asymmetric Source");
  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Scaled Target", 1.8f);
  set_transform(harness, source.entity,
                {.position = {3.0f, -2.0f, 5.0f},
                 .rotation_euler = {0.3f, -0.45f, 0.2f},
                 .scale = {2.0f, 0.75f, 1.5f}});
  set_transform(harness, target.entity,
                {.position = {-7.0f, 4.0f, -3.0f},
                 .rotation_euler = {-0.25f, 0.6f, -0.35f},
                 .scale = {0.5f, 2.25f, 0.8f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());

  ASSERT_TRUE(harness.session.assets().update_mesh(
      mesh_id(harness, source.entity), [](quader::MeshAsset &mesh) {
        ASSERT_FALSE(mesh.geometry().vertices.empty());
        mesh.edit_geometry().vertices.front().position = {1.0f, 1.0f, 1.0f};
      }));

  const quader::MeshAsset *source_mesh =
      harness.session.assets().mesh(mesh_id(harness, source.entity));
  ASSERT_NE(source_mesh, nullptr);
  ASSERT_NE(source_mesh->authored_polygon_document, nullptr);
  ASSERT_EQ(source_mesh->authored_polygon_document->vertices.size(), 4u);
  ASSERT_FALSE(harness.session.scene().transform(source.entity)->custom_pivot);
  quader::QVec3 cpu_sum;
  for (const quader::MeshVertex &vertex : source_mesh->geometry().vertices) {
    cpu_sum += vertex.position;
  }
  const quader::QVec3 cpu_mean =
      cpu_sum / static_cast<float>(source_mesh->geometry().vertices.size());
  EXPECT_GT(quader::length(cpu_mean - local_mean(*source_mesh)), 0.1f);

  const quader::QVec3 source_visible_pivot =
      quader::bounds_center(bounds(harness, source.entity));
  EXPECT_GT(quader::length(source_visible_pivot -
                           world_origin(harness, source.entity)),
            0.1f);
  const quader::QVec3 target_hybrid = hybrid_center(harness, target.entity);
  const quader::QVec3 source_offset =
      source_visible_pivot - hybrid_center(harness, source.entity);
  const quader::QVec3 expected =
      expected_top_level_target_pivot(harness, source.entity, target.entity);
  EXPECT_NEAR(quader::length(expected - target_hybrid),
              quader::length(source_offset), kEpsilon);
  EXPECT_GT(quader::length(expected - world_origin(harness, target.entity)),
            0.1f);

  const quader::TransformComponent source_before =
      *harness.session.scene().transform(source.entity);
  const quader::TransformComponent target_before =
      *harness.session.scene().transform(target.entity);
  const std::vector<quader::QVec3> source_vertices_before =
      world_vertices(harness, source.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  const quader::SceneBounds target_bounds_before =
      bounds(harness, target.entity);
  const std::vector<quader::EntityId> selected_before =
      harness.session.scene().selected_entities();
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_vec3_near(world_origin(harness, target.entity), expected);
  EXPECT_TRUE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_transform_near(*harness.session.scene().transform(source.entity),
                        source_before);
  expect_vertices_near(world_vertices(harness, source.entity),
                       source_vertices_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_bounds_near(bounds(harness, target.entity), target_bounds_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), source.entity);
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(),
            "Copy Pivot From Active");

  const quader::QuaderApiVoidResult unchanged =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_transform_near(*harness.session.scene().transform(target.entity),
                        target_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  EXPECT_FALSE(harness.editor.history().can_undo());
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_vec3_near(world_origin(harness, target.entity), expected);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
}

TEST(CopyPivotFromActiveTests,
     CopiesCustomSourcePivotToMultipleTargetsAndIgnoresNonMeshSelection) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Custom Pivot Source");
  const quader::QuaderModelingObjectRef first =
      create_asymmetric_mesh(harness, "First Target", 0.75f);
  const quader::QuaderModelingObjectRef second =
      create_asymmetric_mesh(harness, "Second Target", 1.4f);
  const quader::QuaderApiResult<quader::QuaderEntityRef> empty =
      harness.editor.scene().create_entity(
          {.name = "Ignored Empty", .kind = quader::QuaderEntityKind::Empty});
  ASSERT_TRUE(empty.ok()) << empty.error_message();

  set_transform(harness, source.entity,
                {.position = {6.0f, 2.0f, -4.0f},
                 .rotation_euler = {0.15f, 0.4f, -0.25f},
                 .scale = {1.75f, 0.6f, 1.1f},
                 .custom_pivot = true});
  set_transform(harness, first.entity,
                {.position = {-2.0f, -5.0f, 3.0f},
                 .rotation_euler = {-0.2f, 0.1f, 0.55f},
                 .scale = {0.9f, 1.8f, 0.7f}});
  set_transform(harness, second.entity,
                {.position = {9.0f, -1.0f, 2.0f},
                 .rotation_euler = {0.35f, -0.5f, 0.1f},
                 .scale = {2.2f, 0.8f, 1.3f},
                 .custom_pivot = true});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, first.entity, second.entity,
                                   empty.value().id},
                                  source.entity)
                  .ok());
  ASSERT_TRUE(harness.session.scene().set_locked(source.entity, true));

  const quader::QVec3 first_expected = expected_top_level_target_pivot(
      harness, source.entity, first.entity);
  const quader::QVec3 second_expected = expected_top_level_target_pivot(
      harness, source.entity, second.entity);
  const quader::QVec3 first_origin_before = world_origin(harness, first.entity);
  const quader::QVec3 second_origin_before =
      world_origin(harness, second.entity);
  const std::vector<quader::QVec3> first_vertices_before =
      world_vertices(harness, first.entity);
  const std::vector<quader::QVec3> second_vertices_before =
      world_vertices(harness, second.entity);
  const std::vector<quader::EntityId> selected_before =
      harness.session.scene().selected_entities();
  harness.session.history().clear();

  ASSERT_TRUE(harness.editor.modeling().can_copy_pivot_from_active());
  const quader::QuaderApiVoidResult copied = harness.editor.commands().execute(
      quader::quader_command::kModelingCopyPivotFromActive);
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_vec3_near(world_origin(harness, first.entity), first_expected);
  expect_vec3_near(world_origin(harness, second.entity), second_expected);
  expect_vertices_near(world_vertices(harness, first.entity),
                       first_vertices_before);
  expect_vertices_near(world_vertices(harness, second.entity),
                       second_vertices_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), source.entity);
  EXPECT_TRUE(harness.session.scene().is_effectively_locked(source.entity));
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(),
            "Copy Pivot From Active");

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_vec3_near(world_origin(harness, first.entity), first_origin_before);
  expect_vec3_near(world_origin(harness, second.entity), second_origin_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_vec3_near(world_origin(harness, first.entity), first_expected);
  expect_vec3_near(world_origin(harness, second.entity), second_expected);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
}

TEST(CopyPivotFromActiveTests,
     PlainSceneMeshSourceFallsBackToStableCpuVertices) {
  CopyPivotHarness harness;
  const quader::EntityId source =
      create_plain_cpu_mesh(harness, "Plain CPU Source");
  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Authored Target", 1.25f);
  set_transform(harness, source,
                {.position = {4.0f, -3.0f, 2.0f},
                 .rotation_euler = {0.25f, -0.35f, 0.15f},
                 .scale = {1.5f, 0.7f, 1.2f}});
  set_transform(harness, target.entity,
                {.position = {-8.0f, 5.0f, -1.0f},
                 .rotation_euler = {-0.2f, 0.45f, -0.1f},
                 .scale = {0.8f, 1.6f, 0.9f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source, target.entity}, source)
                  .ok());

  const quader::MeshAsset *source_mesh =
      harness.session.assets().mesh(mesh_id(harness, source));
  ASSERT_NE(source_mesh, nullptr);
  EXPECT_EQ(source_mesh->authored_polygon_document, nullptr);
  EXPECT_TRUE(source_mesh->has_stable_cpu_topology);
  const quader::QVec3 expected =
      expected_top_level_target_pivot(harness, source, target.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  harness.session.history().clear();

  ASSERT_TRUE(harness.editor.modeling().can_copy_pivot_from_active());
  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_vec3_near(world_origin(harness, target.entity), expected);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), source);
}

TEST(CopyPivotFromActiveTests,
     MatchingNonCustomVisibleBoundsPivotIsUnchangedWithoutHistory) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Matching Source");
  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Matching Target");
  const quader::QVec3 shared_rotation{0.2f, -0.35f, 0.45f};
  const quader::QVec3 shared_scale{1.6f, 0.75f, 1.25f};
  set_transform(harness, source.entity,
                {.position = {3.0f, -4.0f, 2.0f},
                 .rotation_euler = shared_rotation,
                 .scale = shared_scale});
  set_transform(harness, target.entity,
                {.position = {-8.0f, 5.0f, -3.0f},
                 .rotation_euler = shared_rotation,
                 .scale = shared_scale});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());

  ASSERT_FALSE(harness.session.scene().transform(target.entity)->custom_pivot);
  const quader::QVec3 expected =
      expected_top_level_target_pivot(harness, source.entity, target.entity);
  const quader::QVec3 effective_target_pivot =
      quader::bounds_center(bounds(harness, target.entity));
  expect_vec3_near(expected, effective_target_pivot);
  EXPECT_GT(quader::length(effective_target_pivot -
                           world_origin(harness, target.entity)),
            0.1f);

  const quader::TransformComponent target_before =
      *harness.session.scene().transform(target.entity);
  const quader::AssetId target_mesh_before =
      mesh_id(harness, target.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  const std::size_t mesh_count_before = harness.session.assets().mesh_count();
  harness.session.history().clear();

  ASSERT_TRUE(harness.editor.modeling().can_copy_pivot_from_active());
  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  EXPECT_FALSE(copied.changed());
  EXPECT_FALSE(harness.editor.history().can_undo());
  EXPECT_FALSE(harness.editor.history().can_redo());
  expect_transform_near(*harness.session.scene().transform(target.entity),
                        target_before);
  EXPECT_FALSE(harness.session.scene().transform(target.entity)->custom_pivot);
  EXPECT_EQ(mesh_id(harness, target.entity), target_mesh_before);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
}

TEST(CopyPivotFromActiveTests,
     MixedUnchangedAndChangedTargetsOnlyRebaseChangedTarget) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Mixed Source");
  const quader::QuaderModelingObjectRef unchanged =
      create_asymmetric_mesh(harness, "Mixed Unchanged Target");
  const quader::QuaderModelingObjectRef changed =
      create_asymmetric_mesh(harness, "Mixed Changed Target", 1.4f);
  const quader::QVec3 shared_rotation{0.2f, -0.35f, 0.45f};
  const quader::QVec3 shared_scale{1.6f, 0.75f, 1.25f};
  set_transform(harness, source.entity,
                {.position = {3.0f, -4.0f, 2.0f},
                 .rotation_euler = shared_rotation,
                 .scale = shared_scale});
  set_transform(harness, unchanged.entity,
                {.position = {-8.0f, 5.0f, -3.0f},
                 .rotation_euler = shared_rotation,
                 .scale = shared_scale});
  set_transform(harness, changed.entity,
                {.position = {9.0f, 2.0f, -6.0f},
                 .rotation_euler = {-0.3f, 0.1f, -0.25f},
                 .scale = {0.9f, 1.3f, 0.8f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects(
                      {source.entity, unchanged.entity, changed.entity},
                      source.entity)
                  .ok());

  const quader::TransformComponent unchanged_before =
      *harness.session.scene().transform(unchanged.entity);
  const quader::AssetId unchanged_mesh_before =
      mesh_id(harness, unchanged.entity);
  const std::vector<quader::QVec3> unchanged_vertices_before =
      world_vertices(harness, unchanged.entity);
  const quader::QVec3 expected_changed = expected_top_level_target_pivot(
      harness, source.entity, changed.entity);
  const quader::QVec3 changed_origin_before =
      world_origin(harness, changed.entity);
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_transform_near(*harness.session.scene().transform(unchanged.entity),
                        unchanged_before);
  EXPECT_FALSE(
      harness.session.scene().transform(unchanged.entity)->custom_pivot);
  EXPECT_EQ(mesh_id(harness, unchanged.entity), unchanged_mesh_before);
  expect_vertices_near(world_vertices(harness, unchanged.entity),
                       unchanged_vertices_before);
  expect_vec3_near(world_origin(harness, changed.entity), expected_changed);
  EXPECT_TRUE(harness.session.scene().transform(changed.entity)->custom_pivot);
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(),
            "Copy Pivot From Active");

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_vec3_near(world_origin(harness, changed.entity),
                   changed_origin_before);
  expect_transform_near(*harness.session.scene().transform(unchanged.entity),
                        unchanged_before);
  EXPECT_FALSE(harness.editor.history().can_undo());
}

TEST(CopyPivotFromActiveTests,
     MinimumNonuniformScalesStillUseTheRotationOnlyBasis) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Minimum Scale Source");
  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Minimum Scale Target", 1.25f);
  set_transform(harness, source.entity,
                {.position = {3.0f, -4.0f, 2.0f},
                 .rotation_euler = {0.2f, -0.35f, 0.45f},
                 .scale = {0.001f, 0.0015f, 0.002f},
                 .custom_pivot = true});
  set_transform(harness, target.entity,
                {.position = {-8.0f, 5.0f, -3.0f},
                 .rotation_euler = {-0.3f, 0.1f, -0.25f},
                 .scale = {0.002f, 0.001f, 0.003f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity},
                                  source.entity)
                  .ok());

  const quader::QVec3 expected =
      expected_top_level_target_pivot(harness, source.entity, target.entity);
  const quader::QVec3 origin_before = world_origin(harness, target.entity);
  const std::vector<quader::QVec3> vertices_before =
      world_vertices(harness, target.entity);
  harness.session.history().clear();

  ASSERT_TRUE(harness.editor.modeling().can_copy_pivot_from_active());
  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  EXPECT_GT(quader::length(expected - origin_before), 0.0001f);
  expect_vec3_near(world_origin(harness, target.entity), expected, 0.00001f);
  expect_vertices_near(world_vertices(harness, target.entity),
                       vertices_before);
}

TEST(CopyPivotFromActiveTests,
     SharedTargetMeshUsesStableCloneOnWriteAcrossUndoAndRedo) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Clone Source");
  const quader::QuaderModelingObjectRef target =
      create_box(harness, "Clone Target");
  const quader::QuaderModelingObjectRef unselected_sharer =
      create_box(harness, "Unselected Sharer");
  set_transform(harness, source.entity,
                {.position = {5.0f, 3.0f, -2.0f},
                 .rotation_euler = {0.2f, 0.3f, -0.4f},
                 .scale = {1.2f, 0.8f, 1.5f},
                 .custom_pivot = true});
  set_transform(harness, target.entity,
                {.position = {-4.0f, 2.0f, 1.0f},
                 .rotation_euler = {-0.1f, 0.25f, 0.2f},
                 .scale = {0.75f, 1.4f, 0.9f}});
  set_transform(harness, unselected_sharer.entity,
                {.position = {11.0f, -6.0f, 4.0f}});

  const quader::AssetId shared_mesh = mesh_id(harness, target.entity);
  quader::MeshComponent shared_component =
      *harness.session.scene().mesh(unselected_sharer.entity);
  shared_component.mesh = shared_mesh;
  harness.session.scene().set_mesh(unselected_sharer.entity, shared_component);
  ASSERT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());

  const std::size_t mesh_count_before = harness.session.assets().mesh_count();
  const quader::QVec3 target_origin_before =
      world_origin(harness, target.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  const std::vector<quader::QVec3> sharer_vertices_before =
      world_vertices(harness, unselected_sharer.entity);
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  const quader::AssetId cloned_mesh = mesh_id(harness, target.entity);
  EXPECT_NE(cloned_mesh, shared_mesh);
  EXPECT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before + 1u);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_vertices_near(world_vertices(harness, unselected_sharer.entity),
                       sharer_vertices_before);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_EQ(mesh_id(harness, target.entity), shared_mesh);
  EXPECT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before);
  expect_vec3_near(world_origin(harness, target.entity), target_origin_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_vertices_near(world_vertices(harness, unselected_sharer.entity),
                       sharer_vertices_before);

  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_EQ(mesh_id(harness, target.entity), cloned_mesh);
  EXPECT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before + 1u);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_vertices_near(world_vertices(harness, unselected_sharer.entity),
                       sharer_vertices_before);
}

TEST(CopyPivotFromActiveTests,
     ParentAndChildTargetsPreserveWorldGeometryThroughOneUndoStep) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Hierarchy Source");
  const quader::QuaderModelingObjectRef child =
      create_asymmetric_mesh(harness, "Earlier Child", 0.6f);
  const quader::QuaderModelingObjectRef parent =
      create_asymmetric_mesh(harness, "Later Parent", 1.3f);
  const quader::QuaderApiResult<quader::QuaderEntityRef> nonselected_child =
      harness.editor.scene().create_entity({
          .name = "Nonselected Direct Child",
          .kind = quader::QuaderEntityKind::Empty,
          .transform = {.position = {-2.0f, 7.0f, -5.0f},
                        .rotation_euler = {0.3f, -0.1f, 0.2f},
                        .scale = {0.7f, 1.4f, 0.9f}},
      });
  ASSERT_TRUE(nonselected_child.ok()) << nonselected_child.error_message();
  ASSERT_LT(child.entity, parent.entity);
  set_transform(harness, source.entity,
                {.position = {8.0f, 1.0f, -3.0f},
                 .custom_pivot = true});
  set_transform(harness, child.entity,
                {.position = {-5.0f, 6.0f, 2.0f},
                 .rotation_euler = {-0.2f, 0.15f, 0.1f},
                 .scale = {0.8f, 1.25f, 0.95f}});
  set_transform(harness, parent.entity,
                {.position = {3.0f, -4.0f, 1.0f},
                 .rotation_euler = {0.1f, -0.25f, 0.2f},
                 .scale = {1.15f, 0.9f, 1.1f}});
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(child.entity, parent.entity, true)
                  .ok());
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(nonselected_child.value().id, parent.entity, true)
                  .ok());
  ASSERT_EQ(harness.session.scene().parent(child.entity), parent.entity);
  ASSERT_EQ(harness.session.scene().parent(nonselected_child.value().id),
            parent.entity);

  const quader::AssetId shared_mesh = mesh_id(harness, parent.entity);
  quader::MeshComponent child_component =
      *harness.session.scene().mesh(child.entity);
  child_component.mesh = shared_mesh;
  harness.session.scene().set_mesh(child.entity, child_component);
  ASSERT_EQ(mesh_id(harness, child.entity), shared_mesh);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, child.entity, parent.entity},
                                  source.entity)
                  .ok());

  const quader::QVec3 child_origin_before =
      world_origin(harness, child.entity);
  const quader::QVec3 parent_origin_before =
      world_origin(harness, parent.entity);
  const std::vector<quader::QVec3> child_vertices_before =
      world_vertices(harness, child.entity);
  const std::vector<quader::QVec3> parent_vertices_before =
      world_vertices(harness, parent.entity);
  const quader::QMat4 nonselected_child_world_before =
      harness.session.scene().world_transform_matrix(
          nonselected_child.value().id);
  const std::size_t mesh_count_before = harness.session.assets().mesh_count();
  const std::vector<quader::EntityId> selected_before =
      harness.session.scene().selected_entities();
  EXPECT_EQ(std::find(selected_before.begin(), selected_before.end(),
                      nonselected_child.value().id),
            selected_before.end());
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  EXPECT_GT(quader::length(world_origin(harness, child.entity) -
                           child_origin_before),
            0.1f);
  EXPECT_GT(quader::length(world_origin(harness, parent.entity) -
                           parent_origin_before),
            0.1f);
  EXPECT_TRUE(harness.session.scene().transform(child.entity)->custom_pivot);
  EXPECT_TRUE(harness.session.scene().transform(parent.entity)->custom_pivot);
  const quader::AssetId cloned_parent_mesh =
      mesh_id(harness, parent.entity);
  EXPECT_NE(cloned_parent_mesh, shared_mesh);
  EXPECT_EQ(mesh_id(harness, child.entity), shared_mesh);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before + 1u);
  expect_vertices_near(world_vertices(harness, child.entity),
                       child_vertices_before);
  expect_vertices_near(world_vertices(harness, parent.entity),
                       parent_vertices_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         nonselected_child.value().id),
                     nonselected_child_world_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_vec3_near(world_origin(harness, child.entity), child_origin_before);
  expect_vec3_near(world_origin(harness, parent.entity), parent_origin_before);
  expect_vertices_near(world_vertices(harness, child.entity),
                       child_vertices_before);
  expect_vertices_near(world_vertices(harness, parent.entity),
                       parent_vertices_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         nonselected_child.value().id),
                     nonselected_child_world_before);
  EXPECT_EQ(mesh_id(harness, parent.entity), shared_mesh);
  EXPECT_EQ(mesh_id(harness, child.entity), shared_mesh);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before);
  EXPECT_EQ(harness.session.scene().parent(child.entity), parent.entity);
  EXPECT_FALSE(harness.editor.history().can_undo());

  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_EQ(mesh_id(harness, parent.entity), cloned_parent_mesh);
  EXPECT_EQ(mesh_id(harness, child.entity), shared_mesh);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before + 1u);
  expect_vertices_near(world_vertices(harness, child.entity),
                       child_vertices_before);
  expect_vertices_near(world_vertices(harness, parent.entity),
                       parent_vertices_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         nonselected_child.value().id),
                     nonselected_child_world_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
}

TEST(CopyPivotFromActiveTests,
     MirroredNonuniformParentUsesBlenderWorldRotationAndPreservesDescendants) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Mirrored Hierarchy Source");
  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Mirrored Hierarchy Target", 0.9f);
  const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
      harness.editor.scene().create_entity({
          .name = "Mirrored Nonuniform Parent",
          .kind = quader::QuaderEntityKind::Empty,
          .transform = {.position = {-6.0f, 2.0f, 5.0f},
                        .rotation_euler = {0.4f, -0.2f, 0.3f},
                        .scale = {-1.5f, 0.65f, 2.2f}},
      });
  const quader::QuaderApiResult<quader::QuaderEntityRef> direct_child =
      harness.editor.scene().create_entity({
          .name = "Preserved Direct Child",
          .kind = quader::QuaderEntityKind::Empty,
          .transform = {.position = {1.1f, -0.7f, 0.4f},
                        .rotation_euler = {0.2f, 0.1f, -0.3f},
                        .scale = {0.8f, 1.2f, 0.6f}},
      });
  const quader::QuaderApiResult<quader::QuaderEntityRef> grandchild =
      harness.editor.scene().create_entity({
          .name = "Preserved Grandchild",
          .kind = quader::QuaderEntityKind::Empty,
          .transform = {.position = {-0.5f, 1.4f, 2.0f},
                        .rotation_euler = {-0.15f, 0.25f, 0.45f},
                        .scale = {1.1f, 0.7f, 1.3f}},
      });
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  ASSERT_TRUE(direct_child.ok()) << direct_child.error_message();
  ASSERT_TRUE(grandchild.ok()) << grandchild.error_message();

  set_transform(harness, source.entity,
                {.position = {4.0f, -3.0f, 2.0f},
                 .rotation_euler = {0.25f, -0.4f, 0.35f},
                 .scale = {1.2f, 0.7f, 1.5f},
                 .custom_pivot = true});
  set_transform(harness, target.entity,
                {.position = {2.5f, -1.0f, 3.0f},
                 .rotation_euler = {-0.35f, 0.5f, -0.15f},
                 .scale = {0.8f, 1.3f, 0.55f}});
  ASSERT_TRUE(
      harness.editor.scene().reparent(target.entity, parent.value().id, false)
          .ok());
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(direct_child.value().id, target.entity, false)
                  .ok());
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(grandchild.value().id, direct_child.value().id,
                            false)
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());

  // Oracle generated by Blender 4.2 from the exact TRS hierarchy above using
  // matrix_world.decompose().rotation and the add-on's dual-center formula.
  constexpr quader::QVec3 kBlenderExpectedPivot{
      -14.002666473f, -1.897130013f, 9.366803169f};
  const quader::QMat4 parent_world_before =
      harness.session.scene().world_transform_matrix(parent.value().id);
  const quader::QMat4 direct_child_world_before =
      harness.session.scene().world_transform_matrix(direct_child.value().id);
  const quader::QMat4 grandchild_world_before =
      harness.session.scene().world_transform_matrix(grandchild.value().id);
  const std::vector<quader::QVec3> source_vertices_before =
      world_vertices(harness, source.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  const quader::SceneBounds target_bounds_before =
      bounds(harness, target.entity);
  const std::vector<quader::EntityId> selection_before =
      harness.session.scene().selected_entities();
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_vec3_near(world_origin(harness, target.entity),
                   kBlenderExpectedPivot);
  EXPECT_TRUE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_vertices_near(world_vertices(harness, source.entity),
                       source_vertices_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_bounds_near(bounds(harness, target.entity), target_bounds_before);
  expect_matrix_near(
      harness.session.scene().world_transform_matrix(parent.value().id),
      parent_world_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         direct_child.value().id),
                     direct_child_world_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         grandchild.value().id),
                     grandchild_world_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selection_before);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_bounds_near(bounds(harness, target.entity), target_bounds_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         direct_child.value().id),
                     direct_child_world_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         grandchild.value().id),
                     grandchild_world_before);

  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_vec3_near(world_origin(harness, target.entity),
                   kBlenderExpectedPivot);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         direct_child.value().id),
                     direct_child_world_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         grandchild.value().id),
                     grandchild_world_before);
}

/**
 * Enumerates atomic-rejection scenarios shared by the invalid-selection test.
 */
enum class InvalidCopyCase {
  HiddenTarget,
  LockedTarget,
  MissingTargetMesh,
  UnstableTargetTopology,
  HiddenSource,
  MissingSourceMesh,
  UnstableCpuOnlySource,
  SingularSourceTransform,
  SingularTargetTransform,
  ComponentMode,
};

void run_invalid_copy_case(InvalidCopyCase copy_case) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Invalid Case Source");
  const quader::QuaderModelingObjectRef valid_target =
      create_asymmetric_mesh(harness, "Valid Earlier Target", 0.8f);
  const quader::QuaderModelingObjectRef invalid_target =
      create_asymmetric_mesh(harness, "Invalid Later Target", 1.2f);
  set_transform(harness, source.entity,
                {.position = {7.0f, 3.0f, -2.0f},
                 .rotation_euler = {0.2f, -0.3f, 0.1f},
                 .custom_pivot = true});
  set_transform(harness, valid_target.entity,
                {.position = {-6.0f, 1.0f, 4.0f}});
  set_transform(harness, invalid_target.entity,
                {.position = {2.0f, -5.0f, 3.0f}});
  ASSERT_LT(valid_target.entity, invalid_target.entity);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, valid_target.entity,
                                   invalid_target.entity},
                                  source.entity)
                  .ok());

  switch (copy_case) {
  case InvalidCopyCase::HiddenTarget:
    ASSERT_TRUE(
        harness.session.scene().set_visible(invalid_target.entity, false));
    break;
  case InvalidCopyCase::LockedTarget:
    ASSERT_TRUE(
        harness.session.scene().set_locked(invalid_target.entity, true));
    break;
  case InvalidCopyCase::MissingTargetMesh: {
    quader::MeshComponent missing =
        *harness.session.scene().mesh(invalid_target.entity);
    missing.mesh = std::numeric_limits<quader::AssetId>::max();
    harness.session.scene().set_mesh(invalid_target.entity, missing);
    break;
  }
  case InvalidCopyCase::UnstableTargetTopology: {
    const quader::AssetId unstable_mesh =
        mesh_id(harness, invalid_target.entity);
    ASSERT_TRUE(harness.session.assets().update_mesh(
        unstable_mesh, [](quader::MeshAsset &mesh) {
          mesh.has_stable_cpu_topology = false;
        }));
    break;
  }
  case InvalidCopyCase::HiddenSource:
    ASSERT_TRUE(harness.session.scene().set_visible(source.entity, false));
    break;
  case InvalidCopyCase::MissingSourceMesh: {
    quader::MeshComponent missing =
        *harness.session.scene().mesh(source.entity);
    missing.mesh = std::numeric_limits<quader::AssetId>::max();
    harness.session.scene().set_mesh(source.entity, missing);
    break;
  }
  case InvalidCopyCase::UnstableCpuOnlySource: {
    const quader::AssetId unstable_mesh = mesh_id(harness, source.entity);
    ASSERT_TRUE(harness.session.assets().update_mesh(
        unstable_mesh, [](quader::MeshAsset &mesh) {
          mesh.authored_polygon_document.reset();
          mesh.has_authored_polygon_document = false;
          mesh.has_stable_cpu_topology = false;
        }));
    break;
  }
  case InvalidCopyCase::SingularSourceTransform: {
    quader::TransformComponent singular =
        *harness.session.scene().transform(source.entity);
    singular.scale.x = 0.0f;
    set_transform(harness, source.entity, singular);
    break;
  }
  case InvalidCopyCase::SingularTargetTransform: {
    quader::TransformComponent singular =
        *harness.session.scene().transform(invalid_target.entity);
    singular.scale.y = 0.0f;
    set_transform(harness, invalid_target.entity, singular);
    break;
  }
  case InvalidCopyCase::ComponentMode:
    ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Vertex)
                    .ok());
    break;
  }

  const quader::TransformComponent valid_transform_before =
      *harness.session.scene().transform(valid_target.entity);
  const quader::AssetId valid_mesh_before =
      mesh_id(harness, valid_target.entity);
  const std::vector<quader::QVec3> valid_vertices_before =
      world_vertices(harness, valid_target.entity);
  const std::size_t mesh_count_before = harness.session.assets().mesh_count();
  const std::vector<quader::EntityId> selected_before =
      harness.session.scene().selected_entities();
  harness.session.history().clear();

  EXPECT_FALSE(harness.editor.modeling().can_copy_pivot_from_active());
  const quader::QuaderApiVoidResult result =
      harness.editor.modeling().copy_pivot_from_active();
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_FALSE(result.error_message().empty());
  EXPECT_FALSE(harness.editor.history().can_undo());
  EXPECT_FALSE(harness.editor.history().can_redo());
  expect_transform_near(
      *harness.session.scene().transform(valid_target.entity),
      valid_transform_before);
  EXPECT_EQ(mesh_id(harness, valid_target.entity), valid_mesh_before);
  expect_vertices_near(world_vertices(harness, valid_target.entity),
                       valid_vertices_before);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), source.entity);
}

TEST(CopyPivotFromActiveTests,
     InvalidSourcesModesAndTargetsFailWithoutPartialMutationOrHistory) {
  constexpr std::array kCases{
      InvalidCopyCase::HiddenTarget,
      InvalidCopyCase::LockedTarget,
      InvalidCopyCase::MissingTargetMesh,
      InvalidCopyCase::UnstableTargetTopology,
      InvalidCopyCase::HiddenSource,
      InvalidCopyCase::MissingSourceMesh,
      InvalidCopyCase::UnstableCpuOnlySource,
      InvalidCopyCase::SingularSourceTransform,
      InvalidCopyCase::SingularTargetTransform,
      InvalidCopyCase::ComponentMode,
  };
  for (const InvalidCopyCase copy_case : kCases) {
    SCOPED_TRACE(static_cast<int>(copy_case));
    run_invalid_copy_case(copy_case);
  }
}

TEST(CopyPivotFromActiveTests,
     ActiveModelingPreviewBlocksCopyWithoutDiscardingThePreview) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_box_with_bounds(harness, "Preview Guard Source", {-1.0f, -1.0f, -1.0f},
                             {2.0f, 1.0f, 1.0f});
  const quader::QuaderModelingObjectRef target =
      create_box(harness, "Preview Guard Target");
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  harness.session.history().clear();

  const std::string preview_command =
      harness.editor.commands().command_id_for_modeling_action("half_cut");
  ASSERT_FALSE(preview_command.empty());
  const quader::QuaderApiVoidResult preview =
      harness.editor.commands().execute(preview_command);
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  ASSERT_TRUE(harness.editor.modeling().can_cancel_active_tool_or_preview());
  const quader::QuaderToolSettingsTarget settings_before =
      harness.editor.tools().active_settings();
  ASSERT_TRUE(settings_before.active);
  ASSERT_TRUE(settings_before.preview_active);
  EXPECT_EQ(settings_before.tool_id, "half_cut");
  const std::uint64_t scene_revision_before =
      harness.session.scene().revision();
  const std::uint64_t asset_revision_before =
      harness.session.assets().revision();
  const quader::QuaderHistorySnapshot history_before =
      harness.editor.query().history();

  EXPECT_FALSE(harness.editor.modeling().can_copy_pivot_from_active());
  std::optional<quader::QuaderCommandDescriptor> command =
      command_descriptor(harness);
  ASSERT_TRUE(command.has_value());
  EXPECT_FALSE(command->enabled);
  const quader::QuaderActionSet operations =
      harness.editor.modeling().available_action_set({
          .surface = quader::QuaderActionSurface::OperationsPanel,
          .include_disabled = true,
      });
  const quader::QuaderActionPanel *object_panel =
      find_panel(operations, "object_operations");
  ASSERT_NE(object_panel, nullptr);
  const quader::QuaderActionItem *action =
      find_action(*object_panel, "copy_pivot_from_active");
  ASSERT_NE(action, nullptr);
  EXPECT_EQ(action->visibility, quader::QuaderActionVisibility::Disabled);
  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  EXPECT_FALSE(copied.ok());
  EXPECT_FALSE(copied.changed());
  EXPECT_EQ(harness.session.scene().revision(), scene_revision_before);
  EXPECT_EQ(harness.session.assets().revision(), asset_revision_before);
  EXPECT_EQ(harness.editor.query().history().undo_label,
            history_before.undo_label);
  const quader::QuaderToolSettingsTarget settings_after =
      harness.editor.tools().active_settings();
  EXPECT_TRUE(settings_after.active);
  EXPECT_TRUE(settings_after.preview_active);
  EXPECT_EQ(settings_after.tool_id, "half_cut");

  ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
}

TEST(CopyPivotFromActiveTests,
     ActivePreviewRestoreFailureRollsBackDirectCopyWithoutHistory) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_box_with_bounds(harness, "Rollback Source", {-1.0f, -1.0f, -1.0f},
                             {2.0f, 1.0f, 1.0f});
  const quader::QuaderModelingObjectRef target =
      create_box(harness, "Rollback Target");
  const quader::QuaderModelingObjectRef unselected_sharer =
      create_box(harness, "Rollback Unselected Sharer");
  set_transform(harness, source.entity,
                {.position = {5.0f, 3.0f, -2.0f},
                 .rotation_euler = {0.2f, 0.3f, -0.4f},
                 .scale = {1.2f, 0.8f, 1.5f},
                 .custom_pivot = true});
  set_transform(harness, target.entity,
                {.position = {-4.0f, 2.0f, 1.0f},
                 .rotation_euler = {-0.1f, 0.25f, 0.2f},
                 .scale = {0.75f, 1.4f, 0.9f}});
  const quader::AssetId shared_mesh = mesh_id(harness, target.entity);
  quader::MeshComponent shared_component =
      *harness.session.scene().mesh(unselected_sharer.entity);
  shared_component.mesh = shared_mesh;
  ASSERT_TRUE(harness.session.scene().set_mesh(unselected_sharer.entity,
                                               shared_component));
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  harness.session.history().clear();

  const std::string preview_command =
      harness.editor.commands().command_id_for_modeling_action("half_cut");
  ASSERT_FALSE(preview_command.empty());
  const quader::QuaderApiVoidResult preview =
      harness.editor.commands().execute(preview_command);
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  const PivotRollbackState before =
      capture_pivot_rollback_state(harness, target.entity);
  const quader::test::CommandObservableState observable_before =
      quader::test::capture_command_observable_state(harness.editor);

  const quader::CommandResult copied =
      harness.session.pivot().copy_pivot_from_active(
          harness.session.selection());

  EXPECT_FALSE(copied.ok);
  EXPECT_FALSE(copied.changed);
  EXPECT_EQ(copied.message, "Commit or cancel the active preview first.");
  expect_pivot_rollback_state(harness, target.entity, before);
  const quader::test::CommandObservableStateDiff observable_difference =
      quader::test::diff_command_observable_state(
          observable_before,
          quader::test::capture_command_observable_state(harness.editor));
  EXPECT_TRUE(observable_difference.empty())
      << observable_difference.summary;
  EXPECT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  const quader::QuaderToolSettingsTarget settings =
      harness.editor.tools().active_settings();
  EXPECT_TRUE(settings.active);
  EXPECT_TRUE(settings.preview_active);
  EXPECT_EQ(settings.tool_id, "half_cut");

  ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
}

TEST(CopyPivotFromActiveTests,
     ActivePreviewRestoreFailureRollsBackUndoAndKeepsUndoRetryable) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_box_with_bounds(harness, "Undo Rollback Source", {-1.0f, -1.0f, -1.0f},
                             {2.0f, 1.0f, 1.0f});
  const quader::QuaderModelingObjectRef target =
      create_box(harness, "Undo Rollback Target");
  const quader::QuaderModelingObjectRef unselected_sharer =
      create_box(harness, "Undo Rollback Unselected Sharer");
  set_transform(harness, source.entity,
                {.position = {5.0f, 3.0f, -2.0f},
                 .rotation_euler = {0.2f, 0.3f, -0.4f},
                 .scale = {1.2f, 0.8f, 1.5f},
                 .custom_pivot = true});
  const quader::AssetId shared_mesh = mesh_id(harness, target.entity);
  quader::MeshComponent shared_component =
      *harness.session.scene().mesh(unselected_sharer.entity);
  shared_component.mesh = shared_mesh;
  ASSERT_TRUE(harness.session.scene().set_mesh(unselected_sharer.entity,
                                               shared_component));
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  harness.session.history().clear();
  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  const quader::AssetId cloned_mesh = mesh_id(harness, target.entity);
  ASSERT_NE(cloned_mesh, shared_mesh);

  const std::string preview_command =
      harness.editor.commands().command_id_for_modeling_action("half_cut");
  ASSERT_FALSE(preview_command.empty());
  const quader::QuaderApiVoidResult preview =
      harness.editor.commands().execute(preview_command);
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  const PivotRollbackState before =
      capture_pivot_rollback_state(harness, target.entity);
  const quader::test::CommandObservableState observable_before =
      quader::test::capture_command_observable_state(harness.editor);

  const quader::CommandResult undone = harness.session.history().undo();

  EXPECT_FALSE(undone.ok);
  EXPECT_FALSE(undone.changed);
  EXPECT_EQ(undone.message, "Commit or cancel the active preview first.");
  expect_pivot_rollback_state(harness, target.entity, before);
  const quader::test::CommandObservableStateDiff observable_difference =
      quader::test::diff_command_observable_state(
          observable_before,
          quader::test::capture_command_observable_state(harness.editor));
  EXPECT_TRUE(observable_difference.empty())
      << observable_difference.summary;
  EXPECT_EQ(mesh_id(harness, target.entity), before.mesh);
  EXPECT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  EXPECT_TRUE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.history().can_redo());
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(),
            "Copy Pivot From Active");
  const quader::QuaderToolSettingsTarget settings =
      harness.editor.tools().active_settings();
  EXPECT_TRUE(settings.active);
  EXPECT_TRUE(settings.preview_active);
  EXPECT_EQ(settings.tool_id, "half_cut");

  ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
  const quader::CommandResult retried = harness.session.history().undo();
  ASSERT_TRUE(retried.ok) << retried.message;
  ASSERT_TRUE(retried.changed);
  EXPECT_EQ(mesh_id(harness, target.entity), shared_mesh);
  EXPECT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_TRUE(harness.session.history().can_redo());
}

TEST(CopyPivotFromActiveTests,
     ActivePreviewRestoreFailureRollsBackRedoAndKeepsRedoRetryable) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_box_with_bounds(harness, "Redo Rollback Source", {-1.0f, -1.0f, -1.0f},
                             {2.0f, 1.0f, 1.0f});
  const quader::QuaderModelingObjectRef target =
      create_box(harness, "Redo Rollback Target");
  const quader::QuaderModelingObjectRef unselected_sharer =
      create_box(harness, "Redo Rollback Unselected Sharer");
  set_transform(harness, source.entity,
                {.position = {5.0f, 3.0f, -2.0f},
                 .rotation_euler = {0.2f, 0.3f, -0.4f},
                 .scale = {1.2f, 0.8f, 1.5f},
                 .custom_pivot = true});
  const quader::AssetId shared_mesh = mesh_id(harness, target.entity);
  quader::MeshComponent shared_component =
      *harness.session.scene().mesh(unselected_sharer.entity);
  shared_component.mesh = shared_mesh;
  ASSERT_TRUE(harness.session.scene().set_mesh(unselected_sharer.entity,
                                               shared_component));
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  harness.session.history().clear();
  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  const quader::AssetId cloned_mesh = mesh_id(harness, target.entity);
  ASSERT_NE(cloned_mesh, shared_mesh);
  const quader::CommandResult undone = harness.session.history().undo();
  ASSERT_TRUE(undone.ok) << undone.message;
  ASSERT_TRUE(undone.changed);
  ASSERT_EQ(mesh_id(harness, target.entity), shared_mesh);

  const std::string preview_command =
      harness.editor.commands().command_id_for_modeling_action("half_cut");
  ASSERT_FALSE(preview_command.empty());
  const quader::QuaderApiVoidResult preview =
      harness.editor.commands().execute(preview_command);
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  const PivotRollbackState before =
      capture_pivot_rollback_state(harness, target.entity);
  const quader::test::CommandObservableState observable_before =
      quader::test::capture_command_observable_state(harness.editor);

  const quader::CommandResult redone = harness.session.history().redo();

  EXPECT_FALSE(redone.ok);
  EXPECT_FALSE(redone.changed);
  EXPECT_EQ(redone.message, "Commit or cancel the active preview first.");
  expect_pivot_rollback_state(harness, target.entity, before);
  const quader::test::CommandObservableStateDiff observable_difference =
      quader::test::diff_command_observable_state(
          observable_before,
          quader::test::capture_command_observable_state(harness.editor));
  EXPECT_TRUE(observable_difference.empty())
      << observable_difference.summary;
  EXPECT_EQ(mesh_id(harness, target.entity), before.mesh);
  EXPECT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_TRUE(harness.session.history().can_redo());
  ASSERT_NE(harness.session.history().redo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().redo_label(),
            "Copy Pivot From Active");
  const quader::QuaderToolSettingsTarget settings =
      harness.editor.tools().active_settings();
  EXPECT_TRUE(settings.active);
  EXPECT_TRUE(settings.preview_active);
  EXPECT_EQ(settings.tool_id, "half_cut");

  ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
  const quader::CommandResult retried = harness.session.history().redo();
  ASSERT_TRUE(retried.ok) << retried.message;
  ASSERT_TRUE(retried.changed);
  EXPECT_EQ(mesh_id(harness, target.entity), cloned_mesh);
  EXPECT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  EXPECT_TRUE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.history().can_redo());
}

TEST(CopyPivotFromActiveTests,
     TypedCommandAndNativeActionsExposeExactAvailabilityAndOrdering) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Action Source");
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());

  std::optional<quader::QuaderCommandDescriptor> command =
      command_descriptor(harness);
  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(command->label, "Copy Pivot From Active");
  EXPECT_TRUE(command->shortcut.empty());
  EXPECT_FALSE(command->enabled);
  EXPECT_EQ(
      harness.editor.commands().command_id_for_modeling_action(
          "copy_pivot_from_active"),
      quader::quader_command::kModelingCopyPivotFromActive.string());

  quader::QuaderActionSet operations =
      harness.editor.modeling().available_action_set({
          .surface = quader::QuaderActionSurface::OperationsPanel,
          .include_disabled = true,
      });
  const quader::QuaderActionPanel *object_panel =
      find_panel(operations, "object_operations");
  ASSERT_NE(object_panel, nullptr);
  ASSERT_FALSE(object_panel->actions.empty());
  const quader::QuaderActionItem *copy_action =
      find_action(*object_panel, "copy_pivot_from_active");
  ASSERT_NE(copy_action, nullptr);
  const quader::QuaderActionItem &disabled = *copy_action;
  EXPECT_EQ(disabled.action_id, "copy_pivot_from_active");
  EXPECT_TRUE(disabled.operation_id.empty());
  EXPECT_EQ(disabled.label, "Copy Pivot From Active");
  EXPECT_EQ(disabled.kind, quader::QuaderActionKind::Operation);
  EXPECT_EQ(disabled.visibility, quader::QuaderActionVisibility::Disabled);
  EXPECT_EQ(disabled.disabled_reason_key,
            "modeling.action.disabled.copy_pivot_from_active_selection");

  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Action Target");
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  command = command_descriptor(harness);
  ASSERT_TRUE(command.has_value());
  EXPECT_TRUE(command->enabled);
  EXPECT_TRUE(harness.editor.modeling().can_copy_pivot_from_active());

  operations = harness.editor.modeling().available_action_set({
      .surface = quader::QuaderActionSurface::OperationsPanel,
      .include_disabled = true,
  });
  object_panel = find_panel(operations, "object_operations");
  ASSERT_NE(object_panel, nullptr);
  ASSERT_FALSE(object_panel->actions.empty());
  copy_action = find_action(*object_panel, "copy_pivot_from_active");
  ASSERT_NE(copy_action, nullptr);
  EXPECT_EQ(copy_action->visibility,
            quader::QuaderActionVisibility::Enabled);

  const quader::QuaderActionSet context =
      harness.editor.modeling().available_action_set({
          .surface = quader::QuaderActionSurface::ViewportContextMenu,
          .include_disabled = true,
      });
  const quader::QuaderActionPanel *context_panel =
      find_panel(context, "viewport_context_menu");
  ASSERT_NE(context_panel, nullptr);
  const auto copy = std::find_if(
      context_panel->actions.begin(), context_panel->actions.end(),
      [](const quader::QuaderActionItem &action) {
        return action.action_id == "copy_pivot_from_active";
      });
  ASSERT_NE(copy, context_panel->actions.end());
  ASSERT_NE(std::next(copy), context_panel->actions.end());
  EXPECT_EQ(std::next(copy)->action_id, "set_origin_to_center");

  ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Vertex)
                  .ok());
  EXPECT_FALSE(harness.editor.modeling().can_copy_pivot_from_active());
  command = command_descriptor(harness);
  ASSERT_TRUE(command.has_value());
  EXPECT_FALSE(command->enabled);
  operations = harness.editor.modeling().available_action_set({
      .surface = quader::QuaderActionSurface::OperationsPanel,
      .include_disabled = true,
      .include_hidden = true,
  });
  object_panel = find_panel(operations, "object_operations");
  ASSERT_NE(object_panel, nullptr);
  const quader::QuaderActionItem *hidden =
      find_action(*object_panel, "copy_pivot_from_active");
  ASSERT_NE(hidden, nullptr);
  EXPECT_EQ(hidden->visibility, quader::QuaderActionVisibility::Hidden);
}

TEST(MeshOriginPlacementTests,
     CenterUsesLocalSurfaceAreaForAuthoredMeshUnderReflectedScale) {
  CopyPivotHarness harness;
  const quader::EntityId target =
      create_asymmetric_mesh(harness, "Area Center").entity;
  const quader::TransformComponent transform{
      .position = {7.0f, -4.0f, 2.0f},
      .rotation_euler = {0.3f, -0.4f, 0.2f},
      .scale = {-2.0f, 0.5f, 3.0f}};
  set_transform(harness, target, transform);
  ASSERT_TRUE(harness.editor.selection().select_objects({target}, target).ok());
  // The two right triangles have local areas 4 and 6. Their weighted
  // centroid differs from both the vertex mean and the bounds center.
  constexpr quader::QVec3 kLocalCenter{4.0f / 3.0f, 4.0f / 15.0f, 0.6f};
  const quader::QVec3 expected = quader::transform_point(
      harness.session.scene().world_transform_matrix(target), kLocalCenter);
  const auto vertices_before = world_vertices(harness, target);
  const auto authored_before = authored_world_vertices(harness, target);
  harness.session.history().clear();

  ASSERT_TRUE(harness.editor.modeling().can_set_origin_to_center());
  const auto result = harness.editor.modeling().set_origin_to_center();
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.changed());
  expect_vec3_near(world_origin(harness, target), expected);
  EXPECT_TRUE(harness.session.scene().transform(target)->custom_pivot);
  expect_vec3_near(harness.session.scene().transform(target)->rotation_euler,
                   transform.rotation_euler);
  expect_vec3_near(harness.session.scene().transform(target)->scale,
                   transform.scale);
  expect_vertices_near(world_vertices(harness, target), vertices_before);
  expect_vertices_near(authored_world_vertices(harness, target),
                       authored_before);

  const PivotRollbackState after =
      capture_pivot_rollback_state(harness, target);
  const auto repeated = harness.editor.modeling().set_origin_to_center();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.changed());
  expect_pivot_rollback_state(harness, target, after);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_transform_near(*harness.session.scene().transform(target), transform);
  expect_vertices_near(world_vertices(harness, target), vertices_before);
  EXPECT_FALSE(harness.editor.history().can_undo());
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_vec3_near(world_origin(harness, target), expected);
  expect_vertices_near(world_vertices(harness, target), vertices_before);
}

TEST(MeshOriginPlacementTests, CenterOfConcavePolygonUsesItsActualSurface) {
  CopyPivotHarness harness;
  quader_poly::Document document;
  // A 4x1 rectangle joined to a 1x2 rectangle has area 6 and centroid
  // (1.5, 1). Starting at this corner also exercises a non-star-shaped fan.
  const std::array<quader::QVec3, 6> positions{{{4.0f, 1.0f, 0.0f},
                                                {1.0f, 1.0f, 0.0f},
                                                {1.0f, 3.0f, 0.0f},
                                                {0.0f, 3.0f, 0.0f},
                                                {0.0f, 0.0f, 0.0f},
                                                {4.0f, 0.0f, 0.0f}}};
  std::vector<quader_poly::ElementId> vertices;
  for (const auto position : positions) {
    vertices.push_back(quader_poly::add_vertex(document, position));
  }
  ASSERT_NE(quader_poly::add_face(document, vertices),
            quader_poly::kInvalidElementId);
  const auto created = harness.editor.modeling().create_mesh(
      quader::QuaderPolygonDocumentInternalAccess::from_backend(document),
      {.name = "Concave Center"});
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(harness.editor.selection().select_object(created.value()).ok());
  const auto before = authored_world_vertices(harness, created.value().entity);

  const auto result = harness.editor.modeling().set_origin_to_center();

  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_TRUE(result.changed());
  expect_vec3_near(world_origin(harness, created.value().entity),
                   {1.5f, 1.0f, 0.0f});
  expect_vertices_near(authored_world_vertices(harness, created.value().entity),
                       before);
}

TEST(MeshOriginPlacementTests, LegacyCpuMeshesRejectWithoutPartialMutation) {
  for (const bool degenerate : {false, true}) {
    SCOPED_TRACE(degenerate);
    CopyPivotHarness harness;
    const auto valid = create_asymmetric_mesh(harness, "Earlier Authored Mesh");
    const auto target = create_plain_cpu_mesh(harness, "Legacy CPU Mesh");
    if (degenerate) {
      ASSERT_TRUE(harness.session.assets().update_mesh(
          mesh_id(harness, target), [](quader::MeshAsset &mesh) {
            auto &geometry = mesh.edit_geometry();
            geometry.vertices[0].position = {0.0f, 0.0f, 0.0f};
            geometry.vertices[1].position = {2.0f, 0.0f, 0.0f};
            geometry.vertices[2].position = {8.0f, 0.0f, 0.0f};
            geometry.vertices[3].position = {10.0f, 0.0f, 0.0f};
          }));
    }
    ASSERT_TRUE(harness.editor.selection()
                    .select_objects({valid.entity, target}, valid.entity)
                    .ok());
    harness.session.history().clear();
    const auto valid_before =
        capture_pivot_rollback_state(harness, valid.entity);
    const auto target_before = capture_pivot_rollback_state(harness, target);
    EXPECT_FALSE(harness.editor.modeling().can_set_origin_to_center());
    EXPECT_FALSE(harness.editor.modeling().can_set_origin_to_pivot());
    for (const bool center : {true, false}) {
      const auto result = center
                              ? harness.editor.modeling().set_origin_to_center()
                              : harness.editor.modeling().set_origin_to_pivot();
      EXPECT_FALSE(result.ok());
      EXPECT_FALSE(result.changed());
      EXPECT_FALSE(result.error_message().empty());
      expect_pivot_rollback_state(harness, valid.entity, valid_before);
      expect_pivot_rollback_state(harness, target, target_before);
    }
  }
}

TEST(MeshOriginPlacementTests, CoincidentOriginAuthorsCustomPivotOnlyOnce) {
  for (const bool center : {true, false}) {
    SCOPED_TRACE(center);
    CopyPivotHarness harness;
    const auto target = create_box(harness, "Already Centered");
    ASSERT_TRUE(harness.editor.selection().select_object(target).ok());
    harness.session.history().clear();
    const auto place = [&] {
      return center ? harness.editor.modeling().set_origin_to_center()
                    : harness.editor.modeling().set_origin_to_pivot();
    };

    const auto first = place();
    ASSERT_TRUE(first.ok()) << first.error_message();
    EXPECT_TRUE(first.changed());
    EXPECT_TRUE(harness.session.scene().transform(target.entity)->custom_pivot);
    const PivotRollbackState after =
        capture_pivot_rollback_state(harness, target.entity);
    const auto repeated = place();
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.changed());
    expect_pivot_rollback_state(harness, target.entity, after);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_FALSE(
        harness.session.scene().transform(target.entity)->custom_pivot);
    EXPECT_FALSE(harness.editor.history().can_undo());
  }
}

TEST(MeshOriginPlacementTests,
     CoincidentCenterKeepsChildTransformAndMeshPayloadExactlyUnchanged) {
  CopyPivotHarness harness;
  const auto parent = create_box(harness, "Centered Parent");
  const auto child = create_box(harness, "Preserved Child");
  set_transform(harness, parent.entity,
                {.position = {3.0f, -7.0f, 5.0f},
                 .rotation_euler = {0.37f, -0.29f, 0.53f},
                 .scale = {-1.3f, 0.7f, 2.1f}});
  set_transform(harness, child.entity,
                {.position = {-2.3f, 4.7f, 1.9f},
                 .rotation_euler = {-0.21f, 0.63f, -0.47f},
                 .scale = {0.8f, 1.4f, 0.6f}});
  ASSERT_TRUE(
      harness.editor.scene().reparent(child.entity, parent.entity, false).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(parent).ok());
  const auto child_before = *harness.session.scene().transform(child.entity);
  const auto child_world =
      harness.session.scene().world_transform_matrix(child.entity);
  const auto mesh_before = quader::clone_mesh_asset_payload(
      *harness.session.assets().mesh(mesh_id(harness, parent.entity)));
  harness.session.history().clear();

  const auto result = harness.editor.modeling().set_origin_to_center();

  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_TRUE(result.changed());
  EXPECT_TRUE(harness.session.scene().transform(parent.entity)->custom_pivot);
  const auto expect_exact_preservation = [&] {
    const auto &after = *harness.session.scene().transform(child.entity);
    EXPECT_EQ(after.position.x, child_before.position.x);
    EXPECT_EQ(after.position.y, child_before.position.y);
    EXPECT_EQ(after.position.z, child_before.position.z);
    EXPECT_EQ(after.rotation_euler.x, child_before.rotation_euler.x);
    EXPECT_EQ(after.rotation_euler.y, child_before.rotation_euler.y);
    EXPECT_EQ(after.rotation_euler.z, child_before.rotation_euler.z);
    EXPECT_EQ(after.scale.x, child_before.scale.x);
    EXPECT_EQ(after.scale.y, child_before.scale.y);
    EXPECT_EQ(after.scale.z, child_before.scale.z);
    EXPECT_EQ(after.custom_pivot, child_before.custom_pivot);
    EXPECT_EQ(after.parenting_offset.values,
              child_before.parenting_offset.values);
    EXPECT_EQ(
        harness.session.scene().world_transform_matrix(child.entity).values,
        child_world.values);
    EXPECT_TRUE(quader::mesh_asset_payload_equal(
        *harness.session.assets().mesh(mesh_id(harness, parent.entity)),
        mesh_before));
  };
  expect_exact_preservation();
  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_exact_preservation();
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_exact_preservation();
}

TEST(MeshOriginPlacementTests,
     PivotSnapshotsSharedVisiblePivotForAllSelectedMeshes) {
  CopyPivotHarness harness;
  const auto first = create_box(harness, "Left Mesh");
  const auto second = create_box_with_bounds(
      harness, "Right Mesh", {0.0f, -1.0f, -1.0f}, {4.0f, 3.0f, 1.0f});
  set_transform(harness, first.entity, {.position = {-6.0f, 0.0f, 0.0f}});
  set_transform(harness, second.entity, {.position = {10.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({first.entity, second.entity}, first.entity)
                  .ok());
  constexpr quader::QVec3 kSharedPivot{3.5f, 1.0f, 0.0f};
  const auto visible_pivot = harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(visible_pivot.has_value());
  expect_vec3_near(*visible_pivot, kSharedPivot);
  const auto first_before = world_vertices(harness, first.entity);
  const auto second_before = world_vertices(harness, second.entity);
  const auto selected_before = harness.session.scene().selected_entities();
  harness.session.history().clear();

  ASSERT_TRUE(harness.editor.modeling().can_set_origin_to_pivot());
  const auto result = harness.editor.commands().execute(
      quader::quader_command::kModelingSetOriginToPivot);

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.changed());
  expect_vec3_near(world_origin(harness, first.entity), kSharedPivot);
  expect_vec3_near(world_origin(harness, second.entity), kSharedPivot);
  expect_vertices_near(world_vertices(harness, first.entity), first_before);
  expect_vertices_near(world_vertices(harness, second.entity), second_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), first.entity);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_vec3_near(world_origin(harness, first.entity), {-6.0f, 0.0f, 0.0f});
  expect_vec3_near(world_origin(harness, second.entity), {10.0f, 0.0f, 0.0f});
  EXPECT_FALSE(harness.editor.history().can_undo());
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_vec3_near(world_origin(harness, first.entity), kSharedPivot);
  expect_vec3_near(world_origin(harness, second.entity), kSharedPivot);
}

TEST(MeshOriginPlacementTests,
     CenterAndQuickPivotPreserveSharedHierarchyUvsAndMaterialsThroughHistory) {
  for (const bool center : {true, false}) {
    SCOPED_TRACE(center);
    CopyPivotHarness harness;
    const auto child = create_textured_asymmetric_mesh(harness, 3u);
    const auto parent = create_asymmetric_mesh(harness, "Selected Parent");
    const auto sharer = create_box(harness, "Unselected Shared Mesh");
    const auto descendant = create_box(harness, "Unselected Descendant");
    ASSERT_TRUE(harness.editor.scene()
                    .reparent(child.entity, parent.entity, false)
                    .ok());
    ASSERT_TRUE(harness.editor.scene()
                    .reparent(descendant.entity, child.entity, false)
                    .ok());
    set_transform(harness, parent.entity,
                  {.position = {5.0f, -2.0f, 3.0f},
                   .rotation_euler = {0.2f, -0.3f, 0.4f},
                   .scale = {-1.5f, 0.7f, 2.0f}});
    set_transform(harness, child.entity,
                  {.position = {-2.0f, 4.0f, 1.0f},
                   .rotation_euler = {-0.4f, 0.1f, 0.3f},
                   .scale = {0.8f, 1.2f, 0.6f}});
    const auto shared_mesh = mesh_id(harness, child.entity);
    for (const auto entity : {parent.entity, sharer.entity}) {
      auto component = *harness.session.scene().mesh(entity);
      component.mesh = shared_mesh;
      ASSERT_TRUE(harness.session.scene().set_mesh(entity, component));
    }
    const quader::TransformComponent mapping_transform{
        .position = {0.3f, -1.0f, 0.5f},
        .rotation_euler = {0.3f, 1.2f, -0.4f},
        .scale = {1.3f, 0.7f, -1.1f}};
    const auto mapping =
        quader::compose_transform(mapping_transform.spatial_transform());
    ASSERT_TRUE(harness.session.scene().set_texture_mapping(
        child.entity, {.sampling_from_local = mapping}));
    ASSERT_TRUE(harness.editor.selection()
                    .select_objects({child.entity, parent.entity}, child.entity)
                    .ok());
    constexpr quader::QVec3 kQuickPivot{19.0f, -11.0f, 7.0f};
    harness.session.pivot().begin_quick(kQuickPivot);
    harness.session.pivot().end_quick_edit();
    const auto quick_before =
        harness.session.pivot().selection_pivot_override();
    ASSERT_TRUE(quick_before.valid);
    constexpr quader::QVec3 kLocalCenter{4.0f / 3.0f, 4.0f / 15.0f, 0.6f};
    const std::array selected{child.entity, parent.entity};
    std::array<quader::QVec3, 2> expected;
    std::array<quader::QMat4, 2> world_before;
    std::array<std::vector<quader::QVec3>, 2> vertices_before;
    for (std::size_t index = 0; index < selected.size(); ++index) {
      world_before[index] =
          harness.session.scene().world_transform_matrix(selected[index]);
      expected[index] =
          center ? quader::transform_point(world_before[index], kLocalCenter)
                 : kQuickPivot;
      vertices_before[index] = world_vertices(harness, selected[index]);
    }
    const auto shared_before = quader::clone_mesh_asset_payload(
        *harness.session.assets().mesh(shared_mesh));
    const auto child_uvs_before = effective_uvs(shared_before, mapping);
    const auto parent_uvs_before = effective_uvs(shared_before, {});
    const auto descendant_world =
        harness.session.scene().world_transform_matrix(descendant.entity);
    const auto sharer_before = world_vertices(harness, sharer.entity);
    const auto selected_before = harness.session.scene().selected_entities();
    const auto mesh_count_before = harness.session.assets().mesh_count();
    harness.session.history().clear();

    const auto placed = harness.editor.commands().execute(
        center ? quader::quader_command::kModelingSetOriginToCenter
               : quader::quader_command::kModelingSetOriginToPivot);
    ASSERT_TRUE(placed.ok()) << placed.error_message();
    ASSERT_TRUE(placed.changed());
    const std::array placed_meshes{mesh_id(harness, child.entity),
                                   mesh_id(harness, parent.entity)};
    EXPECT_NE(placed_meshes[0], shared_mesh);
    EXPECT_NE(placed_meshes[1], shared_mesh);
    EXPECT_NE(placed_meshes[0], placed_meshes[1]);
    EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before + 2u);
    const auto expect_preserved = [&] {
      for (std::size_t index = 0; index < selected.size(); ++index) {
        expect_vec3_near(world_origin(harness, selected[index]),
                         expected[index]);
        expect_vertices_near(world_vertices(harness, selected[index]),
                             vertices_before[index]);
        const auto *asset =
            harness.session.assets().mesh(mesh_id(harness, selected[index]));
        ASSERT_NE(asset, nullptr);
        ASSERT_NE(asset->authored_polygon_document, nullptr);
        ASSERT_EQ(asset->authored_polygon_document->faces.size(),
                  shared_before.authored_polygon_document->faces.size());
        for (std::size_t face = 0;
             face < asset->authored_polygon_document->faces.size(); ++face) {
          EXPECT_EQ(asset->authored_polygon_document->faces[face].material_slot,
                    shared_before.authored_polygon_document->faces[face]
                        .material_slot);
          EXPECT_EQ(
              asset->authored_polygon_document->faces[face].texture_transform,
              shared_before.authored_polygon_document->faces[face]
                  .texture_transform);
        }
        const auto *sampling =
            harness.session.scene().texture_mapping(selected[index]);
        expect_uvs_near(
            effective_uvs(*asset, sampling == nullptr
                                      ? quader::QMat4{}
                                      : sampling->sampling_from_local),
            index == 0u ? child_uvs_before : parent_uvs_before);
      }
      expect_matrix_near(
          harness.session.scene().world_transform_matrix(descendant.entity),
          descendant_world);
      expect_vertices_near(world_vertices(harness, sharer.entity),
                           sharer_before);
      EXPECT_EQ(mesh_id(harness, sharer.entity), shared_mesh);
      EXPECT_TRUE(quader::mesh_asset_payload_equal(
          *harness.session.assets().mesh(shared_mesh), shared_before));
      EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
      EXPECT_EQ(harness.session.scene().primary_selection(), child.entity);
    };
    expect_preserved();
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_FALSE(harness.editor.history().can_undo());
    EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before);
    for (std::size_t index = 0; index < selected.size(); ++index) {
      EXPECT_EQ(mesh_id(harness, selected[index]), shared_mesh);
      expect_matrix_near(
          harness.session.scene().world_transform_matrix(selected[index]),
          world_before[index]);
    }
    ASSERT_TRUE(harness.editor.history().redo().ok());
    EXPECT_EQ(mesh_id(harness, child.entity), placed_meshes[0]);
    EXPECT_EQ(mesh_id(harness, parent.entity), placed_meshes[1]);
    expect_preserved();
  }
}

TEST(MeshOriginPlacementTests, InvalidSelectionRejectsBothActionsAtomically) {
  /// Enumerates invalid selections that must never partially rebase a batch.
  enum class InvalidSelection {
    Empty,
    Mixed,
    Locked,
    Hidden,
    Singular,
    Component,
    MissingMesh,
    UnstableMesh,
    InstanceDefinitionMember,
    InstanceOccurrence,
    ModelingPreview,
    PersistentPivot,
  };
  constexpr std::array kCases{InvalidSelection::Empty,
                              InvalidSelection::Mixed,
                              InvalidSelection::Locked,
                              InvalidSelection::Hidden,
                              InvalidSelection::Singular,
                              InvalidSelection::Component,
                              InvalidSelection::MissingMesh,
                              InvalidSelection::UnstableMesh,
                              InvalidSelection::InstanceDefinitionMember,
                              InvalidSelection::InstanceOccurrence,
                              InvalidSelection::ModelingPreview,
                              InvalidSelection::PersistentPivot};
  for (const auto invalid_case : kCases) {
    SCOPED_TRACE(static_cast<int>(invalid_case));
    CopyPivotHarness harness;
    const auto valid = create_box(harness, "Valid Earlier Target");
    const auto invalid = create_box(harness, "Invalid Later Target");
    set_transform(harness, invalid.entity, {.position = {9.0f, 2.0f, 1.0f}});
    ASSERT_TRUE(
        harness.editor.selection()
            .select_objects({valid.entity, invalid.entity}, valid.entity)
            .ok());
    switch (invalid_case) {
    case InvalidSelection::Empty:
      harness.session.scene().clear_selection();
      break;
    case InvalidSelection::Mixed: {
      const auto empty = harness.editor.scene().create_entity(
          {.name = "Selected Empty", .kind = quader::QuaderEntityKind::Empty});
      ASSERT_TRUE(empty.ok());
      ASSERT_TRUE(
          harness.editor.selection()
              .select_objects({valid.entity, empty.value().id}, valid.entity)
              .ok());
      break;
    }
    case InvalidSelection::Locked:
      ASSERT_TRUE(harness.session.scene().set_locked(invalid.entity, true));
      break;
    case InvalidSelection::Hidden:
      ASSERT_TRUE(harness.session.scene().set_visible(invalid.entity, false));
      break;
    case InvalidSelection::Singular:
      set_transform(harness, invalid.entity, {.scale = {1.0f, 0.0f, 1.0f}});
      break;
    case InvalidSelection::Component:
      ASSERT_TRUE(
          harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
      ASSERT_TRUE(harness.editor.selection()
                      .set_mode(quader::QuaderSelectionMode::Vertex)
                      .ok());
      break;
    case InvalidSelection::MissingMesh: {
      auto component = *harness.session.scene().mesh(invalid.entity);
      component.mesh = std::numeric_limits<quader::AssetId>::max();
      ASSERT_TRUE(harness.session.scene().set_mesh(invalid.entity, component));
      break;
    }
    case InvalidSelection::UnstableMesh:
      ASSERT_TRUE(harness.session.assets().update_mesh(
          mesh_id(harness, invalid.entity), [](quader::MeshAsset &mesh) {
            mesh.has_stable_cpu_topology = false;
          }));
      break;
    case InvalidSelection::InstanceDefinitionMember: {
      const auto definition =
          harness.session.scene().create_entity("Definition");
      ASSERT_TRUE(harness.session.scene().set_parent(invalid.entity, definition,
                                                     false));
      ASSERT_TRUE(harness.session.scene().set_instance_definition(definition));
      break;
    }
    case InvalidSelection::InstanceOccurrence: {
      const auto definition =
          harness.session.scene().create_entity("Definition");
      ASSERT_TRUE(harness.session.scene().set_parent(invalid.entity, definition,
                                                     false));
      ASSERT_TRUE(harness.session.scene().set_instance_definition(definition));
      const auto occurrence = harness.session.scene().create_entity(
          "Occurrence", quader::EntityKind::Instance);
      ASSERT_TRUE(harness.session.scene().set_instance_occurrence(
          occurrence, {.definition = definition}));
      ASSERT_TRUE(harness.editor.selection()
                      .select_objects({valid.entity, occurrence}, valid.entity)
                      .ok());
      break;
    }
    case InvalidSelection::ModelingPreview: {
      const auto preview = harness.editor.commands().execute(
          harness.editor.commands().command_id_for_modeling_action("half_cut"));
      ASSERT_TRUE(preview.ok()) << preview.error_message();
      break;
    }
    case InvalidSelection::PersistentPivot:
      ASSERT_TRUE(harness.editor.selection().select_object(valid).ok());
      ASSERT_TRUE(
          harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok());
      harness.session.pivot().set_persistent_preview_position(
          {8.0f, 3.0f, 2.0f});
      break;
    }
    harness.session.history().clear();
    const auto before = capture_pivot_rollback_state(harness, valid.entity);
    const auto observable_before =
        quader::test::capture_command_observable_state(harness.editor);
    EXPECT_FALSE(harness.editor.modeling().can_set_origin_to_center());
    EXPECT_FALSE(harness.editor.modeling().can_set_origin_to_pivot());
    for (const bool center : {true, false}) {
      const auto result = center
                              ? harness.editor.modeling().set_origin_to_center()
                              : harness.editor.modeling().set_origin_to_pivot();
      EXPECT_FALSE(result.ok());
      EXPECT_FALSE(result.changed());
      EXPECT_FALSE(result.error_message().empty());
      expect_pivot_rollback_state(harness, valid.entity, before);
      const auto difference = quader::test::diff_command_observable_state(
          observable_before,
          quader::test::capture_command_observable_state(harness.editor));
      EXPECT_TRUE(difference.empty()) << difference.summary;
    }
  }
}

TEST(MeshOriginPlacementTests,
     FailedUndoDuringPreviewRestoresStateAndRemainsRetryable) {
  for (const bool center : {true, false}) {
    SCOPED_TRACE(center);
    CopyPivotHarness harness;
    const auto target = create_box_with_bounds(
        harness, "Undo Target", {0.0f, 0.0f, 0.0f}, {4.0f, 2.0f, 3.0f});
    const auto sharer = create_box(harness, "Unselected Sharer");
    const auto shared_mesh = mesh_id(harness, target.entity);
    auto component = *harness.session.scene().mesh(sharer.entity);
    component.mesh = shared_mesh;
    ASSERT_TRUE(harness.session.scene().set_mesh(sharer.entity, component));
    ASSERT_TRUE(harness.editor.selection().select_object(target).ok());
    harness.session.history().clear();
    const auto placed = center
                            ? harness.editor.modeling().set_origin_to_center()
                            : harness.editor.modeling().set_origin_to_pivot();
    ASSERT_TRUE(placed.ok()) << placed.error_message();
    ASSERT_TRUE(placed.changed());
    const auto cloned_mesh = mesh_id(harness, target.entity);
    ASSERT_NE(cloned_mesh, shared_mesh);
    const auto preview = harness.editor.commands().execute(
        harness.editor.commands().command_id_for_modeling_action("half_cut"));
    ASSERT_TRUE(preview.ok()) << preview.error_message();
    const auto before = capture_pivot_rollback_state(harness, target.entity);
    const auto observable_before =
        quader::test::capture_command_observable_state(harness.editor);

    const auto failed = harness.session.history().undo();

    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(failed.message, "Commit or cancel the active preview first.");
    expect_pivot_rollback_state(harness, target.entity, before);
    const auto difference = quader::test::diff_command_observable_state(
        observable_before,
        quader::test::capture_command_observable_state(harness.editor));
    EXPECT_TRUE(difference.empty()) << difference.summary;
    EXPECT_TRUE(harness.session.history().can_undo());
    EXPECT_FALSE(harness.session.history().can_redo());
    ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_EQ(mesh_id(harness, target.entity), shared_mesh);
    EXPECT_FALSE(harness.editor.history().can_undo());
    ASSERT_TRUE(harness.editor.history().redo().ok());
    EXPECT_EQ(mesh_id(harness, target.entity), cloned_mesh);
    EXPECT_EQ(mesh_id(harness, sharer.entity), shared_mesh);
  }
}

TEST(PivotPerformanceTests,
     CopyBatchBuildsOneHierarchyAndUsesSceneMeshReferenceIndex) {
  constexpr std::size_t kTargetCount = 24u;
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Scale Source", 2.0f);
  set_transform(harness, source.entity,
                {.position = {8.0f, 3.0f, -5.0f}, .custom_pivot = true});

  std::vector<quader::QuaderModelingObjectRef> targets;
  targets.reserve(kTargetCount);
  for (std::size_t index = 0u; index < kTargetCount; ++index) {
    targets.push_back(
        create_box(harness, "Scale Target " + std::to_string(index)));
    set_transform(harness, targets.back().entity,
                  {.position = {static_cast<float>(index) * 3.0f,
                                static_cast<float>(index % 5u),
                                -static_cast<float>(index % 7u)}});
    if (index > 0u) {
      ASSERT_TRUE(
          harness.editor.scene()
              .reparent(targets[index].entity, targets[index - 1u].entity, true)
              .ok());
    }
  }

  const quader::AssetId shared_mesh =
      mesh_id(harness, targets.front().entity);
  for (std::size_t index = 1u; index < targets.size(); ++index) {
    quader::MeshComponent component =
        *harness.session.scene().mesh(targets[index].entity);
    component.mesh = shared_mesh;
    harness.session.scene().set_mesh(targets[index].entity, component);
  }

  std::vector<quader::EntityId> selected{source.entity};
  selected.reserve(kTargetCount + 1u);
  std::vector<quader::QVec3> origins_before;
  std::vector<std::vector<quader::QVec3>> vertices_before;
  origins_before.reserve(kTargetCount);
  vertices_before.reserve(kTargetCount);
  for (const quader::QuaderModelingObjectRef target : targets) {
    selected.push_back(target.entity);
    origins_before.push_back(world_origin(harness, target.entity));
    vertices_before.push_back(world_vertices(harness, target.entity));
  }
  ASSERT_TRUE(
      harness.editor.selection().select_objects(selected, source.entity).ok());
  const std::vector<quader::EntityId> selection_before =
      harness.session.scene().selected_entities();
  const std::size_t entity_count =
      harness.session.scene().entities().size();
  const std::size_t mesh_count_before = harness.session.assets().mesh_count();
  const quader::PivotPlanningStats planning_before =
      harness.session.pivot().planning_stats_for_tests();
  const quader::ModelingSdkProjectionStats projection_before =
      harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());

  const quader::PivotPlanningStats planning_after =
      harness.session.pivot().planning_stats_for_tests();
  EXPECT_EQ(planning_after.copy_batch_count,
            planning_before.copy_batch_count + 1u);
  EXPECT_EQ(planning_after.planned_target_count,
            planning_before.planned_target_count + kTargetCount);
  EXPECT_EQ(planning_after.hierarchy_index_build_count,
            planning_before.hierarchy_index_build_count + 1u);
  EXPECT_EQ(planning_after.hierarchy_entity_visit_count,
            planning_before.hierarchy_entity_visit_count + entity_count);
  EXPECT_EQ(planning_after.hierarchy_depth_resolution_count,
            planning_before.hierarchy_depth_resolution_count + kTargetCount);
  EXPECT_EQ(planning_after.mesh_reference_index_build_count,
            planning_before.mesh_reference_index_build_count);
  EXPECT_EQ(planning_after.mesh_reference_entity_visit_count,
            planning_before.mesh_reference_entity_visit_count);
  EXPECT_EQ(planning_after.mesh_reference_lookup_count,
            planning_before.mesh_reference_lookup_count + kTargetCount);

  std::vector<quader::AssetId> copied_meshes;
  copied_meshes.reserve(kTargetCount);
  for (std::size_t index = 0u; index < targets.size(); ++index) {
    EXPECT_TRUE(
        harness.session.scene().transform(targets[index].entity)->custom_pivot);
    EXPECT_GT(quader::length(world_origin(harness, targets[index].entity) -
                             origins_before[index]),
              0.1f);
    expect_vertices_near(world_vertices(harness, targets[index].entity),
                         vertices_before[index]);
    copied_meshes.push_back(mesh_id(harness, targets[index].entity));
    if (index + 1u == targets.size()) {
      EXPECT_EQ(copied_meshes.back(), shared_mesh);
    } else {
      EXPECT_NE(copied_meshes.back(), shared_mesh);
    }
  }
  EXPECT_EQ(harness.session.assets().mesh_count(),
            mesh_count_before + kTargetCount - 1u);
  EXPECT_EQ(harness.session.scene().selected_entities(), selection_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), source.entity);

  const quader::ModelingSdkProjectionStats projection_after =
      harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(projection_after.full_scene_reset_count,
            projection_before.full_scene_reset_count);
  EXPECT_EQ(projection_after.targeted_scene_sync_count,
            projection_before.targeted_scene_sync_count + 1u);
  EXPECT_EQ(projection_after.targeted_scene_sync_document_count,
            projection_before.targeted_scene_sync_document_count +
                kTargetCount);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.editor.history().can_undo());
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before);
  for (std::size_t index = 0u; index < targets.size(); ++index) {
    EXPECT_EQ(mesh_id(harness, targets[index].entity), shared_mesh);
    expect_vec3_near(world_origin(harness, targets[index].entity),
                     origins_before[index]);
    expect_vertices_near(world_vertices(harness, targets[index].entity),
                         vertices_before[index]);
  }
  EXPECT_EQ(harness.session.scene().selected_entities(), selection_before);

  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_EQ(harness.session.pivot().planning_stats_for_tests()
                .copy_batch_count,
            planning_after.copy_batch_count);
  EXPECT_EQ(harness.session.assets().mesh_count(),
            mesh_count_before + kTargetCount - 1u);
  for (std::size_t index = 0u; index < targets.size(); ++index) {
    EXPECT_EQ(mesh_id(harness, targets[index].entity),
              copied_meshes[index]);
    expect_vertices_near(world_vertices(harness, targets[index].entity),
                         vertices_before[index]);
  }
  EXPECT_EQ(harness.session.scene().selected_entities(), selection_before);
}

TEST(PivotPerformanceTests,
     MeshReferenceLookupSkipsSixThousandUnrelatedEntities) {
  constexpr std::size_t kUnrelatedEntityCount = 6'001u;
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Indexed Source", 2.0f);
  set_transform(harness, source.entity,
                {.position = {7.0f, 2.0f, -4.0f},
                 .custom_pivot = true});
  const quader::QuaderModelingObjectRef target =
      create_box(harness, "Indexed Target");
  for (std::size_t index = 0u; index < kUnrelatedEntityCount; ++index) {
    ASSERT_NE(harness.session.scene().create_entity(
                  "Unrelated " + std::to_string(index),
                  quader::EntityKind::Generic),
              quader::kInvalidEntityId);
  }
  const std::size_t entity_count_before =
      harness.session.scene().entities().size();
  const std::array selected = {source.entity, target.entity};
  ASSERT_TRUE(
      harness.editor.selection().select_objects(selected, source.entity).ok());
  const quader::PivotPlanningStats before =
      harness.session.pivot().planning_stats_for_tests();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());

  const quader::PivotPlanningStats after =
      harness.session.pivot().planning_stats_for_tests();
  EXPECT_EQ(after.mesh_reference_index_build_count,
            before.mesh_reference_index_build_count);
  EXPECT_EQ(after.mesh_reference_entity_visit_count,
            before.mesh_reference_entity_visit_count);
  EXPECT_EQ(after.mesh_reference_lookup_count,
            before.mesh_reference_lookup_count + 1u);
  EXPECT_EQ(harness.session.scene().entities().size(), entity_count_before);
}

TEST(PivotPerformanceTests,
     ApplyUndoAndRedoRestoreOnlyTheAffectedModelingDocument) {
  constexpr std::size_t kUnrelatedMeshCount = 16U;
  CopyPivotHarness harness;
  for (std::size_t index = 0; index < kUnrelatedMeshCount; ++index) {
    create_box(harness, "Unrelated Pivot Mesh " + std::to_string(index));
  }
  const quader::QuaderModelingObjectRef target =
      create_box(harness, "Target Pivot Mesh");
  ASSERT_TRUE(harness.editor.selection().select_object(target).ok());

  const quader::ModelingSdkProjectionStats before =
      harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  ASSERT_TRUE(
      harness.editor.tools().execute_action("pivot", "pivot.align.next").ok());
  const quader::QuaderApiVoidResult applied =
      harness.editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());

  const quader::ModelingSdkProjectionStats after_apply =
      harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_apply.full_scene_reset_count,
            before.full_scene_reset_count);
  EXPECT_EQ(after_apply.scene_import_document_count,
            before.scene_import_document_count);
  EXPECT_EQ(after_apply.targeted_scene_sync_count,
            before.targeted_scene_sync_count + 1U);
  EXPECT_EQ(after_apply.targeted_scene_sync_document_count,
            before.targeted_scene_sync_document_count + 1U);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  const quader::ModelingSdkProjectionStats after_undo =
      harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_undo.full_scene_reset_count,
            before.full_scene_reset_count);
  EXPECT_EQ(after_undo.targeted_scene_sync_count,
            after_apply.targeted_scene_sync_count + 1U);
  EXPECT_EQ(after_undo.targeted_scene_sync_document_count,
            after_apply.targeted_scene_sync_document_count + 1U);

  ASSERT_TRUE(harness.editor.history().redo().ok());
  const quader::ModelingSdkProjectionStats after_redo =
      harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_redo.full_scene_reset_count,
            before.full_scene_reset_count);
  EXPECT_EQ(after_redo.targeted_scene_sync_count,
            after_undo.targeted_scene_sync_count + 1U);
  EXPECT_EQ(after_redo.targeted_scene_sync_document_count,
            after_undo.targeted_scene_sync_document_count + 1U);
}

} // namespace
