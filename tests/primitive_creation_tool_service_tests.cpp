////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/interactions/box_tool.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/modeling/runtime/tool_constants.hpp"
#include "editor/modeling/services/modeling_mesh_sync_service.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/projection/tool_preview_mesh_projection.hpp"
#include "editor/session/scene_session.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_texture_mapping.hpp"

#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace quader {
namespace {

/// Provides isolated modeling and history owners for primitive lifecycle tests.
struct PrimitiveCreationHarness {
  SceneDocument scene;
  AssetRegistry assets;
  EditorHistoryService history;
  ModelingSessionService modeling{scene, assets};
  PrimitiveCreationToolService tool{history, modeling};
};

/// Provides direct coverage of the SDK-to-native creation projection boundary.
struct CreationProjectionHarness {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectRuntimeMap object_map;
  ModelingMeshSyncService mesh_sync;
  ModelingSdkProjectionAdapter projection{scene, assets, object_map,
                                           mesh_sync};
};

std::array<QVec3, 8> oriented_box_corners(QVec3 center, QVec3 x_axis,
                                          QVec3 y_axis, float width,
                                          float height, float depth) {
  const QVec3 half_x = x_axis * (width * 0.5f);
  const QVec3 half_y = y_axis * (height * 0.5f);
  const QVec3 half_z = normalize(cross(x_axis, y_axis)) * (depth * 0.5f);
  return {
      center - half_x - half_y - half_z,
      center - half_x - half_y + half_z,
      center + half_x - half_y + half_z,
      center + half_x - half_y - half_z,
      center - half_x + half_y - half_z,
      center - half_x + half_y + half_z,
      center + half_x + half_y + half_z,
      center + half_x + half_y - half_z,
  };
}

ModelingBoxPreview oriented_primitive_preview(QVec3 center, QVec3 x_axis,
                                               QVec3 y_axis, float width,
                                               float height, float depth) {
  ModelingBoxPreview preview;
  preview.ok = true;
  preview.valid = true;
  preview.drag_state = ModelingBoxDragState::Height;
  preview.corners = oriented_box_corners(center, x_axis, y_axis, width,
                                         height, depth);
  preview.corner_count = preview.corners.size();
  return preview;
}

bool near_point(QVec3 left, QVec3 right, float epsilon = 0.0001f) {
  return length_squared(left - right) <= epsilon * epsilon;
}

bool mesh_contains_world_point(const MeshAsset &mesh, const QMat4 &world,
                               QVec3 expected) {
  return std::ranges::any_of(
      mesh.geometry().vertices, [&](const MeshVertex &vertex) {
        return near_point(transform_point(world, vertex.position), expected);
      });
}

ModelingBoxPreview valid_primitive_preview(float center_x = 0.0f) {
  ModelingBoxPreview preview;
  preview.ok = true;
  preview.valid = true;
  preview.drag_state = ModelingBoxDragState::Height;
  preview.corners = {
      QVec3{center_x - 1.0f, 0.0f, -1.0f},
      QVec3{center_x - 1.0f, 0.0f, 1.0f},
      QVec3{center_x + 1.0f, 0.0f, 1.0f},
      QVec3{center_x + 1.0f, 0.0f, -1.0f},
      QVec3{center_x - 1.0f, 2.0f, -1.0f},
      QVec3{center_x - 1.0f, 2.0f, 1.0f},
      QVec3{center_x + 1.0f, 2.0f, 1.0f},
      QVec3{center_x + 1.0f, 2.0f, -1.0f},
  };
  preview.corner_count = preview.corners.size();
  return preview;
}

const ModelingObjectRecord *find_object(
    const std::vector<ModelingObjectRecord> &objects, std::string_view name) {
  const auto found =
      std::find_if(objects.begin(), objects.end(), [name](const auto &object) {
        return object.name == name;
      });
  return found != objects.end() ? &*found : nullptr;
}

std::size_t compiled_index_count(const ModelingSessionService &modeling,
                                 std::string_view name) {
  const std::vector<ModelingObjectRecord> objects = modeling.objects();
  const ModelingObjectRecord *const object = find_object(objects, name);
  if (object == nullptr) {
    ADD_FAILURE() << "Missing modeling object named " << name;
    return 0U;
  }
  const std::optional<MeshAsset> mesh =
      modeling.compile_object_mesh(object->modeling_object_id);
  if (!mesh.has_value()) {
    ADD_FAILURE() << "Could not compile modeling object named " << name;
    return 0U;
  }
  return mesh->geometry().indices.size();
}

std::uint64_t compiled_content_id(const ModelingSessionService &modeling,
                                  std::string_view name) {
  const std::vector<ModelingObjectRecord> objects = modeling.objects();
  const ModelingObjectRecord *const object = find_object(objects, name);
  if (object == nullptr) {
    ADD_FAILURE() << "Missing modeling object named " << name;
    return 0U;
  }
  const std::optional<MeshAsset> mesh =
      modeling.compile_object_mesh(object->modeling_object_id);
  if (!mesh.has_value()) {
    ADD_FAILURE() << "Could not compile modeling object named " << name;
    return 0U;
  }
  return mesh->geometry_content_id().value;
}

std::vector<QVec2> effective_texture_uvs(const SceneDocument &scene,
                                       EntityId entity,
                                       const MeshAsset &mesh,
                                       EntityId occurrence = kInvalidEntityId) {
  const std::optional<QMat4> mapping =
      effective_texture_sampling_from_local(scene, entity, occurrence);
  if (!mapping.has_value()) {
    const auto *instance = scene.instance_occurrence(occurrence);
    ADD_FAILURE() << "Missing texture sampling for entity " << entity
                  << "; exists=" << scene.exists(entity)
                  << "; parent=" << scene.parent(entity)
                  << "; member_definition="
                  << scene.containing_instance_definition(entity)
                  << "; occurrence=" << occurrence
                  << "; occurrence_exists=" << scene.exists(occurrence)
                  << "; occurrence_definition="
                  << (instance != nullptr ? instance->definition
                                          : kInvalidEntityId);
    return {};
  }
  const quader_poly::Transform3 sampling =
      MeshProjectionService::texture_mapping_transform(*mapping);
  std::vector<QVec2> result;
  result.reserve(mesh.geometry().vertices.size());
  for (const MeshVertex &vertex : mesh.geometry().vertices) {
    result.push_back(quader_poly::evaluate_texture_mapping(
        vertex.uv, vertex.position, vertex.uv_geometric_normal,
        vertex.uv_carrier, sampling));
  }
  return result;
}

void expect_texture_uvs_near(const std::vector<QVec2> &actual,
                            const std::vector<QVec2> &expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t index = 0; index < actual.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_NEAR(actual[index].x, expected[index].x, 0.0001f);
    EXPECT_NEAR(actual[index].y, expected[index].y, 0.0001f);
  }
}

std::vector<QVec2> world_generated_texture_uvs(const MeshAsset &mesh,
                                             const QMat4 &world) {
  std::vector<QVec2> expected;
  expected.reserve(mesh.geometry().vertices.size());
  for (const MeshVertex &vertex : mesh.geometry().vertices) {
    expected.push_back(quader_poly::generated_texture_uv(
        transform_point(world, vertex.position),
        normalize(transform_vector(world, vertex.uv_geometric_normal))));
  }
  return expected;
}

void expect_world_generated_texture_uvs(const SceneDocument &scene,
                                        EntityId entity,
                                        const MeshAsset &mesh) {
  expect_texture_uvs_near(
      effective_texture_uvs(scene, entity, mesh),
      world_generated_texture_uvs(mesh, scene.world_transform_matrix(entity)));
}

std::optional<QVec2> top_face_texture_uv_at_world_point(
    const SceneDocument &scene, EntityId entity, const MeshAsset &mesh,
    QVec3 point) {
  const QMat4 world = scene.world_transform_matrix(entity);
  const std::vector<QVec2> uvs = effective_texture_uvs(scene, entity, mesh);
  for (std::size_t index = 0; index < uvs.size(); ++index) {
    const MeshVertex &vertex = mesh.geometry().vertices[index];
    if (near_point(vertex.uv_geometric_normal, {0.0f, 1.0f, 0.0f}) &&
        near_point(transform_point(world, vertex.position), point)) {
      return uvs[index];
    }
  }
  return std::nullopt;
}

TEST(PrimitiveCreationToolServiceTests,
     ThirtyTwoMapUnitBoxesStartInSharedWorldTextureSpaceWithEitherLockPolicy) {
  constexpr float kBoxSize =
      32.0f / quader_editor_core::kDefaultMapUnitsPerWorldUnit;
  constexpr float kOneMapUnit =
      1.0f / quader_editor_core::kDefaultMapUnitsPerWorldUnit;
  for (const bool texture_lock : {false, true}) {
    SCOPED_TRACE(::testing::Message() << "texture_lock=" << texture_lock);
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    editor.preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{
        .texture_lock_enabled = [texture_lock] { return texture_lock; },
    });
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
    const std::array<float, 3> offsets{0.0f, kOneMapUnit, kBoxSize};
    for (const float offset : offsets) {
      const ModelingBoxPreview preview = oriented_primitive_preview(
          {0.5f * kBoxSize + offset, 0.5f * kBoxSize, 0.5f * kBoxSize},
          {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, kBoxSize, kBoxSize,
          kBoxSize);
      const CommandResult accepted =
          session.primitive_creation().accept_preview(preview, "World Tile");
      ASSERT_TRUE(accepted.ok) << accepted.message;
    }
    const std::vector<ModelingObjectRecord> objects = session.modeling().objects();
    ASSERT_EQ(objects.size(), offsets.size());
    std::array<MeshAsset, 3> meshes;
    for (std::size_t index = 0; index < objects.size(); ++index) {
      const auto mesh = session.modeling().compile_object_mesh(
          objects[index].modeling_object_id);
      ASSERT_TRUE(mesh.has_value());
      meshes[index] = *mesh;
      expect_world_generated_texture_uvs(session.scene(), objects[index].entity,
                                         meshes[index]);
      for (const MeshVertex &vertex : meshes[index].geometry().vertices) {
        EXPECT_LE(std::abs(vertex.position.x), 0.5f * kBoxSize);
        EXPECT_LE(std::abs(vertex.position.y), 0.5f * kBoxSize);
        EXPECT_LE(std::abs(vertex.position.z), 0.5f * kBoxSize);
      }
    }
    const auto origin_uv = top_face_texture_uv_at_world_point(
        session.scene(), objects[0].entity, meshes[0], {0.0f, kBoxSize, 0.0f});
    const auto offset_uv = top_face_texture_uv_at_world_point(
        session.scene(), objects[1].entity, meshes[1],
        {kOneMapUnit, kBoxSize, 0.0f});
    ASSERT_TRUE(origin_uv.has_value());
    ASSERT_TRUE(offset_uv.has_value());
    EXPECT_NEAR(origin_uv->x, 0.0f, 0.00001f);
    EXPECT_NEAR(origin_uv->y, 0.0f, 0.00001f);
    EXPECT_NEAR(offset_uv->x - origin_uv->x, 1.0f / 64.0f, 0.00001f);
    EXPECT_NEAR(offset_uv->y, origin_uv->y, 0.00001f);
    for (const float z : {0.0f, kBoxSize}) {
      const QVec3 shared_point{kBoxSize, kBoxSize, z};
      const auto first_uv = top_face_texture_uv_at_world_point(
          session.scene(), objects[0].entity, meshes[0], shared_point);
      const auto adjacent_uv = top_face_texture_uv_at_world_point(
          session.scene(), objects[2].entity, meshes[2], shared_point);
      ASSERT_TRUE(first_uv.has_value());
      ASSERT_TRUE(adjacent_uv.has_value());
      EXPECT_NEAR(first_uv->x, adjacent_uv->x, 0.00001f);
      EXPECT_NEAR(first_uv->y, adjacent_uv->y, 0.00001f);
    }
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
    EXPECT_TRUE(session.modeling().objects().empty());
    EXPECT_FALSE(editor.history().can_undo());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     WorldSpaceBoxPreviewMatchesEffectiveCommittedTextureCoordinates) {
  for (const QVec3 normal : {QVec3{0.0f, 1.0f, 0.0f},
                             QVec3{0.0f, 0.8660254f, 0.5f}}) {
    SCOPED_TRACE(::testing::Message() << "normal.y=" << normal.y);
    PrimitiveCreationHarness harness;
    ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);
    quader_editor_core::BoxToolState state;
    const QVec3 start{4.03125f, 2.0f, -3.0f};
    ASSERT_TRUE(quader_editor_core::set_box_construction_plane(
                    state, start, normal).ok);
    ASSERT_TRUE(quader_editor_core::begin_box_footprint(
                    state, start, start + state.plane.axis_u + state.plane.axis_v,
                    false, 0.03125).ok);
    state.drag_state = quader_editor_core::BoxDragState::Height;
    state.height_distance = 1.0;
    const auto descriptor =
        quader_editor_core::box_preview_descriptor(state, false, 0.03125);
    const auto preview_mesh =
        ToolPreviewMeshProjection::box_preview_mesh_from_descriptor(descriptor);
    ASSERT_TRUE(preview_mesh.has_value());
    const auto corners = quader_editor_core::box_preview_corners(descriptor);
    ModelingBoxPreview preview;
    ASSERT_EQ(corners.size(), preview.corners.size());
    preview.ok = true;
    preview.valid = true;
    preview.drag_state = ModelingBoxDragState::Height;
    std::ranges::copy(corners, preview.corners.begin());
    preview.corner_count = corners.size();
    ASSERT_TRUE(harness.tool.accept_preview(preview, "Preview Matched Tile").ok);
    ASSERT_TRUE(harness.tool.apply().ok);
    const auto objects = harness.modeling.objects();
    ASSERT_EQ(objects.size(), 1U);
    const ModelingObjectRecord object = objects.front();
    const auto committed_mesh =
        harness.modeling.compile_object_mesh(object.modeling_object_id);
    ASSERT_TRUE(committed_mesh.has_value());
    const QMat4 world = harness.scene.world_transform_matrix(object.entity);
    const auto committed_uvs =
        effective_texture_uvs(harness.scene, object.entity, *committed_mesh);
    ASSERT_EQ(committed_uvs.size(), committed_mesh->geometry().vertices.size());
    for (std::size_t index = 0; index < committed_uvs.size(); ++index) {
      SCOPED_TRACE(index);
      const MeshVertex &vertex = committed_mesh->geometry().vertices[index];
      const QVec3 world_position = transform_point(world, vertex.position);
      const QVec3 world_normal =
          normalize(transform_vector(world, vertex.uv_geometric_normal));
      const auto preview_vertex = std::ranges::find_if(
          preview_mesh->geometry().vertices, [&](const MeshVertex &candidate) {
            return near_point(candidate.position, world_position) &&
                   near_point(candidate.uv_geometric_normal, world_normal);
          });
      ASSERT_NE(preview_vertex, preview_mesh->geometry().vertices.end());
      EXPECT_NEAR(committed_uvs[index].x, preview_vertex->uv.x, 0.0001f);
      EXPECT_NEAR(committed_uvs[index].y, preview_vertex->uv.y, 0.0001f);
    }
  }
}

TEST(PrimitiveCreationToolServiceTests,
     LivePrimitiveKindsRetainWorldTextureSpaceThroughRebuildHistoryAndCancel) {
  PrimitiveCreationHarness harness;
  constexpr float kCosThirty = 0.8660254f;
  const ModelingBoxPreview preview = oriented_primitive_preview(
      {5.0f, 2.0f, -3.0f}, {1.0f, 0.0f, 0.0f},
      {0.0f, kCosThirty, 0.5f}, 4.0f, 2.0f, 6.0f);
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);
  ASSERT_TRUE(harness.tool.accept_preview(preview, "Live World Texture").ok);
  ASSERT_TRUE(harness.tool.set_setting(kBoxSubdivisionsXSetting, 3.0).ok);
  const auto objects = harness.modeling.objects();
  ASSERT_EQ(objects.size(), 1U);
  const ModelingObjectRecord object = objects.front();
  for (const PrimitiveCreationKind kind : {
           PrimitiveCreationKind::Box, PrimitiveCreationKind::Cylinder,
           PrimitiveCreationKind::Sphere, PrimitiveCreationKind::Cone,
           PrimitiveCreationKind::Torus}) {
    SCOPED_TRACE(primitive_creation_default_name(kind));
    ASSERT_TRUE(harness.tool.set_kind(kind).ok);
    const auto mesh = harness.modeling.compile_object_mesh(object.modeling_object_id);
    ASSERT_TRUE(mesh.has_value());
    expect_world_generated_texture_uvs(harness.scene, object.entity, *mesh);
    EXPECT_EQ(harness.modeling.objects().front().modeling_object_id,
              object.modeling_object_id);
    EXPECT_FALSE(harness.history.can_undo());
  }
  const auto applied_mesh =
      harness.modeling.compile_object_mesh(object.modeling_object_id);
  ASSERT_TRUE(applied_mesh.has_value());
  const auto applied_uvs = effective_texture_uvs(harness.scene, object.entity,
                                               *applied_mesh);
  const auto applied_mapping =
      effective_texture_sampling_from_local(harness.scene, object.entity);
  ASSERT_TRUE(applied_mapping.has_value());
  ASSERT_TRUE(harness.tool.apply().ok);
  ASSERT_TRUE(harness.history.undo().ok);
  EXPECT_TRUE(harness.modeling.objects().empty());
  ASSERT_TRUE(harness.history.redo().ok);
  ASSERT_TRUE(harness.scene.exists(object.entity));
  const auto restored_mesh =
      harness.modeling.compile_object_mesh(object.modeling_object_id);
  ASSERT_TRUE(restored_mesh.has_value());
  expect_texture_uvs_near(
      effective_texture_uvs(harness.scene, object.entity, *restored_mesh),
      applied_uvs);
  const auto restored_mapping =
      effective_texture_sampling_from_local(harness.scene, object.entity);
  ASSERT_TRUE(restored_mapping.has_value());
  EXPECT_EQ(restored_mapping->values, applied_mapping->values);

  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);
  ASSERT_TRUE(harness.tool
                  .accept_preview(valid_primitive_preview(12.0f), "Canceled Tile")
                  .ok);
  ASSERT_TRUE(harness.tool.set_setting(kBoxSubdivisionsYSetting, 2.0).ok);
  ASSERT_TRUE(harness.tool.cancel().ok);
  ASSERT_EQ(harness.modeling.objects().size(), 1U);
  EXPECT_EQ(harness.modeling.objects().front().modeling_object_id,
            object.modeling_object_id);
  const auto after_cancel =
      harness.modeling.compile_object_mesh(object.modeling_object_id);
  ASSERT_TRUE(after_cancel.has_value());
  expect_texture_uvs_near(
      effective_texture_uvs(harness.scene, object.entity, *after_cancel),
      applied_uvs);
  const auto mapping_after_cancel =
      effective_texture_sampling_from_local(harness.scene, object.entity);
  ASSERT_TRUE(mapping_after_cancel.has_value());
  EXPECT_EQ(mapping_after_cancel->values, applied_mapping->values);
  ASSERT_TRUE(harness.history.undo().ok);
  EXPECT_TRUE(harness.modeling.objects().empty());
}

TEST(PrimitiveCreationToolServiceTests,
     AppliedPrimitiveHonorsTextureLockForPublicMoveRotateAndHistory) {
  for (const bool initial_lock : {false, true}) {
    SCOPED_TRACE(::testing::Message() << "texture_lock=" << initial_lock);
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    bool texture_lock = initial_lock;
    editor.preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{
        .texture_lock_enabled = [&] { return texture_lock; },
    });
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
    ASSERT_TRUE(session.primitive_creation()
                    .accept_preview(valid_primitive_preview(3.0f), "Placed Tile")
                    .ok);
    ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
    const auto objects = session.modeling().objects();
    ASSERT_EQ(objects.size(), 1U);
    const ModelingObjectRecord object = objects.front();
    const auto mesh = session.modeling().compile_object_mesh(object.modeling_object_id);
    ASSERT_TRUE(mesh.has_value());
    const MeshGeometryContentId initial_content = mesh->geometry_content_id();
    const auto live_uvs = [&] {
      const auto current_mesh =
          session.modeling().compile_object_mesh(object.modeling_object_id);
      if (!current_mesh.has_value()) {
        ADD_FAILURE() << "Applied primitive no longer has a compiled mesh.";
        return std::vector<QVec2>{};
      }
      EXPECT_EQ(current_mesh->geometry_content_id(), initial_content)
          << "Object placement must not rebake authored geometry or UVs.";
      return effective_texture_uvs(session.scene(), object.entity, *current_mesh);
    };
    const auto initial_uvs = effective_texture_uvs(session.scene(), object.entity,
                                                 *mesh);
    expect_world_generated_texture_uvs(session.scene(), object.entity, *mesh);

    ASSERT_TRUE(editor.modeling().move_selection({0.03125f, 2.0f, -1.0f}).ok());
    const auto moved_uvs = live_uvs();
    if (initial_lock) {
      expect_texture_uvs_near(moved_uvs, initial_uvs);
    } else {
      expect_texture_uvs_near(moved_uvs, world_generated_texture_uvs(
          *mesh, session.scene().world_transform_matrix(object.entity)));
    }
    constexpr float kHalfPi = 1.57079632679f;
    ASSERT_TRUE(editor.modeling()
                    .rotate_selection({0.0f, kHalfPi, 0.0f}, {1.0f, -2.0f, 3.0f})
                    .ok());
    const auto rotated_uvs = live_uvs();
    if (initial_lock) {
      expect_texture_uvs_near(rotated_uvs, initial_uvs);
    } else {
      expect_texture_uvs_near(rotated_uvs, world_generated_texture_uvs(
          *mesh, session.scene().world_transform_matrix(object.entity)));
    }

    // History restores the captured chart, not the current toolbar preference.
    texture_lock = !initial_lock;
    ASSERT_TRUE(editor.history().undo().ok());
    expect_texture_uvs_near(live_uvs(), moved_uvs);
    ASSERT_TRUE(editor.history().undo().ok());
    expect_texture_uvs_near(live_uvs(), initial_uvs);
    ASSERT_TRUE(editor.history().redo().ok());
    expect_texture_uvs_near(live_uvs(), moved_uvs);
    ASSERT_TRUE(editor.history().redo().ok());
    expect_texture_uvs_near(live_uvs(), rotated_uvs);
  }
}

TEST(PrimitiveCreationToolServiceTests,
     InstanceCreationCompensatesOccurrenceSamplingIncludingAnIdentityWorldFrame) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  editor.preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{
      .texture_lock_enabled = [] { return false; },
      .texture_lock_scale_enabled = [] { return false; },
  });
  const auto seed = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Existing Instance Member");
  ASSERT_TRUE(seed.ok()) << seed.error_message();
  const auto instance = editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const EntityId occurrence = instance.value().occurrence.root.id;
  const EntityId definition = instance.value().definition.root.id;
  ASSERT_TRUE(editor.scene().set_transform(occurrence, {
      .position = {9.0f, 4.0f, -6.0f},
      .rotation_euler = {0.0f, 0.35f, 0.0f},
      .scale = {2.0f, 2.0f, 2.0f},
  }).ok());
  const auto occurrence_snapshot = editor.query().entity(occurrence);
  ASSERT_TRUE(occurrence_snapshot.has_value());
  ASSERT_TRUE(occurrence_snapshot->texture_sampling_from_local.has_value());
  const QMat4 occurrence_mapping = *occurrence_snapshot->texture_sampling_from_local;
  EXPECT_NE(occurrence_mapping.values, QMat4{}.values);
  const auto sibling = editor.scene().duplicate(occurrence);
  ASSERT_TRUE(sibling.ok()) << sibling.error_message();
  const auto seed_mesh = session.modeling().compile_object_mesh(
      seed.value().modeling_object_id);
  ASSERT_TRUE(seed_mesh.has_value());
  const auto seed_uvs = effective_texture_uvs(session.scene(), seed.value().entity,
                                             *seed_mesh, occurrence);
  ASSERT_TRUE(editor.modeling().enter_instance_edit(occurrence).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  constexpr float kInvSqrtTwo = 0.70710678f;
  const std::array<QVec3, 3> centers{
      QVec3{}, QVec3{7.0f, 3.0f, -2.0f}, QVec3{4.0f, 1.0f, 2.0f}};
  const std::array<QVec3, 3> x_axes{
      QVec3{1.0f, 0.0f, 0.0f}, QVec3{1.0f, 0.0f, 0.0f},
      QVec3{kInvSqrtTwo, 0.0f, -kInvSqrtTwo}};
  const std::array<const char *, 3> names{
      "Origin Instance Tile", "Offset Instance Tile", "Oriented Instance Tile"};
  std::vector<ModelingObjectRecord> created;
  std::vector<std::vector<QVec2>> created_uvs;
  for (std::size_t index = 0; index < centers.size(); ++index) {
    SCOPED_TRACE(::testing::Message() << "creation: " << names[index]);
    const ModelingBoxPreview preview = oriented_primitive_preview(
        centers[index], x_axes[index], {0.0f, 1.0f, 0.0f},
        1.0f, 1.0f, 1.0f);
    const CommandResult accepted =
        session.primitive_creation().accept_preview(preview, names[index]);
    ASSERT_TRUE(accepted.ok) << accepted.message;
    const auto objects = session.modeling().objects();
    const ModelingObjectRecord *const object = find_object(objects, names[index]);
    ASSERT_NE(object, nullptr);
    created.push_back(*object);
    const auto snapshot = editor.query().entity(object->entity);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->parent, definition);
    const auto world = session.instances().effective_member_world_transform(
        object->entity, occurrence);
    ASSERT_TRUE(world.has_value());
    EXPECT_TRUE(near_point(transform_point(*world, {}), centers[index]));
    const auto mesh = session.modeling().compile_object_mesh(object->modeling_object_id);
    ASSERT_TRUE(mesh.has_value());
    for (const QVec3 corner : preview.corners) {
      EXPECT_TRUE(mesh_contains_world_point(*mesh, *world, corner));
    }
    created_uvs.push_back(effective_texture_uvs(session.scene(), object->entity,
                                               *mesh, occurrence));
    expect_texture_uvs_near(created_uvs.back(), world_generated_texture_uvs(*mesh, *world));
  }
  const auto verify_created_members = [&](std::string_view phase) {
    SCOPED_TRACE(phase);
    ASSERT_TRUE(session.scene().exists(definition));
    ASSERT_TRUE(session.scene().is_instance_definition_entity(definition));
    const auto *instance_component = session.scene().instance_occurrence(occurrence);
    ASSERT_NE(instance_component, nullptr);
    ASSERT_EQ(instance_component->definition, definition);
    const auto current_objects = session.modeling().objects();
    for (std::size_t index = 0; index < created.size(); ++index) {
      SCOPED_TRACE(::testing::Message() << "member=" << created[index].name
                                      << "; expected_entity=" << created[index].entity);
      const auto *current_object = find_object(current_objects, created[index].name);
      ASSERT_NE(current_object, nullptr);
      EXPECT_EQ(current_object->entity, created[index].entity);
      EXPECT_TRUE(session.scene().exists(created[index].entity));
      EXPECT_EQ(session.scene().parent(created[index].entity), definition);
      EXPECT_EQ(session.scene().containing_instance_definition(created[index].entity),
                definition);
      const auto mesh = session.modeling().compile_object_mesh(created[index].modeling_object_id);
      ASSERT_TRUE(mesh.has_value());
      expect_texture_uvs_near(
          effective_texture_uvs(session.scene(), created[index].entity, *mesh, occurrence),
          created_uvs[index]);
    }
  };
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  verify_created_members("after Apply");
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(session.scene().exists(definition));
  ASSERT_TRUE(session.scene().is_instance_definition_entity(definition));
  ASSERT_TRUE(session.scene().exists(occurrence));
  for (const ModelingObjectRecord &object : created) {
    SCOPED_TRACE(::testing::Message() << "after Undo: " << object.name);
    EXPECT_FALSE(session.scene().exists(object.entity));
  }
  ASSERT_TRUE(editor.history().redo().ok());
  verify_created_members("after Redo");
  const auto restored_seed_mesh = session.modeling().compile_object_mesh(
      seed.value().modeling_object_id);
  ASSERT_TRUE(restored_seed_mesh.has_value());
  expect_texture_uvs_near(
      effective_texture_uvs(session.scene(), seed.value().entity, *restored_seed_mesh,
                            occurrence),
      seed_uvs);
  for (const EntityId id : {occurrence, sibling.value().id}) {
    const auto snapshot = editor.query().entity(id);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_TRUE(snapshot->texture_sampling_from_local.has_value());
    EXPECT_EQ(snapshot->texture_sampling_from_local->values, occurrence_mapping.values);
  }
  ASSERT_TRUE(editor.modeling().exit_instance_edit().ok());
}

TEST(PrimitiveCreationToolServiceTests,
     OrientedPrimitiveRetainsLocalGeometryAndPlacementThroughHistory) {
  PrimitiveCreationHarness harness;
  constexpr float kInvSqrtTwo = 0.70710678f;
  const QVec3 center{4.0f, 2.0f, -3.0f};
  const QVec3 x_axis{kInvSqrtTwo, 0.0f, -kInvSqrtTwo};
  const QVec3 y_axis{0.0f, 1.0f, 0.0f};
  const QVec3 z_axis = normalize(cross(x_axis, y_axis));
  const ModelingBoxPreview preview = oriented_primitive_preview(
      center, x_axis, y_axis, 4.0f, 2.0f, 6.0f);

  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);
  const CommandResult accepted =
      harness.tool.accept_preview(preview, "Oriented Primitive");
  ASSERT_TRUE(accepted.ok) << accepted.message;
  const std::vector<ModelingObjectRecord> objects = harness.modeling.objects();
  ASSERT_EQ(objects.size(), 1U);
  const ModelingObjectRecord &object = objects.front();
  const ModelingObjectComponent *const modeling_object =
      harness.scene.modeling_object(object.entity);
  ASSERT_NE(modeling_object, nullptr);
  EXPECT_EQ(modeling_object->local_frame_version,
            kCanonicalModelingObjectLocalFrameVersion);
  const TransformComponent *const transform =
      harness.scene.transform(object.entity);
  ASSERT_NE(transform, nullptr);
  EXPECT_TRUE(near_point(transform->position, center));
  EXPECT_TRUE(near_point(transform->scale, {1.0f, 1.0f, 1.0f}));
  const QMat4 world = harness.scene.world_transform_matrix(object.entity);
  EXPECT_TRUE(near_point(normalize(transform_vector(world, {1.0f, 0.0f, 0.0f})),
                         x_axis));
  EXPECT_TRUE(near_point(normalize(transform_vector(world, {0.0f, 1.0f, 0.0f})),
                         y_axis));
  EXPECT_TRUE(near_point(normalize(transform_vector(world, {0.0f, 0.0f, 1.0f})),
                         z_axis));

  const std::optional<MeshAsset> mesh =
      harness.modeling.compile_object_mesh(object.modeling_object_id);
  ASSERT_TRUE(mesh.has_value());
  ASSERT_FALSE(mesh->geometry().vertices.empty());
  for (const MeshVertex &vertex : mesh->geometry().vertices) {
    EXPECT_LE(std::abs(vertex.position.x), 2.0001f);
    EXPECT_LE(std::abs(vertex.position.y), 1.0001f);
    EXPECT_LE(std::abs(vertex.position.z), 3.0001f);
  }
  for (const QVec3 corner : preview.corners) {
    EXPECT_TRUE(mesh_contains_world_point(*mesh, world, corner));
  }
  expect_world_generated_texture_uvs(harness.scene, object.entity, *mesh);

  ASSERT_TRUE(harness.tool.apply().ok);
  ASSERT_TRUE(harness.history.undo().ok);
  EXPECT_TRUE(harness.modeling.objects().empty());
  ASSERT_TRUE(harness.history.redo().ok);
  const std::vector<ModelingObjectRecord> restored = harness.modeling.objects();
  ASSERT_EQ(restored.size(), 1U);
  const ModelingObjectComponent *const restored_modeling_object =
      harness.scene.modeling_object(restored.front().entity);
  ASSERT_NE(restored_modeling_object, nullptr);
  EXPECT_EQ(restored_modeling_object->local_frame_version,
            kCanonicalModelingObjectLocalFrameVersion);
  const QMat4 restored_world =
      harness.scene.world_transform_matrix(restored.front().entity);
  EXPECT_TRUE(near_point(transform_point(restored_world, {}), center));
  EXPECT_TRUE(near_point(
      normalize(transform_vector(restored_world, {1.0f, 0.0f, 0.0f})),
      x_axis));
  const std::optional<MeshAsset> restored_mesh =
      harness.modeling.compile_object_mesh(restored.front().modeling_object_id);
  ASSERT_TRUE(restored_mesh.has_value());
  for (const QVec3 corner : preview.corners) {
    EXPECT_TRUE(
        mesh_contains_world_point(*restored_mesh, restored_world, corner));
  }
  expect_world_generated_texture_uvs(harness.scene, restored.front().entity,
                                     *restored_mesh);
}

TEST(ModelingSdkProjectionCreationTests,
     BoxCornersBecomeLocalGeometryUnderTheirStoredPlacementFrame) {
  CreationProjectionHarness harness;
  constexpr float kInvSqrtTwo = 0.70710678f;
  const QVec3 center{7.0f, 3.0f, -2.0f};
  const QVec3 x_axis{kInvSqrtTwo, 0.0f, -kInvSqrtTwo};
  const QVec3 y_axis{0.0f, 1.0f, 0.0f};
  const QVec3 z_axis = normalize(cross(x_axis, y_axis));
  const std::array<QVec3, 8> corners =
      oriented_box_corners(center, x_axis, y_axis, 4.0f, 2.0f, 6.0f);

  const ModelingCreateBoxResult created = harness.projection.create_box_from_corners(
      corners, "Oriented Box");
  ASSERT_TRUE(created.ok) << created.message;
  const ModelingObjectComponent *const modeling_object =
      harness.scene.modeling_object(created.object.entity);
  ASSERT_NE(modeling_object, nullptr);
  EXPECT_EQ(modeling_object->local_frame_version,
            kCanonicalModelingObjectLocalFrameVersion);
  const QMat4 world = harness.scene.world_transform_matrix(created.object.entity);
  EXPECT_TRUE(near_point(transform_point(world, {}), center));
  EXPECT_TRUE(near_point(normalize(transform_vector(world, {1.0f, 0.0f, 0.0f})),
                         x_axis));
  EXPECT_TRUE(near_point(normalize(transform_vector(world, {0.0f, 1.0f, 0.0f})),
                         y_axis));
  EXPECT_TRUE(near_point(normalize(transform_vector(world, {0.0f, 0.0f, 1.0f})),
                         z_axis));

  const std::optional<MeshAsset> mesh = harness.projection.compile_object_mesh(
      created.object.modeling_object_id);
  ASSERT_TRUE(mesh.has_value());
  for (const MeshVertex &vertex : mesh->geometry().vertices) {
    EXPECT_LE(std::abs(vertex.position.x), 2.0001f);
    EXPECT_LE(std::abs(vertex.position.y), 1.0001f);
    EXPECT_LE(std::abs(vertex.position.z), 3.0001f);
  }
  for (const QVec3 corner : corners) {
    EXPECT_TRUE(mesh_contains_world_point(*mesh, world, corner));
  }
  expect_world_generated_texture_uvs(harness.scene, created.object.entity, *mesh);
}

TEST(ModelingSdkProjectionCreationTests,
     PolyPointsBecomePlanarLocalGeometryUnderTheirStoredComponentFrame) {
  CreationProjectionHarness harness;
  constexpr float kInvSqrtTwo = 0.70710678f;
  const QVec3 center{6.0f, -2.0f, 4.0f};
  const QVec3 x_axis{1.0f, 0.0f, 0.0f};
  const QVec3 z_axis{0.0f, kInvSqrtTwo, kInvSqrtTwo};
  const QVec3 y_axis = normalize(cross(z_axis, x_axis));
  const std::array<QVec3, 4> points{
      center - x_axis * 2.0f - y_axis,
      center + x_axis * 2.0f - y_axis,
      center + x_axis * 2.0f + y_axis,
      center - x_axis * 2.0f + y_axis,
  };

  const ModelingCreateBoxResult created = harness.projection.create_face(
      points, z_axis, "Oriented Poly");
  ASSERT_TRUE(created.ok) << created.message;
  const ModelingObjectComponent *const modeling_object =
      harness.scene.modeling_object(created.object.entity);
  ASSERT_NE(modeling_object, nullptr);
  EXPECT_EQ(modeling_object->local_frame_version,
            kCanonicalModelingObjectLocalFrameVersion);
  const QMat4 world = harness.scene.world_transform_matrix(created.object.entity);
  EXPECT_TRUE(near_point(transform_point(world, {}), center));
  EXPECT_TRUE(near_point(normalize(transform_vector(world, {1.0f, 0.0f, 0.0f})),
                         x_axis));
  EXPECT_TRUE(near_point(normalize(transform_vector(world, {0.0f, 1.0f, 0.0f})),
                         y_axis));
  EXPECT_TRUE(near_point(normalize(transform_vector(world, {0.0f, 0.0f, 1.0f})),
                         z_axis));

  const std::optional<MeshAsset> mesh = harness.projection.compile_object_mesh(
      created.object.modeling_object_id);
  ASSERT_TRUE(mesh.has_value());
  for (const MeshVertex &vertex : mesh->geometry().vertices) {
    EXPECT_NEAR(vertex.position.z, 0.0f, 0.0001f);
    EXPECT_LE(std::abs(vertex.position.x), 2.0001f);
    EXPECT_LE(std::abs(vertex.position.y), 1.0001f);
  }
  for (const QVec3 point : points) {
    EXPECT_TRUE(mesh_contains_world_point(*mesh, world, point));
  }
  expect_world_generated_texture_uvs(harness.scene, created.object.entity, *mesh);
}

TEST(ModelingSdkProjectionCreationTests,
     GenericAuthoredMeshKeepsItsUvChartDespiteInitialScenePlacement) {
  CreationProjectionHarness harness;
  quader_poly::Document document = quader_poly::make_cube({});
  for (quader_poly::Face &face : document.faces) {
    ASSERT_EQ(face.vertices.size(), 4U);
    face.uvs = {{0.125f, -0.25f}, {1.25f, -0.375f},
                {1.375f, 0.75f}, {0.25f, 1.125f}};
    face.texture_transform = {
        .scale = {2.0f, -0.5f},
        .shift_uv = {0.25f, -0.75f},
        .rotation_degrees = 30.0f,
    };
  }
  const MeshAsset expected =
      MeshProjectionService::compile_polygon_document_mesh(document);
  const ModelingCreateBoxResult created = harness.projection.create_mesh({
      .document = document,
      .name = "Authored UV Import",
      .initial_scene_transform = ModelingTransform3{
          .x_axis = {0.0f, 0.0f, -1.0f},
          .y_axis = {0.0f, 1.0f, 0.0f},
          .z_axis = {1.0f, 0.0f, 0.0f},
          .origin = {7.0f, 3.0f, -2.0f},
      },
  });
  ASSERT_TRUE(created.ok) << created.message;
  const auto actual = harness.projection.compile_object_mesh(
      created.object.modeling_object_id);
  ASSERT_TRUE(actual.has_value());
  ASSERT_EQ(actual->geometry().vertices.size(), expected.geometry().vertices.size());
  const std::vector<QVec2> effective =
      effective_texture_uvs(harness.scene, created.object.entity, *actual);
  ASSERT_EQ(effective.size(), expected.geometry().vertices.size());
  for (std::size_t index = 0; index < effective.size(); ++index) {
    SCOPED_TRACE(index);
    const MeshVertex &expected_vertex = expected.geometry().vertices[index];
    EXPECT_TRUE(near_point(actual->geometry().vertices[index].position,
                           expected_vertex.position));
    EXPECT_FLOAT_EQ(actual->geometry().vertices[index].uv.x, expected_vertex.uv.x);
    EXPECT_FLOAT_EQ(actual->geometry().vertices[index].uv.y, expected_vertex.uv.y);
    EXPECT_FLOAT_EQ(effective[index].x, expected_vertex.uv.x);
    EXPECT_FLOAT_EQ(effective[index].y, expected_vertex.uv.y);
  }
}

TEST(ModelingSdkProjectionCreationTests,
     FailedGeneratedCreationPreservesExistingSamplingAndProjectionRevisions) {
  for (const ModelingSdkProjectionFailurePoint failure : {
           ModelingSdkProjectionFailurePoint::AfterSceneBinding,
           ModelingSdkProjectionFailurePoint::AfterSelection,
           ModelingSdkProjectionFailurePoint::BeforeCommit,
           ModelingSdkProjectionFailurePoint::None}) {
    SCOPED_TRACE(static_cast<int>(failure));
    CreationProjectionHarness harness;
    const auto corners = oriented_box_corners(
        {4.0f, 2.0f, -3.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        1.0f, 1.0f, 1.0f);
    const auto baseline = harness.projection.create_box_from_corners(corners, "Baseline Tile");
    ASSERT_TRUE(baseline.ok) << baseline.message;
    const auto baseline_mesh = harness.projection.compile_object_mesh(
        baseline.object.modeling_object_id);
    ASSERT_TRUE(baseline_mesh.has_value());
    const auto baseline_uvs = effective_texture_uvs(harness.scene, baseline.object.entity,
                                                  *baseline_mesh);
    const auto baseline_mapping =
        effective_texture_sampling_from_local(harness.scene, baseline.object.entity);
    ASSERT_TRUE(baseline_mapping.has_value());
    const auto entities_before = harness.scene.entities();
    const auto selection_before = harness.scene.selected_entities();
    const auto meshes_before = harness.assets.mesh_ids();
    const auto scene_revision_before = harness.scene.revision();
    const auto texture_revision_before = harness.scene.texture_mapping_revision();
    const auto asset_revision_before = harness.assets.revision();
    harness.projection.set_failure_point_for_tests(failure);
    const auto rejected = harness.projection.create_box_from_corners(
        corners, "Rejected Tile", {},
        [failure](const ModelingObjectRecord &) {
          return failure != ModelingSdkProjectionFailurePoint::None;
        });
    EXPECT_FALSE(rejected.ok);
    EXPECT_FALSE(rejected.changed);
    EXPECT_EQ(harness.projection.object_count(), 1U);
    EXPECT_EQ(harness.scene.entities(), entities_before);
    EXPECT_EQ(harness.scene.selected_entities(), selection_before);
    EXPECT_EQ(harness.assets.mesh_ids(), meshes_before);
    EXPECT_EQ(harness.scene.revision(), scene_revision_before);
    EXPECT_EQ(harness.scene.texture_mapping_revision(), texture_revision_before);
    EXPECT_EQ(harness.assets.revision(), asset_revision_before);
    const auto mapping_after =
        effective_texture_sampling_from_local(harness.scene, baseline.object.entity);
    ASSERT_TRUE(mapping_after.has_value());
    EXPECT_EQ(mapping_after->values, baseline_mapping->values);
    expect_texture_uvs_near(
        effective_texture_uvs(harness.scene, baseline.object.entity, *baseline_mesh),
        baseline_uvs);
  }
}

TEST(ModelingSdkProjectionCreationTests,
     FailedProjectionRollsBackAnExplicitInitialSceneTransform) {
  CreationProjectionHarness harness;
  quader_poly::Document document = quader_poly::make_cube({});
  ASSERT_FALSE(document.vertices.empty());
  const std::vector<EntityId> entities_before = harness.scene.entities();
  const std::vector<AssetId> assets_before = harness.assets.mesh_ids();
  harness.projection.set_failure_point_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSceneBinding);

  const ModelingCreateBoxResult failed = harness.projection.create_mesh({
      .document = std::move(document),
      .name = "Rolled Back Oriented Mesh",
      .initial_scene_transform = ModelingTransform3{
          .x_axis = {0.0f, 0.0f, -1.0f},
          .y_axis = {0.0f, 1.0f, 0.0f},
          .z_axis = {1.0f, 0.0f, 0.0f},
          .origin = {20.0f, 30.0f, 40.0f},
      },
  });
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_EQ(harness.projection.object_count(), 0U);
  EXPECT_EQ(harness.scene.entities(), entities_before);
  EXPECT_EQ(harness.assets.mesh_ids(), assets_before);
}

TEST(PrimitiveCreationToolServiceTests,
     LiveSettingsUpdateNewestShapeAndApplyAsOneHistoryTransaction) {
  PrimitiveCreationHarness harness;

  const CommandResult activated =
      harness.tool.activate(PrimitiveCreationKind::Box);
  ASSERT_TRUE(activated.ok) << activated.message;
  EXPECT_TRUE(harness.tool.active());
  EXPECT_EQ(harness.tool.active_tool_id(), kCreatePrimitiveToolId);

  const CommandResult first = harness.tool.accept_preview(
      valid_primitive_preview(), "Low Resolution Box");
  ASSERT_TRUE(first.ok) << first.message;
  EXPECT_TRUE(harness.tool.active());
  EXPECT_EQ(harness.tool.shape_count(), 1U);
  EXPECT_FALSE(harness.history.can_undo());
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            36U);

  const CommandResult updated =
      harness.tool.set_setting(kBoxSubdivisionsXSetting, 2.0);
  ASSERT_TRUE(updated.ok) << updated.message;
  ASSERT_TRUE(updated.changed);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            60U);
  const CommandResult second = harness.tool.accept_preview(
      valid_primitive_preview(4.0f), "High Resolution Box");
  ASSERT_TRUE(second.ok) << second.message;
  EXPECT_TRUE(harness.tool.active());
  EXPECT_EQ(harness.tool.shape_count(), 2U);
  EXPECT_FALSE(harness.history.can_undo());

  // The first shape updates live, then freezes when creating the second.
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            60U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "High Resolution Box"),
            60U);

  const CommandResult newest_updated =
      harness.tool.set_setting(kBoxSubdivisionsXSetting, 3.0);
  ASSERT_TRUE(newest_updated.ok) << newest_updated.message;
  ASSERT_TRUE(newest_updated.changed);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            60U);
  const std::size_t newest_index_count =
      compiled_index_count(harness.modeling, "High Resolution Box");
  EXPECT_NE(newest_index_count, 60U);

  const CommandResult applied = harness.tool.apply();
  ASSERT_TRUE(applied.ok) << applied.message;
  EXPECT_FALSE(harness.tool.active());
  EXPECT_EQ(harness.modeling.objects().size(), 2U);
  ASSERT_TRUE(harness.history.can_undo());
  ASSERT_NE(harness.history.undo_label(), nullptr);
  EXPECT_EQ(*harness.history.undo_label(), "Create Primitives");

  const CommandResult undone = harness.history.undo();
  ASSERT_TRUE(undone.ok) << undone.message;
  EXPECT_TRUE(harness.modeling.objects().empty());
  EXPECT_FALSE(harness.history.can_undo());
  EXPECT_TRUE(harness.history.can_redo());

  const CommandResult redone = harness.history.redo();
  ASSERT_TRUE(redone.ok) << redone.message;
  EXPECT_EQ(harness.modeling.objects().size(), 2U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            60U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "High Resolution Box"),
            newest_index_count);
}

TEST(PrimitiveCreationToolServiceTests,
     ResetRestoresOnlyTheActiveKindAndRebuildsTheLivePrimitiveOnce) {
  PrimitiveCreationHarness harness;
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Torus).ok);
  ASSERT_TRUE(
      harness.tool.set_setting(kTorusMajorSegmentsSetting, 8.0).ok);
  ASSERT_TRUE(
      harness.tool.set_setting(kTorusMinorSegmentsSetting, 4.0).ok);
  ASSERT_TRUE(
      harness.tool.set_setting(kTorusTubeRadiusRatioSetting, 0.3).ok);
  ASSERT_TRUE(
      harness.tool.set_setting(kPrimitiveFlipFacesSetting, 1.0).ok);
  ASSERT_TRUE(
      harness.tool.accept_preview(valid_primitive_preview(), "Reset Torus")
          .ok);
  const std::uint64_t edited_content =
      compiled_content_id(harness.modeling, "Reset Torus");
  const std::uint64_t edited_revision =
      harness.tool.tool_settings_revision();

  const CommandResult reset = harness.tool.reset_settings();
  ASSERT_TRUE(reset.ok) << reset.message;
  EXPECT_TRUE(reset.changed);
  ASSERT_TRUE(harness.tool.active_kind().has_value());
  EXPECT_EQ(*harness.tool.active_kind(), PrimitiveCreationKind::Torus);
  EXPECT_EQ(harness.tool.torus_settings().major_segments, 32);
  EXPECT_EQ(harness.tool.torus_settings().minor_segments, 12);
  EXPECT_DOUBLE_EQ(harness.tool.torus_settings().tube_radius_ratio, 0.2);
  EXPECT_FALSE(harness.tool.torus_settings().flip_faces);
  EXPECT_EQ(harness.tool.tool_settings_revision(), edited_revision + 1u);
  const std::uint64_t reset_content =
      compiled_content_id(harness.modeling, "Reset Torus");
  EXPECT_NE(reset_content, edited_content);

  const CommandResult repeated = harness.tool.reset_settings();
  ASSERT_TRUE(repeated.ok) << repeated.message;
  EXPECT_FALSE(repeated.changed);
  EXPECT_EQ(harness.tool.tool_settings_revision(), edited_revision + 1u);
  EXPECT_EQ(compiled_content_id(harness.modeling, "Reset Torus"),
            reset_content);
  ASSERT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     CancelRollsBackEveryAcceptedShapeToThePreToolState) {
  PrimitiveCreationHarness harness;
  const ModelingCreateBoxResult baseline =
      harness.modeling.create_box_from_bounds({
          .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                     .max = {1.0f, 1.0f, 1.0f}},
          .name = "Baseline",
      });
  ASSERT_TRUE(baseline.ok) << baseline.message;
  const std::size_t baseline_mesh_count = harness.assets.mesh_ids().size();

  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Cylinder).ok);
  ASSERT_TRUE(harness.tool.set_setting(kCylinderSideCountSetting, 3.0).ok);
  ASSERT_TRUE(harness.tool.set_setting(kCylinderCapsSetting, 0.0).ok);
  const CommandResult first = harness.tool.accept_preview(
      valid_primitive_preview(4.0f), "Open Triangle Cylinder");
  ASSERT_TRUE(first.ok) << first.message;
  EXPECT_EQ(compiled_index_count(harness.modeling, "Open Triangle Cylinder"),
            18U);

  ASSERT_TRUE(harness.tool.set_setting(kCylinderSideCountSetting, 8.0).ok);
  ASSERT_TRUE(harness.tool.set_setting(kCylinderCapsSetting, 1.0).ok);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Open Triangle Cylinder"),
            84U);
  const CommandResult second = harness.tool.accept_preview(
      valid_primitive_preview(8.0f), "Capped Octagonal Cylinder");
  ASSERT_TRUE(second.ok) << second.message;
  EXPECT_TRUE(harness.tool.active());
  EXPECT_EQ(harness.tool.shape_count(), 2U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Open Triangle Cylinder"),
            84U);
  EXPECT_EQ(
      compiled_index_count(harness.modeling, "Capped Octagonal Cylinder"),
      84U);
  const std::vector<ModelingObjectRecord> objects_before_cancel =
      harness.modeling.objects();
  ASSERT_EQ(objects_before_cancel.size(), 3U);
  const ModelingObjectRecord *const first_transient =
      find_object(objects_before_cancel, "Open Triangle Cylinder");
  const ModelingObjectRecord *const second_transient =
      find_object(objects_before_cancel, "Capped Octagonal Cylinder");
  ASSERT_NE(first_transient, nullptr);
  ASSERT_NE(second_transient, nullptr);
  const int first_transient_id = first_transient->modeling_object_id;
  const int second_transient_id = second_transient->modeling_object_id;
  EXPECT_NE(first_transient_id, second_transient_id);

  const CommandResult canceled = harness.tool.cancel();
  ASSERT_TRUE(canceled.ok) << canceled.message;
  EXPECT_TRUE(canceled.changed);
  EXPECT_FALSE(harness.tool.active());
  EXPECT_EQ(harness.tool.shape_count(), 0U);
  ASSERT_EQ(harness.modeling.objects().size(), 1U);
  EXPECT_EQ(harness.assets.mesh_ids().size(), baseline_mesh_count);
  EXPECT_EQ(harness.modeling.objects().front().name, "Baseline");
  EXPECT_EQ(harness.modeling.objects().front().modeling_object_id,
            baseline.object.modeling_object_id);
  EXPECT_FALSE(harness.history.can_undo());

  const ModelingSelectionSummary selection =
      harness.modeling.selection_summary();
  EXPECT_EQ(selection.selected_object_count, 1U);
  EXPECT_EQ(selection.active_object_id, baseline.object.modeling_object_id);

  const ModelingCreateBoxResult replacement =
      harness.modeling.create_box_from_bounds({
          .bounds = {.min = {11.0f, -1.0f, -1.0f},
                     .max = {13.0f, 1.0f, 1.0f}},
          .name = "Replacement After Primitive Cancel",
      });
  ASSERT_TRUE(replacement.ok) << replacement.message;
  EXPECT_GT(replacement.object.modeling_object_id, first_transient_id);
  EXPECT_GT(replacement.object.modeling_object_id, second_transient_id);
}

TEST(PrimitiveCreationToolServiceTests,
     PerShapeFailureRollsBackAcceptedShapesAndExitsTheTool) {
  PrimitiveCreationHarness harness;
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Sphere).ok);
  ASSERT_TRUE(
      harness.tool.set_setting(kSphereRadialSegmentsSetting, 8.0).ok);
  ASSERT_TRUE(harness.tool.set_setting(kSphereRingsSetting, 4.0).ok);
  ASSERT_TRUE(harness.tool.set_setting(kSphereHemisphereSetting, 1.0).ok);

  const CommandResult accepted = harness.tool.accept_preview(
      valid_primitive_preview(), "Accepted Hemisphere");
  ASSERT_TRUE(accepted.ok) << accepted.message;
  ASSERT_TRUE(harness.tool.active());
  ASSERT_EQ(harness.tool.shape_count(), 1U);
  ASSERT_EQ(harness.modeling.objects().size(), 1U);
  EXPECT_FALSE(harness.history.can_undo());

  ModelingBoxPreview invalid = valid_primitive_preview(4.0f);
  invalid.corner_count = invalid.corners.size() - 1U;
  const CommandResult failed =
      harness.tool.accept_preview(invalid, "Invalid Sphere");
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.message.empty());
  EXPECT_FALSE(harness.tool.active());
  EXPECT_EQ(harness.tool.shape_count(), 0U);
  EXPECT_TRUE(harness.modeling.objects().empty());
  EXPECT_FALSE(harness.history.can_undo());
  EXPECT_FALSE(harness.history.can_redo());
  EXPECT_EQ(harness.modeling.selection_summary().selected_object_count, 0U);
}

TEST(PrimitiveCreationToolServiceTests,
     LivePrimitiveMorphsUntilSelectionChangesThenFreezes) {
  PrimitiveCreationHarness harness;
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);

  const CommandResult accepted = harness.tool.accept_preview(
      valid_primitive_preview(), "Live Primitive");
  ASSERT_TRUE(accepted.ok) << accepted.message;
  const std::vector<ModelingObjectRecord> before_morph =
      harness.modeling.objects();
  ASSERT_EQ(before_morph.size(), 1U);
  const int live_id = before_morph.front().modeling_object_id;
  EXPECT_EQ(compiled_index_count(harness.modeling, "Live Primitive"), 36U);

  const CommandResult morphed =
      harness.tool.set_kind(PrimitiveCreationKind::Cylinder);
  ASSERT_TRUE(morphed.ok) << morphed.message;
  ASSERT_TRUE(morphed.changed);
  const std::size_t morphed_index_count =
      compiled_index_count(harness.modeling, "Live Primitive");
  EXPECT_NE(morphed_index_count, 36U);
  const std::vector<ModelingObjectRecord> after_morph =
      harness.modeling.objects();
  ASSERT_EQ(after_morph.size(), 1U);
  EXPECT_EQ(after_morph.front().modeling_object_id, live_id);

  ASSERT_TRUE(harness.modeling.clear_selection().ok);
  const CommandResult future_setting =
      harness.tool.set_setting(kCylinderSideCountSetting, 8.0);
  ASSERT_TRUE(future_setting.ok) << future_setting.message;
  EXPECT_EQ(compiled_index_count(harness.modeling, "Live Primitive"),
            morphed_index_count);

  ASSERT_TRUE(harness.tool.cancel().ok);
  ASSERT_TRUE(harness.tool.activate().ok);
  ASSERT_TRUE(harness.tool.active_kind().has_value());
  EXPECT_EQ(*harness.tool.active_kind(), PrimitiveCreationKind::Cylinder);
  EXPECT_EQ(harness.tool.cylinder_settings().side_count, 8);
  EXPECT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     ConeAndTorusSettingsUseTheSharedLivePrimitiveTransaction) {
  PrimitiveCreationHarness harness;
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Cone).ok);
  ASSERT_TRUE(harness.tool.set_setting(kConeSideCountSetting, 3.0).ok);
  ASSERT_TRUE(harness.tool.set_setting(kConeCapsSetting, 0.0).ok);

  const CommandResult cone_created =
      harness.tool.accept_preview(valid_primitive_preview(), {});
  ASSERT_TRUE(cone_created.ok) << cone_created.message;
  EXPECT_EQ(compiled_index_count(harness.modeling, "Cone"), 9U);

  ASSERT_TRUE(harness.tool.set_setting(kConeSideCountSetting, 8.0).ok);
  ASSERT_TRUE(harness.tool.set_setting(kConeCapsSetting, 1.0).ok);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Cone"), 42U);
  EXPECT_EQ(harness.tool.cone_settings().side_count, 8);
  EXPECT_TRUE(harness.tool.cone_settings().caps);

  ASSERT_TRUE(harness.tool.set_kind(PrimitiveCreationKind::Torus).ok);
  ASSERT_TRUE(harness.tool.set_setting(kTorusMajorSegmentsSetting, 8.0).ok);
  ASSERT_TRUE(harness.tool.set_setting(kTorusMinorSegmentsSetting, 4.0).ok);
  ASSERT_TRUE(
      harness.tool.set_setting(kTorusTubeRadiusRatioSetting, 0.25).ok);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Cone"), 192U);
  const std::uint64_t frozen_cone_content =
      compiled_content_id(harness.modeling, "Cone");

  const CommandResult torus_created =
      harness.tool.accept_preview(valid_primitive_preview(4.0f), {});
  ASSERT_TRUE(torus_created.ok) << torus_created.message;
  EXPECT_EQ(compiled_index_count(harness.modeling, "Torus"), 192U);
  const std::uint64_t initial_torus_content =
      compiled_content_id(harness.modeling, "Torus");
  ASSERT_NE(initial_torus_content, 0U);

  ASSERT_TRUE(
      harness.tool.set_setting(kTorusTubeRadiusRatioSetting, 0.3).ok);
  EXPECT_EQ(compiled_content_id(harness.modeling, "Cone"),
            frozen_cone_content);
  const std::uint64_t updated_torus_content =
      compiled_content_id(harness.modeling, "Torus");
  EXPECT_NE(updated_torus_content, initial_torus_content);
  EXPECT_DOUBLE_EQ(harness.tool.torus_settings().tube_radius_ratio, 0.3);

  const std::uint64_t settings_revision =
      harness.tool.tool_settings_revision();
  const CommandResult rejected_nan = harness.tool.set_setting(
      kTorusTubeRadiusRatioSetting,
      std::numeric_limits<double>::quiet_NaN());
  EXPECT_FALSE(rejected_nan.ok);
  EXPECT_EQ(harness.tool.tool_settings_revision(), settings_revision);
  EXPECT_DOUBLE_EQ(harness.tool.torus_settings().tube_radius_ratio, 0.3);
  EXPECT_EQ(compiled_content_id(harness.modeling, "Torus"),
            updated_torus_content);

  const CommandResult applied = harness.tool.apply();
  ASSERT_TRUE(applied.ok) << applied.message;
  ASSERT_TRUE(harness.history.can_undo());
  ASSERT_TRUE(harness.history.undo().ok);
  EXPECT_TRUE(harness.modeling.objects().empty());

  ASSERT_TRUE(harness.tool.activate().ok);
  ASSERT_TRUE(harness.tool.active_kind().has_value());
  EXPECT_EQ(*harness.tool.active_kind(), PrimitiveCreationKind::Torus);
  EXPECT_EQ(harness.tool.torus_settings().major_segments, 8);
  EXPECT_EQ(harness.tool.torus_settings().minor_segments, 4);
  EXPECT_DOUBLE_EQ(harness.tool.torus_settings().tube_radius_ratio, 0.3);
  EXPECT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     PublicDeselectAndConcurrentEditRemainValidAfterCancel) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const ModelingCreateBoxResult baseline =
      session.modeling().create_box_from_bounds({
          .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                     .max = {1.0f, 1.0f, 1.0f}},
          .name = "Concurrent Baseline",
      });
  ASSERT_TRUE(baseline.ok) << baseline.message;
  const std::size_t baseline_mesh_count = session.assets().mesh_ids().size();
  ASSERT_FALSE(editor.history().can_undo());

  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(4.0f),
                                  "Canceled Transient")
                  .ok);
  const std::vector<ModelingObjectRecord> active_objects =
      session.modeling().objects();
  const ModelingObjectRecord *const transient =
      find_object(active_objects, "Canceled Transient");
  ASSERT_NE(transient, nullptr);
  const QuaderApiVoidResult rejected_transient_edit =
      editor.scene().set_visible(transient->entity, false);
  EXPECT_FALSE(rejected_transient_edit.ok());
  EXPECT_TRUE(session.primitive_creation().active());
  EXPECT_FALSE(session.history().can_undo());
  const QuaderApiVoidResult rejected_modeling_preview =
      editor.modeling().begin_preview("half_cut");
  EXPECT_FALSE(rejected_modeling_preview.ok());
  EXPECT_TRUE(session.primitive_creation().active());
  EXPECT_FALSE(session.history().can_undo());
  const std::size_t transient_indices =
      compiled_index_count(session.modeling(), "Canceled Transient");

  ASSERT_TRUE(editor.selection().clear().ok());
  EXPECT_FALSE(session.history().can_undo());
  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive",
                               "primitive.box.subdivisions_x", 3.0)
                  .ok());
  EXPECT_EQ(compiled_index_count(session.modeling(), "Canceled Transient"),
            transient_indices);

  ASSERT_TRUE(editor.scene().set_visible(baseline.object.entity, false).ok());
  ASSERT_TRUE(editor.history().can_undo());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count);
  EXPECT_EQ(session.modeling().objects().front().modeling_object_id,
            baseline.object.modeling_object_id);
  const std::optional<QuaderEntitySnapshot> canceled_baseline =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(canceled_baseline.has_value());
  EXPECT_FALSE(canceled_baseline->visible);
  EXPECT_EQ(session.modeling().selection_summary().active_object_id,
            baseline.object.modeling_object_id);

  ASSERT_TRUE(editor.history().undo().ok());
  const std::optional<QuaderEntitySnapshot> undone_visibility =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(undone_visibility.has_value());
  EXPECT_TRUE(undone_visibility->visible);
  ASSERT_TRUE(editor.history().redo().ok());
  const std::optional<QuaderEntitySnapshot> redone_visibility =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(redone_visibility.has_value());
  EXPECT_FALSE(redone_visibility->visible);
}

TEST(PrimitiveCreationToolServiceTests,
     ApplyHistoryOrdersConcurrentEditsBeforeThePrimitiveDelta) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const ModelingCreateBoxResult baseline =
      session.modeling().create_box_from_bounds({
          .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                     .max = {1.0f, 1.0f, 1.0f}},
          .name = "Apply Baseline",
      });
  ASSERT_TRUE(baseline.ok) << baseline.message;
  const std::size_t baseline_mesh_count = session.assets().mesh_ids().size();

  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(4.0f),
                                  "Applied Primitive")
                  .ok);
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_FALSE(session.history().can_undo());
  ASSERT_TRUE(editor.scene().set_visible(baseline.object.entity, false).ok());
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  const std::vector<ModelingObjectRecord> applied_objects =
      session.modeling().objects();
  ASSERT_EQ(applied_objects.size(), 2U);
  const ModelingObjectRecord *const applied_primitive =
      find_object(applied_objects, "Applied Primitive");
  ASSERT_NE(applied_primitive, nullptr);
  const AssetId applied_mesh = applied_primitive->mesh;
  ASSERT_NE(applied_mesh, kInvalidAssetId);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);

  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count);
  EXPECT_EQ(session.modeling().objects().front().modeling_object_id,
            baseline.object.modeling_object_id);
  const std::optional<QuaderEntitySnapshot> after_primitive_undo =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(after_primitive_undo.has_value());
  EXPECT_FALSE(after_primitive_undo->visible);

  ASSERT_TRUE(editor.history().undo().ok());
  const std::optional<QuaderEntitySnapshot> after_visibility_undo =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(after_visibility_undo.has_value());
  EXPECT_TRUE(after_visibility_undo->visible);

  ASSERT_TRUE(editor.history().redo().ok());
  ASSERT_TRUE(editor.history().redo().ok());
  const std::vector<ModelingObjectRecord> redone_objects =
      session.modeling().objects();
  ASSERT_EQ(redone_objects.size(), 2U);
  const ModelingObjectRecord *const redone_primitive =
      find_object(redone_objects, "Applied Primitive");
  ASSERT_NE(redone_primitive, nullptr);
  EXPECT_EQ(redone_primitive->mesh, applied_mesh);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);
  const std::optional<QuaderEntitySnapshot> after_redo =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(after_redo.has_value());
  EXPECT_FALSE(after_redo->visible);

  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count);
  ASSERT_TRUE(editor.history().redo().ok());
  const std::vector<ModelingObjectRecord> repeated_redo_objects =
      session.modeling().objects();
  const ModelingObjectRecord *const repeated_redo_primitive =
      find_object(repeated_redo_objects, "Applied Primitive");
  ASSERT_NE(repeated_redo_primitive, nullptr);
  EXPECT_EQ(repeated_redo_primitive->mesh, applied_mesh);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);
}

TEST(PrimitiveCreationToolServiceTests,
     TransientPublicDeselectPreservesTheExistingRedoBranch) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  QuaderCreateEntityArgs args;
  args.name = "Redo Sentinel";
  const QuaderApiResult<QuaderEntityRef> sentinel =
      editor.scene().create_entity(args);
  ASSERT_TRUE(sentinel.ok()) << sentinel.error_message();
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.history().can_redo());

  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(),
                                  "Redo Branch Transient")
                  .ok);
  ASSERT_TRUE(editor.selection().clear().ok());
  EXPECT_TRUE(session.history().can_redo());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());

  EXPECT_TRUE(session.modeling().objects().empty());
  ASSERT_TRUE(editor.history().can_redo());
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(editor.query().entity(sentinel.value().id).has_value());
}

TEST(PrimitiveCreationToolServiceTests,
     SettingsAreFrozenWhileTheCurrentFootprintIsBeingDrawn) {
  PrimitiveCreationHarness harness;
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);
  ASSERT_TRUE(
      harness.tool.set_setting(kBoxSubdivisionsXSetting, 4.0).ok);
  const ModelingBoxPreview footprint =
      harness.modeling.begin_box_footprint({
          .screen_position = {400.0f, 300.0f},
          .ray = {.origin = {0.0f, 4.0f, 0.0f},
                  .direction = {0.0f, -1.0f, 0.0f}},
          .grid_size = 1.0,
      });
  ASSERT_TRUE(footprint.ok) << footprint.message;
  ASSERT_EQ(footprint.drag_state, ModelingBoxDragState::Footprint);

  const CommandResult changed_during_draw =
      harness.tool.set_setting(kBoxSubdivisionsXSetting, 5.0);
  EXPECT_FALSE(changed_during_draw.ok);
  EXPECT_FALSE(changed_during_draw.changed);
  EXPECT_EQ(harness.tool.box_settings().subdivisions_x, 4);

  const CommandResult reset_during_draw = harness.tool.reset_settings();
  EXPECT_FALSE(reset_during_draw.ok);
  EXPECT_FALSE(reset_during_draw.changed);
  EXPECT_FALSE(harness.tool.can_reset_settings());
  EXPECT_EQ(harness.tool.box_settings().subdivisions_x, 4);
  EXPECT_EQ(harness.modeling.box_preview(false).drag_state,
            ModelingBoxDragState::Footprint);

  EXPECT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     ResetAvailabilityRevisionTracksPrimitiveFootprintLifecycle) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(editor.tools().active_settings().can_reset);

  const std::uint64_t idle_revision =
      editor.tools().active_settings_revision();
  EXPECT_EQ(editor.tools().active_settings_revision(), idle_revision);

  const ModelingBoxPreview footprint =
      session.modeling().begin_box_footprint({
          .screen_position = {400.0f, 300.0f},
          .ray = {.origin = {0.0f, 4.0f, 0.0f},
                  .direction = {0.0f, -1.0f, 0.0f}},
          .grid_size = 1.0,
      });
  ASSERT_TRUE(footprint.ok) << footprint.message;
  ASSERT_EQ(footprint.drag_state, ModelingBoxDragState::Footprint);

  const std::uint64_t footprint_revision =
      editor.tools().active_settings_revision();
  EXPECT_GT(footprint_revision, idle_revision);
  EXPECT_EQ(editor.tools().active_settings_revision(), footprint_revision);
  EXPECT_FALSE(editor.tools().active_settings().can_reset);
  const QuaderApiVoidResult rejected =
      editor.tools().reset_active_settings();
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());

  const ModelingCommandResult canceled = session.modeling().cancel_box();
  ASSERT_TRUE(canceled.ok) << canceled.message;
  EXPECT_TRUE(canceled.changed);
  const std::uint64_t restored_revision =
      editor.tools().active_settings_revision();
  EXPECT_GT(restored_revision, footprint_revision);
  EXPECT_TRUE(editor.tools().active_settings().can_reset);

  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(PrimitiveCreationToolServiceTests,
     HiddenAppliedSphereDoesNotDriveTheNextConstructionPlane) {
  PrimitiveCreationHarness harness;
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Sphere).ok);
  ASSERT_TRUE(harness.tool
                  .accept_preview(valid_primitive_preview(), "Hidden Sphere")
                  .ok);
  ASSERT_TRUE(harness.tool.apply().ok);

  const std::vector<ModelingObjectRecord> objects = harness.modeling.objects();
  ASSERT_EQ(objects.size(), 1U);
  ASSERT_TRUE(harness.scene.set_visible(objects.front().entity, false));
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);

  const ModelingBoxToolEvent event{
      .ray = {.origin = {0.5f, 5.0f, 5.0f},
              .direction = {0.0f, -1.0f, -1.0f}},
      .grid_size = 1.0,
      .snap_enabled = false,
  };
  EXPECT_FALSE(harness.modeling
                   .pick_object_entity(ModelingPickSelectRequest{
                       .ray = event.ray,
                   })
                   .has_value());

  const ModelingBoxPreview hover = harness.modeling.update_box_hover(event);
  ASSERT_TRUE(hover.ok) << hover.message;
  ASSERT_TRUE(hover.valid);
  EXPECT_NEAR(hover.normal.x, 0.0f, 0.0001f);
  EXPECT_NEAR(hover.normal.y, 1.0f, 0.0001f);
  EXPECT_NEAR(hover.normal.z, 0.0f, 0.0001f);
  ASSERT_EQ(hover.footprint_count, 4U);
  for (std::size_t index = 0; index < hover.footprint_count; ++index) {
    EXPECT_NEAR(hover.footprint[index].y, 0.0f, 0.0001f);
  }

  const ModelingBoxPreview footprint =
      harness.modeling.begin_box_footprint(event);
  ASSERT_TRUE(footprint.ok) << footprint.message;
  EXPECT_EQ(footprint.drag_state, ModelingBoxDragState::Footprint);
  EXPECT_NEAR(footprint.normal.y, 1.0f, 0.0001f);
  ASSERT_EQ(footprint.footprint_count, 4U);
  for (std::size_t index = 0; index < footprint.footprint_count; ++index) {
    EXPECT_NEAR(footprint.footprint[index].y, 0.0f, 0.0001f);
  }
  EXPECT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     TranslatedVisibleMeshDrivesPlacementOnlyAtItsWorldPosition) {
  PrimitiveCreationHarness harness;
  const ModelingCreateBoxResult box = harness.modeling.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Translated Surface",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(harness.scene.set_transform(
      box.object.entity,
      TransformComponent{.position = {10.0f, 0.0f, 0.0f}}));
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);

  const auto event_at_x = [](float x) {
    return ModelingBoxToolEvent{
        .ray = {.origin = {x, 2.0f, 5.0f},
                .direction = {0.0f, -1.5f, -4.0f}},
        .grid_size = 1.0,
        .snap_enabled = false,
    };
  };

  const ModelingBoxToolEvent old_position = event_at_x(0.0f);
  EXPECT_FALSE(harness.modeling
                   .pick_object_entity(ModelingPickSelectRequest{
                       .ray = old_position.ray,
                   })
                   .has_value());
  const ModelingBoxPreview old_hover =
      harness.modeling.update_box_hover(old_position);
  ASSERT_TRUE(old_hover.ok) << old_hover.message;
  ASSERT_TRUE(old_hover.valid);
  EXPECT_NEAR(old_hover.normal.y, 1.0f, 0.0001f);
  ASSERT_EQ(old_hover.footprint_count, 4U);
  for (std::size_t index = 0; index < old_hover.footprint_count; ++index) {
    EXPECT_NEAR(old_hover.footprint[index].y, 0.0f, 0.0001f);
  }

  const ModelingBoxToolEvent world_position = event_at_x(10.0f);
  const std::optional<EntityId> world_hit =
      harness.modeling.pick_object_entity(ModelingPickSelectRequest{
          .ray = world_position.ray,
      });
  ASSERT_TRUE(world_hit.has_value());
  EXPECT_EQ(*world_hit, box.object.entity);
  const ModelingBoxPreview world_hover =
      harness.modeling.update_box_hover(world_position);
  ASSERT_TRUE(world_hover.ok) << world_hover.message;
  ASSERT_TRUE(world_hover.valid);
  EXPECT_NEAR(world_hover.normal.y, 0.0f, 0.0001f);
  EXPECT_NEAR(std::abs(world_hover.normal.z), 1.0f, 0.0001f);
  ASSERT_EQ(world_hover.footprint_count, 4U);
  for (std::size_t index = 0; index < world_hover.footprint_count; ++index) {
    EXPECT_NEAR(world_hover.footprint[index].z, 1.0f, 0.0001f);
  }
  EXPECT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     BackFacingOpenMeshPlaneCanDrivePrimitivePlacement) {
  PrimitiveCreationHarness harness;
  quader_poly::Document plane;
  const quader_poly::ElementId a =
      quader_poly::add_vertex(plane, {-1.0f, 2.0f, -1.0f});
  const quader_poly::ElementId b =
      quader_poly::add_vertex(plane, {-1.0f, 2.0f, 1.0f});
  const quader_poly::ElementId c =
      quader_poly::add_vertex(plane, {1.0f, 2.0f, 1.0f});
  const quader_poly::ElementId d =
      quader_poly::add_vertex(plane, {1.0f, 2.0f, -1.0f});
  ASSERT_NE(quader_poly::add_face(plane, std::array{a, b, c, d}),
            quader_poly::kInvalidElementId);
  const ModelingCreateBoxResult created = harness.modeling.create_mesh({
      .document = std::move(plane),
      .name = "Back-Facing Plane",
  });
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);

  const ModelingBoxToolEvent event{
      .ray = {.origin = {0.0f, -2.0f, 0.0f},
              .direction = {0.0f, 1.0f, 0.0f}},
      .grid_size = 1.0,
      .snap_enabled = false,
  };
  const std::optional<EntityId> hit =
      harness.modeling.pick_object_entity(ModelingPickSelectRequest{
          .ray = event.ray,
      });
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(*hit, created.object.entity);

  const ModelingBoxPreview hover = harness.modeling.update_box_hover(event);
  ASSERT_TRUE(hover.ok) << hover.message;
  ASSERT_TRUE(hover.valid);
  ASSERT_EQ(hover.footprint_count, 4U);
  for (std::size_t index = 0; index < hover.footprint_count; ++index) {
    EXPECT_NEAR(hover.footprint[index].y, 2.0f, 0.0001f);
  }
  EXPECT_TRUE(harness.tool.cancel().ok);
}

} // namespace
} // namespace quader
