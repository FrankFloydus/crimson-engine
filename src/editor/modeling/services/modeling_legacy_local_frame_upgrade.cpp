////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_legacy_local_frame_upgrade.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "editor/modeling/services/modeling_mesh_sync_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "math/glm_bridge.hpp"
#include "quader/modeling/neutral/polygon/document.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float kFrameRelativeTolerance = 0.0001f;
constexpr float kFrameAbsoluteTolerance = 0.00001f;

using CubeFaceLoop = std::array<quader_poly::ElementId, 4>;

constexpr std::array<CubeFaceLoop, 6> kCubeFaceLoops{{
    {1u, 2u, 3u, 4u},
    {5u, 6u, 7u, 8u},
    {8u, 7u, 3u, 2u},
    {5u, 1u, 4u, 6u},
    {6u, 4u, 3u, 7u},
    {5u, 8u, 2u, 1u},
}};

enum class LegacyPlanStatus {
  Ready,
  MarkerOnly,
  Ineligible,
  Unrecognized,
  Unrepresentable,
};

struct LegacyCubeFrame final {
  QVec3 origin;
  QVec3 x_axis;
  QVec3 y_axis;
  QVec3 z_axis;
  std::array<QVec3, 8> canonical_positions{};
};

struct LegacyTransformEdit final {
  EntityId entity = kInvalidEntityId;
  TransformComponent before;
  TransformComponent after;
  QMat4 expected_local_after;
};

struct LegacyUpgradePlan final {
  LegacyPlanStatus status = LegacyPlanStatus::Ineligible;
  EntityId entity = kInvalidEntityId;
  ModelingObjectComponent modeling_before;
  ModelingObjectComponent modeling_after;
  MeshComponent mesh_before;
  MeshAsset mesh_after;
  std::vector<LegacyTransformEdit> transforms;
  bool clone_on_write = false;
};

bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

bool near_scalar(float left, float right,
                 float relative_tolerance = kFrameRelativeTolerance) {
  const float magnitude =
      std::max({1.0f, std::abs(left), std::abs(right)});
  return std::abs(left - right) <= relative_tolerance * magnitude;
}

bool near_vector(QVec3 left, QVec3 right,
                 float relative_tolerance = kFrameRelativeTolerance) {
  return near_scalar(left.x, right.x, relative_tolerance) &&
         near_scalar(left.y, right.y, relative_tolerance) &&
         near_scalar(left.z, right.z, relative_tolerance);
}

bool near_matrix(const QMat4 &left, const QMat4 &right,
                 float relative_tolerance = kFrameRelativeTolerance) {
  for (std::size_t index = 0u; index < left.values.size(); ++index) {
    if (!near_scalar(left.values[index], right.values[index],
                     relative_tolerance)) {
      return false;
    }
  }
  return true;
}

QMat4 frame_matrix(const LegacyCubeFrame &frame) {
  QMat4 matrix;
  matrix.values = {
      frame.x_axis.x, frame.x_axis.y, frame.x_axis.z, 0.0f,
      frame.y_axis.x, frame.y_axis.y, frame.y_axis.z, 0.0f,
      frame.z_axis.x, frame.z_axis.y, frame.z_axis.z, 0.0f,
      frame.origin.x, frame.origin.y, frame.origin.z, 1.0f,
  };
  return matrix;
}

QVec3 project_into_frame(const LegacyCubeFrame &frame, QVec3 point) {
  const QVec3 relative = point - frame.origin;
  return {
      dot(relative, frame.x_axis),
      dot(relative, frame.y_axis),
      dot(relative, frame.z_axis),
  };
}

const quader_poly::Vertex *vertex_with_id(const quader_poly::Document &document,
                                         quader_poly::ElementId id) {
  if (id == quader_poly::kInvalidElementId || id > document.vertices.size()) {
    return nullptr;
  }
  const quader_poly::Vertex &vertex = document.vertices[id - 1u];
  return vertex.id == id ? &vertex : nullptr;
}

bool cube_topology_matches_source_replay(
    const quader_poly::Document &document) {
  if (document.vertices.size() != 8u || document.faces.size() != 6u ||
      document.next_vertex_id != 9u || document.next_face_id != 7u) {
    return false;
  }
  for (std::size_t index = 0u; index < document.vertices.size(); ++index) {
    if (document.vertices[index].id != index + 1u) {
      return false;
    }
  }

  bool normal_winding = true;
  bool flipped_winding = true;
  for (std::size_t index = 0u; index < document.faces.size(); ++index) {
    const quader_poly::Face &face = document.faces[index];
    if (face.id != index + 1u || face.vertices.size() != 4u) {
      return false;
    }
    const CubeFaceLoop &expected = kCubeFaceLoops[index];
    for (std::size_t corner = 0u; corner < expected.size(); ++corner) {
      normal_winding =
          normal_winding && face.vertices[corner] == expected[corner];
      flipped_winding =
          flipped_winding &&
          face.vertices[corner] == expected[expected.size() - 1u - corner];
    }
  }
  return normal_winding || flipped_winding;
}

std::optional<LegacyCubeFrame>
recover_legacy_cube_frame(const quader_poly::Document &document) {
  if (!cube_topology_matches_source_replay(document)) {
    return std::nullopt;
  }

  std::array<QVec3, 8> positions{};
  QVec3 origin;
  for (std::size_t index = 0u; index < positions.size(); ++index) {
    const quader_poly::Vertex *vertex =
        vertex_with_id(document,
                       static_cast<quader_poly::ElementId>(index + 1u));
    if (vertex == nullptr || !finite_vector(vertex->position)) {
      return std::nullopt;
    }
    positions[index] = vertex->position;
    origin += vertex->position;
  }
  origin = origin / static_cast<float>(positions.size());

  const QVec3 x_edge = positions[1] - positions[0];
  const QVec3 y_edge = positions[3] - positions[0];
  const QVec3 z_edge = positions[0] - positions[4];
  const float width = length(x_edge);
  const float height = length(y_edge);
  const float depth = length(z_edge);
  if (!std::isfinite(width) || !std::isfinite(height) ||
      !std::isfinite(depth) || width <= kFrameAbsoluteTolerance ||
      height <= kFrameAbsoluteTolerance || depth <= kFrameAbsoluteTolerance) {
    return std::nullopt;
  }

  const QVec3 x_axis = x_edge / width;
  const QVec3 normalized_y_edge = y_edge / height;
  if (std::abs(dot(x_axis, normalized_y_edge)) >
      kFrameRelativeTolerance) {
    return std::nullopt;
  }
  const QVec3 y_rejected = y_edge - x_axis * dot(y_edge, x_axis);
  const float rejected_height = length(y_rejected);
  if (!std::isfinite(rejected_height) ||
      rejected_height <= kFrameAbsoluteTolerance) {
    return std::nullopt;
  }
  const QVec3 y_axis = y_rejected / rejected_height;
  const QVec3 z_axis = normalize_or_zero(cross(x_axis, y_axis));
  if (!finite_vector(origin) || !finite_vector(x_axis) ||
      !finite_vector(y_axis) || !finite_vector(z_axis) ||
      length_squared(z_axis) < 0.999f ||
      dot(z_edge / depth, z_axis) < 1.0f - kFrameRelativeTolerance) {
    return std::nullopt;
  }

  LegacyCubeFrame frame{
      .origin = origin,
      .x_axis = x_axis,
      .y_axis = y_axis,
      .z_axis = z_axis,
  };
  for (std::size_t index = 0u; index < positions.size(); ++index) {
    frame.canonical_positions[index] =
        project_into_frame(frame, positions[index]);
  }

  const quader_poly::Document replay = quader_poly::make_cube({
      .width = width,
      .height = height,
      .depth = depth,
      .width_segments = 1,
      .height_segments = 1,
      .depth_segments = 1,
      .flip_faces = false,
  });
  if (!cube_topology_matches_source_replay(replay)) {
    return std::nullopt;
  }
  for (std::size_t index = 0u; index < positions.size(); ++index) {
    if (replay.vertices[index].id != document.vertices[index].id ||
        !near_vector(frame.canonical_positions[index],
                     replay.vertices[index].position)) {
      return std::nullopt;
    }
    const QVec3 reconstructed =
        frame.origin + frame.x_axis * frame.canonical_positions[index].x +
        frame.y_axis * frame.canonical_positions[index].y +
        frame.z_axis * frame.canonical_positions[index].z;
    if (!near_vector(reconstructed, positions[index])) {
      return std::nullopt;
    }
  }
  return frame;
}

bool frame_is_identity(const LegacyCubeFrame &frame) {
  return near_vector(frame.origin, {}) &&
         near_vector(frame.x_axis, {1.0f, 0.0f, 0.0f}) &&
         near_vector(frame.y_axis, {0.0f, 1.0f, 0.0f}) &&
         near_vector(frame.z_axis, {0.0f, 0.0f, 1.0f});
}

std::optional<TransformComponent>
representable_transform(const QMat4 &matrix,
                        const TransformComponent &reference) {
  std::optional<TransformComponent> result =
      try_transform_component_from_matrix(matrix, reference.rotation_euler);
  if (!result.has_value()) {
    return std::nullopt;
  }
  result->custom_pivot = reference.custom_pivot;
  if (!near_matrix(compose_transform(result->spatial_transform()), matrix)) {
    return std::nullopt;
  }
  return result;
}

MeshAsset canonicalized_mesh_asset(const MeshAsset &source,
                                   const LegacyCubeFrame &frame) {
  quader_poly::Document document = *source.authored_polygon_document;
  // The old renderer generated UVs from the baked positions. Make that
  // implicit mapping durable before changing the authored coordinate frame.
  quader_poly::ensure_face_uvs(document);
  for (std::size_t index = 0u; index < document.vertices.size(); ++index) {
    document.vertices[index].position = frame.canonical_positions[index];
  }

  MeshAsset result =
      MeshProjectionService::compile_polygon_document_mesh(document);
  result.name = source.name;
  result.has_stable_cpu_topology = source.has_stable_cpu_topology;
  result.replace_authored_polygon_document(std::move(document));
  return result;
}

LegacyUpgradePlan make_upgrade_plan(
    const SceneDocument &scene, const AssetRegistry &assets, EntityId entity,
    const std::unordered_map<AssetId, std::size_t> &mesh_reference_counts) {
  LegacyUpgradePlan plan;
  plan.entity = entity;

  const ModelingObjectComponent *modeling = scene.modeling_object(entity);
  if (modeling == nullptr ||
      modeling->local_frame_version !=
          kLegacyUnknownModelingObjectLocalFrameVersion) {
    return plan;
  }
  plan.modeling_before = *modeling;
  plan.modeling_after = *modeling;
  plan.modeling_after.local_frame_version =
      kCanonicalModelingObjectLocalFrameVersion;

  const TransformComponent *target_transform = scene.transform(entity);
  const MeshComponent *mesh_component = scene.mesh(entity);
  if (target_transform == nullptr || target_transform->custom_pivot ||
      mesh_component == nullptr) {
    return plan;
  }
  const MeshAsset *mesh = assets.mesh(mesh_component->mesh);
  if (mesh == nullptr || !mesh->has_stable_cpu_topology ||
      !mesh->has_authored_polygon_document ||
      mesh->authored_polygon_document == nullptr) {
    return plan;
  }
  plan.mesh_before = *mesh_component;

  const std::optional<LegacyCubeFrame> frame =
      recover_legacy_cube_frame(*mesh->authored_polygon_document);
  if (!frame.has_value()) {
    plan.status = LegacyPlanStatus::Unrecognized;
    return plan;
  }
  if (frame_is_identity(*frame)) {
    plan.status = LegacyPlanStatus::MarkerOnly;
    return plan;
  }

  const QMat4 basis = frame_matrix(*frame);
  const std::optional<QMat4> inverse_basis = try_scene_matrix_inverse(basis);
  if (!inverse_basis.has_value()) {
    plan.status = LegacyPlanStatus::Unrepresentable;
    return plan;
  }

  const QMat4 target_local_before = scene.local_transform_matrix(entity);
  const QMat4 target_local_after =
      scene_transform_multiply(target_local_before, basis);
  const std::optional<TransformComponent> target_after =
      representable_transform(target_local_after, *target_transform);
  if (!target_after.has_value()) {
    plan.status = LegacyPlanStatus::Unrepresentable;
    return plan;
  }
  plan.transforms.push_back(LegacyTransformEdit{
      .entity = entity,
      .before = *target_transform,
      .after = *target_after,
      .expected_local_after = target_local_after,
  });

  const QMat4 target_world_before = scene.world_transform_matrix(entity);
  const EntityId parent = scene.parent(entity);
  const QMat4 parent_world = parent == kInvalidEntityId
                                 ? scene_transform_identity_matrix()
                                 : scene.world_transform_matrix(parent);
  const QMat4 target_world_after = scene_transform_multiply(
      parent_world, compose_transform(target_after->spatial_transform()));
  for (std::size_t index = 0u; index < frame->canonical_positions.size();
       ++index) {
    const QVec3 world_before = transform_point(
        target_world_before,
        mesh->authored_polygon_document->vertices[index].position);
    const QVec3 world_after =
        transform_point(target_world_after, frame->canonical_positions[index]);
    if (!finite_vector(world_before) || !finite_vector(world_after) ||
        !near_vector(world_before, world_after)) {
      plan.status = LegacyPlanStatus::Unrepresentable;
      return plan;
    }
  }

  for (const EntityId child : scene.children(entity)) {
    const TransformComponent *child_before = scene.transform(child);
    if (child_before == nullptr) {
      plan.status = LegacyPlanStatus::Unrepresentable;
      return plan;
    }
    const QMat4 child_local_before = scene.local_transform_matrix(child);
    const QMat4 child_local_after =
        scene_transform_multiply(*inverse_basis, child_local_before);
    const std::optional<TransformComponent> child_after =
        representable_transform(child_local_after, *child_before);
    if (!child_after.has_value()) {
      plan.status = LegacyPlanStatus::Unrepresentable;
      return plan;
    }
    const QMat4 child_world_before = scene.world_transform_matrix(child);
    const QMat4 child_world_after = scene_transform_multiply(
        target_world_after, compose_transform(child_after->spatial_transform()));
    if (!near_matrix(child_world_before, child_world_after)) {
      plan.status = LegacyPlanStatus::Unrepresentable;
      return plan;
    }
    plan.transforms.push_back(LegacyTransformEdit{
        .entity = child,
        .before = *child_before,
        .after = *child_after,
        .expected_local_after = child_local_after,
    });
  }

  plan.mesh_after = canonicalized_mesh_asset(*mesh, *frame);
  if (!assets.validate_mesh(plan.mesh_after).ok ||
      !scene.can_notify_mesh_payloads_changed(
          std::span<const EntityId>(&entity, 1u))) {
    plan.status = LegacyPlanStatus::Unrepresentable;
    return plan;
  }
  const auto references = mesh_reference_counts.find(mesh_component->mesh);
  plan.clone_on_write =
      references != mesh_reference_counts.end() && references->second > 1u;
  plan.status = LegacyPlanStatus::Ready;
  return plan;
}

bool transforms_match_plan(const SceneDocument &scene,
                           const LegacyUpgradePlan &plan) {
  return std::ranges::all_of(plan.transforms,
                             [&scene](const LegacyTransformEdit &edit) {
                               return near_matrix(
                                   scene.local_transform_matrix(edit.entity),
                                   edit.expected_local_after);
                             });
}

void rollback_plan(SceneDocument &scene, AssetRegistry &assets,
                   ModelingMeshSyncService &mesh_sync,
                   const SceneProjectionCheckpoint &scene_checkpoint,
                   const AssetRegistryProjectionCheckpoint &asset_checkpoint,
                   const LegacyUpgradePlan &plan) {
  static_cast<void>(
      mesh_sync.restore_projected_asset_checkpoint(assets, asset_checkpoint));
  static_cast<void>(mesh_sync.set_projected_mesh_component(
      scene, plan.entity, plan.mesh_before));
  std::vector<SceneTransformAssignment> assignments;
  assignments.reserve(plan.transforms.size());
  for (const LegacyTransformEdit &edit : plan.transforms) {
    assignments.push_back(SceneTransformAssignment{
        .entity = edit.entity,
        .transform = edit.before,
    });
  }
  mesh_sync.apply_scene_transforms(scene, assignments);
  static_cast<void>(mesh_sync.set_projected_modeling_object_component(
      scene, plan.entity, plan.modeling_before));
  static_cast<void>(
      mesh_sync.restore_projected_scene_checkpoint(scene, scene_checkpoint));
}

bool apply_marker_only_plan(SceneDocument &scene,
                            ModelingMeshSyncService &mesh_sync,
                            const LegacyUpgradePlan &plan) {
  return mesh_sync.set_projected_modeling_object_component(
             scene, plan.entity, plan.modeling_after) &&
         scene.modeling_object(plan.entity) != nullptr &&
         scene.modeling_object(plan.entity)->local_frame_version ==
             kCanonicalModelingObjectLocalFrameVersion;
}

bool apply_upgrade_plan(SceneDocument &scene, AssetRegistry &assets,
                        ModelingMeshSyncService &mesh_sync,
                        LegacyUpgradePlan &plan) {
  const SceneProjectionCheckpoint scene_checkpoint =
      scene.capture_projection_checkpoint();
  const AssetRegistryProjectionCheckpoint asset_checkpoint =
      assets.capture_projection_checkpoint();

  MeshComponent mesh_after = plan.mesh_before;
  bool mesh_published = false;
  if (plan.clone_on_write) {
    mesh_after.mesh =
        mesh_sync.install_projected_mesh(assets, plan.mesh_after);
    mesh_published = mesh_after.mesh != kInvalidAssetId &&
                     mesh_sync.set_projected_mesh_component(
                         scene, plan.entity, mesh_after);
  } else {
    mesh_published = mesh_sync.replace_projected_mesh(
                         assets, plan.mesh_before.mesh, plan.mesh_after) &&
                     mesh_sync.notify_projected_mesh_changed(scene,
                                                             plan.entity);
  }

  std::vector<SceneTransformAssignment> assignments;
  assignments.reserve(plan.transforms.size());
  for (const LegacyTransformEdit &edit : plan.transforms) {
    assignments.push_back(SceneTransformAssignment{
        .entity = edit.entity,
        .transform = edit.after,
    });
  }
  if (mesh_published) {
    mesh_sync.apply_scene_transforms(scene, assignments);
  }
  const bool marker_published =
      mesh_published &&
      mesh_sync.set_projected_modeling_object_component(
          scene, plan.entity, plan.modeling_after);
  const ModelingObjectComponent *published_modeling =
      scene.modeling_object(plan.entity);
  const MeshComponent *published_mesh = scene.mesh(plan.entity);
  const bool published =
      marker_published && transforms_match_plan(scene, plan) &&
      published_modeling != nullptr &&
      published_modeling->local_frame_version ==
          kCanonicalModelingObjectLocalFrameVersion &&
      published_mesh != nullptr &&
      published_mesh->mesh == mesh_after.mesh;
  if (!published) {
    rollback_plan(scene, assets, mesh_sync, scene_checkpoint, asset_checkpoint,
                  plan);
    return false;
  }
  return true;
}

std::unordered_map<AssetId, std::size_t>
mesh_reference_counts(const SceneDocument &scene) {
  std::unordered_map<AssetId, std::size_t> result;
  result.reserve(scene.entities().size());
  for (const EntityId entity : scene.entities()) {
    if (const MeshComponent *mesh = scene.mesh(entity); mesh != nullptr) {
      ++result[mesh->mesh];
    }
  }
  return result;
}

} // namespace

ModelingLegacyLocalFrameUpgradeStats
ModelingLegacyLocalFrameUpgradeService::upgrade_scene(SceneDocument &scene,
                                                       AssetRegistry &assets,
                                                       ModelingMeshSyncService &mesh_sync) {
  ModelingLegacyLocalFrameUpgradeStats stats;
  std::unordered_map<AssetId, std::size_t> reference_counts =
      mesh_reference_counts(scene);
  const std::vector<EntityId> entities = scene.entities();
  stats.scanned_entity_count = entities.size();

  for (const EntityId entity : entities) {
    const ModelingObjectComponent *modeling = scene.modeling_object(entity);
    if (modeling == nullptr ||
        modeling->local_frame_version !=
            kLegacyUnknownModelingObjectLocalFrameVersion) {
      continue;
    }
    ++stats.legacy_candidate_count;

    LegacyUpgradePlan plan;
    try {
      plan = make_upgrade_plan(scene, assets, entity, reference_counts);
    } catch (const std::exception &) {
      ++stats.failed_object_count;
      continue;
    } catch (...) {
      ++stats.failed_object_count;
      continue;
    }

    switch (plan.status) {
    case LegacyPlanStatus::Ineligible:
      ++stats.ineligible_object_count;
      continue;
    case LegacyPlanStatus::Unrecognized:
      ++stats.unrecognized_object_count;
      continue;
    case LegacyPlanStatus::Unrepresentable:
      ++stats.unrepresentable_object_count;
      continue;
    case LegacyPlanStatus::MarkerOnly:
      if (apply_marker_only_plan(scene, mesh_sync, plan)) {
        ++stats.upgraded_object_count;
        ++stats.marker_only_object_count;
      } else {
        ++stats.failed_object_count;
      }
      continue;
    case LegacyPlanStatus::Ready:
      break;
    }

    if (!apply_upgrade_plan(scene, assets, mesh_sync, plan)) {
      ++stats.failed_object_count;
      continue;
    }
    ++stats.upgraded_object_count;
    if (plan.clone_on_write) {
      ++stats.cloned_mesh_asset_count;
      auto before = reference_counts.find(plan.mesh_before.mesh);
      if (before != reference_counts.end() && before->second > 0u) {
        --before->second;
      }
      const MeshComponent *after = scene.mesh(entity);
      if (after != nullptr) {
        ++reference_counts[after->mesh];
      }
    }
  }
  return stats;
}

} // namespace quader
