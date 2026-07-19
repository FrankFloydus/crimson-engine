////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/transform/mesh_arrangement_service.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document_command_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr double kArrangementEpsilon = 0.000001;

/// Captures immutable pre-operation state for one topmost selected mesh root.
struct ArrangementTarget {
  EntityId entity = kInvalidEntityId;
  SceneBounds bounds;
  TransformComponent transform;
};

/// Stores validated roots and the promoted active alignment reference.
struct ArrangementSelection {
  bool ok = false;
  std::string message;
  std::vector<ArrangementTarget> roots;
  EntityId active_root = kInvalidEntityId;
};

float axis_value(QVec3 value, MeshArrangementAxis axis) {
  switch (axis) {
  case MeshArrangementAxis::X:
    return value.x;
  case MeshArrangementAxis::Y:
    return value.y;
  case MeshArrangementAxis::Z:
    return value.z;
  }
  return 0.0f;
}

QVec3 axis_delta(MeshArrangementAxis axis, float value) {
  switch (axis) {
  case MeshArrangementAxis::X:
    return {value, 0.0f, 0.0f};
  case MeshArrangementAxis::Y:
    return {0.0f, value, 0.0f};
  case MeshArrangementAxis::Z:
    return {0.0f, 0.0f, value};
  }
  return {};
}

float bounds_coordinate(const SceneBounds &bounds, MeshArrangementAxis axis,
                        MeshAlignmentMode alignment) {
  switch (alignment) {
  case MeshAlignmentMode::Minimum:
    return axis_value(bounds.min, axis);
  case MeshAlignmentMode::Center:
    return axis_value(bounds_center(bounds), axis);
  case MeshAlignmentMode::Maximum:
    return axis_value(bounds.max, axis);
  }
  return 0.0f;
}

float bounds_width(const SceneBounds &bounds, MeshArrangementAxis axis) {
  return axis_value(bounds.max, axis) - axis_value(bounds.min, axis);
}

bool valid_snap_options(const MeshArrangementOptions &options) {
  return !options.snap_to_grid || (std::isfinite(options.grid_size) &&
                                   options.grid_size > kArrangementEpsilon);
}

bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

bool valid_bounds(const SceneBounds &bounds) {
  return finite_vector(bounds.min) && finite_vector(bounds.max) &&
         bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y &&
         bounds.min.z <= bounds.max.z;
}

float snapped_delta(float value, const MeshArrangementOptions &options) {
  if (!options.snap_to_grid) {
    return value;
  }
  const double grid = options.grid_size;
  return static_cast<float>(std::round(static_cast<double>(value) / grid) *
                            grid);
}

bool has_selected_mesh_ancestor(
    const SceneDocument &scene, EntityId entity,
    const std::unordered_set<EntityId> &selected_meshes) {
  EntityId parent = scene.parent(entity);
  while (parent != kInvalidEntityId) {
    if (selected_meshes.contains(parent)) {
      return true;
    }
    parent = scene.parent(parent);
  }
  return false;
}

EntityId
selected_mesh_root(const SceneDocument &scene, EntityId entity,
                   const std::unordered_set<EntityId> &selected_meshes) {
  EntityId root = entity;
  EntityId parent = scene.parent(root);
  while (parent != kInvalidEntityId) {
    if (selected_meshes.contains(parent)) {
      root = parent;
    }
    parent = scene.parent(parent);
  }
  return root;
}

ArrangementSelection
collect_arrangement_selection(const SceneDocument &scene,
                              const AssetRegistry &assets,
                              MeshArrangementSelectionContext context) {
  QDR_PROFILE_SCOPE("Mesh arrangement collect selection");
  if (!context.object_mode) {
    return {.message = "Mesh arrangement only works in object mode."};
  }

  std::unordered_set<EntityId> selected_meshes;
  {
    QDR_PROFILE_SCOPE("Mesh arrangement collect selected meshes");
    for (const EntityId entity : scene.selected_entities()) {
      if (scene.modeling_object(entity) != nullptr &&
          scene.mesh(entity) != nullptr) {
        selected_meshes.insert(entity);
      }
    }
  }
  if (selected_meshes.empty()) {
    return {.message = "Select mesh objects before arranging them."};
  }

  for (const EntityId entity : selected_meshes) {
    if (scene.is_effectively_locked(entity)) {
      return {.message = "Locked mesh objects cannot be arranged."};
    }
    const MeshComponent *mesh_component = scene.mesh(entity);
    const MeshAsset *mesh =
        mesh_component != nullptr ? assets.mesh(mesh_component->mesh) : nullptr;
    const std::optional<SceneBounds> bounds =
        mesh != nullptr ? local_mesh_bounds(*mesh) : std::nullopt;
    if (!scene.is_effectively_visible(entity) || !bounds.has_value() ||
        !valid_bounds(*bounds)) {
      return {.message =
                  "Every arranged mesh must be visible and have valid bounds."};
    }
  }

  ArrangementSelection selection;
  selection.roots.reserve(selected_meshes.size());
  for (const EntityId entity : selected_meshes) {
    if (has_selected_mesh_ancestor(scene, entity, selected_meshes)) {
      continue;
    }
    const TransformComponent *transform = scene.transform(entity);
    std::optional<SceneBounds> bounds;
    {
      QDR_PROFILE_SCOPE("Mesh arrangement entity world bounds");
      bounds = entity_world_bounds(scene, assets, entity);
    }
    if (transform == nullptr || !bounds.has_value() || !valid_bounds(*bounds)) {
      return {.message =
                  "Every arranged mesh must be visible and have valid bounds."};
    }
    selection.roots.push_back({
        .entity = entity,
        .bounds = *bounds,
        .transform = *transform,
    });
  }

  if (context.active_mesh != kInvalidEntityId &&
      selected_meshes.contains(context.active_mesh)) {
    selection.active_root =
        selected_mesh_root(scene, context.active_mesh, selected_meshes);
  }
  selection.ok = true;
  return selection;
}

MeshArrangementAvailability collect_arrangement_availability(
    const SceneDocument &scene, const AssetRegistry &assets,
    MeshArrangementSelectionContext context) {
  QDR_PROFILE_SCOPE("Mesh arrangement availability preflight");
  if (!context.object_mode) {
    return {};
  }

  std::unordered_set<EntityId> selected_meshes;
  for (const EntityId entity : scene.selected_entities()) {
    const MeshComponent *mesh_component = scene.mesh(entity);
    if (scene.modeling_object(entity) == nullptr || mesh_component == nullptr) {
      continue;
    }
    const MeshAsset *mesh = assets.mesh(mesh_component->mesh);
    if (scene.is_effectively_locked(entity) ||
        !scene.is_effectively_visible(entity) ||
        scene.transform(entity) == nullptr || mesh == nullptr ||
        mesh->geometry().vertices.empty()) {
      return {};
    }
    selected_meshes.insert(entity);
  }
  if (selected_meshes.empty()) {
    return {};
  }

  std::size_t root_count = 0u;
  for (const EntityId entity : selected_meshes) {
    if (!has_selected_mesh_ancestor(scene, entity, selected_meshes)) {
      ++root_count;
    }
  }
  const EntityId active_root =
      context.active_mesh != kInvalidEntityId &&
              selected_meshes.contains(context.active_mesh)
          ? selected_mesh_root(scene, context.active_mesh, selected_meshes)
          : kInvalidEntityId;
  return {
      .can_align = root_count >= 2u && active_root != kInvalidEntityId,
      .can_distribute = root_count >= 3u,
  };
}

std::optional<DocumentTransformEdit>
translated_edit(const SceneDocument &scene, const ArrangementTarget &target,
                QVec3 world_delta) {
  const EntityId parent = scene.parent(target.entity);
  const QMat4 parent_world = parent == kInvalidEntityId
                                 ? scene_transform_identity_matrix()
                                 : scene.world_transform_matrix(parent);
  const QVec3 local_delta =
      transform_vector(inverse(parent_world), world_delta);
  if (!finite_vector(local_delta)) {
    return std::nullopt;
  }
  TransformComponent after = target.transform;
  after.position = after.position + local_delta;
  return DocumentTransformEdit{
      .entity = target.entity,
      .before = target.transform,
      .after = after,
  };
}

const ArrangementTarget *find_target(const ArrangementSelection &selection,
                                     EntityId entity) {
  const auto found =
      std::find_if(selection.roots.begin(), selection.roots.end(),
                   [entity](const ArrangementTarget &target) {
                     return target.entity == entity;
                   });
  return found == selection.roots.end() ? nullptr : &*found;
}

} // namespace

MeshArrangementService::MeshArrangementService(
    SceneDocument &scene, const AssetRegistry &assets,
    MeshArrangementSelectionContext selection_context,
    DocumentCommandService &document_commands)
    : scene_(scene), assets_(assets), selection_context_(selection_context),
      document_commands_(document_commands) {}

bool MeshArrangementService::can_align_selected_meshes() const {
  return availability().can_align;
}

bool MeshArrangementService::can_distribute_selected_meshes() const {
  return availability().can_distribute;
}

MeshArrangementAvailability MeshArrangementService::availability() const {
  // UI command predicates only need a cheap structural preflight. The command
  // path below still computes and validates exact transformed geometry bounds
  // immediately before applying edits.
  return collect_arrangement_availability(scene_, assets_, selection_context_);
}

DocumentCommandResult
MeshArrangementService::align_selected_meshes(MeshArrangementAxis axis,
                                              MeshAlignmentMode alignment,
                                              MeshArrangementOptions options) {
  if (!valid_snap_options(options)) {
    return document_command_invalid_request_result(
        "Grid size must be finite and greater than zero.");
  }
  const ArrangementSelection selection =
      collect_arrangement_selection(scene_, assets_, selection_context_);
  if (!selection.ok) {
    return document_command_invalid_request_result(selection.message);
  }
  if (selection.roots.size() < 2u) {
    return document_command_invalid_request_result(
        "Select at least two mesh roots to align.");
  }
  const ArrangementTarget *active =
      find_target(selection, selection.active_root);
  if (active == nullptr) {
    return document_command_invalid_request_result(
        "Alignment needs an active selected mesh root.");
  }

  const float reference = bounds_coordinate(active->bounds, axis, alignment);
  std::vector<DocumentTransformEdit> edits;
  edits.reserve(selection.roots.size() - 1u);
  for (const ArrangementTarget &target : selection.roots) {
    if (target.entity == active->entity) {
      continue;
    }
    const float delta = snapped_delta(
        reference - bounds_coordinate(target.bounds, axis, alignment), options);
    if (std::abs(delta) <= kArrangementEpsilon) {
      continue;
    }
    std::optional<DocumentTransformEdit> edit =
        translated_edit(scene_, target, axis_delta(axis, delta));
    if (!edit.has_value()) {
      return document_command_invalid_request_result(
          "A mesh parent transform cannot represent the world translation.");
    }
    edits.push_back(std::move(*edit));
  }
  return document_commands_.set_transforms(std::move(edits), "Align Meshes");
}

DocumentCommandResult MeshArrangementService::distribute_selected_meshes(
    MeshArrangementAxis axis, MeshDistributionMode distribution,
    MeshArrangementOptions options) {
  if (!valid_snap_options(options)) {
    return document_command_invalid_request_result(
        "Grid size must be finite and greater than zero.");
  }
  ArrangementSelection selection =
      collect_arrangement_selection(scene_, assets_, selection_context_);
  if (!selection.ok) {
    return document_command_invalid_request_result(selection.message);
  }
  if (selection.roots.size() < 3u) {
    return document_command_invalid_request_result(
        "Select at least three mesh roots to distribute.");
  }

  std::ranges::sort(selection.roots, [axis](const ArrangementTarget &left,
                                            const ArrangementTarget &right) {
    const float left_center = axis_value(bounds_center(left.bounds), axis);
    const float right_center = axis_value(bounds_center(right.bounds), axis);
    if (left_center != right_center) {
      return left_center < right_center;
    }
    return left.entity < right.entity;
  });

  std::vector<DocumentTransformEdit> edits;
  edits.reserve(selection.roots.size() - 2u);
  if (distribution == MeshDistributionMode::Centers) {
    const float first =
        axis_value(bounds_center(selection.roots.front().bounds), axis);
    const float last =
        axis_value(bounds_center(selection.roots.back().bounds), axis);
    const float step =
        (last - first) / static_cast<float>(selection.roots.size() - 1u);
    for (std::size_t index = 1u; index + 1u < selection.roots.size(); ++index) {
      const ArrangementTarget &target = selection.roots[index];
      const float current = axis_value(bounds_center(target.bounds), axis);
      const float delta = snapped_delta(
          first + (step * static_cast<float>(index)) - current, options);
      if (std::abs(delta) > kArrangementEpsilon) {
        std::optional<DocumentTransformEdit> edit =
            translated_edit(scene_, target, axis_delta(axis, delta));
        if (!edit.has_value()) {
          return document_command_invalid_request_result(
              "A mesh parent transform cannot represent the world "
              "translation.");
        }
        edits.push_back(std::move(*edit));
      }
    }
  } else {
    float total_width = 0.0f;
    for (const ArrangementTarget &target : selection.roots) {
      total_width += bounds_width(target.bounds, axis);
    }
    const float span = axis_value(selection.roots.back().bounds.max, axis) -
                       axis_value(selection.roots.front().bounds.min, axis);
    const float gap =
        (span - total_width) / static_cast<float>(selection.roots.size() - 1u);
    float next_min = axis_value(selection.roots.front().bounds.min, axis) +
                     bounds_width(selection.roots.front().bounds, axis) + gap;
    for (std::size_t index = 1u; index + 1u < selection.roots.size(); ++index) {
      const ArrangementTarget &target = selection.roots[index];
      const float delta = snapped_delta(
          next_min - axis_value(target.bounds.min, axis), options);
      if (std::abs(delta) > kArrangementEpsilon) {
        std::optional<DocumentTransformEdit> edit =
            translated_edit(scene_, target, axis_delta(axis, delta));
        if (!edit.has_value()) {
          return document_command_invalid_request_result(
              "A mesh parent transform cannot represent the world "
              "translation.");
        }
        edits.push_back(std::move(*edit));
      }
      next_min += bounds_width(target.bounds, axis) + gap;
    }
  }

  return document_commands_.set_transforms(std::move(edits),
                                           "Distribute Meshes");
}

} // namespace quader
