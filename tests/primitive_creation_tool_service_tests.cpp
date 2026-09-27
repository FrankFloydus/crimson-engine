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
#include "editor/session/tool_session_defaults.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_texture_mapping.hpp"
#include "support/command_behavior_fixture.hpp"
#include "support/command_observable_state.hpp"

#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
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
  PrimitiveCreationToolService tool;

  explicit PrimitiveCreationHarness(
      PrimitiveCreationSessionDefaults *session_defaults = nullptr)
      : tool(history, modeling, {}, session_defaults) {}
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

void set_nondefault_primitive_settings(QuaderToolsApi &tools) {
  using Kind = PrimitiveCreationKind;
  struct Setting {
    Kind kind;
    std::string_view name;
    double value;
  };
  constexpr Setting kSettings[] = {
      {Kind::Box, kBoxSubdivisionsXSetting, 2.0},
      {Kind::Box, kBoxSubdivisionsYSetting, 3.0},
      {Kind::Box, kBoxSubdivisionsZSetting, 4.0},
      {Kind::Box, kPrimitiveFlipFacesSetting, 1.0},
      {Kind::Cylinder, kCylinderSideCountSetting, 8.0},
      {Kind::Cylinder, kCylinderCapsSetting, 0.0},
      {Kind::Cylinder, kPrimitiveFlipFacesSetting, 1.0},
      {Kind::Cone, kConeSideCountSetting, 8.0},
      {Kind::Cone, kConeCapsSetting, 0.0},
      {Kind::Cone, kPrimitiveFlipFacesSetting, 1.0},
      {Kind::Sphere, kSphereRadialSegmentsSetting, 8.0},
      {Kind::Sphere, kSphereRingsSetting, 4.0},
      {Kind::Sphere, kSphereHemisphereSetting, 1.0},
      {Kind::Sphere, kPrimitiveFlipFacesSetting, 1.0},
      {Kind::Sphere, kSphereTypeSetting, 1.0},
      {Kind::Sphere, kSphereSubdivisionSetting, 2.0},
      {Kind::Torus, kTorusMajorSegmentsSetting, 8.0},
      {Kind::Torus, kTorusMinorSegmentsSetting, 4.0},
      {Kind::Torus, kTorusTubeRadiusRatioSetting, 0.3},
      {Kind::Torus, kPrimitiveFlipFacesSetting, 1.0},
  };
  for (const Setting &setting : kSettings) {
    SCOPED_TRACE(setting.name);
    ASSERT_TRUE(tools.set_setting(kCreatePrimitiveToolId,
                                  kPrimitiveTypeSetting,
                                  static_cast<double>(setting.kind)).ok());
    ASSERT_TRUE(tools.set_setting(kCreatePrimitiveToolId, setting.name,
                                  setting.value).ok());
  }
}

PrimitiveCreationSessionDefaults primitive_settings(
    const PrimitiveCreationToolService &tool) {
  return {.preferred_kind = tool.active_kind().value_or(PrimitiveCreationKind::Box),
          .box = tool.box_settings(),
          .cylinder = tool.cylinder_settings(),
          .cone = tool.cone_settings(),
          .sphere = tool.sphere_settings(),
          .torus = tool.torus_settings()};
}

void expect_primitive_settings(
    const PrimitiveCreationToolService &tool,
    const PrimitiveCreationSessionDefaults &expected) {
  EXPECT_EQ(tool.box_settings().subdivisions_x, expected.box.subdivisions_x);
  EXPECT_EQ(tool.box_settings().subdivisions_y, expected.box.subdivisions_y);
  EXPECT_EQ(tool.box_settings().subdivisions_z, expected.box.subdivisions_z);
  EXPECT_EQ(tool.box_settings().flip_faces, expected.box.flip_faces);
  EXPECT_EQ(tool.cylinder_settings().side_count, expected.cylinder.side_count);
  EXPECT_EQ(tool.cylinder_settings().caps, expected.cylinder.caps);
  EXPECT_EQ(tool.cylinder_settings().flip_faces, expected.cylinder.flip_faces);
  EXPECT_EQ(tool.cone_settings().side_count, expected.cone.side_count);
  EXPECT_EQ(tool.cone_settings().caps, expected.cone.caps);
  EXPECT_EQ(tool.cone_settings().flip_faces, expected.cone.flip_faces);
  EXPECT_EQ(tool.sphere_settings().radial_segments, expected.sphere.radial_segments);
  EXPECT_EQ(tool.sphere_settings().rings, expected.sphere.rings);
  EXPECT_EQ(tool.sphere_settings().hemisphere, expected.sphere.hemisphere);
  EXPECT_EQ(tool.sphere_settings().flip_faces, expected.sphere.flip_faces);
  EXPECT_EQ(tool.sphere_settings().type, expected.sphere.type);
  EXPECT_EQ(tool.sphere_settings().subdivision, expected.sphere.subdivision);
  EXPECT_EQ(tool.torus_settings().major_segments, expected.torus.major_segments);
  EXPECT_EQ(tool.torus_settings().minor_segments, expected.torus.minor_segments);
  EXPECT_DOUBLE_EQ(tool.torus_settings().tube_radius_ratio,
                   expected.torus.tube_radius_ratio);
  EXPECT_EQ(tool.torus_settings().flip_faces, expected.torus.flip_faces);
}

void expect_default_primitive_settings(const PrimitiveCreationToolService &tool) {
  expect_primitive_settings(tool, {});
}

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
     SharedOptionsAdoptLatestOnActivationWithoutSharingLiveState) {
  PrimitiveCreationSessionDefaults remembered;
  PrimitiveCreationHarness first(&remembered);
  PrimitiveCreationHarness second(&remembered);
  ASSERT_TRUE(first.tool.activate(PrimitiveCreationKind::Torus).ok);
  ASSERT_TRUE(first.tool.set_setting(kTorusMajorSegmentsSetting, 8.0).ok);
  ASSERT_TRUE(first.tool.set_setting(kTorusMinorSegmentsSetting, 4.0).ok);
  ASSERT_TRUE(first.tool.set_setting(kTorusTubeRadiusRatioSetting, 0.3).ok);
  ASSERT_TRUE(first.tool.accept_preview(valid_primitive_preview(), "First Torus").ok);
  const auto original_content = compiled_content_id(first.modeling, "First Torus");

  // This owner predates the edits and adopts them only when activated.
  ASSERT_TRUE(second.tool.activate().ok);
  EXPECT_EQ(second.tool.active_kind(), PrimitiveCreationKind::Torus);
  expect_primitive_settings(second.tool, remembered);
  EXPECT_FALSE(second.tool.live_primitive_creation().has_value());
  EXPECT_EQ(second.tool.shape_count(), 0U);
  EXPECT_TRUE(second.modeling.objects().empty());
  ASSERT_TRUE(second.tool.set_kind(PrimitiveCreationKind::Box).ok);
  ASSERT_TRUE(second.tool.set_setting(kBoxSubdivisionsXSetting, 3.0).ok);
  EXPECT_EQ(first.tool.active_kind(), PrimitiveCreationKind::Torus);
  EXPECT_EQ(first.tool.box_settings().subdivisions_x, 1);
  EXPECT_EQ(compiled_content_id(first.modeling, "First Torus"), original_content);

  // Independently active owners merge only the options they accepted.
  ASSERT_TRUE(first.tool.set_setting(kTorusTubeRadiusRatioSetting, 0.25).ok);
  EXPECT_EQ(remembered.box.subdivisions_x, 3);
  EXPECT_DOUBLE_EQ(remembered.torus.tube_radius_ratio, 0.25);
  EXPECT_EQ(remembered.preferred_kind, PrimitiveCreationKind::Torus);
  ASSERT_TRUE(second.tool.cancel().ok);
  EXPECT_EQ(remembered.preferred_kind, PrimitiveCreationKind::Torus);
  EXPECT_DOUBLE_EQ(remembered.torus.tube_radius_ratio, 0.25);

  PrimitiveCreationHarness third(&remembered);
  ASSERT_TRUE(third.tool.activate().ok);
  EXPECT_EQ(third.tool.active_kind(), PrimitiveCreationKind::Torus);
  expect_primitive_settings(third.tool, remembered);
  EXPECT_EQ(third.tool.box_settings().subdivisions_x, 3);
  EXPECT_FALSE(third.tool.live_primitive_creation().has_value());
  ASSERT_TRUE(first.tool.cancel().ok);
  EXPECT_TRUE(first.modeling.objects().empty());
  EXPECT_TRUE(third.tool.active());
  ASSERT_TRUE(third.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     FreshApplicationAndStandaloneOwnersStartWithDefaults) {
  PrimitiveCreationHarness local;
  ASSERT_TRUE(local.tool.activate(PrimitiveCreationKind::Sphere).ok);
  ASSERT_TRUE(local.tool.set_setting(kSphereTypeSetting, 1.0).ok);
  ASSERT_TRUE(local.tool.set_setting(kSphereSubdivisionSetting, 2.0).ok);
  const auto local_options = primitive_settings(local.tool);
  ASSERT_TRUE(local.tool.cancel().ok);
  ASSERT_TRUE(local.tool.activate().ok);
  EXPECT_EQ(local.tool.active_kind(), PrimitiveCreationKind::Sphere);
  expect_primitive_settings(local.tool, local_options);

  PrimitiveCreationHarness independent;
  ASSERT_TRUE(independent.tool.activate().ok);
  EXPECT_EQ(independent.tool.active_kind(), PrimitiveCreationKind::Box);
  expect_default_primitive_settings(independent.tool);

  PrimitiveCreationSessionDefaults application_options;
  PrimitiveCreationHarness application(&application_options);
  ASSERT_TRUE(application.tool.activate(PrimitiveCreationKind::Cone).ok);
  ASSERT_TRUE(application.tool.set_setting(kConeSideCountSetting, 7.0).ok);
  ASSERT_TRUE(application.tool.cancel().ok);
  PrimitiveCreationSessionDefaults next_application_options;
  PrimitiveCreationHarness next_application(&next_application_options);
  ASSERT_TRUE(next_application.tool.activate().ok);
  EXPECT_EQ(next_application.tool.active_kind(), PrimitiveCreationKind::Box);
  expect_default_primitive_settings(next_application.tool);
  ASSERT_TRUE(local.tool.cancel().ok);
  ASSERT_TRUE(independent.tool.cancel().ok);
  ASSERT_TRUE(next_application.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     FailedLiveOptionsDoNotReplaceRememberedValuesOrKind) {
  for (const bool change_kind : {false, true}) {
    SCOPED_TRACE(change_kind);
    PrimitiveCreationSessionDefaults remembered;
    PrimitiveCreationHarness harness(&remembered);
    ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Torus).ok);
    ASSERT_TRUE(harness.tool.set_setting(kTorusMajorSegmentsSetting, 8.0).ok);
    ASSERT_TRUE(harness.tool.set_setting(kTorusMinorSegmentsSetting, 4.0).ok);
    ASSERT_TRUE(harness.tool.set_setting(kTorusTubeRadiusRatioSetting, 0.3).ok);
    ASSERT_TRUE(harness.tool.accept_preview(valid_primitive_preview(), "Rejected Edit").ok);
    const auto accepted_options = remembered;
    const auto accepted_content = compiled_content_id(harness.modeling, "Rejected Edit");
    harness.modeling.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::CommittedSceneNotify);
    const auto rejected = change_kind
        ? harness.tool.set_kind(PrimitiveCreationKind::Sphere)
        : harness.tool.set_setting(kTorusTubeRadiusRatioSetting, 0.4);
    EXPECT_FALSE(rejected.ok);
    EXPECT_FALSE(rejected.changed);
    EXPECT_TRUE(harness.tool.active());
    EXPECT_EQ(harness.tool.active_kind(), PrimitiveCreationKind::Torus);
    ASSERT_EQ(harness.modeling.objects().size(), 1U);
    EXPECT_TRUE(harness.tool.live_primitive_creation().has_value());
    EXPECT_EQ(compiled_content_id(harness.modeling, "Rejected Edit"), accepted_content);
    expect_primitive_settings(harness.tool, accepted_options);
    EXPECT_EQ(remembered.preferred_kind, accepted_options.preferred_kind);
    harness.modeling.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    const auto retried = change_kind
        ? harness.tool.set_kind(PrimitiveCreationKind::Sphere)
        : harness.tool.set_setting(kTorusTubeRadiusRatioSetting, 0.4);
    ASSERT_TRUE(retried.ok) << retried.message;
    ASSERT_TRUE(harness.tool.finish().ok);
    EXPECT_TRUE(harness.history.can_undo());

    auto previous_options = accepted_options;
    PrimitiveCreationHarness observer(&previous_options);
    ASSERT_TRUE(observer.tool.activate().ok);
    EXPECT_EQ(observer.tool.active_kind(), PrimitiveCreationKind::Torus);
    expect_primitive_settings(observer.tool, accepted_options);
    ASSERT_TRUE(observer.tool.cancel().ok);
  }
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
    const auto live_cube = editor.tools().live_primitive_cube();
    ASSERT_TRUE(live_cube.has_value());
    EXPECT_EQ(live_cube->object_id, object->modeling_object_id);
    EXPECT_TRUE(near_point(live_cube->world_origin, centers[index]));
    EXPECT_TRUE(near_point(live_cube->world_x,
                           transform_vector(*world, {1.0f, 0.0f, 0.0f})));
    EXPECT_TRUE(near_point(live_cube->world_y,
                           transform_vector(*world, {0.0f, 1.0f, 0.0f})));
    EXPECT_TRUE(near_point(live_cube->world_z,
                           transform_vector(*world, {0.0f, 0.0f, 1.0f})));
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
  for (std::size_t remaining = created.size(); remaining > 0U; --remaining) {
    ASSERT_TRUE(editor.history().undo().ok());
    for (std::size_t index = 0; index < created.size(); ++index) {
      EXPECT_EQ(session.scene().exists(created[index].entity),
                index + 1U < remaining);
    }
  }
  ASSERT_TRUE(session.scene().exists(definition));
  ASSERT_TRUE(session.scene().is_instance_definition_entity(definition));
  ASSERT_TRUE(session.scene().exists(occurrence));
  for (const ModelingObjectRecord &object : created) {
    SCOPED_TRACE(::testing::Message() << "after Undo: " << object.name);
    EXPECT_FALSE(session.scene().exists(object.entity));
  }
  for (std::size_t count = 0; count < created.size(); ++count) {
    ASSERT_TRUE(editor.history().redo().ok());
  }
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
     LiveSettingsUpdateNewestShapeAndApplyAsIndividualHistoryEntries) {
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
  EXPECT_EQ(*harness.history.undo_label(), "Create Primitive");

  const CommandResult undone = harness.history.undo();
  ASSERT_TRUE(undone.ok) << undone.message;
  ASSERT_EQ(harness.modeling.objects().size(), 1U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            60U);
  EXPECT_TRUE(harness.history.can_undo());
  ASSERT_TRUE(harness.history.undo().ok);
  EXPECT_TRUE(harness.modeling.objects().empty());
  EXPECT_FALSE(harness.history.can_undo());
  EXPECT_TRUE(harness.history.can_redo());

  const CommandResult redone = harness.history.redo();
  ASSERT_TRUE(redone.ok) << redone.message;
  ASSERT_EQ(harness.modeling.objects().size(), 1U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            60U);
  ASSERT_TRUE(harness.history.redo().ok);
  EXPECT_EQ(harness.modeling.objects().size(), 2U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            60U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "High Resolution Box"),
            newest_index_count);
}

TEST(PrimitiveCreationToolServiceTests,
     UndoLimitRetainsNewestShapesWithoutLosingOlderAppliedObjects) {
  PrimitiveCreationHarness harness;
  harness.history.set_max_steps(2U);
  ASSERT_TRUE(harness.tool.activate().ok);
  const std::array<std::string_view, 3> names{
      "Permanent Box", "Retained Box", "Newest Box"};
  for (std::size_t index = 0; index < names.size(); ++index) {
    ASSERT_TRUE(harness.tool
                    .accept_preview(valid_primitive_preview(
                                        static_cast<float>(index) * 4.0f),
                                    std::string(names[index]))
                    .ok);
  }
  ASSERT_TRUE(harness.tool.set_setting(kBoxSubdivisionsXSetting, 3.0).ok);
  const auto objects = harness.modeling.objects();
  const auto *const permanent = find_object(objects, names[0]);
  const auto *const newest = find_object(objects, names[2]);
  ASSERT_NE(permanent, nullptr);
  ASSERT_NE(newest, nullptr);
  const std::uint64_t newest_geometry =
      compiled_content_id(harness.modeling, names[2]);

  ASSERT_TRUE(harness.tool.undo().ok);
  ASSERT_TRUE(harness.tool.undo().ok);
  EXPECT_EQ(harness.tool.shape_count(), 1U);
  EXPECT_FALSE(harness.tool.can_undo());
  ASSERT_EQ(harness.modeling.objects().size(), 1U);
  EXPECT_TRUE(harness.tool.owns_created_object(permanent->modeling_object_id));
  EXPECT_FALSE(harness.tool.owns_created_object(newest->modeling_object_id));
  ASSERT_TRUE(harness.tool.redo().ok);
  ASSERT_TRUE(harness.tool.redo().ok);
  EXPECT_EQ(harness.tool.shape_count(), 3U);
  EXPECT_FALSE(harness.tool.can_redo());
  EXPECT_EQ(compiled_content_id(harness.modeling, names[2]), newest_geometry);
  EXPECT_TRUE(harness.tool.owns_created_object(newest->modeling_object_id));

  ASSERT_TRUE(harness.tool.apply().ok);
  EXPECT_EQ(harness.history.undo_count(), 2U);
  ASSERT_TRUE(harness.history.undo().ok);
  ASSERT_TRUE(harness.history.undo().ok);
  EXPECT_FALSE(harness.history.can_undo());
  ASSERT_EQ(harness.modeling.objects().size(), 1U);
  EXPECT_EQ(harness.modeling.objects().front().modeling_object_id,
            permanent->modeling_object_id);
  ASSERT_TRUE(harness.history.redo().ok);
  ASSERT_TRUE(harness.history.redo().ok);
  EXPECT_EQ(compiled_content_id(harness.modeling, names[2]), newest_geometry);
}

TEST(PrimitiveCreationToolServiceTests,
     BranchAfterLimitedUndoKeepsPermanentPrefixAndRetiresRedo) {
  PrimitiveCreationHarness harness;
  harness.history.set_max_steps(2U);
  ASSERT_TRUE(harness.tool.activate().ok);
  ASSERT_TRUE(harness.tool
                  .accept_preview(valid_primitive_preview(), "Permanent Box")
                  .ok);
  ASSERT_TRUE(harness.tool
                  .accept_preview(valid_primitive_preview(4.0f), "Retained Box")
                  .ok);
  ASSERT_TRUE(harness.tool
                  .accept_preview(valid_primitive_preview(8.0f), "Retired Box")
                  .ok);
  const auto original = harness.modeling.objects();
  const auto *const retired = find_object(original, "Retired Box");
  ASSERT_NE(retired, nullptr);
  ASSERT_TRUE(harness.tool.undo().ok);
  ASSERT_TRUE(harness.tool
                  .accept_preview(valid_primitive_preview(12.0f), "Branch Box")
                  .ok);
  EXPECT_FALSE(harness.tool.can_redo());
  EXPECT_EQ(harness.tool.shape_count(), 3U);
  EXPECT_FALSE(harness.tool.owns_created_object(retired->modeling_object_id));
  const auto branched = harness.modeling.objects();
  ASSERT_NE(find_object(branched, "Branch Box"), nullptr);
  EXPECT_EQ(find_object(branched, "Retired Box"), nullptr);
  ASSERT_TRUE(harness.tool.apply().ok);
  EXPECT_EQ(harness.history.undo_count(), 2U);
  ASSERT_TRUE(harness.history.undo().ok);
  ASSERT_TRUE(harness.history.undo().ok);
  ASSERT_EQ(harness.modeling.objects().size(), 1U);
  EXPECT_EQ(harness.modeling.objects().front().name, "Permanent Box");
  EXPECT_FALSE(harness.history.can_undo());
}

TEST(PrimitiveCreationToolServiceTests,
     CancelRestoresWholeSessionAfterOldShapeHistoryWasEvicted) {
  PrimitiveCreationHarness harness;
  harness.history.set_max_steps(1U);
  const std::size_t baseline_meshes = harness.assets.mesh_ids().size();
  ASSERT_TRUE(harness.tool.activate().ok);
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(harness.tool
                    .accept_preview(valid_primitive_preview(
                                        static_cast<float>(index) * 4.0f),
                                    "Transient Box " + std::to_string(index))
                    .ok);
  }
  ASSERT_TRUE(harness.tool.undo().ok);
  EXPECT_EQ(harness.tool.shape_count(), 3U);
  EXPECT_FALSE(harness.tool.can_undo());
  ASSERT_TRUE(harness.tool.cancel().ok);
  EXPECT_FALSE(harness.tool.active());
  EXPECT_TRUE(harness.modeling.objects().empty());
  EXPECT_EQ(harness.assets.mesh_ids().size(), baseline_meshes);
  EXPECT_FALSE(harness.history.can_undo());
  EXPECT_FALSE(harness.history.can_redo());
}

TEST(PrimitiveCreationToolServiceTests,
     CancelAfterEvictionPreservesPreSessionNativeHistory) {
  SceneSession session;
  session.history().set_max_steps(1U);
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto baseline = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Baseline Box");
  ASSERT_TRUE(baseline.ok()) << baseline.error_message();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(4.0f), "Evicted Box")
                  .ok);
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(8.0f), "Retained Box")
                  .ok);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(session.history().undo_count(), 1U);
  ASSERT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(session.modeling().objects().front().name, "Baseline Box");
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(session.modeling().objects().empty());
  ASSERT_TRUE(editor.history().redo().ok());
  ASSERT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(session.modeling().objects().front().modeling_object_id,
            baseline.value().modeling_object_id);
}

TEST(PrimitiveCreationToolServiceTests,
     DecreasingLimitKeepsContiguousNearestRedoAndCorrectOwnership) {
  PrimitiveCreationHarness harness;
  harness.history.set_max_steps(4U);
  ASSERT_TRUE(harness.tool.activate().ok);
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(harness.tool
                    .accept_preview(valid_primitive_preview(
                                        static_cast<float>(index) * 4.0f),
                                    "Limited Box " + std::to_string(index))
                    .ok);
  }
  ASSERT_TRUE(harness.tool.undo().ok);
  ASSERT_TRUE(harness.tool.undo().ok);
  ASSERT_TRUE(harness.tool.undo().ok);
  harness.history.set_max_steps(1U);
  harness.tool.apply_history_limit();
  EXPECT_EQ(harness.tool.shape_count(), 1U);
  EXPECT_FALSE(harness.tool.can_undo());
  ASSERT_TRUE(harness.tool.can_redo());
  ASSERT_TRUE(harness.tool.redo().ok);
  EXPECT_EQ(harness.tool.shape_count(), 2U);
  EXPECT_FALSE(harness.tool.can_redo());
  EXPECT_NE(find_object(harness.modeling.objects(), "Limited Box 1"), nullptr);
  EXPECT_EQ(find_object(harness.modeling.objects(), "Limited Box 2"), nullptr);
  ASSERT_TRUE(harness.tool.apply().ok);
  EXPECT_EQ(harness.history.undo_count(), 1U);
  ASSERT_TRUE(harness.history.undo().ok);
  ASSERT_EQ(harness.modeling.objects().size(), 1U);
  EXPECT_EQ(harness.modeling.objects().front().name, "Limited Box 0");
  EXPECT_FALSE(harness.history.can_undo());
}

TEST(PrimitiveCreationToolServiceTests,
     ApplyingAfterUndoingAllRetainedShapesRetiresOlderNativeSnapshots) {
  SceneSession session;
  session.history().set_max_steps(2U);
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto baseline = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Baseline Box");
  ASSERT_TRUE(baseline.ok()) << baseline.error_message();
  ASSERT_TRUE(session.history().can_undo());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  for (int index = 0; index < 3; ++index) {
    ASSERT_TRUE(session.primitive_creation()
                    .accept_preview(valid_primitive_preview(
                                        static_cast<float>(index + 1) * 4.0f),
                                    "New Box " + std::to_string(index))
                    .ok);
  }
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(session.primitive_creation().shape_count(), 1U);
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_FALSE(editor.history().can_undo());
  EXPECT_FALSE(editor.history().can_redo());
  EXPECT_EQ(session.modeling().objects().size(), 2U);
  EXPECT_NE(find_object(session.modeling().objects(), "Baseline Box"), nullptr);
  EXPECT_NE(find_object(session.modeling().objects(), "New Box 0"), nullptr);
}

TEST(PrimitiveCreationToolServiceTests,
     ZeroUndoLimitPreservesLiveEditingGestureCancellationAndApply) {
  PrimitiveCreationHarness harness;
  harness.history.set_max_steps(0U);
  ASSERT_TRUE(harness.tool.activate().ok);
  ASSERT_TRUE(harness.tool
                  .accept_preview(valid_primitive_preview(), "First Box")
                  .ok);
  ASSERT_TRUE(harness.tool
                  .accept_preview(valid_primitive_preview(4.0f), "Live Box")
                  .ok);
  ASSERT_TRUE(harness.tool.set_setting(kBoxSubdivisionsXSetting, 3.0).ok);
  EXPECT_NE(compiled_index_count(harness.modeling, "Live Box"), 36U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "First Box"), 36U);
  EXPECT_EQ(harness.tool.shape_count(), 2U);
  EXPECT_TRUE(harness.tool.can_apply());
  EXPECT_FALSE(harness.tool.can_undo());
  EXPECT_FALSE(harness.tool.can_redo());
  EXPECT_FALSE(harness.tool.undo().ok);
  const auto footprint = harness.modeling.begin_box_footprint({
      .screen_position = {400.0f, 300.0f},
      .ray = {.origin = {12.0f, 4.0f, 0.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  });
  ASSERT_TRUE(footprint.ok) << footprint.message;
  ASSERT_TRUE(harness.tool.can_undo());
  ASSERT_TRUE(harness.tool.undo().ok);
  EXPECT_EQ(harness.modeling.box_drag_state(), ModelingBoxDragState::Idle);
  EXPECT_FALSE(harness.tool.can_undo());
  EXPECT_EQ(harness.tool.shape_count(), 2U);
  ASSERT_TRUE(harness.tool.apply().ok);
  EXPECT_EQ(harness.modeling.objects().size(), 2U);
  EXPECT_FALSE(harness.history.can_undo());

  ASSERT_TRUE(harness.tool.activate().ok);
  ASSERT_TRUE(harness.tool
                  .accept_preview(valid_primitive_preview(8.0f), "Canceled Box")
                  .ok);
  ASSERT_TRUE(harness.tool.cancel().ok);
  EXPECT_EQ(harness.modeling.objects().size(), 2U);
  EXPECT_NE(find_object(harness.modeling.objects(), "First Box"), nullptr);
  EXPECT_NE(find_object(harness.modeling.objects(), "Live Box"), nullptr);
}

TEST(PrimitiveCreationToolServiceTests,
     PublicUndoRemovesOnlyNewestDrawnPrimitiveAndRedoRestoresExactShapes) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const std::size_t baseline_mesh_count = session.assets().mesh_ids().size();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  const std::array<std::string_view, 3> names{
      "First Drawn Box", "Second Drawn Box", "Third Drawn Box"};
  std::vector<ModelingObjectRecord> created;
  for (std::size_t index = 0; index < names.size(); ++index) {
    const CommandResult accepted = session.primitive_creation().accept_preview(
        valid_primitive_preview(static_cast<float>(index) * 4.0f),
        std::string(names[index]));
    ASSERT_TRUE(accepted.ok) << accepted.message;
    const auto objects = session.modeling().objects();
    const ModelingObjectRecord *const object = find_object(objects, names[index]);
    ASSERT_NE(object, nullptr);
    created.push_back(*object);
  }
  ASSERT_TRUE(editor.tools()
                  .set_setting(kCreatePrimitiveToolId,
                               kBoxSubdivisionsXSetting, 3.0)
                  .ok());
  const std::array<std::uint64_t, 3> content_ids{
      compiled_content_id(session.modeling(), names[0]),
      compiled_content_id(session.modeling(), names[1]),
      compiled_content_id(session.modeling(), names[2])};
  EXPECT_EQ(compiled_index_count(session.modeling(), names[0]), 36U);
  EXPECT_EQ(compiled_index_count(session.modeling(), names[1]), 36U);
  EXPECT_NE(compiled_index_count(session.modeling(), names[2]), 36U);

  const auto expect_prefix = [&](std::size_t count) {
    const auto objects = session.modeling().objects();
    EXPECT_EQ(objects.size(), count);
    EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + count);
    EXPECT_TRUE(session.primitive_creation().active());
    EXPECT_EQ(editor.tools().active_tool(), kCreatePrimitiveToolId);
    EXPECT_EQ(session.primitive_creation().shape_count(), count);
    const QuaderSelectionSummary selection = editor.query().selection();
    EXPECT_EQ(selection.selected_object_count, count == 0U ? 0U : 1U);
    if (count > 0U) {
      EXPECT_EQ(selection.active_object_id,
                created[count - 1U].modeling_object_id);
    }
    for (std::size_t index = 0; index < created.size(); ++index) {
      SCOPED_TRACE(names[index]);
      EXPECT_EQ(session.scene().exists(created[index].entity), index < count);
      const ModelingObjectRecord *const object = find_object(objects, names[index]);
      if (index >= count) {
        EXPECT_EQ(object, nullptr);
        continue;
      }
      ASSERT_NE(object, nullptr);
      EXPECT_EQ(object->entity, created[index].entity);
      EXPECT_EQ(object->modeling_object_id, created[index].modeling_object_id);
      EXPECT_EQ(object->mesh, created[index].mesh);
      EXPECT_EQ(compiled_content_id(session.modeling(), names[index]),
                content_ids[index]);
    }
  };

  expect_prefix(3U);
  for (std::size_t count = created.size(); count > 0U; --count) {
    SCOPED_TRACE(::testing::Message() << "undo count=" << count);
    ASSERT_TRUE(editor.history().can_undo());
    const QuaderApiVoidResult undone =
        editor.commands().execute(quader_command::kEditUndo);
    ASSERT_TRUE(undone.ok()) << undone.error_message();
    EXPECT_TRUE(undone.changed());
    expect_prefix(count - 1U);
    EXPECT_TRUE(editor.history().can_redo());
  }
  EXPECT_FALSE(session.primitive_creation().can_undo());
  for (std::size_t count = 1U; count <= created.size(); ++count) {
    SCOPED_TRACE(::testing::Message() << "redo count=" << count);
    const QuaderApiVoidResult redone =
        editor.commands().execute(quader_command::kEditRedo);
    ASSERT_TRUE(redone.ok()) << redone.error_message();
    EXPECT_TRUE(redone.changed());
    expect_prefix(count);
  }
  EXPECT_FALSE(editor.history().can_redo());
  // History restores accepted geometry without making a frozen shape live again.
  ASSERT_TRUE(editor.tools()
                  .set_setting(kCreatePrimitiveToolId,
                               kBoxSubdivisionsXSetting, 4.0)
                  .ok());
  expect_prefix(3U);
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_FALSE(session.primitive_creation().active());
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditUndo).ok());
  EXPECT_EQ(session.modeling().objects().size(), 2U);
  EXPECT_TRUE(session.scene().exists(created[0].entity));
  EXPECT_TRUE(session.scene().exists(created[1].entity));
  EXPECT_FALSE(session.scene().exists(created[2].entity));
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditRedo).ok());
  EXPECT_EQ(compiled_content_id(session.modeling(), names[2]), content_ids[2]);
}

TEST(PrimitiveCreationToolServiceTests,
     CreatingAfterLocalUndoDropsRedoAndApplyKeepsIndividualHistoryEntries) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const std::size_t baseline_mesh_count = session.assets().mesh_ids().size();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(), "Retained Box")
                  .ok);
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(4.0f), "Retired Box")
                  .ok);
  const auto original = session.modeling().objects();
  ASSERT_EQ(original.size(), 2U);
  const ModelingObjectRecord *const retired = find_object(original, "Retired Box");
  ASSERT_NE(retired, nullptr);
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditUndo).ok());
  ASSERT_TRUE(editor.history().can_redo());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(8.0f), "Branch Box")
                  .ok);
  EXPECT_FALSE(editor.history().can_redo());
  const auto branched = session.modeling().objects();
  ASSERT_EQ(branched.size(), 2U);
  const ModelingObjectRecord *const replacement = find_object(branched, "Branch Box");
  ASSERT_NE(replacement, nullptr);
  EXPECT_GT(replacement->modeling_object_id, retired->modeling_object_id);
  EXPECT_NE(replacement->entity, retired->entity);
  EXPECT_FALSE(session.scene().exists(retired->entity));
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 2U);

  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditUndo).ok());
  ASSERT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(session.modeling().objects().front().name, "Retained Box");
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditUndo).ok());
  EXPECT_TRUE(session.modeling().objects().empty());
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count);
  EXPECT_FALSE(editor.history().can_undo());
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditRedo).ok());
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditRedo).ok());
  const auto restored = session.modeling().objects();
  ASSERT_EQ(restored.size(), 2U);
  const ModelingObjectRecord *const restored_branch = find_object(restored, "Branch Box");
  ASSERT_NE(restored_branch, nullptr);
  EXPECT_EQ(restored_branch->modeling_object_id, replacement->modeling_object_id);
  EXPECT_EQ(restored_branch->entity, replacement->entity);
  EXPECT_EQ(restored_branch->mesh, replacement->mesh);
  EXPECT_FALSE(session.scene().exists(retired->entity));
  EXPECT_FALSE(editor.history().can_redo());
}

TEST(PrimitiveCreationToolServiceTests,
     LocalPrimitiveUndoPreservesConcurrentSceneEditAndCancelDiscardsLocalRedo) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto baseline = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Concurrent Undo Baseline");
  ASSERT_TRUE(baseline.ok()) << baseline.error_message();
  const std::size_t baseline_mesh_count = session.assets().mesh_ids().size();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(4.0f), "Retained Transient")
                  .ok);
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(8.0f), "Undo Transient")
                  .ok);
  const auto expect_state = [&](std::size_t primitive_count, bool visible) {
    EXPECT_TRUE(session.primitive_creation().active());
    EXPECT_EQ(session.primitive_creation().shape_count(), primitive_count);
    EXPECT_EQ(session.modeling().objects().size(), 1U + primitive_count);
    EXPECT_EQ(session.assets().mesh_ids().size(),
              baseline_mesh_count + primitive_count);
    const auto current_baseline = editor.query().entity(baseline.value().entity);
    ASSERT_TRUE(current_baseline.has_value());
    EXPECT_EQ(current_baseline->visible, visible);
  };
  // Rebase every local history step against the current independent scene edit.
  for (const bool visible : {false, true}) {
    SCOPED_TRACE(::testing::Message() << "visible=" << visible);
    ASSERT_TRUE(editor.scene().set_visible(baseline.value().entity, visible).ok());
    for (std::size_t remaining = 2U; remaining > 0U; --remaining) {
      ASSERT_TRUE(editor.commands().execute(quader_command::kEditUndo).ok());
      expect_state(remaining - 1U, visible);
      EXPECT_TRUE(editor.history().can_redo());
    }
    for (std::size_t count = 1U; count <= 2U; ++count) {
      ASSERT_TRUE(editor.commands().execute(quader_command::kEditRedo).ok());
      expect_state(count, visible);
    }
  }
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditUndo).ok());
  expect_state(1U, true);
  EXPECT_TRUE(editor.history().can_redo());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count);
  EXPECT_EQ(editor.query().selection().active_object_id,
            baseline.value().modeling_object_id);
  EXPECT_FALSE(editor.history().can_redo());
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditUndo).ok());
  const auto after_visibility_undo = editor.query().entity(baseline.value().entity);
  ASSERT_TRUE(after_visibility_undo.has_value());
  EXPECT_FALSE(after_visibility_undo->visible);
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditUndo).ok());
  const auto after_both_visibility_undos =
      editor.query().entity(baseline.value().entity);
  ASSERT_TRUE(after_both_visibility_undos.has_value());
  EXPECT_TRUE(after_both_visibility_undos->visible);
}

TEST(PrimitiveCreationToolServiceTests,
     UndoDuringFootprintClearsOnlyTheGestureAndPreservesPrimitiveRedo) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(), "First Gesture Box")
                  .ok);
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(4.0f), "Second Gesture Box")
                  .ok);
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditUndo).ok());
  ASSERT_TRUE(editor.history().can_redo());
  const auto retained = session.modeling().objects();
  ASSERT_EQ(retained.size(), 1U);
  const auto mesh_ids = session.assets().mesh_ids();
  const auto footprint = session.modeling().begin_box_footprint({
      .screen_position = {400.0f, 300.0f},
      .ray = {.origin = {12.0f, 4.0f, 0.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  });
  ASSERT_TRUE(footprint.ok) << footprint.message;
  ASSERT_EQ(footprint.drag_state, ModelingBoxDragState::Footprint);
  const QuaderSelectionSummary footprint_selection = editor.query().selection();
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditUndo).ok());
  EXPECT_TRUE(session.primitive_creation().active());
  EXPECT_EQ(editor.tools().active_tool(), kCreatePrimitiveToolId);
  EXPECT_EQ(session.primitive_creation().shape_count(), 1U);
  EXPECT_EQ(session.modeling().box_preview(false).drag_state,
            ModelingBoxDragState::Idle);
  ASSERT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(session.modeling().objects().front().modeling_object_id,
            retained.front().modeling_object_id);
  EXPECT_EQ(session.assets().mesh_ids(), mesh_ids);
  EXPECT_EQ(editor.query().selection().active_object_id,
            footprint_selection.active_object_id);
  EXPECT_EQ(editor.query().selection().selected_object_count,
            footprint_selection.selected_object_count);
  ASSERT_TRUE(editor.history().can_redo());
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditRedo).ok());
  EXPECT_EQ(session.modeling().objects().size(), 2U);
  EXPECT_TRUE(session.primitive_creation().active());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_TRUE(session.modeling().objects().empty());
  EXPECT_FALSE(editor.history().can_undo());
  EXPECT_FALSE(editor.history().can_redo());
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
     RepeatedCubesRetainSettingsAfterFinalizeAndReactivation) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_NO_FATAL_FAILURE(set_nondefault_primitive_settings(editor.tools()));
  ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId,
                                        kPrimitiveTypeSetting, 0.0).ok());
  // A rejected Finalize leaves the ongoing session and its settings intact.
  EXPECT_FALSE(editor.tools().commit_active_tool_preview().ok());
  ASSERT_TRUE(session.primitive_creation().active());
  EXPECT_TRUE(session.primitive_creation().box_settings().flip_faces);
  ASSERT_TRUE(session.primitive_creation().accept_preview(
      valid_primitive_preview(), "First Custom Cube").ok);
  const std::uint64_t first_content =
      compiled_content_id(session.modeling(), "First Custom Cube");
  ASSERT_TRUE(session.primitive_creation().accept_preview(
      valid_primitive_preview(4.0f), "Second Custom Cube").ok);
  const auto cube = editor.tools().live_primitive_cube();
  ASSERT_TRUE(cube.has_value());
  EXPECT_EQ(cube->subdivisions, (std::array<int, 3>{2, 3, 4}));
  EXPECT_TRUE(cube->flip_faces);
  EXPECT_EQ(compiled_index_count(session.modeling(), "First Custom Cube"),
            312U);
  EXPECT_EQ(compiled_index_count(session.modeling(), "Second Custom Cube"),
            312U);
  EXPECT_EQ(compiled_content_id(session.modeling(), "First Custom Cube"),
            first_content);
  const std::uint64_t second_content =
      compiled_content_id(session.modeling(), "Second Custom Cube");
  const std::uint64_t settings_revision =
      session.primitive_creation().tool_settings_revision();
  const auto remembered = primitive_settings(session.primitive_creation());
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_FALSE(session.primitive_creation().active());
  expect_primitive_settings(session.primitive_creation(), remembered);
  EXPECT_GT(session.primitive_creation().tool_settings_revision(),
            settings_revision);
  EXPECT_EQ(compiled_content_id(session.modeling(), "First Custom Cube"),
            first_content);
  EXPECT_EQ(compiled_content_id(session.modeling(), "Second Custom Cube"),
            second_content);

  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(compiled_content_id(session.modeling(), "First Custom Cube"),
            first_content);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(session.modeling().objects().empty());
  ASSERT_TRUE(editor.history().redo().ok());
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(compiled_content_id(session.modeling(), "First Custom Cube"),
            first_content);
  EXPECT_EQ(compiled_content_id(session.modeling(), "Second Custom Cube"),
            second_content);
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  EXPECT_EQ(session.primitive_creation().active_kind(),
            PrimitiveCreationKind::Box);
  expect_primitive_settings(session.primitive_creation(), remembered);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(compiled_content_id(session.modeling(), "First Custom Cube"),
            first_content);
  EXPECT_EQ(compiled_content_id(session.modeling(), "Second Custom Cube"),
            second_content);
}

TEST(PrimitiveCreationToolServiceTests,
     CancelAndToolSwitchRetainAllSettingsForEmptyAndRepeatedShapeSessions) {
  for (const bool accept_shapes : {false, true}) {
    for (const bool switch_tool : {false, true}) {
      SCOPED_TRACE(::testing::Message() << "accept_shapes=" << accept_shapes
                                       << ";switch_tool=" << switch_tool);
      SceneSession session;
      QuaderEditorApi editor(
          QuaderApiSessionInternalAccess::from_scene_session(session));
      ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
      ASSERT_NO_FATAL_FAILURE(set_nondefault_primitive_settings(editor.tools()));
      if (accept_shapes) {
        ASSERT_TRUE(session.primitive_creation().accept_preview(
            valid_primitive_preview(), "First Custom Torus").ok);
        ASSERT_TRUE(session.primitive_creation().accept_preview(
            valid_primitive_preview(4.0f), "Second Custom Torus").ok);
        EXPECT_EQ(compiled_index_count(session.modeling(), "First Custom Torus"),
                  192U);
        EXPECT_EQ(compiled_index_count(session.modeling(), "Second Custom Torus"),
                  192U);
        EXPECT_DOUBLE_EQ(session.primitive_creation().torus_settings()
                             .tube_radius_ratio, 0.3);
        EXPECT_TRUE(session.primitive_creation().torus_settings().flip_faces);
      }
      const auto remembered = primitive_settings(session.primitive_creation());
      const std::uint64_t settings_revision =
          session.primitive_creation().tool_settings_revision();
      const QuaderApiVoidResult exited = switch_tool
          ? editor.tools().activate(ViewportToolId::Select)
          : editor.tools().cancel_active_tool_preview();
      ASSERT_TRUE(exited.ok()) << exited.error_message();
      EXPECT_FALSE(session.primitive_creation().active());
      EXPECT_EQ(session.modeling().objects().size(),
                switch_tool && accept_shapes ? 2u : 0u);
      EXPECT_EQ(editor.history().can_undo(), switch_tool && accept_shapes);
      EXPECT_FALSE(editor.history().can_redo());
      expect_primitive_settings(session.primitive_creation(), remembered);
      EXPECT_GT(session.primitive_creation().tool_settings_revision(),
                settings_revision);
      ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
      EXPECT_EQ(session.primitive_creation().active_kind(),
                PrimitiveCreationKind::Torus);
      expect_primitive_settings(session.primitive_creation(), remembered);
      ASSERT_TRUE(session.primitive_creation().accept_preview(
          valid_primitive_preview(), "Remembered Torus").ok);
      const auto torus = editor.tools().live_primitive_creation();
      ASSERT_TRUE(torus.has_value());
      EXPECT_EQ(torus->major_segments, 8);
      EXPECT_EQ(torus->minor_segments, 4);
      EXPECT_DOUBLE_EQ(torus->tube_radius_ratio, 0.3);
      EXPECT_TRUE(torus->flip_faces);
      EXPECT_EQ(compiled_index_count(session.modeling(), "Remembered Torus"), 192U);
      EXPECT_TRUE(editor.tools().cancel_active_tool_preview().ok());
    }
  }
}

TEST(PrimitiveCreationToolServiceTests,
     LivePublicationFailureRetainsTheActiveDraftForRetry) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_NO_FATAL_FAILURE(set_nondefault_primitive_settings(editor.tools()));
  ASSERT_TRUE(session.primitive_creation().accept_preview(
      valid_primitive_preview(), "Custom Torus").ok);
  const auto remembered = primitive_settings(session.primitive_creation());
  const auto content = compiled_content_id(session.modeling(), "Custom Torus");
  session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify);
  const QuaderApiVoidResult failed = editor.tools().set_setting(
      kCreatePrimitiveToolId, kTorusTubeRadiusRatioSetting, 0.4);
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.error_message().empty());
  EXPECT_TRUE(session.primitive_creation().active());
  ASSERT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(compiled_content_id(session.modeling(), "Custom Torus"), content);
  EXPECT_TRUE(editor.tools().live_primitive_creation().has_value());
  EXPECT_TRUE(editor.history().can_undo());
  EXPECT_EQ(session.history().undo_count(), 0U);
  expect_primitive_settings(session.primitive_creation(), remembered);
  session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  EXPECT_EQ(session.primitive_creation().active_kind(),
            PrimitiveCreationKind::Torus);
  expect_primitive_settings(session.primitive_creation(), remembered);
  ASSERT_TRUE(editor.tools().set_setting(
      kCreatePrimitiveToolId, kTorusTubeRadiusRatioSetting, 0.4).ok());
  EXPECT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(PrimitiveCreationToolServiceTests,
     InvalidShapeRetainsAcceptedShapesAndTheLiveSessionForRetry) {
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

  const auto remembered = primitive_settings(harness.tool);
  const auto accepted_indices = compiled_index_count(harness.modeling, "Accepted Hemisphere");
  ModelingBoxPreview invalid = valid_primitive_preview(4.0f);
  invalid.corner_count = invalid.corners.size() - 1U;
  const CommandResult failed =
      harness.tool.accept_preview(invalid, "Invalid Sphere");
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.message.empty());
  EXPECT_TRUE(harness.tool.active());
  EXPECT_EQ(harness.tool.shape_count(), 1U);
  EXPECT_EQ(harness.modeling.objects().size(), 1u);
  EXPECT_FALSE(harness.history.can_undo());
  EXPECT_FALSE(harness.history.can_redo());
  EXPECT_EQ(harness.modeling.selection_summary().selected_object_count, 1U);
  EXPECT_TRUE(harness.tool.live_primitive_creation().has_value());
  EXPECT_EQ(compiled_index_count(harness.modeling, "Accepted Hemisphere"), accepted_indices);
  expect_primitive_settings(harness.tool, remembered);
  ASSERT_TRUE(harness.tool.activate().ok);
  EXPECT_EQ(harness.tool.active_kind(), PrimitiveCreationKind::Sphere);
  expect_primitive_settings(harness.tool, remembered);
  ASSERT_TRUE(harness.tool.accept_preview(valid_primitive_preview(),
                                          "Remembered Hemisphere After Failure").ok);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Remembered Hemisphere After Failure"),
            accepted_indices);
  EXPECT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     NewShapePublicationFailurePreservesPriorShapesLiveEditAndLocalRedo) {
  for (const auto failure : {ModelingSdkProjectionFailurePoint::AfterSceneBinding,
                             ModelingSdkProjectionFailurePoint::AfterSelection,
                             ModelingSdkProjectionFailurePoint::BeforeCommit}) {
    SCOPED_TRACE(static_cast<int>(failure));
    PrimitiveCreationHarness harness;
    ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);
    ASSERT_TRUE(harness.tool.accept_preview(valid_primitive_preview(), "Kept Shape").ok);
    ASSERT_TRUE(harness.tool.accept_preview(valid_primitive_preview(4.0f), "Redo Shape").ok);
    ASSERT_TRUE(harness.tool.undo().ok);
    ASSERT_TRUE(harness.tool.can_redo());
    const auto content = compiled_content_id(harness.modeling, "Kept Shape");
    const auto selected = harness.modeling.selection_summary().active_object_id;
    harness.modeling.set_modeling_sdk_projection_failure_for_tests(failure);
    const auto failed = harness.tool.accept_preview(valid_primitive_preview(8.0f), "Rejected Shape");
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_TRUE(harness.tool.active());
    EXPECT_EQ(harness.tool.shape_count(), 1u);
    EXPECT_TRUE(harness.tool.can_redo());
    EXPECT_EQ(harness.modeling.objects().size(), 1u);
    EXPECT_EQ(compiled_content_id(harness.modeling, "Kept Shape"), content);
    EXPECT_EQ(harness.modeling.selection_summary().active_object_id, selected);
    EXPECT_FALSE(harness.history.can_undo());
    harness.modeling.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    ASSERT_TRUE(harness.tool.accept_preview(valid_primitive_preview(8.0f), "Retried Shape").ok);
    EXPECT_EQ(harness.tool.shape_count(), 2u);
    EXPECT_FALSE(harness.tool.can_redo());
    ASSERT_TRUE(harness.tool.finish().ok);
    EXPECT_EQ(harness.history.undo_count(), 2u);
    ASSERT_TRUE(harness.history.undo().ok);
    EXPECT_EQ(harness.modeling.objects().size(), 1u);
    EXPECT_EQ(compiled_content_id(harness.modeling, "Kept Shape"), content);
  }
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

  const auto remembered = primitive_settings(harness.tool);
  ASSERT_TRUE(harness.tool.cancel().ok);
  ASSERT_TRUE(harness.tool.activate().ok);
  ASSERT_TRUE(harness.tool.active_kind().has_value());
  EXPECT_EQ(*harness.tool.active_kind(), PrimitiveCreationKind::Cylinder);
  expect_primitive_settings(harness.tool, remembered);
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

  const auto remembered = primitive_settings(harness.tool);
  const CommandResult applied = harness.tool.apply();
  ASSERT_TRUE(applied.ok) << applied.message;
  ASSERT_TRUE(harness.history.can_undo());
  ASSERT_TRUE(harness.history.undo().ok);
  ASSERT_EQ(harness.modeling.objects().size(), 1U);
  EXPECT_EQ(compiled_content_id(harness.modeling, "Cone"),
            frozen_cone_content);
  ASSERT_TRUE(harness.history.undo().ok);
  EXPECT_TRUE(harness.modeling.objects().empty());

  ASSERT_TRUE(harness.tool.activate().ok);
  ASSERT_TRUE(harness.tool.active_kind().has_value());
  EXPECT_EQ(*harness.tool.active_kind(), PrimitiveCreationKind::Torus);
  expect_primitive_settings(harness.tool, remembered);
  EXPECT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     PublicDeselectAcceptsPrimitiveBeforeConcurrentEditAndLaterDraftCancellation) {
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
  EXPECT_EQ(session.history().undo_count(), 2U);
  EXPECT_FALSE(session.primitive_creation().active());
  EXPECT_TRUE(session.scene().selected_entities().empty());
  EXPECT_FALSE(editor.tools()
                  .set_setting("create_primitive",
                               "primitive.box.subdivisions_x", 3.0)
                  .ok());
  EXPECT_EQ(compiled_index_count(session.modeling(), "Canceled Transient"),
            transient_indices);

  ASSERT_TRUE(editor.scene().set_visible(baseline.object.entity, false).ok());
  EXPECT_EQ(session.history().undo_count(), 3U);
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(8.0f), "Later Canceled Draft")
                  .ok);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_EQ(session.modeling().objects().size(), 2U);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);
  EXPECT_EQ(session.modeling().objects().front().modeling_object_id,
            baseline.object.modeling_object_id);
  EXPECT_TRUE(session.scene().exists(transient->entity));
  EXPECT_EQ(session.history().undo_count(), 3U);
  const std::optional<QuaderEntitySnapshot> canceled_baseline =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(canceled_baseline.has_value());
  EXPECT_FALSE(canceled_baseline->visible);
  EXPECT_FALSE(session.modeling().selection_summary().has_selection);
  EXPECT_TRUE(session.scene().selected_entities().empty());

  ASSERT_TRUE(editor.history().undo().ok());
  const std::optional<QuaderEntitySnapshot> undone_visibility =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(undone_visibility.has_value());
  EXPECT_TRUE(undone_visibility->visible);
  // Selection is its own command after primitive acceptance.
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(session.scene().primary_selection(), transient->entity);
  EXPECT_TRUE(session.scene().exists(transient->entity));
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_FALSE(session.scene().exists(transient->entity));
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count);
  EXPECT_FALSE(editor.history().can_undo());
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(session.scene().exists(transient->entity));
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);
  EXPECT_EQ(session.scene().primary_selection(), transient->entity);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(session.scene().selected_entities().empty());
  ASSERT_TRUE(editor.history().redo().ok());
  const std::optional<QuaderEntitySnapshot> redone_visibility =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(redone_visibility.has_value());
  EXPECT_FALSE(redone_visibility->visible);
}

TEST(PrimitiveCreationToolServiceTests,
     PublicDeselectOrdersPrimitiveDeltasBeforeTheFollowingSceneEdit) {
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
                                  "First Applied Primitive")
                  .ok);
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(8.0f),
                                  "Second Applied Primitive")
                  .ok);
  ASSERT_TRUE(editor.selection().clear().ok());
  EXPECT_EQ(session.history().undo_count(), 3U);
  EXPECT_FALSE(session.primitive_creation().active());
  ASSERT_TRUE(editor.scene().set_visible(baseline.object.entity, false).ok());
  EXPECT_EQ(session.history().undo_count(), 4U);
  const std::vector<ModelingObjectRecord> applied_objects =
      session.modeling().objects();
  ASSERT_EQ(applied_objects.size(), 3U);
  const ModelingObjectRecord *const first_primitive =
      find_object(applied_objects, "First Applied Primitive");
  const ModelingObjectRecord *const second_primitive =
      find_object(applied_objects, "Second Applied Primitive");
  ASSERT_NE(first_primitive, nullptr);
  ASSERT_NE(second_primitive, nullptr);
  ASSERT_NE(first_primitive->mesh, kInvalidAssetId);
  ASSERT_NE(second_primitive->mesh, kInvalidAssetId);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 2U);

  const auto expect_visibility = [&](bool visible) {
    const auto current_baseline = editor.query().entity(baseline.object.entity);
    ASSERT_TRUE(current_baseline.has_value());
    EXPECT_EQ(current_baseline->visible, visible);
  };

  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_EQ(session.modeling().objects().size(), 3U);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 2U);
  expect_visibility(true);
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_EQ(session.modeling().objects().size(), 3U);
  EXPECT_EQ(session.scene().primary_selection(), second_primitive->entity);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 2U);
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_EQ(session.modeling().objects().size(), 2U);
  EXPECT_TRUE(session.scene().exists(first_primitive->entity));
  EXPECT_FALSE(session.scene().exists(second_primitive->entity));
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);
  expect_visibility(true);
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count);
  EXPECT_EQ(session.modeling().objects().front().modeling_object_id,
            baseline.object.modeling_object_id);
  expect_visibility(true);
  EXPECT_FALSE(editor.history().can_undo());

  ASSERT_TRUE(editor.history().redo().ok());
  ASSERT_EQ(session.modeling().objects().size(), 2U);
  EXPECT_TRUE(session.scene().exists(first_primitive->entity));
  EXPECT_FALSE(session.scene().exists(second_primitive->entity));
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);
  expect_visibility(true);
  ASSERT_TRUE(editor.history().redo().ok());
  const std::vector<ModelingObjectRecord> redone_objects =
      session.modeling().objects();
  ASSERT_EQ(redone_objects.size(), 3U);
  for (const ModelingObjectRecord *const expected :
       {first_primitive, second_primitive}) {
    const ModelingObjectRecord *const redone =
        find_object(redone_objects, expected->name);
    ASSERT_NE(redone, nullptr);
    EXPECT_EQ(redone->entity, expected->entity);
    EXPECT_EQ(redone->modeling_object_id, expected->modeling_object_id);
    EXPECT_EQ(redone->mesh, expected->mesh);
  }
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 2U);
  expect_visibility(true);
  EXPECT_EQ(session.scene().primary_selection(), second_primitive->entity);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(session.scene().selected_entities().empty());
  ASSERT_TRUE(editor.history().redo().ok());
  expect_visibility(false);

  ASSERT_TRUE(editor.history().undo().ok());
  expect_visibility(true);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(session.scene().primary_selection(), second_primitive->entity);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 2U);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);
  ASSERT_TRUE(editor.history().redo().ok());
  const std::vector<ModelingObjectRecord> repeated_redo_objects =
      session.modeling().objects();
  const ModelingObjectRecord *const repeated_redo_primitive =
      find_object(repeated_redo_objects, "Second Applied Primitive");
  ASSERT_NE(repeated_redo_primitive, nullptr);
  EXPECT_EQ(repeated_redo_primitive->mesh, second_primitive->mesh);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 2U);
  expect_visibility(true);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(session.scene().selected_entities().empty());
  ASSERT_TRUE(editor.history().redo().ok());
  expect_visibility(false);
}

TEST(PrimitiveCreationToolServiceTests,
     PublicDeselectReplacesRedoWhileExplicitCancelPreservesIt) {
  for (const bool accept_by_deselecting : {false, true}) {
    SCOPED_TRACE(accept_by_deselecting);
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
                    .accept_preview(valid_primitive_preview(), "Redo Branch Transient")
                    .ok);
    ASSERT_TRUE(session.history().can_redo());
    if (!accept_by_deselecting) {
      ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
      EXPECT_TRUE(session.modeling().objects().empty());
      ASSERT_TRUE(editor.history().can_redo());
      ASSERT_TRUE(editor.history().redo().ok());
      EXPECT_TRUE(editor.query().entity(sentinel.value().id).has_value());
      continue;
    }

    ASSERT_TRUE(editor.selection().clear().ok());
    EXPECT_FALSE(session.primitive_creation().active());
    EXPECT_FALSE(session.history().can_redo());
    EXPECT_EQ(session.history().undo_count(), 2U);
    ASSERT_EQ(session.modeling().objects().size(), 1U);
    const ModelingObjectRecord accepted = session.modeling().objects().front();
    EXPECT_FALSE(editor.query().entity(sentinel.value().id).has_value());
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_EQ(session.modeling().objects().size(), 1U);
    EXPECT_EQ(session.scene().primary_selection(), accepted.entity);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(session.modeling().objects().empty());
    ASSERT_TRUE(editor.history().redo().ok());
    ASSERT_EQ(session.modeling().objects().size(), 1U);
    const ModelingObjectRecord redone = session.modeling().objects().front();
    EXPECT_EQ(redone.entity, accepted.entity);
    EXPECT_EQ(redone.modeling_object_id, accepted.modeling_object_id);
    EXPECT_EQ(redone.mesh, accepted.mesh);
    EXPECT_EQ(session.scene().primary_selection(), accepted.entity);
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_TRUE(session.scene().selected_entities().empty());
    EXPECT_FALSE(editor.query().entity(sentinel.value().id).has_value());
  }
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
     FootprintPreviewStartsEmptyThenGrowsThroughLineAndRectangle) {
  PrimitiveCreationHarness harness;
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);

  const ModelingBoxToolEvent start{
      .ray = {.origin = {2.0f, 4.0f, 3.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  };
  const ModelingBoxPreview hover = harness.modeling.update_box_hover(start);
  ASSERT_TRUE(hover.ok) << hover.message;
  EXPECT_EQ(hover.drag_state, ModelingBoxDragState::Idle);
  EXPECT_FALSE(hover.valid);
  EXPECT_EQ(hover.footprint_count, 0U);

  const ModelingBoxPreview pressed =
      harness.modeling.begin_box_footprint(start);
  ASSERT_TRUE(pressed.ok) << pressed.message;
  EXPECT_EQ(pressed.drag_state, ModelingBoxDragState::Footprint);
  EXPECT_FALSE(pressed.valid);
  EXPECT_EQ(pressed.footprint_count, 0U);
  EXPECT_FLOAT_EQ(pressed.footprint_size.x, 0.0f);
  EXPECT_FLOAT_EQ(pressed.footprint_size.y, 0.0f);

  const ModelingBoxPreview line = harness.modeling.update_box_footprint({
      .ray = {.origin = {5.0f, 4.0f, 3.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  });
  ASSERT_TRUE(line.ok) << line.message;
  EXPECT_EQ(line.drag_state, ModelingBoxDragState::Footprint);
  EXPECT_FALSE(line.valid);
  ASSERT_EQ(line.footprint_count, 2U);
  EXPECT_TRUE(near_point(line.footprint[0], {2.0f, 0.0f, 3.0f}));
  EXPECT_TRUE(near_point(line.footprint[1], {5.0f, 0.0f, 3.0f}));
  EXPECT_FLOAT_EQ(line.footprint_size.x, 3.0f);
  EXPECT_FLOAT_EQ(line.footprint_size.y, 0.0f);

  const ModelingBoxPreview rectangle = harness.modeling.update_box_footprint({
      .ray = {.origin = {5.0f, 4.0f, 7.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  });
  ASSERT_TRUE(rectangle.ok) << rectangle.message;
  EXPECT_EQ(rectangle.drag_state, ModelingBoxDragState::Footprint);
  ASSERT_TRUE(rectangle.valid);
  ASSERT_EQ(rectangle.footprint_count, 4U);
  EXPECT_TRUE(near_point(rectangle.footprint[0], {2.0f, 0.0f, 3.0f}));
  EXPECT_TRUE(near_point(rectangle.footprint[2], {5.0f, 0.0f, 7.0f}));
  EXPECT_FLOAT_EQ(rectangle.footprint_size.x, 3.0f);
  EXPECT_FLOAT_EQ(rectangle.footprint_size.y, 4.0f);
  EXPECT_TRUE(harness.tool.cancel().ok);
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
  EXPECT_FALSE(hover.valid);
  EXPECT_NEAR(hover.normal.x, 0.0f, 0.0001f);
  EXPECT_NEAR(hover.normal.y, 1.0f, 0.0001f);
  EXPECT_NEAR(hover.normal.z, 0.0f, 0.0001f);
  EXPECT_EQ(hover.footprint_count, 0U);

  const ModelingBoxPreview footprint =
      harness.modeling.begin_box_footprint(event);
  ASSERT_TRUE(footprint.ok) << footprint.message;
  EXPECT_EQ(footprint.drag_state, ModelingBoxDragState::Footprint);
  EXPECT_NEAR(footprint.normal.y, 1.0f, 0.0001f);
  EXPECT_FALSE(footprint.valid);
  EXPECT_EQ(footprint.footprint_count, 0U);

  const ModelingBoxPreview dragged =
      harness.modeling.update_box_footprint({
          .ray = {.origin = {2.5f, 5.0f, 7.0f},
                  .direction = {0.0f, -1.0f, -1.0f}},
          .grid_size = 1.0,
          .snap_enabled = false,
      });
  ASSERT_TRUE(dragged.ok) << dragged.message;
  ASSERT_TRUE(dragged.valid);
  ASSERT_EQ(dragged.footprint_count, 4U);
  for (std::size_t index = 0; index < dragged.footprint_count; ++index) {
    EXPECT_NEAR(dragged.footprint[index].y, 0.0f, 0.0001f);
  }
  EXPECT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     TranslatedVisibleVerticalSurfaceIsPickedButCannotDrivePlacement) {
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
  EXPECT_FALSE(old_hover.valid);
  EXPECT_NEAR(old_hover.normal.y, 1.0f, 0.0001f);
  EXPECT_EQ(old_hover.footprint_count, 0U);

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
  EXPECT_FALSE(world_hover.valid);
  EXPECT_NEAR(world_hover.normal.x, 0.0f, 0.0001f);
  EXPECT_NEAR(world_hover.normal.y, 1.0f, 0.0001f);
  EXPECT_NEAR(world_hover.normal.z, 0.0f, 0.0001f);
  EXPECT_EQ(world_hover.footprint_count, 0U);

  const ModelingBoxPreview footprint =
      harness.modeling.begin_box_footprint(world_position);
  ASSERT_TRUE(footprint.ok) << footprint.message;
  EXPECT_EQ(footprint.footprint_count, 0U);
  const ModelingBoxPreview dragged =
      harness.modeling.update_box_footprint({
          .ray = {.origin = {12.0f, 2.0f, 7.0f},
                  .direction = {0.0f, -1.5f, -4.0f}},
          .grid_size = 1.0,
          .snap_enabled = false,
      });
  ASSERT_TRUE(dragged.ok) << dragged.message;
  ASSERT_TRUE(dragged.valid);
  ASSERT_EQ(dragged.footprint_count, 4U);
  for (std::size_t index = 0; index < dragged.footprint_count; ++index) {
    EXPECT_NEAR(dragged.footprint[index].y, 0.0f, 0.0001f);
  }
  EXPECT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     GroundParallelBackFacingPlaneCanDrivePrimitivePlacement) {
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
  EXPECT_FALSE(hover.valid);
  EXPECT_EQ(hover.footprint_count, 0U);
  EXPECT_NEAR(hover.normal.y, 1.0f, 0.0001f);

  const ModelingBoxPreview footprint =
      harness.modeling.begin_box_footprint(event);
  ASSERT_TRUE(footprint.ok) << footprint.message;
  EXPECT_EQ(footprint.footprint_count, 0U);
  const ModelingBoxPreview dragged =
      harness.modeling.update_box_footprint({
          .ray = {.origin = {0.75f, -2.0f, 0.75f},
                  .direction = {0.0f, 1.0f, 0.0f}},
          .grid_size = 1.0,
          .snap_enabled = false,
      });
  ASSERT_TRUE(dragged.ok) << dragged.message;
  ASSERT_TRUE(dragged.valid);
  ASSERT_EQ(dragged.footprint_count, 4U);
  for (std::size_t index = 0; index < dragged.footprint_count; ++index) {
    EXPECT_NEAR(dragged.footprint[index].y, 2.0f, 0.0001f);
  }
  EXPECT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     TiltedNaturalSurfaceFallsBackToTheGroundConstructionPlane) {
  PrimitiveCreationHarness harness;
  quader_poly::Document plane;
  const quader_poly::ElementId a =
      quader_poly::add_vertex(plane, {-2.0f, 1.0f, -2.0f});
  const quader_poly::ElementId b =
      quader_poly::add_vertex(plane, {-2.0f, 3.0f, 2.0f});
  const quader_poly::ElementId c =
      quader_poly::add_vertex(plane, {2.0f, 3.0f, 2.0f});
  const quader_poly::ElementId d =
      quader_poly::add_vertex(plane, {2.0f, 1.0f, -2.0f});
  ASSERT_NE(quader_poly::add_face(plane, std::array{a, b, c, d}),
            quader_poly::kInvalidElementId);
  const ModelingCreateBoxResult created = harness.modeling.create_mesh({
      .document = std::move(plane),
      .name = "Tilted Plane",
  });
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);

  const ModelingBoxToolEvent event{
      .ray = {.origin = {0.0f, 5.0f, 0.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
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
  EXPECT_FALSE(hover.valid);
  EXPECT_EQ(hover.footprint_count, 0U);
  EXPECT_NEAR(hover.normal.x, 0.0f, 0.0001f);
  EXPECT_NEAR(hover.normal.y, 1.0f, 0.0001f);
  EXPECT_NEAR(hover.normal.z, 0.0f, 0.0001f);

  const ModelingBoxPreview footprint =
      harness.modeling.begin_box_footprint(event);
  ASSERT_TRUE(footprint.ok) << footprint.message;
  EXPECT_EQ(footprint.footprint_count, 0U);
  const ModelingBoxPreview dragged =
      harness.modeling.update_box_footprint({
          .ray = {.origin = {1.0f, 5.0f, 1.0f},
                  .direction = {0.0f, -1.0f, 0.0f}},
          .grid_size = 1.0,
          .snap_enabled = false,
      });
  ASSERT_TRUE(dragged.ok) << dragged.message;
  ASSERT_TRUE(dragged.valid);
  ASSERT_EQ(dragged.footprint_count, 4U);
  for (std::size_t index = 0; index < dragged.footprint_count; ++index) {
    EXPECT_NEAR(dragged.footprint[index].y, 0.0f, 0.0001f);
  }
  EXPECT_TRUE(harness.tool.cancel().ok);
}

// Cube creation controls are exercised through the same owner and public
// facades used by the viewport; no native window or shared render state is needed.
std::array<float, 3> cube_components(QVec3 value) {
  return {value.x, value.y, value.z};
}

void expect_cube_mesh_bounds(const ModelingSessionService &modeling,
                             int object_id, QVec3 minimum, QVec3 maximum) {
  const auto mesh = modeling.compile_object_mesh(object_id);
  ASSERT_TRUE(mesh.has_value());
  ASSERT_FALSE(mesh->geometry().vertices.empty());
  QVec3 actual_min = mesh->geometry().vertices.front().position;
  QVec3 actual_max = actual_min;
  for (const MeshVertex &vertex : mesh->geometry().vertices) {
    actual_min = {std::min(actual_min.x, vertex.position.x),
                  std::min(actual_min.y, vertex.position.y),
                  std::min(actual_min.z, vertex.position.z)};
    actual_max = {std::max(actual_max.x, vertex.position.x),
                  std::max(actual_max.y, vertex.position.y),
                  std::max(actual_max.z, vertex.position.z)};
  }
  EXPECT_EQ(cube_components(actual_min), cube_components(minimum));
  EXPECT_EQ(cube_components(actual_max), cube_components(maximum));
}

double cube_signed_volume(const MeshAsset &mesh) {
  const auto &geometry = mesh.geometry();
  double volume = 0.0;
  for (std::size_t index = 0; index + 2U < geometry.indices.size(); index += 3U) {
    const QVec3 a = geometry.vertices[geometry.indices[index]].position;
    const QVec3 b = geometry.vertices[geometry.indices[index + 1U]].position;
    const QVec3 c = geometry.vertices[geometry.indices[index + 2U]].position;
    volume += static_cast<double>(dot(a, cross(b, c))) / 6.0;
  }
  return volume;
}

TEST(PrimitiveCreationToolServiceTests,
     PublicCubeResizeMovesEachFaceFromItsBaselineAndKeepsOppositeFaceFixed) {
  const std::array axes{QuaderPrimitiveCubeAxis::X, QuaderPrimitiveCubeAxis::Y,
                        QuaderPrimitiveCubeAxis::Z};
  for (std::size_t axis = 0; axis < axes.size(); ++axis) {
    for (const bool positive : {false, true}) {
      SCOPED_TRACE(::testing::Message() << "axis=" << axis << ", positive=" << positive);
      SceneSession session;
      QuaderEditorApi editor(
          QuaderApiSessionInternalAccess::from_scene_session(session));
      ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
      ASSERT_TRUE(session.primitive_creation()
                      .accept_preview(valid_primitive_preview(), "Resizable Box")
                      .ok);
      const auto before = editor.tools().live_primitive_cube();
      ASSERT_TRUE(before.has_value());
      ASSERT_TRUE(editor.tools().begin_primitive_cube_resize(axes[axis], positive).ok());
      EXPECT_TRUE(editor.tools().live_primitive_cube()->resizing);
      const double outward = positive ? 0.75 : -0.75;
      ASSERT_TRUE(editor.tools().update_primitive_cube_resize(outward).ok());
      auto expected_min = cube_components(before->local_min);
      auto expected_max = cube_components(before->local_max);
      (positive ? expected_max : expected_min)[axis] += static_cast<float>(outward);
      const auto grown = editor.tools().live_primitive_cube();
      ASSERT_TRUE(grown.has_value());
      EXPECT_EQ(cube_components(grown->local_min), expected_min);
      EXPECT_EQ(cube_components(grown->local_max), expected_max);
      EXPECT_GT(grown->revision, before->revision);
      expect_cube_mesh_bounds(session.modeling(), grown->object_id,
                               grown->local_min, grown->local_max);

      // An update is measured from the press, not accumulated from prior events.
      const double inward = positive ? -0.25 : 0.25;
      ASSERT_TRUE(editor.tools().update_primitive_cube_resize(inward).ok());
      expected_min = cube_components(before->local_min);
      expected_max = cube_components(before->local_max);
      (positive ? expected_max : expected_min)[axis] += static_cast<float>(inward);
      const auto shrunk = editor.tools().live_primitive_cube();
      ASSERT_TRUE(shrunk.has_value());
      EXPECT_EQ(cube_components(shrunk->local_min), expected_min);
      EXPECT_EQ(cube_components(shrunk->local_max), expected_max);
      ASSERT_TRUE(editor.tools().end_primitive_cube_resize().ok());
      EXPECT_FALSE(editor.tools().live_primitive_cube()->resizing);
      EXPECT_EQ(session.primitive_creation().shape_count(), 1U);
      EXPECT_EQ(session.history().undo_count(), 0U);

      // A subsequent drag starts at the last committed face, including -Y.
      ASSERT_TRUE(editor.tools().begin_primitive_cube_resize(axes[axis], positive).ok());
      ASSERT_TRUE(editor.tools().update_primitive_cube_resize(outward).ok());
      const auto repeated = editor.tools().live_primitive_cube();
      ASSERT_TRUE(repeated.has_value());
      (positive ? expected_max : expected_min)[axis] += static_cast<float>(outward);
      EXPECT_EQ(cube_components(repeated->local_min), expected_min);
      EXPECT_EQ(cube_components(repeated->local_max), expected_max);
      expect_cube_mesh_bounds(session.modeling(), repeated->object_id,
                               repeated->local_min, repeated->local_max);
      ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
      EXPECT_TRUE(session.modeling().objects().empty());
    }
  }
}

TEST(PrimitiveCreationToolServiceTests,
     CubeResizeClampsCrossingFacesRejectsNonfiniteAndSkipsUnchangedPublication) {
  for (const PrimitiveCubeAxis axis : {PrimitiveCubeAxis::X, PrimitiveCubeAxis::Y,
                                       PrimitiveCubeAxis::Z}) {
    for (const bool positive : {false, true}) {
      SCOPED_TRACE(::testing::Message() << "axis=" << static_cast<int>(axis)
                                       << ", positive=" << positive);
      PrimitiveCreationHarness harness;
      ASSERT_TRUE(harness.tool.activate().ok);
      ASSERT_TRUE(harness.tool.accept_preview(valid_primitive_preview(), "Clamp Box").ok);
      ASSERT_TRUE(harness.tool.begin_cube_resize(axis, positive).ok);
      const auto original = harness.tool.live_cube();
      ASSERT_TRUE(original.has_value());
      const auto source_before = harness.modeling.modeling_sdk_projection_stats_for_tests();
      const auto content_before = harness.modeling.content_revision();
      EXPECT_FALSE(harness.tool.update_cube_resize(0.0).changed);
      EXPECT_FALSE(harness.tool.set_setting(kBoxSubdivisionsXSetting, 1.0).changed);
      for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
                                   std::numeric_limits<double>::infinity(),
                                   -std::numeric_limits<double>::infinity()}) {
        const auto rejected = harness.tool.update_cube_resize(invalid);
        EXPECT_FALSE(rejected.ok);
        EXPECT_FALSE(rejected.changed);
      }
      EXPECT_FALSE(harness.tool.begin_cube_resize(axis, !positive).ok);
      EXPECT_EQ(harness.tool.live_cube()->revision, original->revision);
      EXPECT_EQ(harness.modeling.content_revision(), content_before);
      auto source_after = harness.modeling.modeling_sdk_projection_stats_for_tests();
      EXPECT_EQ(source_after.mesh_compile_count, source_before.mesh_compile_count);
      EXPECT_EQ(source_after.asset_publication_count, source_before.asset_publication_count);
      EXPECT_EQ(source_after.scene_publication_count, source_before.scene_publication_count);

      ASSERT_TRUE(harness.tool.update_cube_resize(positive ? -100.0 : 100.0).ok);
      const auto clamped = harness.tool.live_cube();
      ASSERT_TRUE(clamped.has_value());
      const auto minimum = cube_components(clamped->local_min);
      const auto maximum = cube_components(clamped->local_max);
      const auto index = static_cast<std::size_t>(axis);
      const double extent = static_cast<double>(maximum[index]) - minimum[index];
      EXPECT_GE(extent, kPrimitiveCubeMinimumExtent);
      EXPECT_LT(extent, 0.000101);
      expect_cube_mesh_bounds(harness.modeling, clamped->modeling_object_id,
                               clamped->local_min, clamped->local_max);
      const auto clamped_source = harness.modeling.modeling_sdk_projection_stats_for_tests();
      const auto unchanged = harness.tool.update_cube_resize(positive ? -200.0 : 200.0);
      EXPECT_TRUE(unchanged.ok);
      EXPECT_FALSE(unchanged.changed);
      EXPECT_EQ(harness.tool.live_cube()->revision, clamped->revision);
      source_after = harness.modeling.modeling_sdk_projection_stats_for_tests();
      EXPECT_EQ(source_after.mesh_compile_count, clamped_source.mesh_compile_count);
      EXPECT_EQ(source_after.asset_publication_count, clamped_source.asset_publication_count);
      ASSERT_TRUE(harness.tool.cancel_cube_resize().ok);
      expect_cube_mesh_bounds(harness.modeling, original->modeling_object_id,
                               original->local_min, original->local_max);
      EXPECT_FALSE(harness.tool.cancel_cube_resize().changed);
      EXPECT_FALSE(harness.tool.end_cube_resize().changed);
      EXPECT_FALSE(harness.tool.update_cube_resize(1.0).ok);
      ASSERT_TRUE(harness.tool.cancel().ok);
    }
  }
}

TEST(PrimitiveCreationToolServiceTests,
     CubeDescriptorUsesAuthoritativeTransformAndResizePreservesMaterialAndTextureFrame) {
  for (const bool texture_lock : {false, true}) {
    SCOPED_TRACE(texture_lock);
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    editor.preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{
        .texture_lock_enabled = [texture_lock] { return texture_lock; },
    });
    const AssetId material = session.assets().add_material(make_default_quader_material("Cube Material"));
    ASSERT_NE(material, kInvalidAssetId);
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
    ASSERT_TRUE(session.primitive_creation()
                    .accept_preview(oriented_primitive_preview(
                                        {4.0f, 2.0f, -3.0f}, {0.0f, 0.0f, -1.0f},
                                        {0.0f, 1.0f, 0.0f}, 4.0f, 2.0f, 6.0f),
                                    "Placed Box", MaterialReference(material))
                    .ok);
    const auto before = editor.tools().live_primitive_cube();
    ASSERT_TRUE(before.has_value());
    const EntityId entity = *session.modeling().entity_for_modeling_object(before->object_id);
    const TransformComponent transform{
        .position = {7.0f, 3.0f, -2.0f},
        .rotation_euler = {0.2f, 0.7f, -0.1f},
        .scale = {2.0f, 0.5f, 3.0f},
    };
    ASSERT_TRUE(session.scene().set_transform(entity, transform));
    const QMat4 world = session.scene().world_transform_matrix(entity);
    const auto placed = editor.tools().live_primitive_cube();
    ASSERT_TRUE(placed.has_value());
    EXPECT_GT(placed->revision, before->revision);
    EXPECT_EQ(placed->revision, editor.tools().live_primitive_cube()->revision);
    EXPECT_TRUE(near_point(placed->world_origin, transform.position));
    EXPECT_TRUE(near_point(placed->world_x, transform_vector(world, {1.0f, 0.0f, 0.0f})));
    EXPECT_TRUE(near_point(placed->world_y, transform_vector(world, {0.0f, 1.0f, 0.0f})));
    EXPECT_TRUE(near_point(placed->world_z, transform_vector(world, {0.0f, 0.0f, 1.0f})));
    const auto sampling = effective_texture_sampling_from_local(session.scene(), entity);
    ASSERT_TRUE(sampling.has_value());
    const auto selected = session.scene().selected_entities();
    ASSERT_NE(session.scene().material(entity), nullptr);
    const auto material_before = *session.scene().material(entity);
    ASSERT_TRUE(editor.tools().begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::Y, false).ok());
    ASSERT_TRUE(editor.tools().update_primitive_cube_resize(-4.0).ok());
    ASSERT_TRUE(editor.tools().end_primitive_cube_resize().ok());
    for (const auto setting : {kBoxSubdivisionsXSetting, kBoxSubdivisionsYSetting,
                               kBoxSubdivisionsZSetting}) {
      ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, setting, 2.0).ok());
    }
    const auto resized = editor.tools().live_primitive_cube();
    ASSERT_TRUE(resized.has_value());
    EXPECT_EQ(resized->object_id, before->object_id);
    EXPECT_FLOAT_EQ(resized->local_min.y, before->local_min.y - 4.0f);
    EXPECT_EQ(resized->subdivisions, (std::array<int, 3>{2, 2, 2}));
    EXPECT_EQ(session.scene().world_transform_matrix(entity).values, world.values);
    EXPECT_EQ(session.scene().selected_entities(), selected);
    ASSERT_NE(session.scene().material(entity), nullptr);
    EXPECT_EQ(*session.scene().material(entity), material_before);
    const auto sampling_after = effective_texture_sampling_from_local(session.scene(), entity);
    ASSERT_TRUE(sampling_after.has_value());
    EXPECT_EQ(sampling_after->values, sampling->values);
    expect_cube_mesh_bounds(session.modeling(), resized->object_id,
                             resized->local_min, resized->local_max);
    const auto mesh = session.modeling().compile_object_mesh(resized->object_id);
    ASSERT_TRUE(mesh.has_value());
    // The retained sampling frame evaluates regenerated vertices with the same
    // world/locked policy; no entity motion or fresh chart is introduced.
    const auto expected_sampling = MeshProjectionService::texture_mapping_transform(*sampling);
    std::vector<QVec2> expected_uvs;
    for (const auto &vertex : mesh->geometry().vertices) {
      expected_uvs.push_back(quader_poly::evaluate_texture_mapping(
          vertex.uv, vertex.position, vertex.uv_geometric_normal,
          vertex.uv_carrier, expected_sampling));
    }
    expect_texture_uvs_near(effective_texture_uvs(session.scene(), entity, *mesh), expected_uvs);
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     PublicCubeSubdivisionSettingsChangeOnlyTheirAxisAndRespectTheMaximum) {
  const std::array settings{kBoxSubdivisionsXSetting, kBoxSubdivisionsYSetting,
                            kBoxSubdivisionsZSetting};
  for (std::size_t axis = 0; axis < settings.size(); ++axis) {
    SCOPED_TRACE(axis);
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
    ASSERT_TRUE(session.primitive_creation()
                    .accept_preview(valid_primitive_preview(), "Subdivided Box")
                    .ok);
    const auto original = editor.tools().live_primitive_cube();
    ASSERT_TRUE(original.has_value());
    ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, settings[axis], 2.0).ok());
    auto expected = std::array<int, 3>{1, 1, 1};
    expected[axis] = 2;
    EXPECT_EQ(editor.tools().live_primitive_cube()->subdivisions, expected);
    EXPECT_EQ(compiled_index_count(session.modeling(), "Subdivided Box"), 60U);
    ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, settings[axis], 128.0).ok());
    expected[axis] = 128;
    const auto maximum = editor.tools().live_primitive_cube();
    ASSERT_TRUE(maximum.has_value());
    EXPECT_EQ(maximum->subdivisions, expected);
    EXPECT_EQ(compiled_index_count(session.modeling(), "Subdivided Box"), 3084U);
    EXPECT_FALSE(editor.tools().set_setting(kCreatePrimitiveToolId, settings[axis], 128.0).changed());
    EXPECT_FALSE(editor.tools().set_setting(kCreatePrimitiveToolId, settings[axis], 129.0).ok());
    EXPECT_EQ(editor.tools().live_primitive_cube()->revision, maximum->revision);
    expect_cube_mesh_bounds(session.modeling(), original->object_id,
                             original->local_min, original->local_max);
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     PublicFlipTargetsPendingFootprintOrIdleCubeAndCancelResizeKeepsSettings) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  EXPECT_FALSE(editor.tools().flip_primitive_faces().ok());
  EXPECT_FALSE(editor.tools().live_primitive_cube().has_value());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
  EXPECT_TRUE(session.primitive_creation().box_settings().flip_faces);
  EXPECT_FALSE(editor.tools().live_primitive_cube().has_value());
  ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(), "First Box").ok);
  const auto first = editor.tools().live_primitive_cube();
  ASSERT_TRUE(first.has_value());
  const auto first_mesh = session.modeling().compile_object_mesh(first->object_id);
  ASSERT_TRUE(first_mesh.has_value());
  const auto first_content = compiled_content_id(session.modeling(), "First Box");
  ASSERT_TRUE(session.modeling().begin_box_footprint({
      .ray = {.origin = {6.0f, 4.0f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  }).ok);
  EXPECT_FALSE(editor.tools().live_primitive_cube().has_value());
  EXPECT_FALSE(editor.tools().begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::X, true).ok());
  ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
  EXPECT_FALSE(session.primitive_creation().box_settings().flip_faces);
  EXPECT_EQ(compiled_content_id(session.modeling(), "First Box"), first_content);
  ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(6.0f), "Second Box").ok);
  const auto second = editor.tools().live_primitive_cube();
  ASSERT_TRUE(second.has_value());
  EXPECT_NE(second->object_id, first->object_id);
  const auto second_mesh = session.modeling().compile_object_mesh(second->object_id);
  ASSERT_TRUE(second_mesh.has_value());
  EXPECT_NEAR(cube_signed_volume(*first_mesh), -cube_signed_volume(*second_mesh), 0.0001);
  ASSERT_TRUE(editor.tools().begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::Z, true).ok());
  ASSERT_TRUE(editor.tools().update_primitive_cube_resize(2.0).ok());
  ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
  ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kBoxSubdivisionsXSetting, 3.0).ok());
  ASSERT_TRUE(editor.tools().cancel_primitive_cube_resize().ok());
  const auto canceled = editor.tools().live_primitive_cube();
  ASSERT_TRUE(canceled.has_value());
  EXPECT_FALSE(canceled->resizing);
  EXPECT_TRUE(canceled->flip_faces);
  EXPECT_EQ(canceled->subdivisions[0], 3);
  EXPECT_EQ(cube_components(canceled->local_min), cube_components(second->local_min));
  EXPECT_EQ(cube_components(canceled->local_max), cube_components(second->local_max));
  expect_cube_mesh_bounds(session.modeling(), canceled->object_id,
                           second->local_min, second->local_max);
  EXPECT_EQ(compiled_content_id(session.modeling(), "First Box"), first_content);
  const auto flipped_mesh = session.modeling().compile_object_mesh(canceled->object_id);
  ASSERT_TRUE(flipped_mesh.has_value());
  EXPECT_NEAR(cube_signed_volume(*flipped_mesh), -cube_signed_volume(*second_mesh), 0.0001);
  ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
  const auto unflipped_mesh = session.modeling().compile_object_mesh(canceled->object_id);
  ASSERT_TRUE(unflipped_mesh.has_value());
  EXPECT_NEAR(cube_signed_volume(*unflipped_mesh), cube_signed_volume(*second_mesh), 0.0001);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(PrimitiveCreationToolServiceTests,
     PublicUndoCancelsResizeBeforeShapeUndoAndHistoryCoalescesLiveEdits) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(), "History Box").ok);
  const auto original = editor.tools().live_primitive_cube();
  ASSERT_TRUE(original.has_value());
  ASSERT_TRUE(editor.tools().begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::X, true).ok());
  ASSERT_TRUE(editor.tools().update_primitive_cube_resize(1.0).ok());
  ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
  ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kBoxSubdivisionsYSetting, 3.0).ok());
  ASSERT_TRUE(editor.history().undo().ok());
  const auto canceled = editor.tools().live_primitive_cube();
  ASSERT_TRUE(canceled.has_value());
  EXPECT_FALSE(canceled->resizing);
  EXPECT_TRUE(canceled->flip_faces);
  EXPECT_EQ(canceled->subdivisions[1], 3);
  EXPECT_EQ(cube_components(canceled->local_max), cube_components(original->local_max));
  EXPECT_EQ(session.primitive_creation().shape_count(), 1U);
  EXPECT_EQ(session.history().undo_count(), 0U);
  ASSERT_TRUE(editor.tools().begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::Y, false).ok());
  ASSERT_TRUE(editor.tools().update_primitive_cube_resize(-2.0).ok());
  ASSERT_TRUE(editor.tools().end_primitive_cube_resize().ok());
  const auto final_cube = editor.tools().live_primitive_cube();
  ASSERT_TRUE(final_cube.has_value());
  const auto final_content = compiled_content_id(session.modeling(), "History Box");
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(session.modeling().objects().empty());
  EXPECT_FALSE(editor.tools().live_primitive_cube().has_value());
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(compiled_content_id(session.modeling(), "History Box"), final_content);
  EXPECT_FALSE(editor.tools().live_primitive_cube().has_value());
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_EQ(session.history().undo_count(), 1U);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(session.modeling().objects().empty());
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(compiled_content_id(session.modeling(), "History Box"), final_content);
  expect_cube_mesh_bounds(session.modeling(), final_cube->object_id,
                           final_cube->local_min, final_cube->local_max);
}

TEST(PrimitiveCreationToolServiceTests,
     PendingFlipThenFootprintCancellationNeverReopensThePreviousLiveCube) {
  for (const bool cancel_with_undo : {false, true}) {
    SCOPED_TRACE(cancel_with_undo);
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
    ASSERT_TRUE(session.primitive_creation()
                    .accept_preview(valid_primitive_preview(), "Frozen Box")
                    .ok);
    const auto previous = editor.tools().live_primitive_cube();
    ASSERT_TRUE(previous.has_value());
    EXPECT_FALSE(previous->flip_faces);
    const auto previous_content = compiled_content_id(session.modeling(), "Frozen Box");
    ASSERT_TRUE(session.modeling().begin_box_footprint({
        .ray = {.origin = {6.0f, 4.0f, 0.0f},
                .direction = {0.0f, -1.0f, 0.0f}},
        .grid_size = 1.0,
    }).ok);
    ASSERT_TRUE(session.scene().selected_entities().empty());
    const auto footprint_selection = session.scene().selected_entities();
    ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
    EXPECT_TRUE(session.primitive_creation().box_settings().flip_faces);
    EXPECT_EQ(compiled_content_id(session.modeling(), "Frozen Box"), previous_content);
    if (cancel_with_undo) {
      ASSERT_TRUE(editor.history().undo().ok());
    } else {
      // The viewport's canceled footprint path can reset only the runtime
      // drawing gesture while leaving the persistent primitive session active.
      ASSERT_TRUE(session.modeling().cancel_box().ok);
      ASSERT_TRUE(session.modeling().activate_box_tool().ok);
    }
    EXPECT_EQ(session.modeling().box_drag_state(), ModelingBoxDragState::Idle);
    EXPECT_EQ(session.primitive_creation().shape_count(), 1U);
    EXPECT_EQ(session.scene().selected_entities(), footprint_selection);
    EXPECT_FALSE(editor.tools().live_primitive_cube().has_value());
    EXPECT_FALSE(editor.tools()
                     .begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::X, true)
                     .ok());
    EXPECT_FALSE(editor.tools().update_primitive_cube_resize(1.0).ok());
    ASSERT_TRUE(editor.tools()
                    .set_setting(kCreatePrimitiveToolId, kBoxSubdivisionsXSetting, 2.0)
                    .ok());
    EXPECT_EQ(compiled_content_id(session.modeling(), "Frozen Box"), previous_content);
    ASSERT_TRUE(session.primitive_creation()
                    .accept_preview(valid_primitive_preview(6.0f), "Pending Flipped Box")
                    .ok);
    const auto next = editor.tools().live_primitive_cube();
    ASSERT_TRUE(next.has_value());
    EXPECT_TRUE(next->flip_faces);
    EXPECT_EQ(next->subdivisions[0], 2);
    EXPECT_NE(next->object_id, previous->object_id);
    const auto previous_mesh = session.modeling().compile_object_mesh(previous->object_id);
    const auto next_mesh = session.modeling().compile_object_mesh(next->object_id);
    ASSERT_TRUE(previous_mesh.has_value());
    ASSERT_TRUE(next_mesh.has_value());
    EXPECT_NEAR(cube_signed_volume(*previous_mesh), -cube_signed_volume(*next_mesh), 0.0001);
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     PublicSelectionAcceptsCubeResizeAndRejectsStaleCancellationWithoutReselecting) {
  for (const bool select_other : {false, true}) {
    SCOPED_TRACE(select_other);
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    const auto target = editor.modeling().create_box(
        {.min = {7.0f, -1.0f, -1.0f}, .max = {9.0f, 1.0f, 1.0f}},
        "Selection Target");
    ASSERT_TRUE(target.ok()) << target.error_message();
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
    ASSERT_TRUE(session.primitive_creation()
                    .accept_preview(valid_primitive_preview(), "Selection Box")
                    .ok);
    const auto cube = editor.tools().live_primitive_cube();
    ASSERT_TRUE(cube.has_value());
    const auto entity = session.modeling().entity_for_modeling_object(cube->object_id);
    ASSERT_TRUE(entity.has_value());
    ASSERT_TRUE(editor.tools()
                    .begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::X, false)
                    .ok());
    ASSERT_TRUE(editor.tools().update_primitive_cube_resize(-1.0).ok());
    const auto resized = editor.tools().live_primitive_cube();
    ASSERT_TRUE(resized.has_value());
    ASSERT_TRUE(editor.selection().clear().ok());
    ASSERT_TRUE(session.scene().selected_entities().empty());
    if (select_other) {
      ASSERT_TRUE(editor.selection().select_object(target.value()).ok());
    }
    const auto expected_selection = session.scene().selected_entities();
    const auto expected_summary = session.modeling().selection_summary();
    const auto selection_revision = session.scene().selection_revision();
    EXPECT_FALSE(editor.tools().live_primitive_cube().has_value());
    EXPECT_FALSE(editor.tools().update_primitive_cube_resize(-2.0).ok());
    const auto stale_cancel = editor.tools().cancel_primitive_cube_resize();
    ASSERT_TRUE(stale_cancel.ok());
    EXPECT_FALSE(stale_cancel.changed());
    EXPECT_EQ(session.scene().selected_entities(), expected_selection);
    EXPECT_EQ(session.scene().selection_revision(), selection_revision);
    const auto restored_summary = session.modeling().selection_summary();
    EXPECT_EQ(restored_summary.has_selection, expected_summary.has_selection);
    EXPECT_EQ(restored_summary.active_object_id, expected_summary.active_object_id);
    EXPECT_EQ(restored_summary.selected_object_count, expected_summary.selected_object_count);
    expect_cube_mesh_bounds(session.modeling(), cube->object_id,
                             resized->local_min, resized->local_max);
    const auto accepted_content = compiled_content_id(session.modeling(), "Selection Box");
    EXPECT_FALSE(session.primitive_creation().active());
    EXPECT_FALSE(editor.tools().flip_primitive_faces().ok());
    EXPECT_EQ(compiled_content_id(session.modeling(), "Selection Box"), accepted_content);
    ASSERT_TRUE(editor.selection().select_object(*entity).ok());
    EXPECT_FALSE(editor.tools().live_primitive_cube().has_value());
    EXPECT_FALSE(editor.tools()
                     .begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::X, true)
                     .ok());
    EXPECT_FALSE(editor.tools().cancel_active_tool_preview().ok());
    // Undo reselect, optional target selection, and deselect before creation.
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(session.scene().selected_entities(), expected_selection);
    EXPECT_TRUE(session.scene().exists(*entity));
    if (select_other) {
      ASSERT_TRUE(editor.history().undo().ok());
      EXPECT_TRUE(session.scene().selected_entities().empty());
      EXPECT_TRUE(session.scene().exists(*entity));
    }
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(session.scene().primary_selection(), *entity);
    expect_cube_mesh_bounds(session.modeling(), cube->object_id,
                             resized->local_min, resized->local_max);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_FALSE(session.scene().exists(*entity));
    ASSERT_TRUE(editor.history().redo().ok());
    expect_cube_mesh_bounds(session.modeling(), cube->object_id,
                             resized->local_min, resized->local_max);
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_TRUE(session.scene().selected_entities().empty());
    if (select_other) {
      ASSERT_TRUE(editor.history().redo().ok());
      EXPECT_EQ(session.scene().selected_entities(), expected_selection);
    }
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(session.scene().primary_selection(), *entity);
  }
}

TEST(PrimitiveCreationToolServiceTests,
     ZeroHistoryLimitStillAllowsUndoToCancelAnUnfinishedCubeResize) {
  PrimitiveCreationHarness harness;
  harness.history.set_max_steps(0U);
  ASSERT_TRUE(harness.tool.activate().ok);
  ASSERT_TRUE(harness.tool.accept_preview(valid_primitive_preview(), "Unrecorded Box").ok);
  const auto cube = harness.tool.live_cube();
  ASSERT_TRUE(cube.has_value());
  EXPECT_FALSE(harness.tool.can_undo());
  ASSERT_TRUE(harness.tool.begin_cube_resize(PrimitiveCubeAxis::Y, false).ok);
  ASSERT_TRUE(harness.tool.update_cube_resize(-3.0).ok);
  ASSERT_TRUE(harness.tool.flip_faces().ok);
  EXPECT_TRUE(harness.tool.can_undo());
  ASSERT_TRUE(harness.tool.undo().ok);
  EXPECT_FALSE(harness.tool.can_undo());
  EXPECT_TRUE(harness.tool.live_cube()->flip_faces);
  EXPECT_EQ(harness.tool.shape_count(), 1U);
  expect_cube_mesh_bounds(harness.modeling, cube->modeling_object_id,
                           cube->local_min, cube->local_max);
  ASSERT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     SettingsBeforeViewportRetirementAcceptLostSelectionResizeFirst) {
  for (const std::string_view action : {"flip", "subdivide", "unchanged"}) {
    SCOPED_TRACE(action);
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
    ASSERT_TRUE(session.primitive_creation()
                    .accept_preview(valid_primitive_preview(), "Interrupted Box")
                    .ok);
    const auto cube = editor.tools().live_primitive_cube();
    ASSERT_TRUE(cube.has_value());
    ASSERT_TRUE(editor.tools()
                    .begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::X, true)
                    .ok());
    ASSERT_TRUE(editor.tools().update_primitive_cube_resize(1.0).ok());
    const auto resized_cube = editor.tools().live_primitive_cube();
    ASSERT_TRUE(resized_cube.has_value());
    ASSERT_TRUE(session.modeling().clear_selection().ok);
    const auto resized_content = compiled_content_id(session.modeling(), "Interrupted Box");
    // Rejected input leaves the unfinished gesture available for cancellation.
    EXPECT_FALSE(editor.tools()
                     .set_setting(kCreatePrimitiveToolId, kBoxSubdivisionsXSetting, 129.0)
                     .ok());
    EXPECT_EQ(compiled_content_id(session.modeling(), "Interrupted Box"), resized_content);
    const auto changed = action == "flip"
                             ? editor.tools().flip_primitive_faces()
                             : editor.tools().set_setting(
                                   kCreatePrimitiveToolId, kBoxSubdivisionsXSetting,
                                   action == "subdivide" ? 3.0 : 1.0);
    ASSERT_TRUE(changed.ok()) << changed.error_message();
    // Even an unchanged setting reports that the gesture was finalized.
    EXPECT_TRUE(changed.changed());
    EXPECT_TRUE(session.primitive_creation().active());
    EXPECT_FALSE(editor.tools().live_primitive_cube().has_value());
    EXPECT_TRUE(session.scene().selected_entities().empty());
    EXPECT_EQ(compiled_content_id(session.modeling(), "Interrupted Box"), resized_content);
    expect_cube_mesh_bounds(session.modeling(), cube->object_id,
                             resized_cube->local_min, resized_cube->local_max);
    EXPECT_EQ(session.primitive_creation().box_settings().flip_faces, action == "flip");
    EXPECT_EQ(session.primitive_creation().box_settings().subdivisions_x,
              action == "subdivide" ? 3 : 1);
    // The viewport's later cleanup sees an already retired owner gesture.
    EXPECT_FALSE(editor.tools().cancel_primitive_cube_resize().changed());
    EXPECT_FALSE(editor.tools().end_primitive_cube_resize().changed());
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     SettingsRetireInterruptedResizeWithoutRepublishingItsDisplayedGeometry) {
  for (const bool flip : {false, true}) {
    for (const auto failure : {ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
                               ModelingSdkProjectionFailurePoint::CommittedSceneNotify}) {
      SCOPED_TRACE(::testing::Message() << "flip=" << flip
                                       << ", failure=" << static_cast<int>(failure));
      SceneSession session;
      QuaderEditorApi editor(
          QuaderApiSessionInternalAccess::from_scene_session(session));
      const auto baseline = editor.modeling().create_box(
          {.min = {7.0f, -1.0f, -1.0f}, .max = {9.0f, 1.0f, 1.0f}}, "Baseline Box");
      ASSERT_TRUE(baseline.ok());
      ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
      ASSERT_TRUE(session.primitive_creation()
                      .accept_preview(valid_primitive_preview(), "Failed Interrupted Box")
                      .ok);
      ASSERT_TRUE(editor.tools()
                      .begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::X, true)
                      .ok());
      ASSERT_TRUE(editor.tools().update_primitive_cube_resize(1.0).ok());
      const auto content = compiled_content_id(session.modeling(), "Failed Interrupted Box");
      ASSERT_TRUE(session.modeling().clear_selection().ok);
      session.modeling().set_modeling_sdk_projection_failure_for_tests(failure);
      const auto changed = flip
                                ? editor.tools().flip_primitive_faces()
                                : editor.tools().set_setting(
                                      kCreatePrimitiveToolId, kBoxSubdivisionsXSetting, 3.0);
      ASSERT_TRUE(changed.ok()) << changed.error_message();
      EXPECT_TRUE(changed.changed());
      EXPECT_TRUE(session.primitive_creation().active());
      EXPECT_EQ(session.primitive_creation().box_settings().flip_faces, flip);
      EXPECT_EQ(session.primitive_creation().box_settings().subdivisions_x, flip ? 1 : 3);
      ASSERT_EQ(session.modeling().objects().size(), 2U);
      EXPECT_EQ(compiled_content_id(session.modeling(), "Failed Interrupted Box"), content);
      EXPECT_FALSE(editor.tools().live_primitive_cube().has_value());
      EXPECT_FALSE(editor.tools().cancel_primitive_cube_resize().changed());
      session.modeling().set_modeling_sdk_projection_failure_for_tests(
          ModelingSdkProjectionFailurePoint::None);
      ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
      ASSERT_EQ(session.modeling().objects().size(), 1U);
      EXPECT_EQ(session.modeling().objects().front().modeling_object_id,
                baseline.value().modeling_object_id);
    }
  }
}

TEST(PrimitiveCreationToolServiceTests,
     PublicComponentSelectionAcceptsCubeResizeAndKeepsItsActiveElement) {
  for (const auto kind : {QuaderComponentKind::Vertex, QuaderComponentKind::Edge,
                          QuaderComponentKind::Face}) {
    SCOPED_TRACE(static_cast<int>(kind));
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
    ASSERT_TRUE(session.primitive_creation()
                    .accept_preview(valid_primitive_preview(), "Component Box")
                    .ok);
    const auto cube = editor.tools().live_primitive_cube();
    ASSERT_TRUE(cube.has_value());
    const auto object = editor.query().modeling_object_for_id(cube->object_id);
    ASSERT_TRUE(object.has_value());
    ASSERT_TRUE(editor.tools()
                    .begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::Z, true)
                    .ok());
    ASSERT_TRUE(editor.tools().update_primitive_cube_resize(1.0).ok());
    const auto resized = editor.tools().live_primitive_cube();
    ASSERT_TRUE(resized.has_value());
    if (kind == QuaderComponentKind::Vertex) {
      ASSERT_TRUE(editor.selection().select_vertices(*object, {1, 2}).ok());
    } else if (kind == QuaderComponentKind::Edge) {
      ASSERT_TRUE(editor.selection().select_edge(*object, 1, 2).ok());
      ASSERT_TRUE(editor.selection()
                      .select_edge(*object, 2, 3, QuaderSelectionEdit::Add)
                      .ok());
    } else {
      ASSERT_TRUE(editor.selection().select_faces(*object, {1, 2}).ok());
    }
    const auto expected = editor.query().selection();
    const auto scene_selection = session.scene().selected_entities();
    const auto primary_selection = session.scene().primary_selection();
    const auto scene_selection_revision = session.scene().selection_revision();
    ASSERT_TRUE(expected.has_selection);
    EXPECT_EQ(expected.selected_vertex_count + expected.selected_edge_count +
                  expected.selected_face_count,
              2U);
    EXPECT_FALSE(editor.tools().live_primitive_cube().has_value());
    ASSERT_TRUE(editor.tools().cancel_primitive_cube_resize().ok());
    const auto restored = editor.query().selection();
    EXPECT_EQ(restored.mode, expected.mode);
    EXPECT_EQ(restored.has_selection, expected.has_selection);
    EXPECT_EQ(restored.active_kind, expected.active_kind);
    EXPECT_EQ(restored.active_object_id, expected.active_object_id);
    EXPECT_EQ(restored.active_vertex_id, expected.active_vertex_id);
    EXPECT_EQ(restored.active_edge_a, expected.active_edge_a);
    EXPECT_EQ(restored.active_edge_b, expected.active_edge_b);
    EXPECT_EQ(restored.active_face_id, expected.active_face_id);
    EXPECT_EQ(restored.selected_vertex_count, expected.selected_vertex_count);
    EXPECT_EQ(restored.selected_edge_count, expected.selected_edge_count);
    EXPECT_EQ(restored.selected_face_count, expected.selected_face_count);
    EXPECT_EQ(restored.selected_object_count, expected.selected_object_count);
    EXPECT_EQ(session.scene().selected_entities(), scene_selection);
    EXPECT_EQ(session.scene().primary_selection(), primary_selection);
    EXPECT_EQ(session.scene().selection_revision(), scene_selection_revision);
    expect_cube_mesh_bounds(session.modeling(), cube->object_id,
                             resized->local_min, resized->local_max);
    EXPECT_FALSE(session.primitive_creation().active());
    EXPECT_EQ(session.history().undo_count(), 1U);
    EXPECT_FALSE(editor.tools().cancel_active_tool_preview().ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     FailedCubeResizePublicationRetainsTheSessionAndLastDisplayedBoundsForRetry) {
  for (const auto failure : {ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
                             ModelingSdkProjectionFailurePoint::CommittedSceneNotify}) {
    SCOPED_TRACE(static_cast<int>(failure));
    PrimitiveCreationHarness harness;
    const auto baseline = harness.modeling.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Baseline Box",
    });
    ASSERT_TRUE(baseline.ok);
    const auto baseline_content = compiled_content_id(harness.modeling, "Baseline Box");
    ASSERT_TRUE(harness.tool.activate().ok);
    ASSERT_TRUE(harness.tool.accept_preview(valid_primitive_preview(4.0f), "Rejected Resize Box").ok);
    const auto content = compiled_content_id(harness.modeling, "Rejected Resize Box");
    const auto cube = harness.tool.live_cube();
    ASSERT_TRUE(cube.has_value());
    const auto assets = harness.assets.mesh_ids();
    ASSERT_TRUE(harness.tool.begin_cube_resize(PrimitiveCubeAxis::Z, true).ok);
    harness.modeling.set_modeling_sdk_projection_failure_for_tests(failure);
    const auto rejected = harness.tool.update_cube_resize(1.0);
    EXPECT_FALSE(rejected.ok);
    EXPECT_FALSE(rejected.changed);
    EXPECT_TRUE(harness.tool.active());
    ASSERT_TRUE(harness.tool.live_cube().has_value());
    EXPECT_TRUE(harness.tool.live_cube()->resizing);
    ASSERT_EQ(harness.modeling.objects().size(), 2U);
    EXPECT_EQ(compiled_content_id(harness.modeling, "Rejected Resize Box"), content);
    expect_cube_mesh_bounds(harness.modeling, cube->modeling_object_id,
                           cube->local_min, cube->local_max);
    EXPECT_FLOAT_EQ(harness.tool.live_cube()->local_min.z, cube->local_min.z);
    EXPECT_FLOAT_EQ(harness.tool.live_cube()->local_max.z, cube->local_max.z);
    EXPECT_EQ(compiled_content_id(harness.modeling, "Baseline Box"), baseline_content);
    EXPECT_EQ(harness.assets.mesh_ids(), assets);
    EXPECT_FALSE(harness.history.can_undo());
    harness.modeling.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    ASSERT_TRUE(harness.tool.update_cube_resize(1.0).ok);
    ASSERT_TRUE(harness.tool.finish().ok);
    EXPECT_TRUE(harness.history.can_undo());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     CubePublicFacadeRejectsOtherKindsInvalidAxesAndClearsDescriptorOnRebind) {
  SceneSession first;
  SceneSession second;
  QuaderToolsApi tools(QuaderApiSessionInternalAccess::from_scene_session(first));
  EXPECT_FALSE(tools.live_primitive_cube().has_value());
  EXPECT_FALSE(tools.begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::X, true).ok());
  ASSERT_TRUE(first.primitive_creation().activate(PrimitiveCreationKind::Cylinder).ok);
  ASSERT_TRUE(first.primitive_creation().accept_preview(valid_primitive_preview(), "Cylinder").ok);
  EXPECT_FALSE(tools.live_primitive_cube().has_value());
  const auto cylinder = tools.live_primitive_creation();
  ASSERT_TRUE(cylinder.has_value());
  EXPECT_EQ(cylinder->shape, QuaderPrimitiveCreationShape::Cylinder);
  ASSERT_TRUE(tools.flip_primitive_faces().ok());
  ASSERT_TRUE(tools.toggle_primitive_caps().ok());
  EXPECT_TRUE(tools.live_primitive_creation()->flip_faces);
  EXPECT_FALSE(tools.live_primitive_creation()->caps);
  ASSERT_TRUE(tools.begin_primitive_resize(QuaderPrimitiveResizeControl::Radius).ok());
  EXPECT_FALSE(tools.begin_primitive_cube_resize(QuaderPrimitiveCubeAxis::Y, true).ok());
  EXPECT_FALSE(tools.update_primitive_cube_resize(1.0).ok());
  EXPECT_FALSE(tools.cancel_primitive_cube_resize().changed());
  EXPECT_FALSE(tools.end_primitive_cube_resize().changed());
  EXPECT_TRUE(tools.live_primitive_creation()->resizing);
  ASSERT_TRUE(tools.cancel_primitive_resize().ok());
  ASSERT_TRUE(first.primitive_creation().set_kind(PrimitiveCreationKind::Box).ok);
  const auto cube = tools.live_primitive_cube();
  ASSERT_TRUE(cube.has_value());
  EXPECT_EQ(tools.live_primitive_creation()->revision, cube->revision);
  EXPECT_EQ(tools.live_primitive_creation()->shape, QuaderPrimitiveCreationShape::Box);
  EXPECT_FALSE(tools.toggle_primitive_caps().ok());
  EXPECT_FALSE(tools.begin_primitive_resize(QuaderPrimitiveResizeControl::Radius).ok());
  EXPECT_FALSE(tools.begin_primitive_cube_resize(static_cast<QuaderPrimitiveCubeAxis>(99), true).ok());
  EXPECT_EQ(tools.live_primitive_cube()->revision, cube->revision);
  tools.rebind_session(QuaderApiSessionInternalAccess::from_scene_session(second));
  EXPECT_FALSE(tools.live_primitive_cube().has_value());
  EXPECT_FALSE(tools.live_primitive_creation().has_value());
  EXPECT_FALSE(tools.update_primitive_cube_resize(1.0).ok());
  ASSERT_TRUE(second.primitive_creation().activate().ok);
  ASSERT_TRUE(second.primitive_creation().accept_preview(valid_primitive_preview(8.0f), "Other Scene Box").ok);
  const auto rebound = tools.live_primitive_cube();
  ASSERT_TRUE(rebound.has_value());
  EXPECT_GT(rebound->revision, cube->revision);
  EXPECT_NEAR(rebound->world_origin.x, 8.0f, 0.0001f);
  EXPECT_EQ(tools.live_primitive_creation()->revision, rebound->revision);
  ASSERT_TRUE(second.primitive_creation().set_kind(PrimitiveCreationKind::Sphere).ok);
  ASSERT_TRUE(tools.live_primitive_creation().has_value());
  EXPECT_EQ(tools.live_primitive_creation()->shape, QuaderPrimitiveCreationShape::Sphere);
  EXPECT_FALSE(tools.live_primitive_cube().has_value());
  ASSERT_TRUE(tools.flip_primitive_faces().ok());
  ASSERT_TRUE(tools.toggle_primitive_hemisphere().ok());
  EXPECT_TRUE(tools.live_primitive_creation()->hemisphere);
  EXPECT_FALSE(tools.toggle_primitive_caps().ok());
  ASSERT_TRUE(first.primitive_creation().cancel().ok);
  ASSERT_TRUE(second.primitive_creation().cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     PublicBeginPrimitiveCreationMapsEveryShapeWithoutCreatingContent) {
  struct ShapeCase {
    QuaderPrimitiveCreationShape api_shape;
    PrimitiveCreationKind owner_kind;
  };
  constexpr ShapeCase kCases[] = {
      {QuaderPrimitiveCreationShape::Box, PrimitiveCreationKind::Box},
      {QuaderPrimitiveCreationShape::Cylinder,
       PrimitiveCreationKind::Cylinder},
      {QuaderPrimitiveCreationShape::Sphere, PrimitiveCreationKind::Sphere},
      {QuaderPrimitiveCreationShape::Torus, PrimitiveCreationKind::Torus},
      {QuaderPrimitiveCreationShape::Cone, PrimitiveCreationKind::Cone},
  };

  for (const ShapeCase &test_case : kCases) {
    SCOPED_TRACE(static_cast<int>(test_case.api_shape));
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));

    const QuaderApiVoidResult result =
        editor.tools().begin_primitive_creation(test_case.api_shape);

    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());
    ASSERT_TRUE(session.primitive_creation().active_kind().has_value());
    EXPECT_EQ(*session.primitive_creation().active_kind(),
              test_case.owner_kind);
    EXPECT_TRUE(session.modeling().objects().empty());
    EXPECT_FALSE(session.history().can_undo());
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     PublicBeginPrimitiveCreationRejectsInvalidShapeAndRetainsRepeatedDrawing) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools()
                  .begin_primitive_creation(QuaderPrimitiveCreationShape::Box)
                  .ok());
  ASSERT_TRUE(session.modeling()
                  .begin_box_footprint({
                      .ray = {.origin = {0.0f, 8.0f, 0.0f},
                              .direction = {0.0f, -1.0f, 0.0f}},
                      .grid_size = 1.0,
                  })
                  .ok);
  const auto preview_before = session.modeling().box_preview(false);

  const auto before_repeated = test::capture_command_observable_state(editor);
  const QuaderApiVoidResult during_footprint =
      editor.tools().begin_primitive_creation(
          QuaderPrimitiveCreationShape::Box);
  EXPECT_TRUE(during_footprint.ok());
  EXPECT_FALSE(during_footprint.changed());
  const auto repeated_difference = test::diff_command_observable_state(
      before_repeated, test::capture_command_observable_state(editor));
  EXPECT_TRUE(repeated_difference.empty()) << repeated_difference.summary;
  ASSERT_TRUE(session.primitive_creation().active_kind().has_value());
  EXPECT_EQ(*session.primitive_creation().active_kind(),
            PrimitiveCreationKind::Box);
  EXPECT_EQ(session.modeling().box_preview(false).drag_state,
            preview_before.drag_state);
  EXPECT_TRUE(session.modeling().objects().empty());
  EXPECT_FALSE(session.history().can_undo());

  const QuaderApiVoidResult invalid = editor.tools().begin_primitive_creation(
      static_cast<QuaderPrimitiveCreationShape>(99));
  EXPECT_FALSE(invalid.ok());
  EXPECT_FALSE(invalid.changed());
  ASSERT_TRUE(session.primitive_creation().active_kind().has_value());
  EXPECT_EQ(*session.primitive_creation().active_kind(),
            PrimitiveCreationKind::Box);
  EXPECT_EQ(session.modeling().box_preview(false).drag_state,
            preview_before.drag_state);
  EXPECT_TRUE(session.modeling().objects().empty());
  EXPECT_FALSE(session.history().can_undo());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(PrimitiveCreationToolServiceTests,
     ShapeLaunchFinishesAcceptedShapesAndCancelOnlyRollsBackTheNewSession) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  auto &tool = session.primitive_creation();
  ASSERT_TRUE(editor.tools().begin_primitive_creation(QuaderPrimitiveCreationShape::Box).ok());
  ASSERT_TRUE(tool.accept_preview(valid_primitive_preview(), "First Box").ok);
  ASSERT_TRUE(tool.accept_preview(valid_primitive_preview(4.0f), "Second Box").ok);
  ASSERT_TRUE(tool.accept_preview(valid_primitive_preview(8.0f), "Undone Box").ok);
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.history().can_redo());
  const auto first_content = compiled_content_id(session.modeling(), "First Box");
  const auto second_content = compiled_content_id(session.modeling(), "Second Box");
  const auto selected_object = session.modeling().selection_summary().active_object_id;

  ASSERT_TRUE(editor.tools().begin_primitive_creation(QuaderPrimitiveCreationShape::Cylinder).ok());
  EXPECT_EQ(tool.active_kind(), PrimitiveCreationKind::Cylinder);
  EXPECT_EQ(tool.shape_count(), 0u);
  EXPECT_FALSE(tool.can_undo());
  EXPECT_FALSE(tool.can_redo());
  EXPECT_TRUE(session.history().can_undo());
  EXPECT_FALSE(session.history().can_redo());
  EXPECT_FALSE(editor.tools().live_primitive_creation().has_value());
  ASSERT_EQ(session.modeling().objects().size(), 2u);
  EXPECT_EQ(compiled_content_id(session.modeling(), "First Box"), first_content);
  EXPECT_EQ(compiled_content_id(session.modeling(), "Second Box"), second_content);
  EXPECT_EQ(session.modeling().selection_summary().active_object_id, selected_object);
  ASSERT_TRUE(tool.accept_preview(valid_primitive_preview(8.0f), "New Cylinder").ok);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_EQ(session.modeling().objects().size(), 2u);
  EXPECT_EQ(compiled_content_id(session.modeling(), "First Box"), first_content);
  EXPECT_EQ(compiled_content_id(session.modeling(), "Second Box"), second_content);
  EXPECT_EQ(editor.tools().preferred_primitive_shape(), QuaderPrimitiveCreationShape::Cylinder);

  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_EQ(session.modeling().objects().size(), 1u);
  EXPECT_EQ(compiled_content_id(session.modeling(), "First Box"), first_content);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(session.modeling().objects().empty());
  ASSERT_TRUE(editor.history().redo().ok());
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(compiled_content_id(session.modeling(), "Second Box"), second_content);
}

TEST(PrimitiveCreationToolServiceTests,
     ShapeSwitchPreservesUnfinishedFootprintAndRejectsTheTransition) {
  for (const bool accepted_shape : {false, true}) {
    for (const bool height_stage : {false, true}) {
      SCOPED_TRACE(::testing::Message() << accepted_shape << ";" << height_stage);
      SceneSession session;
      QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
      ASSERT_TRUE(editor.tools().begin_primitive_creation(QuaderPrimitiveCreationShape::Box).ok());
      if (accepted_shape) {
        ASSERT_TRUE(session.primitive_creation().accept_preview(
            valid_primitive_preview(), "Completed Box").ok);
      }
      const ModelingBoxToolEvent start{
          .ray = {.origin = {4.0f, 8.0f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}},
          .grid_size = 1.0};
      ASSERT_TRUE(session.modeling().begin_box_footprint(start).ok);
      auto finish = start;
      finish.ray.origin.x += 2.0f;
      finish.ray.origin.z += 2.0f;
      ASSERT_TRUE(session.modeling().update_box_footprint(finish).valid);
      if (height_stage) {
        ASSERT_TRUE(session.modeling().begin_box_height(finish).valid);
      }
      const auto before_repeated = test::capture_command_observable_state(editor);
      const auto repeated = editor.tools().begin_primitive_creation(QuaderPrimitiveCreationShape::Box);
      EXPECT_TRUE(repeated.ok());
      EXPECT_FALSE(repeated.changed());
      const auto difference = test::diff_command_observable_state(
          before_repeated, test::capture_command_observable_state(editor));
      EXPECT_TRUE(difference.empty()) << difference.summary;

      const auto refused = editor.tools().begin_primitive_creation(
          QuaderPrimitiveCreationShape::Sphere);
      EXPECT_FALSE(refused.ok());
      EXPECT_FALSE(refused.changed());
      EXPECT_EQ(session.primitive_creation().active_kind(), PrimitiveCreationKind::Box);
      EXPECT_NE(session.modeling().box_drag_state(), ModelingBoxDragState::Idle);
      EXPECT_TRUE(session.modeling().box_preview(false).valid);
      EXPECT_EQ(session.modeling().objects().size(), accepted_shape ? 1u : 0u);
      EXPECT_FALSE(session.history().can_undo());
      ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
      EXPECT_TRUE(session.modeling().objects().empty());
    }
  }
}

TEST(PrimitiveCreationToolServiceTests,
     ShapeSwitchAcceptsTheDisplayedResizeBeforeCommittingTheCompletedObject) {
  for (const auto shape : {QuaderPrimitiveCreationShape::Box, QuaderPrimitiveCreationShape::Torus}) {
    SCOPED_TRACE(static_cast<int>(shape));
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.tools().begin_primitive_creation(shape).ok());
    ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(), "Completed Shape").ok);
    const auto original = editor.tools().live_primitive_creation();
    ASSERT_TRUE(original.has_value());
    const auto control = shape == QuaderPrimitiveCreationShape::Box
        ? QuaderPrimitiveResizeControl::PositiveX : QuaderPrimitiveResizeControl::InnerRadius;
    ASSERT_TRUE(editor.tools().begin_primitive_resize(control).ok());
    ASSERT_TRUE(editor.tools().update_primitive_resize(0.2).ok());
    const auto resized = editor.tools().live_primitive_creation();
    ASSERT_TRUE(resized.has_value());
    const auto resized_content = compiled_content_id(session.modeling(), "Completed Shape");
    const auto before_repeated = test::capture_command_observable_state(editor);
    const auto repeated = editor.tools().begin_primitive_creation(shape);
    EXPECT_TRUE(repeated.ok());
    EXPECT_FALSE(repeated.changed());
    const auto difference = test::diff_command_observable_state(
        before_repeated, test::capture_command_observable_state(editor));
    EXPECT_TRUE(difference.empty()) << difference.summary;
    EXPECT_TRUE(editor.tools().live_primitive_creation()->resizing);

    ASSERT_TRUE(editor.tools().begin_primitive_creation(QuaderPrimitiveCreationShape::Sphere).ok());
    ASSERT_EQ(session.modeling().objects().size(), 1u);
    const auto mesh = session.modeling().compile_object_mesh(original->object_id);
    ASSERT_TRUE(mesh.has_value());
    for (const auto &vertex : mesh->geometry().vertices) {
      EXPECT_GE(vertex.position.x, resized->local_min.x - 0.0001f);
      EXPECT_LE(vertex.position.x, resized->local_max.x + 0.0001f);
    }
    if (shape == QuaderPrimitiveCreationShape::Torus) {
      EXPECT_DOUBLE_EQ(session.primitive_creation().torus_settings().tube_radius_ratio,
                       resized->tube_radius_ratio);
    }
    EXPECT_EQ(compiled_content_id(session.modeling(), "Completed Shape"), resized_content);
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(session.modeling().objects().empty());
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(session.modeling().objects().front().modeling_object_id, original->object_id);
    EXPECT_EQ(compiled_content_id(session.modeling(), "Completed Shape"), resized_content);
  }
}

TEST(PrimitiveCreationToolServiceTests,
     ShapeSwitchAcceptsResizeWithoutRepublishingOrRestoringItsBaseline) {
  for (const auto failure : {ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
                             ModelingSdkProjectionFailurePoint::CommittedSceneNotify}) {
    SCOPED_TRACE(static_cast<int>(failure));
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.tools().begin_primitive_creation(QuaderPrimitiveCreationShape::Box).ok());
    ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(), "Failed Resize").ok);
    ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::PositiveX).ok());
    ASSERT_TRUE(editor.tools().update_primitive_resize(0.5).ok());
    const auto resized_content = compiled_content_id(session.modeling(), "Failed Resize");
    session.modeling().set_modeling_sdk_projection_failure_for_tests(failure);
    const auto switched = editor.tools().begin_primitive_creation(QuaderPrimitiveCreationShape::Sphere);
    EXPECT_TRUE(switched.ok()) << switched.error_message();
    EXPECT_TRUE(session.primitive_creation().active());
    EXPECT_EQ(session.primitive_creation().active_kind(), PrimitiveCreationKind::Sphere);
    EXPECT_FALSE(editor.tools().live_primitive_creation().has_value());
    EXPECT_EQ(editor.tools().preferred_primitive_shape(), QuaderPrimitiveCreationShape::Sphere);
    EXPECT_TRUE(session.history().can_undo());
    ASSERT_EQ(session.modeling().objects().size(), 1u);
    EXPECT_EQ(compiled_content_id(session.modeling(), "Failed Resize"), resized_content);
  }
}

TEST(PrimitiveCreationToolServiceTests,
     PreferredShapeReadsApplicationDefaultsWithoutReplacingActiveSceneChoices) {
  auto defaults = std::make_shared<ToolSessionDefaults>();
  SceneSession first({}, {}, defaults);
  SceneSession second({}, {}, defaults);
  QuaderEditorApi first_editor(QuaderApiSessionInternalAccess::from_scene_session(first));
  QuaderEditorApi second_editor(QuaderApiSessionInternalAccess::from_scene_session(second));
  EXPECT_EQ(first_editor.tools().preferred_primitive_shape(), QuaderPrimitiveCreationShape::Box);
  ASSERT_TRUE(first_editor.tools().begin_primitive_creation(QuaderPrimitiveCreationShape::Cylinder).ok());
  EXPECT_EQ(second_editor.tools().preferred_primitive_shape(), QuaderPrimitiveCreationShape::Cylinder);
  ASSERT_TRUE(second_editor.tools().begin_primitive_creation(QuaderPrimitiveCreationShape::Sphere).ok());
  EXPECT_EQ(first_editor.tools().preferred_primitive_shape(), QuaderPrimitiveCreationShape::Cylinder);
  ASSERT_TRUE(first_editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(first_editor.tools().preferred_primitive_shape(), QuaderPrimitiveCreationShape::Sphere);
  ASSERT_TRUE(second_editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(second_editor.tools().preferred_primitive_shape(), QuaderPrimitiveCreationShape::Sphere);
}

void activate_public_cylinder(QuaderToolsApi &tools) {
  ASSERT_TRUE(tools.activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(tools.set_setting(kCreatePrimitiveToolId, kPrimitiveTypeSetting,
                                "cylinder").ok());
}

void expect_cylinder_mesh_volume(const ModelingSessionService &modeling,
                                 const QuaderPrimitiveCreationState &state) {
  const auto mesh = modeling.compile_object_mesh(state.object_id);
  ASSERT_TRUE(mesh.has_value());
  ASSERT_FALSE(mesh->geometry().vertices.empty());
  const QVec3 center = (state.local_min + state.local_max) * 0.5f;
  const QVec3 radius = (state.local_max - state.local_min) * 0.5f;
  bool bottom = false;
  bool top = false;
  for (const auto &vertex : mesh->geometry().vertices) {
    const double x = (vertex.position.x - center.x) / radius.x;
    const double z = (vertex.position.z - center.z) / radius.z;
    EXPECT_NEAR(x * x + z * z, 1.0, 0.00001);
    EXPECT_TRUE(vertex.position.y == state.local_min.y ||
                vertex.position.y == state.local_max.y);
    bottom = bottom || vertex.position.y == state.local_min.y;
    top = top || vertex.position.y == state.local_max.y;
  }
  EXPECT_TRUE(bottom);
  EXPECT_TRUE(top);
}

TEST(PrimitiveCreationToolServiceTests,
     CylinderEndControlsKeepOppositeEndFixedAcrossUpdatesAndClampCrossing) {
  for (const bool positive : {false, true}) {
    SCOPED_TRACE(positive);
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_NO_FATAL_FAILURE(activate_public_cylinder(editor.tools()));
    ASSERT_TRUE(session.primitive_creation().accept_preview(
        oriented_primitive_preview({2.0f, 2.0f, 3.0f}, {1.0f, 0.0f, 0.0f},
                                   {0.0f, 1.0f, 0.0f}, 6.0f, 4.0f, 2.0f),
        "End Cylinder").ok);
    const auto before = editor.tools().live_primitive_creation();
    ASSERT_TRUE(before.has_value());
    const auto control = positive ? QuaderPrimitiveResizeControl::PositiveY
                                  : QuaderPrimitiveResizeControl::NegativeY;
    ASSERT_TRUE(editor.tools().begin_primitive_resize(control).ok());
    for (const double delta : {positive ? 2.0 : -2.0, positive ? -0.5 : 0.5}) {
      ASSERT_TRUE(editor.tools().update_primitive_resize(delta).ok());
      const auto resized = editor.tools().live_primitive_creation();
      ASSERT_TRUE(resized.has_value());
      EXPECT_FLOAT_EQ(resized->local_min.y,
                      before->local_min.y + (positive ? 0.0f : static_cast<float>(delta)));
      EXPECT_FLOAT_EQ(resized->local_max.y,
                      before->local_max.y + (positive ? static_cast<float>(delta) : 0.0f));
      EXPECT_FLOAT_EQ(resized->local_min.x, before->local_min.x);
      EXPECT_FLOAT_EQ(resized->local_max.x, before->local_max.x);
      EXPECT_FLOAT_EQ(resized->local_min.z, before->local_min.z);
      EXPECT_FLOAT_EQ(resized->local_max.z, before->local_max.z);
      expect_cylinder_mesh_volume(session.modeling(), *resized);
    }
    ASSERT_TRUE(editor.tools().end_primitive_resize().ok());
    const auto repeated_start = editor.tools().live_primitive_creation();
    ASSERT_TRUE(repeated_start.has_value());
    ASSERT_TRUE(editor.tools().begin_primitive_resize(control).ok());
    const double outward = positive ? 1.0 : -1.0;
    ASSERT_TRUE(editor.tools().update_primitive_resize(outward).ok());
    const auto repeated = editor.tools().live_primitive_creation();
    EXPECT_FLOAT_EQ(positive ? repeated->local_max.y : repeated->local_min.y,
                   (positive ? repeated_start->local_max.y : repeated_start->local_min.y)
                       + static_cast<float>(outward));
    ASSERT_TRUE(editor.tools().update_primitive_resize(positive ? -100.0 : 100.0).ok());
    const auto clamped = editor.tools().live_primitive_creation();
    ASSERT_TRUE(clamped.has_value());
    EXPECT_GE(static_cast<double>(clamped->local_max.y) - clamped->local_min.y,
              kPrimitiveMinimumExtent);
    EXPECT_LT(clamped->local_max.y - clamped->local_min.y, 0.000101f);
    expect_cylinder_mesh_volume(session.modeling(), *clamped);
    const auto stats = session.modeling().modeling_sdk_projection_stats_for_tests();
    EXPECT_FALSE(editor.tools().update_primitive_resize(positive ? -200.0 : 200.0).changed());
    EXPECT_EQ(editor.tools().live_primitive_creation()->revision, clamped->revision);
    EXPECT_EQ(session.modeling().modeling_sdk_projection_stats_for_tests().mesh_compile_count,
              stats.mesh_compile_count);
    ASSERT_TRUE(editor.tools().cancel_primitive_resize().ok());
    const auto restored = editor.tools().live_primitive_creation();
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(cube_components(restored->local_min), cube_components(repeated_start->local_min));
    EXPECT_EQ(cube_components(restored->local_max), cube_components(repeated_start->local_max));
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     CylinderRadiusUsesEllipseRayDistancePreservesAspectAndSkipsRepeatedPublication) {
  for (const QVec3 axis : {QVec3{2.0f, 0.0f, 0.0f}, QVec3{0.0f, 0.0f, -3.0f},
                           QVec3{3.0f, 7.0f, 4.0f}}) {
    SCOPED_TRACE(::testing::Message() << axis.x << "," << axis.z);
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_NO_FATAL_FAILURE(activate_public_cylinder(editor.tools()));
    ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kCylinderSideCountSetting, 7.0).ok());
    ASSERT_TRUE(session.primitive_creation().accept_preview(
        oriented_primitive_preview({}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                                   6.0f, 4.0f, 2.0f), "Ellipse Cylinder").ok);
    const auto before = editor.tools().live_primitive_creation();
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(cube_components(before->local_min), (std::array<float, 3>{-3.0f, -2.0f, -1.0f}));
    EXPECT_EQ(cube_components(before->local_max), (std::array<float, 3>{3.0f, 2.0f, 1.0f}));
    for (const auto invalid : {QuaderPrimitiveResizeControl::PositiveX,
                               QuaderPrimitiveResizeControl::NegativeX,
                               QuaderPrimitiveResizeControl::PositiveZ,
                               QuaderPrimitiveResizeControl::NegativeZ,
                               static_cast<QuaderPrimitiveResizeControl>(99)}) {
      EXPECT_FALSE(editor.tools().begin_primitive_resize(invalid).ok());
    }
    for (const QVec3 invalid : {QVec3{}, QVec3{0.0f, 1.0f, 0.0f},
             QVec3{std::numeric_limits<float>::infinity(), 0.0f, 0.0f}}) {
      EXPECT_FALSE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::Radius, invalid).ok());
    }
    EXPECT_EQ(editor.tools().live_primitive_creation()->revision, before->revision);
    ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::Radius, axis).ok());
    const auto start_stats = session.modeling().modeling_sdk_projection_stats_for_tests();
    EXPECT_FALSE(editor.tools().update_primitive_resize(0.0).changed());
    for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::max()}) {
      EXPECT_FALSE(editor.tools().update_primitive_resize(invalid).ok());
    }
    EXPECT_EQ(session.modeling().modeling_sdk_projection_stats_for_tests().mesh_compile_count,
              start_stats.mesh_compile_count);
    const double norm = std::hypot(axis.x, axis.z);
    const double ray_radius = 1.0 / std::hypot(axis.x / norm / 3.0, axis.z / norm);
    for (const double delta : {0.75, -0.25}) {
      ASSERT_TRUE(editor.tools().update_primitive_resize(delta).ok());
      const double scale = (ray_radius + delta) / ray_radius;
      const auto resized = editor.tools().live_primitive_creation();
      ASSERT_TRUE(resized.has_value());
      EXPECT_NEAR(resized->local_max.x, 3.0 * scale, 0.000001);
      EXPECT_NEAR(resized->local_max.z, scale, 0.000001);
      EXPECT_FLOAT_EQ(resized->local_min.x, -resized->local_max.x);
      EXPECT_FLOAT_EQ(resized->local_min.z, -resized->local_max.z);
      EXPECT_FLOAT_EQ(resized->local_min.y, before->local_min.y);
      EXPECT_FLOAT_EQ(resized->local_max.y, before->local_max.y);
      expect_cylinder_mesh_volume(session.modeling(), *resized);
      const auto stats = session.modeling().modeling_sdk_projection_stats_for_tests();
      EXPECT_FALSE(editor.tools().update_primitive_resize(delta).changed());
      EXPECT_EQ(editor.tools().live_primitive_creation()->revision, resized->revision);
      const auto repeated_stats = session.modeling().modeling_sdk_projection_stats_for_tests();
      EXPECT_EQ(repeated_stats.mesh_compile_count, stats.mesh_compile_count);
      EXPECT_EQ(repeated_stats.asset_publication_count, stats.asset_publication_count);
      EXPECT_EQ(repeated_stats.scene_publication_count, stats.scene_publication_count);
    }
    ASSERT_TRUE(editor.tools().update_primitive_resize(-100.0).ok());
    const auto clamped = editor.tools().live_primitive_creation();
    ASSERT_TRUE(clamped.has_value());
    const QVec3 extent = clamped->local_max - clamped->local_min;
    EXPECT_GE(extent.x, kPrimitiveMinimumExtent);
    EXPECT_GE(extent.z, kPrimitiveMinimumExtent);
    EXPECT_LT(extent.z, 0.000101);
    EXPECT_NEAR(extent.x / extent.z, 3.0, 0.000001);
    EXPECT_FALSE(editor.tools().update_primitive_resize(-200.0).changed());
    ASSERT_TRUE(editor.tools().cancel_primitive_resize().ok());
    EXPECT_EQ(cube_components(editor.tools().live_primitive_creation()->local_max),
              cube_components(before->local_max));
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     CylinderCapsSidesAndFlipRetainResizedBoundsAndCoalesceHistoryAcrossRepeats) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_NO_FATAL_FAILURE(activate_public_cylinder(editor.tools()));
  ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
  ASSERT_TRUE(editor.tools().toggle_primitive_caps().ok());
  ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(), "First Cylinder").ok);
  const auto original = editor.tools().live_primitive_creation();
  ASSERT_TRUE(original.has_value());
  EXPECT_TRUE(original->flip_faces);
  EXPECT_FALSE(original->caps);
  ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::Radius).ok());
  ASSERT_TRUE(editor.tools().update_primitive_resize(1.0).ok());
  ASSERT_TRUE(editor.tools().toggle_primitive_caps().ok());
  ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
  ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kCylinderSideCountSetting, 8.0).ok());
  ASSERT_TRUE(editor.history().undo().ok());
  const auto canceled = editor.tools().live_primitive_creation();
  ASSERT_TRUE(canceled.has_value());
  EXPECT_FALSE(canceled->resizing);
  EXPECT_FALSE(canceled->flip_faces);
  EXPECT_TRUE(canceled->caps);
  EXPECT_EQ(canceled->side_count, 8);
  EXPECT_EQ(cube_components(canceled->local_min), cube_components(original->local_min));
  EXPECT_EQ(cube_components(canceled->local_max), cube_components(original->local_max));
  ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::NegativeY).ok());
  ASSERT_TRUE(editor.tools().update_primitive_resize(-2.0).ok());
  ASSERT_TRUE(editor.tools().end_primitive_resize().ok());
  ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::Radius).ok());
  ASSERT_TRUE(editor.tools().update_primitive_resize(0.5).ok());
  ASSERT_TRUE(editor.tools().end_primitive_resize().ok());
  const auto resized = editor.tools().live_primitive_creation();
  ASSERT_TRUE(resized.has_value());
  for (const int sides : {3, 128, 7}) {
    ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kCylinderSideCountSetting,
                                          static_cast<double>(sides)).ok());
    EXPECT_EQ(compiled_index_count(session.modeling(), "First Cylinder"),
              static_cast<std::size_t>(12 * sides - 12));
    const auto state = editor.tools().live_primitive_creation();
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->side_count, sides);
    EXPECT_EQ(cube_components(state->local_min), cube_components(resized->local_min));
    EXPECT_EQ(cube_components(state->local_max), cube_components(resized->local_max));
    expect_cylinder_mesh_volume(session.modeling(), *state);
    EXPECT_FALSE(editor.tools().set_setting(kCreatePrimitiveToolId, kCylinderSideCountSetting,
                                           static_cast<double>(sides)).changed());
  }
  for (const double invalid : {2.0, 129.0, 7.5}) {
    EXPECT_FALSE(editor.tools().set_setting(kCreatePrimitiveToolId, kCylinderSideCountSetting, invalid).ok());
  }
  const auto before_flip = session.modeling().compile_object_mesh(resized->object_id);
  ASSERT_TRUE(before_flip.has_value());
  ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
  const auto after_flip = session.modeling().compile_object_mesh(resized->object_id);
  ASSERT_TRUE(after_flip.has_value());
  EXPECT_NEAR(cube_signed_volume(*after_flip), -cube_signed_volume(*before_flip), 0.00001);
  ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
  ASSERT_TRUE(editor.tools().toggle_primitive_caps().ok());
  EXPECT_EQ(compiled_index_count(session.modeling(), "First Cylinder"), 42U);
  const auto first_content = compiled_content_id(session.modeling(), "First Cylinder");
  ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(5.0f), "Second Cylinder").ok);
  const auto second = editor.tools().live_primitive_creation();
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->side_count, 7);
  EXPECT_FALSE(second->caps);
  EXPECT_FALSE(second->flip_faces);
  // New footprints retain settings while establishing their own dimensions.
  EXPECT_EQ(cube_components(second->local_max), (std::array<float, 3>{1.0f, 1.0f, 1.0f}));
  EXPECT_EQ(compiled_content_id(session.modeling(), "First Cylinder"), first_content);
  const auto second_content = compiled_content_id(session.modeling(), "Second Cylinder");
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(session.primitive_creation().shape_count(), 1U);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_FALSE(editor.tools().live_primitive_creation().has_value());
  EXPECT_EQ(compiled_content_id(session.modeling(), "Second Cylinder"), second_content);
  const auto remembered = primitive_settings(session.primitive_creation());
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_EQ(session.history().undo_count(), 2U);
  expect_primitive_settings(session.primitive_creation(), remembered);
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(session.modeling().objects().empty());
  ASSERT_TRUE(editor.history().redo().ok());
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(compiled_content_id(session.modeling(), "First Cylinder"), first_content);
  EXPECT_EQ(compiled_content_id(session.modeling(), "Second Cylinder"), second_content);
  ASSERT_NO_FATAL_FAILURE(activate_public_cylinder(editor.tools()));
  expect_primitive_settings(session.primitive_creation(), remembered);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(PrimitiveCreationToolServiceTests,
     CylinderResizeDescriptorPreservesAuthoritativePlacementMaterialsAndTextureFrame) {
  for (const bool texture_lock : {false, true}) {
    SCOPED_TRACE(texture_lock);
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    editor.preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{
        .texture_lock_enabled = [texture_lock] { return texture_lock; },
    });
    const auto material = session.assets().add_material(make_default_quader_material("Cylinder Material"));
    ASSERT_NO_FATAL_FAILURE(activate_public_cylinder(editor.tools()));
    ASSERT_TRUE(session.primitive_creation().accept_preview(
        oriented_primitive_preview({4.0f, 3.0f, -2.0f}, {0.0f, 0.0f, -1.0f},
                                   {0.0f, 1.0f, 0.0f}, 6.0f, 4.0f, 2.0f),
        "Placed Cylinder", MaterialReference(material)).ok);
    const auto original = editor.tools().live_primitive_creation();
    ASSERT_TRUE(original.has_value());
    EXPECT_TRUE(near_point(original->world_x, {0.0f, 0.0f, -1.0f}));
    const EntityId entity = *session.modeling().entity_for_modeling_object(original->object_id);
    const TransformComponent transform{
        .position = {7.0f, 3.0f, -2.0f},
        .rotation_euler = {0.2f, 0.7f, -0.1f},
        .scale = {2.0f, 0.5f, 3.0f},
    };
    ASSERT_TRUE(session.scene().set_transform(entity, transform));
    const auto world = session.scene().world_transform_matrix(entity);
    const auto placed = editor.tools().live_primitive_creation();
    ASSERT_TRUE(placed.has_value());
    EXPECT_GT(placed->revision, original->revision);
    EXPECT_EQ(editor.tools().live_primitive_creation()->revision, placed->revision);
    EXPECT_TRUE(near_point(placed->world_origin, transform.position));
    EXPECT_TRUE(near_point(placed->world_x, transform_vector(world, {1.0f, 0.0f, 0.0f})));
    EXPECT_TRUE(near_point(placed->world_y, transform_vector(world, {0.0f, 1.0f, 0.0f})));
    EXPECT_TRUE(near_point(placed->world_z, transform_vector(world, {0.0f, 0.0f, 1.0f})));
    const auto sampling = effective_texture_sampling_from_local(session.scene(), entity);
    ASSERT_TRUE(sampling.has_value());
    const auto selected = session.scene().selected_entities();
    const auto material_before = *session.scene().material(entity);
    ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::NegativeY).ok());
    ASSERT_TRUE(editor.tools().update_primitive_resize(-4.0).ok());
    ASSERT_TRUE(editor.tools().end_primitive_resize().ok());
    ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::Radius,
                                                    {3.0f, 0.0f, -4.0f}).ok());
    ASSERT_TRUE(editor.tools().update_primitive_resize(1.0).ok());
    ASSERT_TRUE(editor.tools().end_primitive_resize().ok());
    ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kCylinderSideCountSetting, 7.0).ok());
    ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
    ASSERT_TRUE(editor.tools().toggle_primitive_caps().ok());
    const auto resized = editor.tools().live_primitive_creation();
    ASSERT_TRUE(resized.has_value());
    EXPECT_EQ(resized->object_id, original->object_id);
    EXPECT_FLOAT_EQ(resized->local_min.y, original->local_min.y - 4.0f);
    EXPECT_FLOAT_EQ(resized->local_max.y, original->local_max.y);
    EXPECT_EQ(session.scene().world_transform_matrix(entity).values, world.values);
    EXPECT_EQ(session.scene().selected_entities(), selected);
    ASSERT_NE(session.scene().material(entity), nullptr);
    EXPECT_EQ(*session.scene().material(entity), material_before);
    const auto sampling_after = effective_texture_sampling_from_local(session.scene(), entity);
    ASSERT_TRUE(sampling_after.has_value());
    EXPECT_EQ(sampling_after->values, sampling->values);
    expect_cylinder_mesh_volume(session.modeling(), *resized);
    const auto mesh = session.modeling().compile_object_mesh(resized->object_id);
    ASSERT_TRUE(mesh.has_value());
    const auto expected_sampling = MeshProjectionService::texture_mapping_transform(*sampling);
    std::vector<QVec2> expected_uvs;
    for (const auto &vertex : mesh->geometry().vertices) {
      expected_uvs.push_back(quader_poly::evaluate_texture_mapping(
          vertex.uv, vertex.position, vertex.uv_geometric_normal,
          vertex.uv_carrier, expected_sampling));
    }
    expect_texture_uvs_near(effective_texture_uvs(session.scene(), entity, *mesh), expected_uvs);
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     CylinderPendingCapsAndFlipNeverReopenPreviousShapeAfterFootprintCancellation) {
  for (const bool caps : {false, true}) {
    for (const bool undo : {false, true}) {
      SCOPED_TRACE(::testing::Message() << "caps=" << caps << ";undo=" << undo);
      SceneSession session;
      QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
      ASSERT_NO_FATAL_FAILURE(activate_public_cylinder(editor.tools()));
      ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(), "Frozen Cylinder").ok);
      const auto original_content = compiled_content_id(session.modeling(), "Frozen Cylinder");
      ASSERT_TRUE(session.modeling().begin_box_footprint({
          .ray = {.origin = {6.0f, 4.0f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}},
          .grid_size = 1.0,
      }).ok);
      EXPECT_FALSE(editor.tools().live_primitive_creation().has_value());
      ASSERT_TRUE((caps ? editor.tools().toggle_primitive_caps()
                        : editor.tools().flip_primitive_faces()).ok());
      EXPECT_FALSE(editor.tools().set_setting(kCreatePrimitiveToolId, kCylinderSideCountSetting, 7.0).ok());
      EXPECT_EQ(compiled_content_id(session.modeling(), "Frozen Cylinder"), original_content);
      if (undo) {
        ASSERT_TRUE(editor.history().undo().ok());
      } else {
        ASSERT_TRUE(session.modeling().cancel_box().ok);
        ASSERT_TRUE(session.modeling().activate_box_tool().ok);
      }
      EXPECT_FALSE(editor.tools().live_primitive_creation().has_value());
      EXPECT_FALSE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::Radius).ok());
      ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kCylinderSideCountSetting, 7.0).ok());
      ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(6.0f), "Pending Cylinder").ok);
      const auto pending = editor.tools().live_primitive_creation();
      ASSERT_TRUE(pending.has_value());
      EXPECT_EQ(pending->caps, !caps);
      EXPECT_EQ(pending->flip_faces, !caps);
      EXPECT_EQ(pending->side_count, 7);
      EXPECT_EQ(compiled_content_id(session.modeling(), "Frozen Cylinder"), original_content);
      const auto remembered = primitive_settings(session.primitive_creation());
      ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
      EXPECT_TRUE(session.modeling().objects().empty());
      expect_primitive_settings(session.primitive_creation(), remembered);
    }
  }
}

TEST(PrimitiveCreationToolServiceTests,
     CylinderSelectionLossAcceptsResizeExceptForExplicitCancellation) {
  for (const std::string_view action : {"cancel", "caps", "flip", "unchanged"}) {
    SCOPED_TRACE(action);
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_NO_FATAL_FAILURE(activate_public_cylinder(editor.tools()));
    ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(), "Interrupted Cylinder").ok);
    const auto original_content = compiled_content_id(session.modeling(), "Interrupted Cylinder");
    ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::Radius).ok());
    ASSERT_TRUE(editor.tools().update_primitive_resize(1.0).ok());
    const auto resized_content = compiled_content_id(session.modeling(), "Interrupted Cylinder");
    ASSERT_TRUE(session.modeling().clear_selection().ok);
    const auto selection_revision = session.scene().selection_revision();
    EXPECT_FALSE(editor.tools().live_primitive_creation().has_value());
    EXPECT_FALSE(editor.tools().set_setting(kCreatePrimitiveToolId, kCylinderSideCountSetting, 2.0).ok());
    const auto result = action == "cancel" ? editor.tools().cancel_primitive_resize()
        : action == "caps" ? editor.tools().toggle_primitive_caps()
        : action == "flip" ? editor.tools().flip_primitive_faces()
        : editor.tools().set_setting(kCreatePrimitiveToolId, kCylinderSideCountSetting, 32.0);
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());
    EXPECT_EQ(compiled_content_id(session.modeling(), "Interrupted Cylinder"),
              action == "cancel" ? original_content : resized_content);
    EXPECT_TRUE(session.scene().selected_entities().empty());
    EXPECT_EQ(session.scene().selection_revision(), selection_revision);
    EXPECT_FALSE(editor.tools().live_primitive_creation().has_value());
    EXPECT_FALSE(editor.tools().cancel_primitive_resize().changed());
    EXPECT_FALSE(editor.tools().end_primitive_resize().changed());
    EXPECT_EQ(session.primitive_creation().cylinder_settings().caps, action != "caps");
    EXPECT_EQ(session.primitive_creation().cylinder_settings().flip_faces, action == "flip");
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     CylinderResizePublicationFailuresPreserveTheDraftAndSettingsForRetry) {
  for (const auto failure : {ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
                             ModelingSdkProjectionFailurePoint::CommittedSceneNotify}) {
    for (const std::string_view action : {"resize", "cancel", "caps"}) {
      SCOPED_TRACE(::testing::Message() << static_cast<int>(failure) << ";" << action);
      SceneSession session;
      QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
      ASSERT_TRUE(editor.modeling().create_box(
          {.min = {7.0f, -1.0f, -1.0f}, .max = {9.0f, 1.0f, 1.0f}}, "Baseline").ok());
      const auto baseline = compiled_content_id(session.modeling(), "Baseline");
      ASSERT_NO_FATAL_FAILURE(activate_public_cylinder(editor.tools()));
      ASSERT_TRUE(editor.tools().toggle_primitive_caps().ok());
      ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(), "Failed Cylinder").ok);
      ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::Radius).ok());
      if (action != "resize") {
        ASSERT_TRUE(editor.tools().update_primitive_resize(0.5).ok());
      }
      const auto remembered = primitive_settings(session.primitive_creation());
      const auto content = compiled_content_id(session.modeling(), "Failed Cylinder");
      const auto before = editor.tools().live_primitive_creation();
      ASSERT_TRUE(before.has_value());
      session.modeling().set_modeling_sdk_projection_failure_for_tests(failure);
      const auto result = action == "resize" ? editor.tools().update_primitive_resize(1.0)
          : action == "cancel" ? editor.tools().cancel_primitive_resize()
          : editor.tools().toggle_primitive_caps();
      EXPECT_FALSE(result.ok());
      EXPECT_FALSE(result.changed());
      EXPECT_TRUE(session.primitive_creation().active());
      ASSERT_TRUE(editor.tools().live_primitive_creation().has_value());
      EXPECT_TRUE(editor.tools().live_primitive_creation()->resizing);
      EXPECT_FLOAT_EQ(editor.tools().live_primitive_creation()->local_min.x, before->local_min.x);
      EXPECT_FLOAT_EQ(editor.tools().live_primitive_creation()->local_max.x, before->local_max.x);
      EXPECT_FLOAT_EQ(editor.tools().live_primitive_creation()->local_min.z, before->local_min.z);
      EXPECT_FLOAT_EQ(editor.tools().live_primitive_creation()->local_max.z, before->local_max.z);
      ASSERT_EQ(session.modeling().objects().size(), 2U);
      EXPECT_EQ(compiled_content_id(session.modeling(), "Failed Cylinder"), content);
      EXPECT_EQ(compiled_content_id(session.modeling(), "Baseline"), baseline);
      expect_primitive_settings(session.primitive_creation(), remembered);
      session.modeling().set_modeling_sdk_projection_failure_for_tests(
          ModelingSdkProjectionFailurePoint::None);
      const auto retried = action == "resize" ? editor.tools().update_primitive_resize(1.0)
          : action == "cancel" ? editor.tools().cancel_primitive_resize()
          : editor.tools().toggle_primitive_caps();
      ASSERT_TRUE(retried.ok()) << retried.error_message();
      EXPECT_EQ(session.primitive_creation().cylinder_settings().caps, action == "caps");
      ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
      ASSERT_EQ(session.modeling().objects().size(), 1U);
      EXPECT_EQ(compiled_content_id(session.modeling(), "Baseline"), baseline);
    }
  }
}

/// Starts an API-owned primitive session with a small torus topology for focused tests.
void activate_public_primitive(QuaderToolsApi &tools, std::string_view kind) {
  ASSERT_TRUE(tools.activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(tools.set_setting(kCreatePrimitiveToolId, kPrimitiveTypeSetting, kind).ok());
  if (kind == "torus") {
    ASSERT_TRUE(tools.set_setting(kCreatePrimitiveToolId, kTorusMajorSegmentsSetting, 8.0).ok());
    ASSERT_TRUE(tools.set_setting(kCreatePrimitiveToolId, kTorusMinorSegmentsSetting, 8.0).ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     PublicMaterialReferenceAcceptancePreservesEmbeddedAndLegacyDefaults) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  const AssetId embedded =
      session.assets().add_material(make_default_quader_material("Chosen Material"));
  ASSERT_NE(embedded, kInvalidAssetId);
  ASSERT_NO_FATAL_FAILURE(activate_public_primitive(editor.tools(), "box"));
  for (int acceptance = 0; acceptance < 4; ++acceptance) {
    SCOPED_TRACE(acceptance);
    const ModelingBoxToolEvent start{
        .ray = {.origin = {static_cast<float>(acceptance) * 4.0f, 8.0f, 0.0f},
                .direction = {0.0f, -1.0f, 0.0f}},
        .grid_size = 1.0,
    };
    ASSERT_TRUE(session.modeling().begin_box_footprint(start).ok);
    auto finish = start;
    finish.ray.origin.x += 2.0f;
    finish.ray.origin.z = 2.0f;
    ASSERT_TRUE(session.modeling().update_box_footprint(finish).valid);
    ASSERT_TRUE(session.modeling().begin_box_height(finish).valid);
    const auto result = acceptance == 0
        ? editor.tools().accept_active_primitive(
              "Typed Embedded", QuaderMaterialReference::embedded(embedded))
        : acceptance == 1
        ? editor.tools().accept_active_primitive("Legacy Embedded", embedded)
        : acceptance == 2
        ? editor.tools().accept_active_primitive(
              "Missing Embedded", QuaderMaterialReference::embedded(
                  std::numeric_limits<AssetId>::max()))
        : editor.tools().accept_active_primitive("Ordinary Default");
    ASSERT_TRUE(result.ok()) << result.error_message();
    const auto state = editor.tools().live_primitive_creation();
    ASSERT_TRUE(state.has_value());
    const auto entity = session.modeling().entity_for_modeling_object(state->object_id);
    ASSERT_TRUE(entity.has_value());
    const auto *material = session.scene().material(*entity);
    if (acceptance < 2) {
      ASSERT_NE(material, nullptr);
      EXPECT_EQ(material->material, MaterialReference(embedded));
    } else {
      EXPECT_EQ(material, nullptr);
    }
  }
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_EQ(session.history().undo_count(), 4u);
}

TEST(PrimitiveCreationToolServiceTests,
     FootprintSphereAndTorusProportionsIgnoreGridHeight) {
  struct FootprintCase {
    double grid;
    float width;
    float depth;
    double tube_ratio;
  };
  // Equal footprints on different grids, plus an ellipse with a nondefault tube.
  constexpr FootprintCase kCases[] = {
      {0.25, 4.0f, 4.0f, 0.2},
      {2.0, 4.0f, 4.0f, 0.2},
      {0.5, 6.0f, 2.0f, 0.3},
  };
  for (const std::string_view shape : {"sphere", "hemisphere", "torus"}) {
    SCOPED_TRACE(shape);
    for (const auto &input : kCases) {
      SCOPED_TRACE(input.grid);
      SceneSession session;
      QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
      ASSERT_NO_FATAL_FAILURE(activate_public_primitive(
          editor.tools(), shape == "hemisphere" ? "sphere" : shape));
      if (shape == "torus") {
        ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId,
            kTorusTubeRadiusRatioSetting, input.tube_ratio).ok());
      }
      const ModelingBoxToolEvent start{
          .ray = {.origin = {0.0f, 8.0f, 0.0f},
                  .direction = {0.0f, -1.0f, 0.0f}},
          .grid_size = input.grid,
      };
      ASSERT_TRUE(session.modeling().begin_box_footprint(start).ok);
      if (shape == "hemisphere") {
        // Pending H must affect the very first accepted volume's proportions.
        ASSERT_TRUE(editor.tools().toggle_primitive_hemisphere().ok());
      }
      auto finish = start;
      finish.ray.origin = {input.width, 8.0f, input.depth};
      ASSERT_TRUE(session.modeling().update_box_footprint(finish).valid);
      const auto preview = session.modeling().begin_box_height(finish);
      ASSERT_TRUE(preview.ok) << preview.message;
      ASSERT_TRUE(preview.valid);
      ASSERT_EQ(preview.corner_count, 8U);
      EXPECT_NEAR(length(preview.corners[4] - preview.corners[0]), input.grid, 0.0001);

      // This is the same public acceptance path as the viewport's pointer release.
      ASSERT_TRUE(editor.tools().accept_active_primitive("Natural Primitive").ok());
      const auto state = editor.tools().live_primitive_creation();
      ASSERT_TRUE(state.has_value());
      const float diameter = std::min(input.width, input.depth);
      const double height = diameter * (shape == "torus" ? input.tube_ratio
          : shape == "hemisphere" ? 0.5 : 1.0);
      EXPECT_NEAR(state->local_max.x - state->local_min.x,
                  length(preview.corners[3] - preview.corners[0]), 0.0001);
      EXPECT_NEAR(state->local_max.z - state->local_min.z,
                  length(preview.corners[1] - preview.corners[0]), 0.0001);
      EXPECT_NEAR(state->local_max.y - state->local_min.y, height, 0.0001);
      const auto bottom = (preview.corners[0] + preview.corners[1] +
                            preview.corners[2] + preview.corners[3]) * 0.25f;
      EXPECT_TRUE(near_point(state->world_origin + state->world_y * state->local_min.y,
                             bottom));
      const auto mesh = session.modeling().compile_object_mesh(state->object_id);
      ASSERT_TRUE(mesh.has_value());
      float minimum_y = std::numeric_limits<float>::infinity();
      float maximum_y = -minimum_y;
      for (const auto &vertex : mesh->geometry().vertices) {
        minimum_y = std::min(minimum_y, vertex.position.y);
        maximum_y = std::max(maximum_y, vertex.position.y);
      }
      EXPECT_NEAR(minimum_y, state->local_min.y, 0.0001);
      EXPECT_NEAR(maximum_y, state->local_max.y, 0.0001);
      ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
      EXPECT_TRUE(session.modeling().objects().empty());
    }
  }
}

TEST(PrimitiveCreationToolServiceTests,
     NaturalFootprintHeightKeepsRotatedBaseAndLaterAuthoredDimensions) {
  for (const std::string_view kind : {"sphere", "torus"}) {
    SCOPED_TRACE(kind);
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_NO_FATAL_FAILURE(activate_public_primitive(editor.tools(), kind));
    const QVec3 bottom{4.0f, 3.0f, -2.0f};
    const QVec3 normal{0.0f, 0.6f, 0.8f};
    ASSERT_TRUE(session.primitive_creation().accept_footprint(
        oriented_primitive_preview(bottom + normal * 0.125f,
            {1.0f, 0.0f, 0.0f}, normal, 6.0f, 0.25f, 2.0f),
        "Rotated Natural Primitive").ok);
    const auto initial = editor.tools().live_primitive_creation();
    ASSERT_TRUE(initial.has_value());
    EXPECT_NEAR(initial->local_max.y - initial->local_min.y,
                kind == "sphere" ? 2.0f : 0.4f, 0.0001);
    EXPECT_TRUE(near_point(initial->world_y, normal));
    EXPECT_TRUE(near_point(initial->world_origin + initial->world_y * initial->local_min.y,
                           bottom));
    const auto entity = *session.modeling().entity_for_modeling_object(initial->object_id);
    const auto world = session.scene().world_transform_matrix(entity);
    ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::PositiveY).ok());
    ASSERT_TRUE(editor.tools().update_primitive_resize(0.75).ok());
    ASSERT_TRUE(editor.tools().end_primitive_resize().ok());
    const auto resized = editor.tools().live_primitive_creation();
    ASSERT_TRUE(resized.has_value());
    EXPECT_FLOAT_EQ(resized->local_min.y, initial->local_min.y);
    EXPECT_FLOAT_EQ(resized->local_max.y, initial->local_max.y + 0.75f);
    if (kind == "sphere") {
      ASSERT_TRUE(editor.tools().toggle_primitive_hemisphere().ok());
      ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kSphereRingsSetting, 8.0).ok());
    } else {
      ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kTorusTubeRadiusRatioSetting, 0.3).ok());
    }
    ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
    const auto edited = editor.tools().live_primitive_creation();
    ASSERT_TRUE(edited.has_value());
    EXPECT_EQ(cube_components(edited->local_min), cube_components(resized->local_min));
    EXPECT_EQ(cube_components(edited->local_max), cube_components(resized->local_max));
    EXPECT_EQ(session.scene().world_transform_matrix(entity).values, world.values);
    const auto content = compiled_content_id(session.modeling(), "Rotated Natural Primitive");
    ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
    EXPECT_EQ(session.history().undo_count(), 1U);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(session.modeling().objects().empty());
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(compiled_content_id(session.modeling(), "Rotated Natural Primitive"), content);
    EXPECT_EQ(session.scene().world_transform_matrix(entity).values, world.values);
  }
}

TEST(PrimitiveCreationToolServiceTests,
     ConeAndSphereResizeBoundsSurviveSettingsWhileCancelRetainsIndependentToggles) {
  for (const std::string_view kind : {"cone", "sphere"}) {
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_NO_FATAL_FAILURE(activate_public_primitive(editor.tools(), kind));
    ASSERT_TRUE(session.primitive_creation().accept_preview(
        oriented_primitive_preview({}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                                   6.0f, 4.0f, 2.0f), "Curved Primitive").ok);
    const auto original = editor.tools().live_primitive_creation();
    ASSERT_TRUE(original.has_value());
    EXPECT_EQ(original->shape, kind == "cone" ? QuaderPrimitiveCreationShape::Cone
                                              : QuaderPrimitiveCreationShape::Sphere);
    for (const auto invalid : {QuaderPrimitiveResizeControl::PositiveX,
                               QuaderPrimitiveResizeControl::NegativeZ,
                               QuaderPrimitiveResizeControl::OuterRadius,
                               QuaderPrimitiveResizeControl::InnerRadius}) {
      EXPECT_FALSE(editor.tools().begin_primitive_resize(invalid).ok());
    }
    for (const auto control : {QuaderPrimitiveResizeControl::NegativeY,
                               QuaderPrimitiveResizeControl::PositiveY,
                               QuaderPrimitiveResizeControl::Radius}) {
      SCOPED_TRACE(::testing::Message() << kind << ";" << static_cast<int>(control));
      ASSERT_TRUE(editor.tools().begin_primitive_resize(control).ok());
      const double delta = control == QuaderPrimitiveResizeControl::NegativeY ? -1.0 : 1.0;
      ASSERT_TRUE(editor.tools().update_primitive_resize(delta).ok());
      auto changed = editor.tools().live_primitive_creation();
      ASSERT_TRUE(changed.has_value());
      if (control == QuaderPrimitiveResizeControl::Radius) {
        EXPECT_FLOAT_EQ(changed->local_max.x, 4.0f);
        EXPECT_NEAR(changed->local_max.z, 4.0f / 3.0f, 0.000001f);
        EXPECT_FLOAT_EQ(changed->local_min.y, original->local_min.y);
        EXPECT_FLOAT_EQ(changed->local_max.y, original->local_max.y);
      } else {
        EXPECT_FLOAT_EQ(changed->local_min.y, original->local_min.y +
            (control == QuaderPrimitiveResizeControl::NegativeY ? -1.0f : 0.0f));
        EXPECT_FLOAT_EQ(changed->local_max.y, original->local_max.y +
            (control == QuaderPrimitiveResizeControl::PositiveY ? 1.0f : 0.0f));
      }
      const auto stats = session.modeling().modeling_sdk_projection_stats_for_tests();
      EXPECT_FALSE(editor.tools().update_primitive_resize(delta).changed());
      EXPECT_EQ(session.modeling().modeling_sdk_projection_stats_for_tests().mesh_compile_count,
                stats.mesh_compile_count);
      ASSERT_TRUE((kind == "cone" ? editor.tools().toggle_primitive_caps()
                                  : editor.tools().toggle_primitive_hemisphere()).ok());
      ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
      const auto setting = kind == "cone" ? kConeSideCountSetting : kSphereRadialSegmentsSetting;
      ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, setting, 7.0).ok());
      EXPECT_EQ(cube_components(editor.tools().live_primitive_creation()->local_min),
                cube_components(changed->local_min));
      EXPECT_EQ(cube_components(editor.tools().live_primitive_creation()->local_max),
                cube_components(changed->local_max));
      const auto edited = editor.tools().live_primitive_creation();
      ASSERT_TRUE(editor.tools().cancel_primitive_resize().ok());
      const auto restored = editor.tools().live_primitive_creation();
      ASSERT_TRUE(restored.has_value());
      EXPECT_EQ(cube_components(restored->local_min), cube_components(original->local_min));
      EXPECT_EQ(cube_components(restored->local_max), cube_components(original->local_max));
      EXPECT_EQ(restored->flip_faces, edited->flip_faces);
      EXPECT_EQ(restored->caps, edited->caps);
      EXPECT_EQ(restored->hemisphere, edited->hemisphere);
      EXPECT_EQ(restored->side_count, edited->side_count);
      EXPECT_EQ(restored->radial_segments, edited->radial_segments);
    }
    ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::PositiveY).ok());
    ASSERT_TRUE(editor.tools().update_primitive_resize(-100.0).ok());
    const auto flat = editor.tools().live_primitive_creation();
    EXPECT_GE(static_cast<double>(flat->local_max.y) - flat->local_min.y, kPrimitiveMinimumExtent);
    EXPECT_FALSE(editor.tools().update_primitive_resize(-200.0).changed());
    const auto remembered = primitive_settings(session.primitive_creation());
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
    EXPECT_TRUE(session.modeling().objects().empty());
    expect_primitive_settings(session.primitive_creation(), remembered);
  }
}

TEST(PrimitiveCreationToolServiceTests,
     TorusRadiusControlsHoldOppositeEllipseFixedAndRespectRatioLimits) {
  for (const auto control : {QuaderPrimitiveResizeControl::OuterRadius,
                             QuaderPrimitiveResizeControl::InnerRadius}) {
    SCOPED_TRACE(static_cast<int>(control));
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_NO_FATAL_FAILURE(activate_public_primitive(editor.tools(), "torus"));
    ASSERT_TRUE(session.primitive_creation().accept_preview(
        oriented_primitive_preview({}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                                   6.0f, 4.0f, 2.0f), "Radial Torus").ok);
    const auto before = editor.tools().live_primitive_creation();
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(before->shape, QuaderPrimitiveCreationShape::Torus);
    EXPECT_EQ(before->major_segments, 8);
    EXPECT_EQ(before->minor_segments, 8);
    EXPECT_DOUBLE_EQ(before->tube_radius_ratio, 0.2);
    for (const auto invalid : {QuaderPrimitiveResizeControl::NegativeY,
                               QuaderPrimitiveResizeControl::Radius,
                               QuaderPrimitiveResizeControl::PositiveX}) {
      EXPECT_FALSE(editor.tools().begin_primitive_resize(invalid).ok());
    }
    const QVec3 ray{3.0f, 0.0f, 4.0f};
    const double outer = 1.0 / std::hypot(0.6 / 3.0, 0.8);
    const double inner = outer * 0.6;
    ASSERT_TRUE(editor.tools().begin_primitive_resize(control, ray).ok());
    const auto start = editor.tools().live_primitive_creation();
    EXPECT_FALSE(editor.tools().update_primitive_resize(0.0).changed());
    EXPECT_EQ(editor.tools().live_primitive_creation()->revision, start->revision);
    for (const double delta : {0.25, -0.1, 1000.0, -1000.0}) {
      ASSERT_TRUE(editor.tools().update_primitive_resize(delta).ok());
      const auto resized = editor.tools().live_primitive_creation();
      ASSERT_TRUE(resized.has_value());
      EXPECT_GE(resized->tube_radius_ratio, kTorusMinimumTubeRadiusRatio);
      EXPECT_LE(resized->tube_radius_ratio, kTorusMaximumTubeRadiusRatio);
      const double scale = resized->local_max.x / 3.0;
      EXPECT_NEAR(resized->local_max.z, scale, 0.00001);
      EXPECT_FLOAT_EQ(resized->local_min.x, -resized->local_max.x);
      EXPECT_FLOAT_EQ(resized->local_min.z, -resized->local_max.z);
      EXPECT_FLOAT_EQ(resized->local_min.y, before->local_min.y);
      EXPECT_FLOAT_EQ(resized->local_max.y, before->local_max.y);
      if (control == QuaderPrimitiveResizeControl::OuterRadius) {
        EXPECT_NEAR(outer * scale * (1.0 - 2.0 * resized->tube_radius_ratio), inner, 0.00001);
        const double requested = std::clamp(outer + delta, inner / 0.98, inner / 0.02);
        EXPECT_NEAR(outer * scale, requested, 0.00001);
      } else {
        EXPECT_EQ(cube_components(resized->local_min), cube_components(before->local_min));
        EXPECT_EQ(cube_components(resized->local_max), cube_components(before->local_max));
        EXPECT_NEAR(outer * (1.0 - 2.0 * resized->tube_radius_ratio),
                    std::clamp(inner + delta, outer * 0.02, outer * 0.98), 0.00001);
      }
      const auto stats = session.modeling().modeling_sdk_projection_stats_for_tests();
      EXPECT_FALSE(editor.tools().update_primitive_resize(delta).changed());
      EXPECT_EQ(editor.tools().live_primitive_creation()->revision, resized->revision);
      const auto after = session.modeling().modeling_sdk_projection_stats_for_tests();
      EXPECT_EQ(after.mesh_compile_count, stats.mesh_compile_count);
      EXPECT_EQ(after.asset_publication_count, stats.asset_publication_count);
    }
    EXPECT_FALSE(editor.tools().update_primitive_resize(std::numeric_limits<double>::quiet_NaN()).ok());
    ASSERT_TRUE(editor.tools().cancel_primitive_resize().ok());
    EXPECT_DOUBLE_EQ(editor.tools().live_primitive_creation()->tube_radius_ratio, 0.2);
    ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::PositiveY).ok());
    ASSERT_TRUE(editor.tools().update_primitive_resize(1.0).ok());
    EXPECT_FLOAT_EQ(editor.tools().live_primitive_creation()->local_min.y, before->local_min.y);
    EXPECT_FLOAT_EQ(editor.tools().live_primitive_creation()->local_max.y, before->local_max.y + 1.0f);
    ASSERT_TRUE(editor.tools().end_primitive_resize().ok());
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     TorusCancelAndUndoRestoreRatioWithBoundsWhileHistoryKeepsFinalSettings) {
  for (const bool undo : {false, true}) {
    SCOPED_TRACE(undo);
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_NO_FATAL_FAILURE(activate_public_primitive(editor.tools(), "torus"));
    ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(), "History Torus").ok);
    const auto original = editor.tools().live_primitive_creation();
    for (const auto control : {QuaderPrimitiveResizeControl::InnerRadius,
                               QuaderPrimitiveResizeControl::OuterRadius}) {
      ASSERT_TRUE(editor.tools().begin_primitive_resize(control).ok());
      ASSERT_TRUE(editor.tools().update_primitive_resize(0.25).ok());
      ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
      ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kTorusMinorSegmentsSetting, 7.0).ok());
      const bool flipped = editor.tools().live_primitive_creation()->flip_faces;
      ASSERT_TRUE((undo ? editor.history().undo() : editor.tools().cancel_primitive_resize()).ok());
      const auto restored = editor.tools().live_primitive_creation();
      ASSERT_TRUE(restored.has_value());
      EXPECT_FALSE(restored->resizing);
      EXPECT_DOUBLE_EQ(restored->tube_radius_ratio, original->tube_radius_ratio);
      EXPECT_EQ(cube_components(restored->local_min), cube_components(original->local_min));
      EXPECT_EQ(cube_components(restored->local_max), cube_components(original->local_max));
      EXPECT_EQ(restored->flip_faces, flipped);
      EXPECT_EQ(restored->minor_segments, 7);
      EXPECT_EQ(session.primitive_creation().shape_count(), 1U);
    }
    ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::OuterRadius).ok());
    ASSERT_TRUE(editor.tools().update_primitive_resize(0.5).ok());
    ASSERT_TRUE(editor.tools().end_primitive_resize().ok());
    const auto resized = editor.tools().live_primitive_creation();
    ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kTorusTubeRadiusRatioSetting, 0.3).ok());
    ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kTorusMajorSegmentsSetting, 7.0).ok());
    EXPECT_EQ(cube_components(editor.tools().live_primitive_creation()->local_max), cube_components(resized->local_max));
    const auto first_content = compiled_content_id(session.modeling(), "History Torus");
    ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(4.0f), "Repeated Torus").ok);
    EXPECT_DOUBLE_EQ(editor.tools().live_primitive_creation()->tube_radius_ratio, 0.3);
    EXPECT_FLOAT_EQ(editor.tools().live_primitive_creation()->local_max.x, 1.0f);
    const auto second_content = compiled_content_id(session.modeling(), "Repeated Torus");
    const auto remembered = primitive_settings(session.primitive_creation());
    ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
    EXPECT_EQ(session.history().undo_count(), 2U);
    expect_primitive_settings(session.primitive_creation(), remembered);
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(session.modeling().objects().empty());
    ASSERT_TRUE(editor.history().redo().ok());
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(compiled_content_id(session.modeling(), "History Torus"), first_content);
    EXPECT_EQ(compiled_content_id(session.modeling(), "Repeated Torus"), second_content);
  }
}

TEST(PrimitiveCreationToolServiceTests,
     CurvedPrimitiveGenerationKeepsPlacementTextureAndShapeParameterization) {
  for (const std::string_view kind : {"cone", "sphere", "torus"}) {
    SCOPED_TRACE(kind);
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_NO_FATAL_FAILURE(activate_public_primitive(editor.tools(), kind));
    const auto material = session.assets().add_material(make_default_quader_material("Curved Material"));
    ASSERT_TRUE(session.primitive_creation().accept_preview(
        oriented_primitive_preview({4.0f, 2.0f, -3.0f}, {0.0f, 0.0f, -1.0f},
                                   {0.0f, 1.0f, 0.0f}, 6.0f, 4.0f, 2.0f),
        "Parametric Primitive", MaterialReference(material)).ok);
    const auto original = editor.tools().live_primitive_creation();
    ASSERT_TRUE(original.has_value());
    // Explicit volumes retain their authored height; only drawn footprints
    // receive natural Sphere/Torus proportions.
    EXPECT_FLOAT_EQ(original->local_max.x - original->local_min.x, 6.0f);
    EXPECT_FLOAT_EQ(original->local_max.y - original->local_min.y, 4.0f);
    EXPECT_FLOAT_EQ(original->local_max.z - original->local_min.z, 2.0f);
    const auto entity = *session.modeling().entity_for_modeling_object(original->object_id);
    const TransformComponent transform{.position = {7.0f, 3.0f, -2.0f},
                                      .rotation_euler = {0.2f, 0.7f, -0.1f},
                                      .scale = {2.0f, 0.5f, 3.0f}};
    ASSERT_TRUE(session.scene().set_transform(entity, transform));
    const auto world = session.scene().world_transform_matrix(entity);
    const auto placed = editor.tools().live_primitive_creation();
    EXPECT_GT(placed->revision, original->revision);
    EXPECT_TRUE(near_point(placed->world_x, transform_vector(world, {1.0f, 0.0f, 0.0f})));
    const auto sampling = effective_texture_sampling_from_local(session.scene(), entity);
    ASSERT_TRUE(sampling.has_value());
    const auto material_before = *session.scene().material(entity);
    const auto selection = session.scene().selected_entities();
    ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::PositiveY).ok());
    ASSERT_TRUE(editor.tools().update_primitive_resize(1.0).ok());
    ASSERT_TRUE(editor.tools().end_primitive_resize().ok());
    ASSERT_TRUE(editor.tools().begin_primitive_resize(kind == "torus"
        ? QuaderPrimitiveResizeControl::OuterRadius : QuaderPrimitiveResizeControl::Radius).ok());
    ASSERT_TRUE(editor.tools().update_primitive_resize(0.5).ok());
    ASSERT_TRUE(editor.tools().end_primitive_resize().ok());
    if (kind == "sphere") {
      ASSERT_TRUE(editor.tools().toggle_primitive_hemisphere().ok());
      ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kSphereRingsSetting, 7.0).ok());
    } else if (kind == "cone") {
      ASSERT_TRUE(editor.tools().toggle_primitive_caps().ok());
    }
    ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
    const auto state = editor.tools().live_primitive_creation();
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->object_id, original->object_id);
    EXPECT_EQ(session.scene().world_transform_matrix(entity).values, world.values);
    EXPECT_EQ(session.scene().selected_entities(), selection);
    EXPECT_EQ(*session.scene().material(entity), material_before);
    const auto sampling_after = effective_texture_sampling_from_local(session.scene(), entity);
    ASSERT_TRUE(sampling_after.has_value());
    EXPECT_EQ(sampling_after->values, sampling->values);
    const auto mesh = session.modeling().compile_object_mesh(state->object_id);
    ASSERT_TRUE(mesh.has_value());
    const auto center = (state->local_min + state->local_max) * 0.5f;
    const auto radius = (state->local_max - state->local_min) * 0.5f;
    for (const auto &vertex : mesh->geometry().vertices) {
      const double x = (vertex.position.x - center.x) / radius.x;
      const double z = (vertex.position.z - center.z) / radius.z;
      const double height = (vertex.position.y - state->local_min.y) / (2.0f * radius.y);
      if (kind == "cone") {
        EXPECT_NEAR(x * x + z * z, (1.0 - height) * (1.0 - height), 0.00001);
      } else if (kind == "sphere") {
        // Hemisphere occupies the whole height, with its equator at the bottom.
        EXPECT_NEAR(x * x + z * z + height * height, 1.0, 0.00001);
      } else {
        const double tube = (std::hypot(x, z) - (1.0 - state->tube_radius_ratio)) /
                            state->tube_radius_ratio;
        const double y = (vertex.position.y - center.y) / radius.y;
        EXPECT_NEAR(tube * tube + y * y, 1.0, 0.00001);
      }
    }
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     NewShapePendingFlipCapsAndHemisphereLeavePreviousPrimitiveFrozen) {
  for (const std::string_view kind : {"cone", "sphere", "torus"}) {
    SCOPED_TRACE(kind);
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_NO_FATAL_FAILURE(activate_public_primitive(editor.tools(), kind));
    ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
    ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
    if (kind != "sphere") EXPECT_FALSE(editor.tools().toggle_primitive_hemisphere().ok());
    if (kind != "cone") EXPECT_FALSE(editor.tools().toggle_primitive_caps().ok());
    ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(), "Frozen Shape").ok);
    const auto content = compiled_content_id(session.modeling(), "Frozen Shape");
    ASSERT_TRUE(session.modeling().begin_box_footprint({
        .ray = {.origin = {6.0f, 4.0f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}},
        .grid_size = 1.0,
    }).ok);
    EXPECT_FALSE(editor.tools().live_primitive_creation().has_value());
    for (int toggle = 0; toggle < 3; ++toggle) {
      ASSERT_TRUE(editor.tools().flip_primitive_faces().ok());
      if (kind == "cone") ASSERT_TRUE(editor.tools().toggle_primitive_caps().ok());
      if (kind == "sphere") ASSERT_TRUE(editor.tools().toggle_primitive_hemisphere().ok());
    }
    EXPECT_EQ(compiled_content_id(session.modeling(), "Frozen Shape"), content);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_FALSE(editor.tools().live_primitive_creation().has_value());
    ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(6.0f), "Pending Shape").ok);
    const auto pending = editor.tools().live_primitive_creation();
    ASSERT_TRUE(pending.has_value());
    EXPECT_TRUE(pending->flip_faces);
    if (kind == "cone") EXPECT_FALSE(pending->caps);
    if (kind == "sphere") EXPECT_TRUE(pending->hemisphere);
    EXPECT_EQ(compiled_content_id(session.modeling(), "Frozen Shape"), content);
    const auto remembered = primitive_settings(session.primitive_creation());
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
    expect_primitive_settings(session.primitive_creation(), remembered);
    EXPECT_TRUE(session.modeling().objects().empty());
  }
}

TEST(PrimitiveCreationToolServiceTests,
     TorusInterruptedResizeRetainsRatioWithoutRepublishingBeforePendingFlip) {
  for (const auto failure : {ModelingSdkProjectionFailurePoint::None,
                             ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
                             ModelingSdkProjectionFailurePoint::CommittedSceneNotify}) {
    SCOPED_TRACE(static_cast<int>(failure));
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.modeling().create_box(
        {.min = {7.0f, -1.0f, -1.0f}, .max = {9.0f, 1.0f, 1.0f}}, "Baseline").ok());
    const auto baseline = compiled_content_id(session.modeling(), "Baseline");
    ASSERT_NO_FATAL_FAILURE(activate_public_primitive(editor.tools(), "torus"));
    ASSERT_TRUE(session.primitive_creation().accept_preview(valid_primitive_preview(), "Interrupted Torus").ok);
    ASSERT_TRUE(editor.tools().begin_primitive_resize(QuaderPrimitiveResizeControl::InnerRadius).ok());
    ASSERT_TRUE(editor.tools().update_primitive_resize(0.25).ok());
    const auto content = compiled_content_id(session.modeling(), "Interrupted Torus");
    ASSERT_TRUE(session.modeling().clear_selection().ok);
    const auto selection_revision = session.scene().selection_revision();
    EXPECT_FALSE(editor.tools().set_setting(kCreatePrimitiveToolId, kTorusTubeRadiusRatioSetting, 0.0).ok());
    EXPECT_NE(session.primitive_creation().torus_settings().tube_radius_ratio, 0.2);
    const auto remembered = primitive_settings(session.primitive_creation());
    session.modeling().set_modeling_sdk_projection_failure_for_tests(failure);
    const auto flipped = editor.tools().flip_primitive_faces();
    ASSERT_TRUE(flipped.ok()) << flipped.error_message();
    EXPECT_TRUE(flipped.changed());
    EXPECT_EQ(compiled_content_id(session.modeling(), "Interrupted Torus"), content);
    EXPECT_TRUE(session.scene().selected_entities().empty());
    EXPECT_EQ(session.scene().selection_revision(), selection_revision);
    EXPECT_DOUBLE_EQ(session.primitive_creation().torus_settings().tube_radius_ratio,
                     remembered.torus.tube_radius_ratio);
    EXPECT_TRUE(session.primitive_creation().torus_settings().flip_faces);
    EXPECT_FALSE(editor.tools().live_primitive_creation().has_value());
    EXPECT_FALSE(editor.tools().cancel_primitive_resize().changed());
    EXPECT_TRUE(session.primitive_creation().active());
    ASSERT_EQ(session.modeling().objects().size(), 2U);
    session.modeling().set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
    ASSERT_EQ(session.modeling().objects().size(), 1U);
    EXPECT_EQ(compiled_content_id(session.modeling(), "Baseline"), baseline);
  }
}

TEST(PrimitiveCreationToolServiceTests,
     SphereTypeChangesRetainIndependentTopologyBoundsMaterialAndPlacement) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  auto &tool = session.primitive_creation();
  ASSERT_TRUE(tool.activate(PrimitiveCreationKind::Sphere).ok);
  ASSERT_TRUE(tool.set_setting(kSphereRadialSegmentsSetting, 8.0).ok);
  ASSERT_TRUE(tool.set_setting(kSphereRingsSetting, 4.0).ok);
  const auto material = session.assets().add_material(
      make_default_quader_material("Sphere Material"));
  ASSERT_TRUE(tool.accept_preview(
      oriented_primitive_preview({4.0f, 2.0f, -3.0f}, {0.0f, 0.0f, -1.0f},
                                 {0.0f, 1.0f, 0.0f}, 6.0f, 4.0f, 2.0f),
      "Typed Sphere", MaterialReference(material)).ok);
  ASSERT_TRUE(tool.begin_resize(PrimitiveResizeControl::PositiveY).ok);
  ASSERT_TRUE(tool.update_resize(1.0).ok);
  ASSERT_TRUE(tool.end_resize().ok);
  const auto original = tool.live_primitive_creation();
  ASSERT_TRUE(original.has_value());
  const auto entity = *session.modeling().entity_for_modeling_object(
      original->modeling_object_id);
  ASSERT_TRUE(session.scene().set_transform(entity, {
      .position = {7.0f, 3.0f, -2.0f},
      .rotation_euler = {0.2f, 0.7f, -0.1f},
      .scale = {2.0f, 0.5f, 3.0f}}));
  const auto world = session.scene().world_transform_matrix(entity);
  const auto material_before = *session.scene().material(entity);
  const auto selection = session.scene().selected_entities();
  const auto sampling = effective_texture_sampling_from_local(session.scene(), entity);
  ASSERT_TRUE(sampling.has_value());
  const auto uv_content = compiled_content_id(session.modeling(), "Typed Sphere");

  ASSERT_TRUE(tool.set_setting(kSphereTypeSetting, 1.0).ok);
  EXPECT_EQ(tool.live_primitive_creation()->sphere_type, PrimitiveSphereType::Quad);
  EXPECT_EQ(tool.live_primitive_creation()->sphere_subdivision, 3);
  EXPECT_EQ(compiled_index_count(session.modeling(), "Typed Sphere"), 384U * 6U);
  ASSERT_TRUE(tool.set_setting(kSphereSubdivisionSetting, 2.0).ok);
  EXPECT_EQ(compiled_index_count(session.modeling(), "Typed Sphere"), 96U * 6U);
  const auto quad_content = compiled_content_id(session.modeling(), "Typed Sphere");
  const auto projected = editor.tools().live_primitive_creation();
  ASSERT_TRUE(projected.has_value());
  EXPECT_EQ(projected->sphere_type, QuaderSphereType::Quad);
  EXPECT_EQ(projected->sphere_subdivision, 2);
  const auto projection_stats = session.modeling().modeling_sdk_projection_stats_for_tests();
  for (int frame = 0; frame < 20; ++frame) {
    const auto retained = editor.tools().live_primitive_creation();
    ASSERT_TRUE(retained.has_value());
    EXPECT_EQ(retained->revision, projected->revision);
    EXPECT_EQ(retained->sphere_type, QuaderSphereType::Quad);
    EXPECT_EQ(retained->sphere_subdivision, 2);
  }
  EXPECT_EQ(session.modeling().modeling_sdk_projection_stats_for_tests().mesh_compile_count,
            projection_stats.mesh_compile_count);
  ASSERT_TRUE(tool.set_setting(kSphereTypeSetting, 0.0).ok);
  EXPECT_EQ(tool.sphere_settings().radial_segments, 8);
  EXPECT_EQ(tool.sphere_settings().rings, 4);
  EXPECT_EQ(compiled_content_id(session.modeling(), "Typed Sphere"), uv_content);
  ASSERT_TRUE(tool.set_setting(kSphereTypeSetting, 1.0).ok);
  EXPECT_EQ(tool.sphere_settings().subdivision, 2);
  EXPECT_EQ(compiled_content_id(session.modeling(), "Typed Sphere"), quad_content);
  ASSERT_TRUE(tool.toggle_hemisphere().ok);
  EXPECT_EQ(compiled_index_count(session.modeling(), "Typed Sphere"), 64U * 6U);
  const auto unflipped = session.modeling().compile_object_mesh(original->modeling_object_id);
  ASSERT_TRUE(unflipped.has_value());
  ASSERT_TRUE(tool.flip_faces().ok);
  const auto flipped = session.modeling().compile_object_mesh(original->modeling_object_id);
  ASSERT_TRUE(flipped.has_value());
  EXPECT_NEAR(cube_signed_volume(*flipped), -cube_signed_volume(*unflipped), 0.0001);

  const auto changed = tool.live_primitive_creation();
  ASSERT_TRUE(changed.has_value());
  EXPECT_EQ(changed->modeling_object_id, original->modeling_object_id);
  EXPECT_EQ(cube_components(changed->local_min), cube_components(original->local_min));
  EXPECT_EQ(cube_components(changed->local_max), cube_components(original->local_max));
  EXPECT_EQ(session.scene().world_transform_matrix(entity).values, world.values);
  EXPECT_EQ(session.scene().selected_entities(), selection);
  EXPECT_EQ(*session.scene().material(entity), material_before);
  const auto sampling_after = effective_texture_sampling_from_local(session.scene(), entity);
  ASSERT_TRUE(sampling_after.has_value());
  EXPECT_EQ(sampling_after->values, sampling->values);

  const auto revision = tool.tool_settings_revision();
  ASSERT_TRUE(tool.reset_settings().ok);
  expect_default_primitive_settings(tool);
  EXPECT_EQ(tool.tool_settings_revision(), revision + 1U);
  EXPECT_EQ(cube_components(tool.live_primitive_creation()->local_min),
            cube_components(original->local_min));
  EXPECT_EQ(cube_components(tool.live_primitive_creation()->local_max),
            cube_components(original->local_max));
  const auto reset_content = compiled_content_id(session.modeling(), "Typed Sphere");
  EXPECT_FALSE(tool.reset_settings().changed);
  EXPECT_EQ(tool.tool_settings_revision(), revision + 1U);
  EXPECT_EQ(compiled_content_id(session.modeling(), "Typed Sphere"), reset_content);
  ASSERT_TRUE(tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     SphereTypeRetiresResizeAndRejectsInvalidOrInactiveEditsBeforeMutation) {
  PrimitiveCreationHarness harness;
  auto &tool = harness.tool;
  ASSERT_TRUE(tool.activate(PrimitiveCreationKind::Sphere).ok);
  EXPECT_FALSE(tool.set_setting(kSphereSubdivisionSetting, 2.0).ok);
  ASSERT_TRUE(tool.set_setting(kSphereTypeSetting, 1.0).ok);
  ASSERT_TRUE(tool.set_setting(kSphereSubdivisionSetting, 1.0).ok);
  ASSERT_TRUE(tool.accept_preview(valid_primitive_preview(), "Resize Sphere").ok);
  ASSERT_TRUE(tool.begin_resize(PrimitiveResizeControl::Radius).ok);
  ASSERT_TRUE(tool.update_resize(0.5).ok);
  const auto resized = *tool.live_primitive_creation();
  const auto content = compiled_content_id(harness.modeling, "Resize Sphere");
  const auto selection_revision = harness.modeling.selection_summary().selection_revision;
  struct InvalidSetting {
    std::string_view name;
    double value;
  };
  for (const auto &setting : {
           InvalidSetting{kSphereTypeSetting, -1.0},
           InvalidSetting{kSphereTypeSetting, 2.0},
           InvalidSetting{kSphereTypeSetting, 0.5},
           InvalidSetting{kSphereTypeSetting, std::numeric_limits<double>::quiet_NaN()},
           InvalidSetting{kSphereSubdivisionSetting, 0.0},
           InvalidSetting{kSphereSubdivisionSetting, 7.0},
           InvalidSetting{kSphereSubdivisionSetting, 1.5},
           InvalidSetting{kSphereRadialSegmentsSetting, 8.0},
           InvalidSetting{kSphereRingsSetting, 4.0}}) {
    SCOPED_TRACE(setting.name);
    SCOPED_TRACE(setting.value);
    const auto rejected = tool.set_setting(setting.name, setting.value);
    EXPECT_FALSE(rejected.ok);
    EXPECT_FALSE(rejected.changed);
    const auto retained = tool.live_primitive_creation();
    ASSERT_TRUE(retained.has_value());
    EXPECT_TRUE(retained->resizing);
    EXPECT_EQ(retained->revision, resized.revision);
    EXPECT_EQ(cube_components(retained->local_min), cube_components(resized.local_min));
    EXPECT_EQ(cube_components(retained->local_max), cube_components(resized.local_max));
    EXPECT_EQ(compiled_content_id(harness.modeling, "Resize Sphere"), content);
    EXPECT_EQ(harness.modeling.selection_summary().selection_revision, selection_revision);
  }
  EXPECT_FALSE(tool.set_setting(kSphereTypeSetting, 1.0).changed);
  EXPECT_TRUE(tool.live_primitive_creation()->resizing);
  ASSERT_TRUE(tool.set_setting(kSphereTypeSetting, 0.0).ok);
  EXPECT_FALSE(tool.live_primitive_creation()->resizing);
  EXPECT_EQ(cube_components(tool.live_primitive_creation()->local_max),
            cube_components(resized.local_max));
  EXPECT_FALSE(tool.update_resize(2.0).ok);
  EXPECT_FALSE(tool.cancel_resize().changed);

  ASSERT_TRUE(tool.set_setting(kSphereTypeSetting, 1.0).ok);
  ASSERT_TRUE(tool.begin_resize(PrimitiveResizeControl::PositiveY).ok);
  ASSERT_TRUE(tool.update_resize(1.0).ok);
  ASSERT_TRUE(tool.set_setting(kSphereSubdivisionSetting, 2.0).ok);
  EXPECT_TRUE(tool.live_primitive_creation()->resizing);
  ASSERT_TRUE(tool.cancel_resize().ok);
  EXPECT_EQ(tool.live_primitive_creation()->sphere_subdivision, 2);
  EXPECT_EQ(cube_components(tool.live_primitive_creation()->local_max),
            cube_components(resized.local_max));
  ASSERT_TRUE(tool.begin_resize(PrimitiveResizeControl::PositiveY).ok);
  ASSERT_TRUE(tool.update_resize(1.0).ok);
  const auto before_reset = *tool.live_primitive_creation();
  ASSERT_TRUE(tool.reset_settings().ok);
  EXPECT_FALSE(tool.live_primitive_creation()->resizing);
  EXPECT_EQ(cube_components(tool.live_primitive_creation()->local_max),
            cube_components(before_reset.local_max));
  expect_default_primitive_settings(tool);
  ASSERT_TRUE(tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     QuadSphereFootprintRestrictionsAndSelectionLossFreezeAcceptedGeometry) {
  PrimitiveCreationHarness harness;
  auto &tool = harness.tool;
  ASSERT_TRUE(tool.activate(PrimitiveCreationKind::Sphere).ok);
  ASSERT_TRUE(tool.set_setting(kSphereTypeSetting, 1.0).ok);
  ASSERT_TRUE(tool.set_setting(kSphereSubdivisionSetting, 1.0).ok);
  ASSERT_TRUE(tool.accept_preview(valid_primitive_preview(), "Frozen Sphere").ok);
  const auto content = compiled_content_id(harness.modeling, "Frozen Sphere");
  ASSERT_TRUE(harness.modeling.begin_box_footprint({
      .ray = {.origin = {6.0f, 4.0f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  }).ok);
  const auto revision = tool.tool_settings_revision();
  for (const auto name : {kSphereTypeSetting, kSphereSubdivisionSetting,
                          kSphereRadialSegmentsSetting, kSphereRingsSetting}) {
    const auto rejected = tool.set_setting(name, 0.0);
    EXPECT_FALSE(rejected.ok);
    EXPECT_FALSE(rejected.changed);
  }
  EXPECT_FALSE(tool.can_reset_settings());
  EXPECT_FALSE(tool.reset_settings().ok);
  EXPECT_EQ(tool.tool_settings_revision(), revision);
  ASSERT_TRUE(tool.toggle_hemisphere().ok);
  ASSERT_TRUE(tool.flip_faces().ok);
  EXPECT_EQ(compiled_content_id(harness.modeling, "Frozen Sphere"), content);
  ASSERT_TRUE(tool.undo().ok);
  EXPECT_FALSE(tool.live_primitive_creation().has_value());
  ASSERT_TRUE(tool.accept_footprint(valid_primitive_preview(6.0f), "Pending Sphere").ok);
  const auto pending = tool.live_primitive_creation();
  ASSERT_TRUE(pending.has_value());
  EXPECT_EQ(pending->sphere_type, PrimitiveSphereType::Quad);
  EXPECT_EQ(pending->sphere_subdivision, 1);
  EXPECT_TRUE(pending->hemisphere);
  EXPECT_TRUE(pending->flip_faces);
  EXPECT_FLOAT_EQ(pending->local_max.y - pending->local_min.y, 1.0f);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Pending Sphere"), 16U * 6U);
  const auto pending_content = compiled_content_id(harness.modeling, "Pending Sphere");
  ASSERT_TRUE(harness.modeling.clear_selection().ok);
  ASSERT_TRUE(tool.set_setting(kSphereTypeSetting, 0.0).ok);
  EXPECT_FALSE(tool.live_primitive_creation().has_value());
  EXPECT_EQ(compiled_content_id(harness.modeling, "Pending Sphere"), pending_content);
  EXPECT_EQ(compiled_content_id(harness.modeling, "Frozen Sphere"), content);
  const auto remembered = primitive_settings(tool);
  ASSERT_TRUE(tool.cancel().ok);
  EXPECT_TRUE(harness.modeling.objects().empty());
  expect_primitive_settings(tool, remembered);
}

TEST(PrimitiveCreationToolServiceTests,
     SphereTypeEditsRemainWithinPerShapeHistoryAndPersistAfterSessionEnds) {
  PrimitiveCreationHarness harness;
  auto &tool = harness.tool;
  ASSERT_TRUE(tool.activate(PrimitiveCreationKind::Sphere).ok);
  ASSERT_TRUE(tool.set_setting(kSphereTypeSetting, 1.0).ok);
  ASSERT_TRUE(tool.set_setting(kSphereSubdivisionSetting, 1.0).ok);
  ASSERT_TRUE(tool.accept_preview(valid_primitive_preview(), "First Sphere").ok);
  ASSERT_TRUE(tool.set_setting(kSphereSubdivisionSetting, 2.0).ok);
  const auto first_content = compiled_content_id(harness.modeling, "First Sphere");
  ASSERT_TRUE(tool.accept_preview(valid_primitive_preview(5.0f), "Second Sphere").ok);
  EXPECT_EQ(tool.live_primitive_creation()->sphere_type, PrimitiveSphereType::Quad);
  EXPECT_EQ(tool.live_primitive_creation()->sphere_subdivision, 2);
  ASSERT_TRUE(tool.set_setting(kSphereTypeSetting, 0.0).ok);
  const auto second_content = compiled_content_id(harness.modeling, "Second Sphere");
  EXPECT_EQ(compiled_content_id(harness.modeling, "First Sphere"), first_content);
  ASSERT_TRUE(tool.undo().ok);
  EXPECT_EQ(tool.shape_count(), 1U);
  ASSERT_TRUE(tool.redo().ok);
  EXPECT_FALSE(tool.live_primitive_creation().has_value());
  EXPECT_EQ(compiled_content_id(harness.modeling, "Second Sphere"), second_content);
  const auto remembered = primitive_settings(tool);
  ASSERT_TRUE(tool.apply().ok);
  EXPECT_EQ(harness.history.undo_count(), 2U);
  expect_primitive_settings(tool, remembered);
  ASSERT_TRUE(harness.history.undo().ok);
  ASSERT_TRUE(harness.history.undo().ok);
  EXPECT_TRUE(harness.modeling.objects().empty());
  ASSERT_TRUE(harness.history.redo().ok);
  ASSERT_TRUE(harness.history.redo().ok);
  EXPECT_EQ(compiled_content_id(harness.modeling, "First Sphere"), first_content);
  EXPECT_EQ(compiled_content_id(harness.modeling, "Second Sphere"), second_content);
}

TEST(PrimitiveCreationToolServiceTests, PrimitiveFlipFacesDuringFootprintLeavesPreviousCubeFrozen) {
  constexpr auto kFlip = quader::quader_command::kToolsFlipPrimitiveFaces;
  const auto *contract =
      quader::test::find_command_behavior_contract(kFlip.string());
  ASSERT_NE(contract, nullptr);
  auto fixture = quader::test::CommandBehaviorFixture::create(*contract);
  auto &editor = fixture->editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto footprint = fixture->native_scene().modeling().begin_box_footprint({
      .screen_position = {400.0f, 300.0f},
      .ray = {.origin = {12.0f, 4.0f, 0.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  });
  ASSERT_TRUE(footprint.ok) << footprint.message;
  ASSERT_EQ(footprint.drag_state, quader::ModelingBoxDragState::Footprint);
  const auto before = quader::test::capture_command_observable_state(editor);
  for (const bool expected : {true, false}) {
    const auto dispatch = editor.commands().dispatch_viewport_input_binding(
        QuaderInputChord{.shift = true,
                         .device = QuaderInputDevice::Keyboard,
                         .key = QuaderKeyCode::F},
        {.active_tool = quader::ViewportToolId::CreatePrimitive}, true, false,
        {.modal_tool_active = true, .active_tool_owns_viewport = true});
    ASSERT_EQ(dispatch.command_id, kFlip.string());
    const auto result = editor.commands().execute(dispatch.command_id);
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());
    const auto settings = editor.tools().active_settings();
    const auto flip = std::find_if(settings.controls.begin(), settings.controls.end(),
        [](const auto &control) { return control.id == "primitive.flip_faces"; });
    ASSERT_NE(flip, settings.controls.end());
    EXPECT_EQ(flip->bool_value, expected);
    using Domain = quader::test::CommandObservableDomain;
    const auto after = quader::test::capture_command_observable_state(editor);
    EXPECT_EQ(after.domain(Domain::Geometry), before.domain(Domain::Geometry));
    EXPECT_EQ(after.domain(Domain::History), before.domain(Domain::History));
    EXPECT_EQ(fixture->native_scene().modeling().box_drag_state(),
              quader::ModelingBoxDragState::Footprint);
  }
}

} // namespace
} // namespace quader
