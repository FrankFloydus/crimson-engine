////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/session/scene_session.hpp"
#include "math/glm_bridge.hpp"
#include "quader/modeling/neutral/geometry/geometry.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_transform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr float kEpsilon = 0.001f;

/// Owns an isolated editor session for public Snap to Floor behavior tests.
struct SnapToFloorHarness {
  quader::SceneSession session;
  quader::QuaderEditorApi editor;

  SnapToFloorHarness()
      : editor(quader::QuaderApiSessionInternalAccess::from_scene_session(
            session)) {}
};

/// Keeps the public document and authored IDs for one four-vertex test surface.
struct QuadDocument {
  quader::QuaderPolygonDocument document;
  std::array<quader::QuaderElementId, 4> vertices = {};
};

/// Captures the public renderer-neutral geometry needed for exact replay checks.
struct MeshGeometryState {
  std::vector<quader::QVec3> positions;
  std::vector<std::uint32_t> indices;
  std::vector<quader::QuaderMeshPrimitiveSpan> primitives;
};

quader::QuaderModelingObjectRef
create_box(SnapToFloorHarness &harness, std::string_view name,
           quader::QuaderBounds local_bounds,
           quader::QuaderTransform transform = {}) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      harness.editor.modeling().create_box(local_bounds, std::string(name));
  EXPECT_TRUE(created.ok()) << created.error_message();
  if (created.ok()) {
    const quader::QuaderApiVoidResult transformed =
        harness.editor.scene().set_transform(created.value().entity, transform);
    EXPECT_TRUE(transformed.ok()) << transformed.error_message();
  }
  return created.value();
}

quader::QuaderModelingObjectRef create_mesh(
    SnapToFloorHarness &harness, std::string_view name,
    quader::QuaderPolygonDocument document,
    quader::QuaderTransform transform = {}) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      harness.editor.modeling().create_mesh(
          std::move(document), {.name = std::string(name)});
  EXPECT_TRUE(created.ok()) << created.error_message();
  if (created.ok()) {
    const quader::QuaderApiVoidResult transformed =
        harness.editor.scene().set_transform(created.value().entity, transform);
    EXPECT_TRUE(transformed.ok()) << transformed.error_message();
  }
  return created.value();
}

QuadDocument make_quad_document(
    const std::array<quader::QVec3, 4> &positions) {
  quader_poly::Document backend;
  QuadDocument result;
  std::array<quader_poly::ElementId, 4> backend_vertices = {};
  for (std::size_t index = 0; index < positions.size(); ++index) {
    backend_vertices[index] = quader_poly::add_vertex(backend, positions[index]);
    result.vertices[index] =
        static_cast<quader::QuaderElementId>(backend_vertices[index]);
  }
  EXPECT_NE(quader_poly::add_face(backend, backend_vertices),
            quader_poly::kInvalidElementId);
  result.document =
      quader::QuaderPolygonDocumentInternalAccess::from_backend(
          std::move(backend));
  return result;
}

quader::QuaderPolygonDocument make_sphere_document(float diameter) {
  quader_poly::SpherePrimitiveSpec spec;
  spec.width = diameter;
  spec.height = diameter;
  spec.depth = diameter;
  spec.radial_segments = 32;
  spec.rings = 16;
  return quader::QuaderPolygonDocumentInternalAccess::from_backend(
      quader_poly::make_sphere(spec));
}

quader::SceneBounds bounds(const SnapToFloorHarness &harness,
                           quader::EntityId entity) {
  const std::optional<quader::SceneBounds> result = quader::entity_world_bounds(
      harness.session.scene(), harness.session.assets(), entity);
  EXPECT_TRUE(result.has_value());
  return result.value_or(quader::SceneBounds{});
}

quader::QMat4 effective_instance_member_world(
    const SnapToFloorHarness &harness, quader::EntityId occurrence,
    quader::EntityId definition, quader::EntityId member) {
  const std::optional<quader::QMat4> definition_inverse =
      quader::try_scene_matrix_inverse(
          harness.session.scene().world_transform_matrix(definition));
  EXPECT_TRUE(definition_inverse.has_value());
  if (!definition_inverse.has_value()) {
    return {};
  }
  return quader::scene_transform_multiply(
      harness.session.scene().world_transform_matrix(occurrence),
      quader::scene_transform_multiply(
          *definition_inverse,
          harness.session.scene().world_transform_matrix(member)));
}

quader::SceneBounds transformed_bounds(quader::QuaderBounds local_bounds,
                                       const quader::QMat4 &world_from_local) {
  const std::array<quader::QVec3, 8> corners{
      quader::QVec3{local_bounds.min.x, local_bounds.min.y,
                    local_bounds.min.z},
      quader::QVec3{local_bounds.max.x, local_bounds.min.y,
                    local_bounds.min.z},
      quader::QVec3{local_bounds.min.x, local_bounds.max.y,
                    local_bounds.min.z},
      quader::QVec3{local_bounds.max.x, local_bounds.max.y,
                    local_bounds.min.z},
      quader::QVec3{local_bounds.min.x, local_bounds.min.y,
                    local_bounds.max.z},
      quader::QVec3{local_bounds.max.x, local_bounds.min.y,
                    local_bounds.max.z},
      quader::QVec3{local_bounds.min.x, local_bounds.max.y,
                    local_bounds.max.z},
      quader::QVec3{local_bounds.max.x, local_bounds.max.y,
                    local_bounds.max.z},
  };
  const quader::QVec3 first =
      quader::transform_point(world_from_local, corners.front());
  quader::SceneBounds result{.min = first, .max = first};
  for (std::size_t index = 1; index < corners.size(); ++index) {
    quader::include_point(
        result, quader::transform_point(world_from_local, corners[index]));
  }
  return result;
}

quader::QuaderEntityRef create_standalone_quad_mesh(
    SnapToFloorHarness &harness, std::string_view name,
    quader::QuaderTransform transform = {}) {
  quader::QuaderMeshAsset mesh;
  mesh.name = std::string(name) + " Mesh";
  mesh.vertices = {
      {.position = {-4.0f, 0.0f, -4.0f}, .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {4.0f, 0.0f, -4.0f}, .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {4.0f, 0.0f, 4.0f}, .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {-4.0f, 0.0f, 4.0f}, .normal = {0.0f, 1.0f, 0.0f}},
  };
  mesh.indices = {0u, 2u, 1u, 0u, 3u, 2u};
  const quader::QuaderApiResult<quader::QuaderAssetRef> asset =
      harness.editor.assets().add_standalone_mesh(std::move(mesh));
  EXPECT_TRUE(asset.ok()) << asset.error_message();

  quader::QuaderCreateEntityArgs args;
  args.name = std::string(name);
  args.kind = quader::QuaderEntityKind::Mesh;
  args.transform = transform;
  const quader::QuaderApiResult<quader::QuaderEntityRef> entity =
      harness.editor.scene().create_entity(args);
  EXPECT_TRUE(entity.ok()) << entity.error_message();
  if (entity.ok() && asset.ok()) {
    EXPECT_TRUE(harness.session.scene().set_mesh(
        entity.value().id, quader::MeshComponent{.mesh = asset.value().id}));
  }
  return entity.value();
}

void expect_vec3_near(quader::QVec3 actual, quader::QVec3 expected,
                      float epsilon = kEpsilon) {
  EXPECT_NEAR(actual.x, expected.x, epsilon);
  EXPECT_NEAR(actual.y, expected.y, epsilon);
  EXPECT_NEAR(actual.z, expected.z, epsilon);
}

void expect_transform_near(const quader::TransformComponent &actual,
                           const quader::TransformComponent &expected,
                           float epsilon = kEpsilon) {
  expect_vec3_near(actual.position, expected.position, epsilon);
  expect_vec3_near(actual.rotation_euler, expected.rotation_euler, epsilon);
  expect_vec3_near(actual.scale, expected.scale, epsilon);
  EXPECT_EQ(actual.custom_pivot, expected.custom_pivot);
}

void expect_history_equal(const quader::QuaderHistorySnapshot &actual,
                          const quader::QuaderHistorySnapshot &expected) {
  EXPECT_EQ(actual.can_undo, expected.can_undo);
  EXPECT_EQ(actual.can_redo, expected.can_redo);
  EXPECT_EQ(actual.undo_label, expected.undo_label);
  EXPECT_EQ(actual.redo_label, expected.redo_label);
}

void expect_revisions_equal(const quader::QuaderRevisionStamp &actual,
                            const quader::QuaderRevisionStamp &expected) {
  EXPECT_EQ(actual.project_revision, expected.project_revision);
  EXPECT_EQ(actual.scene_revision, expected.scene_revision);
  EXPECT_EQ(actual.scene_layer_revision, expected.scene_layer_revision);
  EXPECT_EQ(actual.asset_revision, expected.asset_revision);
  EXPECT_EQ(actual.modeling_content_revision,
            expected.modeling_content_revision);
  EXPECT_EQ(actual.modeling_selection_revision,
            expected.modeling_selection_revision);
}

MeshGeometryState mesh_geometry(const SnapToFloorHarness &harness,
                                quader::QuaderModelingObjectRef object) {
  MeshGeometryState result;
  const std::optional<quader::QuaderMeshAsset> mesh =
      harness.editor.query().compiled_object_mesh(object);
  EXPECT_TRUE(mesh.has_value());
  if (!mesh.has_value()) {
    return result;
  }
  result.positions.reserve(mesh->vertices.size());
  for (const quader::QuaderMeshVertex &vertex : mesh->vertices) {
    result.positions.push_back(vertex.position);
  }
  result.indices = mesh->indices;
  result.primitives = mesh->primitives;
  return result;
}

void expect_mesh_geometry_near(const MeshGeometryState &actual,
                               const MeshGeometryState &expected,
                               float epsilon = kEpsilon) {
  ASSERT_EQ(actual.positions.size(), expected.positions.size());
  for (std::size_t index = 0; index < actual.positions.size(); ++index) {
    expect_vec3_near(actual.positions[index], expected.positions[index],
                     epsilon);
  }
  EXPECT_EQ(actual.indices, expected.indices);
  ASSERT_EQ(actual.primitives.size(), expected.primitives.size());
  for (std::size_t index = 0; index < actual.primitives.size(); ++index) {
    EXPECT_EQ(actual.primitives[index].index_offset,
              expected.primitives[index].index_offset);
    EXPECT_EQ(actual.primitives[index].index_count,
              expected.primitives[index].index_count);
    EXPECT_EQ(actual.primitives[index].material_slot,
              expected.primitives[index].material_slot);
  }
}

std::vector<quader::QVec3>
world_mesh_positions(const SnapToFloorHarness &harness,
                     quader::QuaderModelingObjectRef object) {
  std::vector<quader::QVec3> result;
  const std::optional<quader::QuaderMeshAsset> mesh =
      harness.editor.query().compiled_object_mesh(object);
  EXPECT_TRUE(mesh.has_value());
  if (!mesh.has_value()) {
    return result;
  }
  const quader::QMat4 world_from_object =
      harness.session.scene().world_transform_matrix(object.entity);
  result.reserve(mesh->vertices.size());
  for (const quader::QuaderMeshVertex &vertex : mesh->vertices) {
    result.push_back(
        quader::transform_point(world_from_object, vertex.position));
  }
  return result;
}

bool has_position_near(const std::vector<quader::QVec3> &positions,
                       quader::QVec3 expected, float epsilon = kEpsilon) {
  return std::ranges::any_of(positions, [expected, epsilon](quader::QVec3 value) {
    return quader::length_squared(value - expected) <= epsilon * epsilon;
  });
}

const quader::QuaderActionItem *
find_action(const quader::QuaderActionSet &actions,
            std::string_view panel_id, std::string_view action_id) {
  const auto panel = std::ranges::find_if(
      actions.panels, [panel_id](const quader::QuaderActionPanel &candidate) {
        return candidate.panel_id == panel_id;
      });
  if (panel == actions.panels.end()) {
    return nullptr;
  }
  const auto action = std::ranges::find_if(
      panel->actions, [action_id](const quader::QuaderActionItem &candidate) {
        return candidate.action_id == action_id;
      });
  return action == panel->actions.end() ? nullptr : &*action;
}

std::optional<float>
first_downward_hit_y(const SnapToFloorHarness &harness,
                     quader::QuaderModelingObjectRef receiver,
                     quader::QVec3 ray_origin) {
  const std::optional<quader::QuaderMeshAsset> mesh =
      harness.editor.query().compiled_object_mesh(receiver);
  EXPECT_TRUE(mesh.has_value());
  if (!mesh.has_value()) {
    return std::nullopt;
  }
  const quader::QMat4 world_from_object =
      harness.session.scene().world_transform_matrix(receiver.entity);
  const quader_geometry::QRay3<float> ray{
      {ray_origin.x, ray_origin.y, ray_origin.z}, {0.0f, -1.0f, 0.0f}};
  float nearest_distance = std::numeric_limits<float>::infinity();
  std::optional<float> hit_y;
  for (std::size_t index = 0; index + 2 < mesh->indices.size(); index += 3) {
    const auto point = [&](std::uint32_t vertex_index) {
      const quader::QVec3 world = quader::transform_point(
          world_from_object, mesh->vertices[vertex_index].position);
      return quader_geometry::QVec3f{world.x, world.y, world.z};
    };
    const quader_geometry::QTriangle3<float> triangle{
        point(mesh->indices[index]), point(mesh->indices[index + 1]),
        point(mesh->indices[index + 2])};
    const quader_geometry::QRayTriangleHit<float> hit =
        quader_geometry::intersect_ray_triangle(ray, triangle);
    if (hit.hit && hit.distance < nearest_distance) {
      nearest_distance = hit.distance;
      hit_y = hit.point.y;
    }
  }
  return hit_y;
}

void select_quad_vertices(SnapToFloorHarness &harness,
                          quader::QuaderModelingObjectRef object,
                          const std::array<quader::QuaderElementId, 4> &vertices,
                          quader::QuaderSelectionEdit edit) {
  quader::QuaderComponentSelectionArgs args;
  args.object = object;
  args.kind = quader::QuaderComponentKind::Vertex;
  args.vertices.assign(vertices.begin(), vertices.end());
  args.active = {
      .object = object,
      .kind = quader::QuaderComponentKind::Vertex,
      .vertex = vertices.back(),
  };
  args.edit = edit;
  const quader::QuaderApiVoidResult selected =
      harness.editor.selection().select_components(args);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
}

TEST(SnapToFloorTests,
     ObjectUsesAabbBottomCenterAndNearestReceiverThenUndoRedoExactly) {
  SnapToFloorHarness harness;
  const quader::QuaderModelingObjectRef source = create_box(
      harness, "Asymmetric Source",
      {.min = {2.0f, -2.0f, 4.0f}, .max = {4.0f, 1.0f, 6.0f}},
      {.position = {0.0f, 10.0f, 0.0f}});
  static_cast<void>(create_box(
      harness, "Lower Receiver",
      {.min = {2.25f, -0.25f, 4.25f}, .max = {3.75f, 0.25f, 5.75f}},
      {.position = {0.0f, 1.0f, 0.0f}}));
  const quader::QuaderModelingObjectRef nearest = create_box(
      harness, "Nearest Receiver",
      {.min = {2.25f, -0.25f, 4.25f}, .max = {3.75f, 0.25f, 5.75f}},
      {.position = {0.0f, 4.0f, 0.0f}});
  // A higher decoy below the entity pivot distinguishes the AABB-bottom-center
  // ray from a ray cast through the object's transform origin.
  static_cast<void>(create_box(
      harness, "Pivot Decoy",
      {.min = {-0.5f, -0.25f, -0.5f}, .max = {0.5f, 0.25f, 0.5f}},
      {.position = {0.0f, 6.0f, 0.0f}}));
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  ASSERT_TRUE(harness.editor.modeling().can_snap_selected_meshes_to_floor());

  const quader::TransformComponent before =
      *harness.session.scene().transform(source.entity);
  const quader::SceneBounds before_bounds = bounds(harness, source.entity);
  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());

  const float receiver_top = bounds(harness, nearest.entity).max.y;
  const quader::SceneBounds after_bounds = bounds(harness, source.entity);
  EXPECT_NEAR(after_bounds.min.y, receiver_top, kEpsilon);
  EXPECT_NEAR(quader::bounds_center(after_bounds).x,
              quader::bounds_center(before_bounds).x, kEpsilon);
  EXPECT_NEAR(quader::bounds_center(after_bounds).z,
              quader::bounds_center(before_bounds).z, kEpsilon);
  expect_vec3_near(after_bounds.max - after_bounds.min,
                   before_bounds.max - before_bounds.min);
  EXPECT_EQ(harness.editor.query().history().undo_label,
            "Snap Meshes to Floor");
  const quader::TransformComponent after =
      *harness.session.scene().transform(source.entity);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_transform_near(*harness.session.scene().transform(source.entity),
                        before);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_transform_near(*harness.session.scene().transform(source.entity),
                        after);
  EXPECT_NEAR(bounds(harness, source.entity).min.y, receiver_top, kEpsilon);
}

TEST(SnapToFloorTests,
     ObjectFallbackMovesAboveAndBelowFloorAndExcludesEverySelectedSource) {
  SnapToFloorHarness harness;
  const quader::QuaderBounds box_bounds{
      .min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
  const quader::QuaderModelingObjectRef lower =
      create_box(harness, "Lower Source", box_bounds,
                 {.position = {0.0f, 3.0f, 0.0f}});
  const quader::QuaderModelingObjectRef upper =
      create_box(harness, "Upper Source", box_bounds,
                 {.position = {0.0f, 7.0f, 0.0f}});
  const quader::QuaderModelingObjectRef below =
      create_box(harness, "Below Floor Source", box_bounds,
                 {.position = {0.0f, -4.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({lower.entity, upper.entity, below.entity},
                                  lower.entity)
                  .ok());

  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());
  EXPECT_NEAR(bounds(harness, lower.entity).min.y, 0.0f, kEpsilon);
  EXPECT_NEAR(bounds(harness, upper.entity).min.y, 0.0f, kEpsilon);
  EXPECT_NEAR(bounds(harness, below.entity).min.y, 0.0f, kEpsilon);
  EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 3u);
}

TEST(SnapToFloorTests, ObjectIncludesLockedReceiversAndIgnoresHiddenOnes) {
  SnapToFloorHarness harness;
  const quader::QuaderModelingObjectRef source = create_box(
      harness, "Source", {.min = {-1.0f, -1.0f, -1.0f},
                          .max = {1.0f, 1.0f, 1.0f}},
      {.position = {0.0f, 10.0f, 0.0f}});
  const quader::QuaderModelingObjectRef hidden = create_box(
      harness, "Hidden Receiver", {.min = {-3.0f, -0.5f, -3.0f},
                                    .max = {3.0f, 0.5f, 3.0f}},
      {.position = {0.0f, 6.0f, 0.0f}});
  const quader::QuaderModelingObjectRef locked = create_box(
      harness, "Locked Receiver", {.min = {-3.0f, -0.5f, -3.0f},
                                    .max = {3.0f, 0.5f, 3.0f}},
      {.position = {0.0f, 3.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.scene().set_visible(hidden.entity, false).ok());
  ASSERT_TRUE(harness.editor.scene().set_locked(locked.entity, true).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());

  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());
  EXPECT_NEAR(bounds(harness, source.entity).min.y,
              bounds(harness, locked.entity).max.y, kEpsilon);
  EXPECT_GT(6.5f, bounds(harness, source.entity).min.y + 1.0f);
}

TEST(SnapToFloorTests,
     ObjectUnderAffineParentPreservesWorldXZAndLocalOrientationScale) {
  SnapToFloorHarness harness;
  quader::QuaderCreateEntityArgs parent_args;
  parent_args.name = "Affine Parent";
  parent_args.kind = quader::QuaderEntityKind::Empty;
  parent_args.transform = {
      .position = {3.0f, -2.0f, 4.0f},
      .rotation_euler = {0.18f, -0.37f, 0.24f},
      .scale = {1.6f, 0.75f, 1.3f},
  };
  const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
      harness.editor.scene().create_entity(parent_args);
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  const quader::QuaderModelingObjectRef source = create_box(
      harness, "Parented Source",
      {.min = {-1.5f, -1.0f, -0.75f}, .max = {1.5f, 1.0f, 0.75f}},
      {.position = {5.0f, 9.0f, -3.0f},
       .rotation_euler = {-0.16f, 0.29f, 0.21f},
       .scale = {1.25f, 0.8f, 1.1f}});
  const quader::QuaderModelingObjectRef receiver = create_box(
      harness, "Wide Receiver",
      {.min = {-30.0f, -0.5f, -30.0f}, .max = {30.0f, 0.5f, 30.0f}},
      {.position = {0.0f, 2.0f, 0.0f}});
  ASSERT_TRUE(
      harness.editor.scene().reparent(source.entity, parent.value().id, true).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());

  const quader::TransformComponent local_before =
      *harness.session.scene().transform(source.entity);
  const quader::TransformComponent parent_before =
      *harness.session.scene().transform(parent.value().id);
  const quader::SceneBounds world_before = bounds(harness, source.entity);
  const quader::QVec3 origin_before = quader::transform_point(
      harness.session.scene().world_transform_matrix(source.entity), {});

  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());
  const quader::TransformComponent local_after =
      *harness.session.scene().transform(source.entity);
  expect_vec3_near(local_after.rotation_euler, local_before.rotation_euler);
  expect_vec3_near(local_after.scale, local_before.scale);
  const quader::QVec3 origin_after = quader::transform_point(
      harness.session.scene().world_transform_matrix(source.entity), {});
  EXPECT_NEAR(origin_after.x, origin_before.x, kEpsilon);
  EXPECT_NEAR(origin_after.z, origin_before.z, kEpsilon);
  const quader::SceneBounds world_after = bounds(harness, source.entity);
  EXPECT_NEAR(world_after.min.y, bounds(harness, receiver.entity).max.y,
              kEpsilon);
  EXPECT_NEAR(quader::bounds_center(world_after).x,
              quader::bounds_center(world_before).x, kEpsilon);
  EXPECT_NEAR(quader::bounds_center(world_after).z,
              quader::bounds_center(world_before).z, kEpsilon);
  expect_vec3_near(world_after.max - world_after.min,
                   world_before.max - world_before.min);
  expect_transform_near(*harness.session.scene().transform(parent.value().id),
                        parent_before);
}

TEST(SnapToFloorTests, ObjectExactNoOpDoesNotPublishOrAddHistory) {
  SnapToFloorHarness harness;
  const quader::QuaderModelingObjectRef source = create_box(
      harness, "Already Floored", {.min = {-1.0f, -1.0f, -1.0f},
                                    .max = {1.0f, 1.0f, 1.0f}},
      {.position = {2.0f, 1.0f, -4.0f}});
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  const quader::TransformComponent transform_before =
      *harness.session.scene().transform(source.entity);
  const quader::QuaderHistorySnapshot history_before =
      harness.editor.query().history();
  const quader::QuaderRevisionStamp revisions_before =
      harness.editor.query().revisions();

  ASSERT_TRUE(harness.editor.modeling().can_snap_selected_meshes_to_floor());
  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  EXPECT_FALSE(snapped.changed());
  expect_transform_near(*harness.session.scene().transform(source.entity),
                        transform_before);
  expect_history_equal(harness.editor.query().history(), history_before);
  expect_revisions_equal(harness.editor.query().revisions(), revisions_before);
}

TEST(SnapToFloorTests, ObjectReceiverBelowWorldFloorStillTakesPrecedence) {
  SnapToFloorHarness harness;
  const quader::QuaderModelingObjectRef source = create_box(
      harness, "Source Above Floor",
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      {.position = {0.0f, 4.0f, 0.0f}});
  const quader::QuaderModelingObjectRef receiver = create_box(
      harness, "Receiver Below Floor",
      {.min = {-3.0f, -0.5f, -3.0f}, .max = {3.0f, 0.5f, 3.0f}},
      {.position = {0.0f, -4.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());

  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());
  const float receiver_top = bounds(harness, receiver.entity).max.y;
  ASSERT_LT(receiver_top, 0.0f);
  EXPECT_NEAR(bounds(harness, source.entity).min.y, receiver_top, kEpsilon);
}

TEST(SnapToFloorTests,
     FormalOccurrenceCanReceiveAndSelectedOccurrenceMovesWithoutDefinition) {
  SnapToFloorHarness harness;
  constexpr quader::QuaderBounds kReceiverBounds{
      .min = {-3.0f, -0.5f, -3.0f}, .max = {3.0f, 0.5f, 3.0f}};
  const quader::QuaderModelingObjectRef receiver_member = create_box(
      harness, "Occurrence Receiver Member", kReceiverBounds,
      {.position = {0.0f, 3.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.selection().select_object(receiver_member).ok());
  const quader::QuaderApiResult<quader::QuaderInstanceCreateResult>
      receiver_instance =
          harness.editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(receiver_instance.ok()) << receiver_instance.error_message();
  const quader::EntityId receiver_occurrence =
      receiver_instance.value().occurrence.root.id;
  const quader::EntityId receiver_definition =
      receiver_instance.value().definition.root.id;
  const float occurrence_receiver_top = transformed_bounds(
      kReceiverBounds,
      effective_instance_member_world(harness, receiver_occurrence,
                                      receiver_definition,
                                      receiver_member.entity))
                                            .max.y;

  const quader::QuaderModelingObjectRef direct_source = create_box(
      harness, "Direct Source",
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      {.position = {0.0f, 9.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.selection().select_object(direct_source).ok());
  ASSERT_TRUE(
      harness.editor.modeling().snap_selected_meshes_to_floor().ok());
  EXPECT_NEAR(bounds(harness, direct_source.entity).min.y,
              occurrence_receiver_top, kEpsilon);

  constexpr quader::QuaderBounds kSourceBounds{
      .min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
  const quader::QuaderModelingObjectRef source_member = create_box(
      harness, "Occurrence Source Member", kSourceBounds,
      {.position = {10.0f, 8.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.selection().select_object(source_member).ok());
  const quader::QuaderApiResult<quader::QuaderInstanceCreateResult>
      source_instance =
          harness.editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(source_instance.ok()) << source_instance.error_message();
  const quader::EntityId source_occurrence =
      source_instance.value().occurrence.root.id;
  const quader::EntityId source_definition =
      source_instance.value().definition.root.id;
  const quader::QuaderApiResult<quader::QuaderEntityRef> sibling =
      harness.editor.scene().duplicate(source_occurrence);
  ASSERT_TRUE(sibling.ok()) << sibling.error_message();
  quader::TransformComponent sibling_transform =
      *harness.session.scene().transform(sibling.value().id);
  sibling_transform.position.x += 20.0f;
  ASSERT_TRUE(harness.editor.scene()
                  .set_transform(sibling.value().id,
                                 {.position = sibling_transform.position,
                                  .rotation_euler =
                                      sibling_transform.rotation_euler,
                                  .scale = sibling_transform.scale})
                  .ok());
  const quader::QuaderModelingObjectRef direct_receiver = create_box(
      harness, "Direct Receiver For Occurrence",
      {.min = {7.0f, -0.5f, -3.0f}, .max = {13.0f, 0.5f, 3.0f}},
      {.position = {0.0f, 1.0f, 0.0f}});
  ASSERT_TRUE(
      harness.editor.selection().select_object(source_occurrence).ok());
  const quader::TransformComponent member_before =
      *harness.session.scene().transform(source_member.entity);
  const quader::TransformComponent sibling_before =
      *harness.session.scene().transform(sibling.value().id);
  const quader::SceneBounds sibling_bounds_before = transformed_bounds(
      kSourceBounds,
      effective_instance_member_world(harness, sibling.value().id,
                                      source_definition,
                                      source_member.entity));

  const quader::QuaderApiVoidResult snapped_occurrence =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  ASSERT_TRUE(snapped_occurrence.ok())
      << snapped_occurrence.error_message();
  ASSERT_TRUE(snapped_occurrence.changed());
  const quader::SceneBounds source_after = transformed_bounds(
      kSourceBounds,
      effective_instance_member_world(harness, source_occurrence,
                                      source_definition,
                                      source_member.entity));
  EXPECT_NEAR(source_after.min.y,
              bounds(harness, direct_receiver.entity).max.y, kEpsilon);
  expect_transform_near(
      *harness.session.scene().transform(source_member.entity), member_before);
  expect_transform_near(*harness.session.scene().transform(sibling.value().id),
                        sibling_before);
  const quader::SceneBounds sibling_bounds_after = transformed_bounds(
      kSourceBounds,
      effective_instance_member_world(harness, sibling.value().id,
                                      source_definition,
                                      source_member.entity));
  expect_vec3_near(sibling_bounds_after.min, sibling_bounds_before.min);
  expect_vec3_near(sibling_bounds_after.max, sibling_bounds_before.max);
}

TEST(SnapToFloorTests,
     NestedSelectionPromotesRootAndIncludesNestedOccurrenceInFrozenSource) {
  SnapToFloorHarness harness;
  constexpr quader::QuaderBounds kUnitBounds{
      .min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
  const quader::QuaderModelingObjectRef parent =
      create_box(harness, "Selected Root", kUnitBounds,
                 {.position = {0.0f, 6.0f, 0.0f}});
  const quader::QuaderModelingObjectRef selected_child =
      create_box(harness, "Selected Child", kUnitBounds,
                 {.position = {0.0f, 9.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(selected_child.entity, parent.entity, true)
                  .ok());

  const quader::QuaderModelingObjectRef nested_member =
      create_box(harness, "Nested Occurrence Member", kUnitBounds,
                 {.position = {0.0f, 4.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.selection().select_object(nested_member).ok());
  const quader::QuaderApiResult<quader::QuaderInstanceCreateResult> instance =
      harness.editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const quader::EntityId occurrence = instance.value().occurrence.root.id;
  const quader::EntityId definition = instance.value().definition.root.id;
  ASSERT_TRUE(
      harness.editor.scene().reparent(occurrence, parent.entity, true).ok());
  const quader::QuaderModelingObjectRef independent =
      create_box(harness, "Independent Selected Source", kUnitBounds,
                 {.position = {0.0f, 12.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects(
                      {parent.entity, selected_child.entity,
                       independent.entity},
                      parent.entity)
                  .ok());
  const quader::TransformComponent child_local_before =
      *harness.session.scene().transform(selected_child.entity);
  const quader::SceneBounds occurrence_before = transformed_bounds(
      kUnitBounds,
      effective_instance_member_world(harness, occurrence, definition,
                                      nested_member.entity));
  ASSERT_NEAR(occurrence_before.min.y, 3.0f, kEpsilon);
  ASSERT_TRUE(harness.editor.modeling().can_snap_selected_meshes_to_floor());

  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());
  const quader::SceneBounds occurrence_after = transformed_bounds(
      kUnitBounds,
      effective_instance_member_world(harness, occurrence, definition,
                                      nested_member.entity));
  EXPECT_NEAR(occurrence_after.min.y, 0.0f, kEpsilon);
  EXPECT_NEAR(bounds(harness, parent.entity).min.y, 2.0f, kEpsilon);
  EXPECT_NEAR(bounds(harness, selected_child.entity).min.y, 5.0f,
              kEpsilon);
  EXPECT_NEAR(bounds(harness, independent.entity).min.y, 0.0f, kEpsilon);
  expect_transform_near(
      *harness.session.scene().transform(selected_child.entity),
      child_local_before);
  EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 3u);
}

TEST(SnapToFloorTests, ImportedStandaloneMeshIsAReceiver) {
  SnapToFloorHarness harness;
  const quader::QuaderEntityRef receiver = create_standalone_quad_mesh(
      harness, "Imported Receiver", {.position = {0.0f, 2.25f, 0.0f}});
  EXPECT_FALSE(harness.editor.query()
                   .modeling_object_for_entity(receiver.id)
                   .has_value());
  const quader::QuaderModelingObjectRef source = create_box(
      harness, "Modeled Source",
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      {.position = {0.0f, 8.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());

  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());
  EXPECT_NEAR(bounds(harness, source.entity).min.y, 2.25f, kEpsilon);
}

TEST(SnapToFloorTests,
     ImportedStandaloneMeshCanBeSelectedSourceWithUndoRedo) {
  SnapToFloorHarness harness;
  const quader::QuaderEntityRef source = create_standalone_quad_mesh(
      harness, "Imported Source", {.position = {3.0f, 6.0f, -2.0f}});
  ASSERT_FALSE(harness.editor.query()
                   .modeling_object_for_entity(source.id)
                   .has_value());
  ASSERT_TRUE(harness.editor.selection().select_object(source.id).ok());
  ASSERT_TRUE(harness.editor.modeling().can_snap_selected_meshes_to_floor());
  const quader::TransformComponent before =
      *harness.session.scene().transform(source.id);

  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());
  EXPECT_NEAR(bounds(harness, source.id).min.y, 0.0f, kEpsilon);
  EXPECT_EQ(harness.editor.query().history().undo_label,
            "Snap Meshes to Floor");
  const quader::TransformComponent after =
      *harness.session.scene().transform(source.id);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_transform_near(*harness.session.scene().transform(source.id), before);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_transform_near(*harness.session.scene().transform(source.id), after);
  EXPECT_NEAR(bounds(harness, source.id).min.y, 0.0f, kEpsilon);
}

TEST(SnapToFloorTests,
     SingularParentRejectsAllSelectedSourcesWithoutPartialHistory) {
  SnapToFloorHarness harness;
  const quader::QuaderModelingObjectRef valid_source = create_box(
      harness, "Valid Source",
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      {.position = {-5.0f, 6.0f, 0.0f}});
  quader::QuaderCreateEntityArgs parent_args;
  parent_args.name = "Singular Parent";
  parent_args.kind = quader::QuaderEntityKind::Empty;
  parent_args.transform = {
      .position = {5.0f, 0.0f, 0.0f},
      .scale = {0.0f, 1.0f, 1.0f},
  };
  const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
      harness.editor.scene().create_entity(parent_args);
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  const quader::QuaderModelingObjectRef singular_source = create_box(
      harness, "Singular Source",
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      {.position = {0.0f, 7.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(singular_source.entity, parent.value().id, false)
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects(
                      {valid_source.entity, singular_source.entity},
                      valid_source.entity)
                  .ok());
  ASSERT_TRUE(harness.editor.modeling().can_snap_selected_meshes_to_floor());
  const quader::TransformComponent valid_before =
      *harness.session.scene().transform(valid_source.entity);
  const quader::TransformComponent singular_before =
      *harness.session.scene().transform(singular_source.entity);
  const quader::QuaderHistorySnapshot history_before =
      harness.editor.query().history();
  const quader::QuaderRevisionStamp revisions_before =
      harness.editor.query().revisions();

  const quader::QuaderApiVoidResult rejected =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  expect_transform_near(
      *harness.session.scene().transform(valid_source.entity), valid_before);
  expect_transform_near(
      *harness.session.scene().transform(singular_source.entity),
      singular_before);
  expect_history_equal(harness.editor.query().history(), history_before);
  expect_revisions_equal(harness.editor.query().revisions(), revisions_before);
}

TEST(SnapToFloorTests, LocalVisibilityExcludesOtherwiseNearestReceiver) {
  SnapToFloorHarness harness;
  const quader::QuaderModelingObjectRef source = create_box(
      harness, "Local Source",
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      {.position = {0.0f, 10.0f, 0.0f}});
  const quader::QuaderModelingObjectRef included = create_box(
      harness, "Included Receiver",
      {.min = {-3.0f, -0.5f, -3.0f}, .max = {3.0f, 0.5f, 3.0f}},
      {.position = {0.0f, 3.0f, 0.0f}});
  static_cast<void>(create_box(
      harness, "Locally Excluded Receiver",
      {.min = {-3.0f, -0.5f, -3.0f}, .max = {3.0f, 0.5f, 3.0f}},
      {.position = {0.0f, 6.0f, 0.0f}}));
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, included.entity},
                                  source.entity)
                  .ok());
  ASSERT_TRUE(harness.editor.viewports().can_enter_local_visibility());
  ASSERT_TRUE(harness.editor.viewports().toggle_local_visibility().ok());
  ASSERT_TRUE(harness.editor.viewports().local_visibility_active());
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());

  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_meshes_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());
  EXPECT_NEAR(bounds(harness, source.entity).min.y,
              bounds(harness, included.entity).max.y, kEpsilon);
}

TEST(SnapToFloorTests,
     VerticesConformToCurvedReceiverAndUndoRedoPreserveTopologyAndSelection) {
  SnapToFloorHarness harness;
  const quader::QuaderModelingObjectRef sphere = create_mesh(
      harness, "Curved Receiver", make_sphere_document(4.0f));
  const std::array<quader::QVec3, 4> source_positions{
      quader::QVec3{-0.6f, 5.0f, -0.2f},
      quader::QVec3{0.8f, 5.0f, -0.2f},
      quader::QVec3{0.8f, 5.0f, 0.4f},
      quader::QVec3{-0.6f, 5.0f, 0.4f},
  };
  QuadDocument source_document = make_quad_document(source_positions);
  const std::array<quader::QuaderElementId, 4> source_vertices =
      source_document.vertices;
  const quader::QuaderModelingObjectRef source =
      create_mesh(harness, "Conforming Vertices",
                  std::move(source_document.document));
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  select_quad_vertices(harness, source, source_vertices,
                       quader::QuaderSelectionEdit::Replace);
  ASSERT_TRUE(harness.editor.modeling().can_snap_selected_vertices_to_floor());

  std::array<quader::QVec3, 4> expected_positions = source_positions;
  float minimum_hit_y = std::numeric_limits<float>::infinity();
  float maximum_hit_y = -std::numeric_limits<float>::infinity();
  for (std::size_t index = 0; index < source_positions.size(); ++index) {
    const std::optional<float> hit_y =
        first_downward_hit_y(harness, sphere, source_positions[index]);
    ASSERT_TRUE(hit_y.has_value());
    expected_positions[index].y = *hit_y;
    minimum_hit_y = std::min(minimum_hit_y, *hit_y);
    maximum_hit_y = std::max(maximum_hit_y, *hit_y);
  }
  ASSERT_GT(maximum_hit_y - minimum_hit_y, 0.05f);

  const MeshGeometryState source_before = mesh_geometry(harness, source);
  const MeshGeometryState sphere_before = mesh_geometry(harness, sphere);
  const quader::QuaderSelectionSummary selection_before =
      harness.editor.selection().summary();
  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_vertices_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());

  const std::vector<quader::QVec3> source_world =
      world_mesh_positions(harness, source);
  for (quader::QVec3 expected : expected_positions) {
    EXPECT_TRUE(has_position_near(source_world, expected));
  }
  const MeshGeometryState source_after = mesh_geometry(harness, source);
  EXPECT_EQ(source_after.positions.size(), source_before.positions.size());
  EXPECT_EQ(source_after.indices, source_before.indices);
  EXPECT_EQ(source_after.primitives.size(), source_before.primitives.size());
  expect_mesh_geometry_near(mesh_geometry(harness, sphere), sphere_before);
  const quader::QuaderSelectionSummary selection_after =
      harness.editor.selection().summary();
  EXPECT_EQ(selection_after.mode, quader::QuaderSelectionMode::Vertex);
  EXPECT_EQ(selection_after.selected_vertex_count,
            selection_before.selected_vertex_count);
  EXPECT_EQ(selection_after.active_kind, selection_before.active_kind);
  EXPECT_EQ(selection_after.active_object_id, selection_before.active_object_id);
  EXPECT_EQ(selection_after.active_vertex_id, selection_before.active_vertex_id);
  EXPECT_EQ(harness.editor.query().history().undo_label,
            "Snap Vertices to Floor");

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_mesh_geometry_near(mesh_geometry(harness, source), source_before);
  EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count,
            selection_before.selected_vertex_count);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_mesh_geometry_near(mesh_geometry(harness, source), source_after);
  EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count,
            selection_before.selected_vertex_count);
}

TEST(SnapToFloorTests,
     VerticesMixReceiverAndFloorAcrossObjectsWhileExcludingAllSources) {
  SnapToFloorHarness harness;
  const quader::QuaderModelingObjectRef receiver = create_box(
      harness, "Partial Receiver",
      {.min = {-2.7f, -0.5f, -1.5f}, .max = {-1.3f, 0.5f, 1.5f}},
      {.position = {0.0f, 0.5f, 0.0f}});
  const std::array<quader::QVec3, 4> upper_positions{
      quader::QVec3{-2.2f, 5.0f, -0.5f},
      quader::QVec3{2.2f, 5.0f, -0.5f},
      quader::QVec3{2.2f, 5.0f, 0.5f},
      quader::QVec3{-2.2f, 5.0f, 0.5f},
  };
  const std::array<quader::QVec3, 4> lower_positions{
      quader::QVec3{-2.2f, 3.0f, -0.5f},
      quader::QVec3{2.2f, 3.0f, -0.5f},
      quader::QVec3{2.2f, 3.0f, 0.5f},
      quader::QVec3{-2.2f, 3.0f, 0.5f},
  };
  QuadDocument upper_document = make_quad_document(upper_positions);
  QuadDocument lower_document = make_quad_document(lower_positions);
  const auto upper_vertices = upper_document.vertices;
  const auto lower_vertices = lower_document.vertices;
  const quader::QuaderModelingObjectRef upper =
      create_mesh(harness, "Upper Source", std::move(upper_document.document));
  const quader::QuaderModelingObjectRef lower =
      create_mesh(harness, "Lower Source", std::move(lower_document.document));
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({upper.entity, lower.entity}, upper.entity)
                  .ok());
  ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  select_quad_vertices(harness, upper, upper_vertices,
                       quader::QuaderSelectionEdit::Replace);
  select_quad_vertices(harness, lower, lower_vertices,
                       quader::QuaderSelectionEdit::Add);
  ASSERT_EQ(harness.editor.selection().summary().selected_vertex_count, 8u);

  const MeshGeometryState receiver_before = mesh_geometry(harness, receiver);
  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_vertices_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());
  const float receiver_top = bounds(harness, receiver.entity).max.y;
  for (const quader::QuaderModelingObjectRef source : {upper, lower}) {
    const std::vector<quader::QVec3> world =
        world_mesh_positions(harness, source);
    EXPECT_TRUE(has_position_near(world, {-2.2f, receiver_top, -0.5f}));
    EXPECT_TRUE(has_position_near(world, {-2.2f, receiver_top, 0.5f}));
    EXPECT_TRUE(has_position_near(world, {2.2f, 0.0f, -0.5f}));
    EXPECT_TRUE(has_position_near(world, {2.2f, 0.0f, 0.5f}));
    EXPECT_FALSE(has_position_near(world, {2.2f, 3.0f, -0.5f}));
  }
  expect_mesh_geometry_near(mesh_geometry(harness, receiver), receiver_before);
  EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count, 8u);
}

TEST(SnapToFloorTests,
     VerticesProjectInWorldSpaceThroughAffineNonuniformObjectTransform) {
  SnapToFloorHarness harness;
  const quader::QuaderModelingObjectRef receiver = create_box(
      harness, "Affine Vertex Receiver",
      {.min = {-40.0f, -0.5f, -40.0f}, .max = {40.0f, 0.5f, 40.0f}},
      {.position = {0.0f, 1.5f, 0.0f}});
  QuadDocument source_document = make_quad_document({
      quader::QVec3{-1.0f, 2.0f, -1.0f},
      quader::QVec3{1.0f, 2.0f, -1.0f},
      quader::QVec3{1.0f, 2.0f, 1.0f},
      quader::QVec3{-1.0f, 2.0f, 1.0f},
  });
  const auto source_vertices = source_document.vertices;
  const quader::QuaderTransform source_transform{
      .position = {3.0f, 10.0f, -2.0f},
      .rotation_euler = {0.22f, -0.31f, 0.27f},
      .scale = {2.0f, 0.7f, 1.4f},
  };
  const quader::QuaderModelingObjectRef source = create_mesh(
      harness, "Affine Vertex Source", std::move(source_document.document),
      source_transform);
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  select_quad_vertices(harness, source, source_vertices,
                       quader::QuaderSelectionEdit::Replace);
  const quader::TransformComponent transform_before =
      *harness.session.scene().transform(source.entity);
  const std::vector<quader::QVec3> world_before =
      world_mesh_positions(harness, source);
  ASSERT_FALSE(world_before.empty());

  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_vertices_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());
  const std::vector<quader::QVec3> world_after =
      world_mesh_positions(harness, source);
  ASSERT_EQ(world_after.size(), world_before.size());
  const float receiver_top = bounds(harness, receiver.entity).max.y;
  for (std::size_t index = 0; index < world_after.size(); ++index) {
    EXPECT_NEAR(world_after[index].x, world_before[index].x, kEpsilon);
    EXPECT_NEAR(world_after[index].z, world_before[index].z, kEpsilon);
    EXPECT_NEAR(world_after[index].y, receiver_top, kEpsilon);
  }
  expect_transform_near(*harness.session.scene().transform(source.entity),
                        transform_before);
}

TEST(SnapToFloorTests, VertexRedoRestoresCapturedResultWithoutReraycasting) {
  SnapToFloorHarness harness;
  const quader::QuaderModelingObjectRef receiver = create_box(
      harness, "Movable Receiver",
      {.min = {-4.0f, -0.5f, -4.0f}, .max = {4.0f, 0.5f, 4.0f}},
      {.position = {0.0f, 2.0f, 0.0f}});
  QuadDocument source_document = make_quad_document({
      quader::QVec3{-1.0f, 6.0f, -1.0f},
      quader::QVec3{1.0f, 6.0f, -1.0f},
      quader::QVec3{1.0f, 6.0f, 1.0f},
      quader::QVec3{-1.0f, 6.0f, 1.0f},
  });
  const auto source_vertices = source_document.vertices;
  const quader::QuaderModelingObjectRef source =
      create_mesh(harness, "Redo Source", std::move(source_document.document));
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  select_quad_vertices(harness, source, source_vertices,
                       quader::QuaderSelectionEdit::Replace);

  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_vertices_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());
  const float captured_receiver_top = bounds(harness, receiver.entity).max.y;
  const MeshGeometryState snapped_geometry = mesh_geometry(harness, source);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(harness.editor.query().history().can_redo);

  // Runtime-local visibility excludes the receiver without touching document
  // history. A redo that reran raycasts would therefore fall back to world Y=0.
  ASSERT_TRUE(harness.editor.viewports().can_enter_local_visibility());
  ASSERT_TRUE(harness.editor.viewports().toggle_local_visibility().ok());
  ASSERT_TRUE(harness.editor.viewports().local_visibility_active());
  ASSERT_TRUE(harness.editor.query().history().can_redo);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_mesh_geometry_near(mesh_geometry(harness, source), snapped_geometry);
  for (const quader::QVec3 world : world_mesh_positions(harness, source)) {
    EXPECT_NEAR(world.y, captured_receiver_top, kEpsilon);
  }
}

TEST(SnapToFloorTests,
     VertexMultiObjectSingularTransformRejectsAtomically) {
  SnapToFloorHarness harness;
  QuadDocument valid_document = make_quad_document({
      quader::QVec3{-6.0f, 5.0f, -1.0f},
      quader::QVec3{-4.0f, 5.0f, -1.0f},
      quader::QVec3{-4.0f, 5.0f, 1.0f},
      quader::QVec3{-6.0f, 5.0f, 1.0f},
  });
  QuadDocument singular_document = make_quad_document({
      quader::QVec3{-1.0f, 6.0f, -1.0f},
      quader::QVec3{1.0f, 6.0f, -1.0f},
      quader::QVec3{1.0f, 6.0f, 1.0f},
      quader::QVec3{-1.0f, 6.0f, 1.0f},
  });
  const auto valid_vertices = valid_document.vertices;
  const auto singular_vertices = singular_document.vertices;
  const quader::QuaderModelingObjectRef valid = create_mesh(
      harness, "Valid Vertex Source", std::move(valid_document.document));
  const quader::QuaderModelingObjectRef singular =
      create_mesh(harness, "Singular Vertex Source",
                  std::move(singular_document.document));

  quader::QuaderCreateEntityArgs parent_args;
  parent_args.name = "Singular Vertex Parent";
  parent_args.kind = quader::QuaderEntityKind::Empty;
  parent_args.transform = {
      .position = {5.0f, 0.0f, 0.0f},
      .scale = {0.0f, 1.0f, 1.0f},
  };
  const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
      harness.editor.scene().create_entity(parent_args);
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(singular.entity, parent.value().id, false)
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({valid.entity, singular.entity}, valid.entity)
                  .ok());
  ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  select_quad_vertices(harness, valid, valid_vertices,
                       quader::QuaderSelectionEdit::Replace);
  select_quad_vertices(harness, singular, singular_vertices,
                       quader::QuaderSelectionEdit::Add);
  ASSERT_EQ(harness.editor.selection().summary().selected_vertex_count, 8u);
  ASSERT_TRUE(harness.editor.modeling().can_snap_selected_vertices_to_floor());

  const MeshGeometryState valid_before = mesh_geometry(harness, valid);
  const MeshGeometryState singular_before = mesh_geometry(harness, singular);
  const quader::QuaderHistorySnapshot history_before =
      harness.editor.query().history();
  const quader::QuaderRevisionStamp revisions_before =
      harness.editor.query().revisions();

  const quader::QuaderApiVoidResult rejected =
      harness.editor.modeling().snap_selected_vertices_to_floor();
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  expect_mesh_geometry_near(mesh_geometry(harness, valid), valid_before);
  expect_mesh_geometry_near(mesh_geometry(harness, singular), singular_before);
  EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count, 8u);
  expect_history_equal(harness.editor.query().history(), history_before);
  expect_revisions_equal(harness.editor.query().revisions(), revisions_before);
}

TEST(SnapToFloorTests, VertexExactNoOpDoesNotPublishOrAddHistory) {
  SnapToFloorHarness harness;
  QuadDocument source_document = make_quad_document({
      quader::QVec3{-1.0f, 0.0f, -1.0f},
      quader::QVec3{1.0f, 0.0f, -1.0f},
      quader::QVec3{1.0f, 0.0f, 1.0f},
      quader::QVec3{-1.0f, 0.0f, 1.0f},
  });
  const auto source_vertices = source_document.vertices;
  const quader::QuaderModelingObjectRef source =
      create_mesh(harness, "Already Floored Vertices",
                  std::move(source_document.document));
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  select_quad_vertices(harness, source, source_vertices,
                       quader::QuaderSelectionEdit::Replace);
  const MeshGeometryState geometry_before = mesh_geometry(harness, source);
  const quader::QuaderSelectionSummary selection_before =
      harness.editor.selection().summary();
  const quader::QuaderHistorySnapshot history_before =
      harness.editor.query().history();
  const quader::QuaderRevisionStamp revisions_before =
      harness.editor.query().revisions();

  ASSERT_TRUE(harness.editor.modeling().can_snap_selected_vertices_to_floor());
  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().snap_selected_vertices_to_floor();
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  EXPECT_FALSE(snapped.changed());
  expect_mesh_geometry_near(mesh_geometry(harness, source), geometry_before);
  const quader::QuaderSelectionSummary selection_after =
      harness.editor.selection().summary();
  EXPECT_EQ(selection_after.selected_vertex_count,
            selection_before.selected_vertex_count);
  EXPECT_EQ(selection_after.active_object_id, selection_before.active_object_id);
  EXPECT_EQ(selection_after.active_vertex_id, selection_before.active_vertex_id);
  expect_history_equal(harness.editor.query().history(), history_before);
  expect_revisions_equal(harness.editor.query().revisions(), revisions_before);
}

TEST(SnapToFloorTests,
     VertexSnapIsRejectedDuringInstanceEditWithoutChangingSharedGeometry) {
  SnapToFloorHarness harness;
  const quader::QuaderModelingObjectRef member = create_box(
      harness, "Instance Definition Member",
      {.min = {-1.0f, 2.0f, -1.0f}, .max = {1.0f, 4.0f, 1.0f}});
  ASSERT_TRUE(harness.editor.selection().select_object(member).ok());
  const quader::QuaderApiResult<quader::QuaderInstanceCreateResult> instance =
      harness.editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const quader::EntityId occurrence = instance.value().occurrence.root.id;
  ASSERT_TRUE(harness.editor.modeling().enter_instance_edit(occurrence).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(member).ok());
  ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(harness.editor.selection().select_vertex(member, 1).ok());
  ASSERT_EQ(harness.editor.selection().summary().mode,
            quader::QuaderSelectionMode::Vertex);

  const MeshGeometryState geometry_before = mesh_geometry(harness, member);
  const quader::QuaderHistorySnapshot history_before =
      harness.editor.query().history();
  EXPECT_FALSE(harness.editor.modeling().can_snap_selected_vertices_to_floor());
  const quader::QuaderActionSet actions =
      harness.editor.modeling().available_action_set({
          .surface = quader::QuaderActionSurface::OperationsPanel,
          .include_disabled = true,
      });
  const quader::QuaderActionItem *snap =
      find_action(actions, "vertex_operations", "snap_vertices_to_floor");
  ASSERT_NE(snap, nullptr);
  EXPECT_EQ(snap->visibility, quader::QuaderActionVisibility::Disabled);
  EXPECT_EQ(
      snap->disabled_reason_key,
      "modeling.action.disabled.snap_vertices_to_floor_instance_edit");

  const quader::QuaderApiVoidResult rejected =
      harness.editor.modeling().snap_selected_vertices_to_floor();
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  expect_mesh_geometry_near(mesh_geometry(harness, member), geometry_before);
  expect_history_equal(harness.editor.query().history(), history_before);
}

} // namespace
