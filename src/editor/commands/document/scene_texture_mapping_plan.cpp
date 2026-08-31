////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/document/scene_texture_mapping_plan.hpp"

#include "scene/scene_texture_mapping.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace quader {
namespace {

bool same_matrix(const QMat4 &left, const QMat4 &right) {
  for (std::size_t index = 0; index < left.values.size(); ++index) {
    const float tolerance = 0.000002f *
        std::max({1.0f, std::abs(left.values[index]), std::abs(right.values[index])});
    if (std::abs(left.values[index] - right.values[index]) > tolerance) {
      return false;
    }
  }
  return true;
}

bool locks_whole_operation(TextureLockSettings settings,
                          SceneTextureTransformKind kind) {
  switch (kind) {
  case SceneTextureTransformKind::Translation:
  case SceneTextureTransformKind::Rotation:
    return settings.texture_lock;
  case SceneTextureTransformKind::Scale:
    return settings.texture_lock_scale;
  case SceneTextureTransformKind::Combined:
    return settings.texture_lock && settings.texture_lock_scale;
  }
  return false;
}

std::optional<QMat4> locked_local_delta(
    const TransformComponent &before, const TransformComponent &after,
    TextureLockSettings settings, SceneTextureTransformKind kind) {
  if (document_command_same_transform(before, after)) {
    return QMat4{};
  }
  const QMat4 before_matrix = compose_transform(before.spatial_transform());
  const QMat4 after_matrix = compose_transform(after.spatial_transform());
  const std::optional<QMat4> before_inverse = try_scene_matrix_inverse(before_matrix);
  if (!before_inverse.has_value()) {
    return std::nullopt;
  }
  if (locks_whole_operation(settings, kind)) {
    return scene_transform_multiply(after_matrix, *before_inverse);
  }
  if (kind != SceneTextureTransformKind::Combined ||
      (!settings.texture_lock && !settings.texture_lock_scale)) {
    return QMat4{};
  }

  // Absolute TRS edits have one deterministic order in their authored parent
  // frame: scale, then rotation, then translation. Never decompose world TRS.
  TransformComponent scaled = before;
  scaled.scale = after.scale;
  TransformComponent rotated = scaled;
  rotated.rotation_euler = after.rotation_euler;
  const QMat4 scaled_matrix = compose_transform(scaled.spatial_transform());
  const QMat4 rotated_matrix = compose_transform(rotated.spatial_transform());
  const std::optional<QMat4> scaled_inverse = try_scene_matrix_inverse(scaled_matrix);
  const std::optional<QMat4> rotated_inverse = try_scene_matrix_inverse(rotated_matrix);
  if (!scaled_inverse.has_value() || !rotated_inverse.has_value()) {
    return std::nullopt;
  }
  QMat4 result;
  if (settings.texture_lock_scale) {
    result = scene_transform_multiply(scaled_matrix, *before_inverse);
  }
  if (settings.texture_lock) {
    result = scene_transform_multiply(
        scene_transform_multiply(rotated_matrix, *scaled_inverse), result);
    result = scene_transform_multiply(
        scene_transform_multiply(after_matrix, *rotated_inverse), result);
  }
  return result;
}

} // namespace

std::optional<SceneTextureMappingPlan> SceneTextureMappingPlan::capture(
    const SceneDocument &scene, std::span<const EntityId> targets,
    TextureLockSettings settings) {
  SceneTextureMappingPlan plan;
  plan.settings_ = settings;
  const std::unordered_set<EntityId> selected(targets.begin(), targets.end());
  const std::shared_ptr<const SceneHierarchyProjectionSnapshot> hierarchy =
      scene.hierarchy_projection();
  for (const SceneHierarchyProjectionRecord &record : hierarchy->records()) {
    const auto parent = plan.node_by_entity_.find(record.parent);
    if (!selected.contains(record.entity) && parent == plan.node_by_entity_.end()) {
      continue;
    }
    const TransformComponent *transform = scene.transform(record.entity);
    if (transform == nullptr || !scene_transform_is_finite(*transform)) {
      return std::nullopt;
    }
    const SceneHierarchyProjectionRecord *parent_record =
        hierarchy->find(record.parent);
    const QMat4 parent_world = parent_record != nullptr
                                  ? parent_record->world_transform : QMat4{};
    const std::optional<QMat4> world_inverse =
        try_scene_matrix_inverse(record.world_transform);
    const std::optional<QMat4> parent_inverse = try_scene_matrix_inverse(parent_world);
    if (!world_inverse.has_value() || !parent_inverse.has_value()) {
      return std::nullopt;
    }
    const TextureMappingComponent *mapping = scene.texture_mapping(record.entity);
    if (mapping != nullptr && !texture_mapping_is_valid(*mapping)) {
      return std::nullopt;
    }
    Node node{
        .entity = record.entity,
        .parent_index = parent == plan.node_by_entity_.end()
                            ? std::nullopt : std::optional<std::size_t>{parent->second},
        .before_transform = *transform,
        .before_world = record.world_transform,
        .before_world_inverse = *world_inverse,
        .before_parent_world = parent_world,
        .before_parent_inverse = *parent_inverse,
        .before_mapping = mapping == nullptr
                              ? std::nullopt : std::optional<TextureMappingComponent>{*mapping},
        .mapping_target = scene.kind(record.entity) == EntityKind::Mesh ||
                          scene.instance_occurrence(record.entity) != nullptr,
    };
    plan.node_by_entity_.emplace(record.entity, plan.nodes_.size());
    plan.nodes_.push_back(std::move(node));
  }
  for (const EntityId entity : selected) {
    if (!plan.node_by_entity_.contains(entity)) {
      return std::nullopt;
    }
  }
  return plan;
}

std::optional<std::vector<DocumentTextureMappingEdit>>
SceneTextureMappingPlan::evaluate(
    std::span<const DocumentTransformEdit> transforms,
    SceneTextureTransformKind kind) const {
  std::vector<const TransformComponent *> after_by_node(nodes_.size(), nullptr);
  for (const DocumentTransformEdit &edit : transforms) {
    const auto found = node_by_entity_.find(edit.entity);
    if (found == node_by_entity_.end() || !scene_transform_is_finite(edit.after) ||
        !try_scene_matrix_inverse(compose_transform(edit.after.spatial_transform())).has_value()) {
      return std::nullopt;
    }
    after_by_node[found->second] = &edit.after;
  }
  std::vector<DocumentTextureMappingEdit> result;
  result.reserve(nodes_.size());
  if (locks_whole_operation(settings_, kind)) {
    for (const Node &node : nodes_) {
      if (node.mapping_target) {
        result.push_back({.entity = node.entity,
                          .before = node.before_mapping,
                          .after = node.before_mapping});
      }
    }
    return result;
  }

  std::vector<QMat4> after_world(nodes_.size());
  std::vector<QMat4> locked_world_delta(nodes_.size());
  for (std::size_t index = 0; index < nodes_.size(); ++index) {
    const Node &node = nodes_[index];
    const TransformComponent &after = after_by_node[index] != nullptr
                                          ? *after_by_node[index] : node.before_transform;
    const QMat4 parent_after = node.parent_index.has_value()
                                  ? after_world[*node.parent_index] : node.before_parent_world;
    after_world[index] = scene_transform_multiply(
        parent_after, compose_transform(after.spatial_transform()));
    if (!try_scene_matrix_inverse(after_world[index]).has_value()) {
      return std::nullopt;
    }
    const std::optional<QMat4> local_delta = locked_local_delta(
        node.before_transform, after, settings_, kind);
    if (!local_delta.has_value()) {
      return std::nullopt;
    }
    const QMat4 own_locked_world_delta = scene_transform_multiply(
        scene_transform_multiply(node.before_parent_world, *local_delta),
        node.before_parent_inverse);
    locked_world_delta[index] = node.parent_index.has_value()
        ? scene_transform_multiply(locked_world_delta[*node.parent_index], own_locked_world_delta)
        : own_locked_world_delta;
    if (!node.mapping_target) {
      continue;
    }
    const std::optional<QMat4> unlocked_from_world =
        try_scene_matrix_inverse(locked_world_delta[index]);
    if (!unlocked_from_world.has_value()) {
      return std::nullopt;
    }
    const QMat4 before_mapping = node.before_mapping.has_value()
                                    ? node.before_mapping->sampling_from_local : QMat4{};
    QMat4 mapped = scene_transform_multiply(
        scene_transform_multiply(before_mapping, node.before_world_inverse),
        scene_transform_multiply(*unlocked_from_world, after_world[index]));
    // Products of affine matrices are affine; force the exact homogeneous row
    // after the double-precision inverse conversion.
    mapped.values[3] = mapped.values[7] = mapped.values[11] = 0.0f;
    mapped.values[15] = 1.0f;
    if (!texture_mapping_is_valid({mapped})) {
      return std::nullopt;
    }
    std::optional<TextureMappingComponent> after_mapping;
    if (same_matrix(mapped, before_mapping)) {
      after_mapping = node.before_mapping;
    } else if (!same_matrix(mapped, QMat4{})) {
      after_mapping = TextureMappingComponent{mapped};
    }
    result.push_back({.entity = node.entity,
                      .before = node.before_mapping,
                      .after = after_mapping});
  }
  return result;
}

} // namespace quader
