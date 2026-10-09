////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/transform/mesh_arrangement_service.hpp"

#include "assets/asset_registry.hpp"
#include "core/release_features.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/metadata/host_contract.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_mesh_occurrence_projection.hpp"
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

/// Captures immutable pre-operation state for one topmost selected spatial root.
struct ArrangementTarget {
  EntityId entity = kInvalidEntityId;
  SceneBounds bounds;
  TransformComponent transform;
};

/// Stores validated roots with immutable pre-operation world bounds.
struct ArrangementSelection {
  bool ok = false;
  std::string message;
  std::vector<ArrangementTarget> roots;
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

bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

bool valid_bounds(const SceneBounds &bounds) {
  return finite_vector(bounds.min) && finite_vector(bounds.max) &&
         bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y &&
         bounds.min.z <= bounds.max.z;
}

bool has_selected_object_ancestor(
    const SceneDocument &scene, EntityId entity,
    const std::unordered_set<EntityId> &selected_objects) {
  EntityId parent = scene.parent(entity);
  while (parent != kInvalidEntityId) {
    if (selected_objects.contains(parent)) {
      return true;
    }
    parent = scene.parent(parent);
  }
  return false;
}

bool has_arrangement_geometry(const SceneDocument &scene,
                              const AssetRegistry &assets, EntityId entity) {
  const auto valid_mesh = [&scene, &assets](EntityId geometry_entity) {
    const MeshComponent *component = scene.mesh(geometry_entity);
    const MeshAsset *mesh =
        component != nullptr ? assets.mesh(component->mesh) : nullptr;
    return mesh != nullptr && !mesh->geometry_vertices().empty();
  };
  if (scene.instance_occurrence(entity) != nullptr) {
    const auto projection = scene.mesh_occurrence_projection();
    const auto records = projection->records_for_display(entity);
    return !records.empty() &&
           std::ranges::all_of(records, [&valid_mesh](const auto &record) {
             return valid_mesh(record.geometry_entity);
           });
  }
  return scene.mesh(entity) == nullptr || valid_mesh(entity);
}

bool is_arrangement_container(const SceneDocument &scene, EntityId entity) {
  return scene.has_children(entity) && scene.mesh(entity) == nullptr &&
         scene.curve(entity) == nullptr && scene.light(entity) == nullptr &&
         scene.instance_occurrence(entity) == nullptr;
}

std::optional<SceneBounds> arrangement_world_bounds(
    const SceneDocument &scene, const AssetRegistry &assets, EntityId entity) {
  // Mesh roots retain their own geometry bounds. Container extents include
  // spatial leaves such as lights, without treating group pivots as geometry.
  if (!is_arrangement_container(scene, entity)) {
    return entity_world_bounds(scene, assets, entity);
  }
  std::optional<SceneBounds> result;
  for (const EntityId descendant : scene.descendants(entity, false)) {
    if (is_arrangement_container(scene, descendant)) {
      continue;
    }
    const auto bounds = entity_world_bounds(scene, assets, descendant);
    if (!bounds.has_value()) {
      continue;
    }
    if (result.has_value()) {
      include_point(*result, bounds->min);
      include_point(*result, bounds->max);
    } else {
      result = bounds;
    }
  }
  return result.has_value() ? result : entity_world_bounds(scene, assets, entity);
}

ArrangementSelection
collect_arrangement_selection(const SceneDocument &scene,
                              const AssetRegistry &assets,
                              MeshArrangementSelectionContext context) {
  QDR_PROFILE_SCOPE("Mesh arrangement collect selection");
  if (!context.object_mode) {
    return {.message = "Object arrangement only works in object mode."};
  }

  std::unordered_set<EntityId> selected_objects;
  {
    QDR_PROFILE_SCOPE("Mesh arrangement collect selected objects");
    for (const EntityId entity : scene.selected_entities()) {
      if (scene.transform(entity) != nullptr) {
        selected_objects.insert(entity);
      }
    }
  }
  if (selected_objects.empty()) {
    return {.message = "Select objects before arranging them."};
  }

  for (const EntityId entity : selected_objects) {
    if (!scene.is_effectively_selectable(entity)) {
      return {.message = "Non-selectable objects cannot be arranged."};
    }
    if (!scene.is_effectively_visible(entity) ||
        !has_arrangement_geometry(scene, assets, entity)) {
      return {.message =
                  "Every arranged object must be visible and have valid bounds."};
    }
  }

  ArrangementSelection selection;
  selection.roots.reserve(selected_objects.size());
  for (const EntityId entity : selected_objects) {
    if (has_selected_object_ancestor(scene, entity, selected_objects)) {
      continue;
    }
    const TransformComponent *transform = scene.transform(entity);
    std::optional<SceneBounds> bounds;
    {
      QDR_PROFILE_SCOPE("Mesh arrangement entity world bounds");
      bounds = arrangement_world_bounds(scene, assets, entity);
    }
    if (transform == nullptr || !bounds.has_value() || !valid_bounds(*bounds)) {
      return {.message =
                  "Every arranged object must be visible and have valid bounds."};
    }
    selection.roots.push_back({
        .entity = entity,
        .bounds = *bounds,
        .transform = *transform,
    });
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

  std::unordered_set<EntityId> selected_objects;
  for (const EntityId entity : scene.selected_entities()) {
    if (scene.transform(entity) == nullptr) {
      continue;
    }
    if (!scene.is_effectively_selectable(entity) ||
        !scene.is_effectively_visible(entity) ||
        !has_arrangement_geometry(scene, assets, entity)) {
      return {};
    }
    selected_objects.insert(entity);
  }
  if (selected_objects.empty()) {
    return {};
  }

  std::size_t root_count = 0u;
  for (const EntityId entity : selected_objects) {
    if (!has_selected_object_ancestor(scene, entity, selected_objects)) {
      ++root_count;
    }
  }
  return {
      .can_align = root_count >= 2u,
      .can_distribute = root_count >= 3u,
  };
}

SceneBounds arrangement_selection_bounds(
    const MeshArrangementBaseline &baseline) {
  SceneBounds bounds = baseline.entries.front().bounds;
  for (const MeshArrangementBaselineEntry &entry : baseline.entries) {
    include_point(bounds, entry.bounds.min);
    include_point(bounds, entry.bounds.max);
  }
  return bounds;
}

MeshAlignmentMode alignment_mode(MeshArrangementMode mode) {
  switch (mode) {
  case MeshArrangementMode::Minimum:
    return MeshAlignmentMode::Minimum;
  case MeshArrangementMode::Maximum:
    return MeshAlignmentMode::Maximum;
  default:
    return MeshAlignmentMode::Center;
  }
}

MeshArrangementSnapRequest arrangement_snap_request(bool enabled) {
  MeshArrangementSnapRequest request{.enabled = enabled};
  if (enabled) {
    for (const auto &preset : quader_host_contract::grid_preset_catalog()) {
      request.candidate_grid_sizes.push_back(preset.world_units);
    }
  }
  return request;
}

bool quantized_movement_preserves_plan(std::span<const QVec3> world_deltas,
                                      double grid_size) {
  // Original alignment references and fixed-boundary equal gaps uniquely
  // determine every world delta. A changed quantized delta would change one of
  // those constraints. Compare at the float precision used for publication.
  for (const QVec3 delta : world_deltas) {
    for (const float value : {delta.x, delta.y, delta.z}) {
      const double snapped =
          std::round(static_cast<double>(value) / grid_size) * grid_size;
      if (!std::isfinite(snapped) || static_cast<float>(snapped) != value) {
        return false;
      }
    }
  }
  return true;
}

} // namespace

std::string_view mesh_arrangement_grid_alignment_warning() {
  return "No suitable grid size was found. Grid alignment was not used.";
}

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
  if (!release_has_feature(ReleaseFeature::Arrangement)) {
    return {};
  }
  // UI command predicates only need a cheap structural preflight. The command
  // path below still computes and validates exact transformed geometry bounds
  // immediately before applying edits.
  return collect_arrangement_availability(scene_, assets_, selection_context_);
}

MeshArrangementBaseline MeshArrangementService::capture_baseline() const {
  if (!release_has_feature(ReleaseFeature::Arrangement)) {
    return {.message = release_feature_unavailable_message()};
  }
  const ArrangementSelection selection =
      collect_arrangement_selection(scene_, assets_, selection_context_);
  if (!selection.ok) {
    return {.message = selection.message};
  }
  if (selection.roots.size() < 2u) {
    return {.message = "Select at least two object roots to arrange."};
  }

  MeshArrangementBaseline baseline;
  baseline.entries.reserve(selection.roots.size());
  for (const ArrangementTarget &target : selection.roots) {
    const EntityId parent = scene_.parent(target.entity);
    const QMat4 parent_world = parent == kInvalidEntityId
                                   ? scene_transform_identity_matrix()
                                   : scene_.world_transform_matrix(parent);
    const auto channel_from_world = try_scene_matrix_inverse(
        scene_compensated_parent_matrix(parent_world, target.transform));
    if (!channel_from_world.has_value()) {
      return {.message =
                  "An object parent transform cannot represent the world translation."};
    }
    baseline.entries.push_back({
        .entity = target.entity,
        .bounds = target.bounds,
        .transform = target.transform,
        .channel_from_world = *channel_from_world,
    });
  }
  baseline.ok = true;
  return baseline;
}

MeshArrangementTransformPlan MeshArrangementService::plan(
    const MeshArrangementBaseline &baseline, MeshArrangementIntent intent,
    const MeshArrangementSnapRequest &snapping) {
  if (!baseline.ok) {
    return {.message = baseline.message};
  }
  if (baseline.entries.size() < 2u) {
    return {.message = "Select at least two object roots to arrange."};
  }
  const SceneBounds selection_bounds = arrangement_selection_bounds(baseline);
  std::vector<QVec3> world_deltas(baseline.entries.size());
  constexpr std::array kAxes{MeshArrangementAxis::X, MeshArrangementAxis::Y,
                             MeshArrangementAxis::Z};
  for (std::size_t axis_index = 0; axis_index < kAxes.size(); ++axis_index) {
    const MeshArrangementAxis axis = kAxes[axis_index];
    const MeshArrangementMode mode = intent.axes[axis_index];
    if (mode == MeshArrangementMode::None) {
      continue;
    }
    if (mode == MeshArrangementMode::Minimum ||
        mode == MeshArrangementMode::Center ||
        mode == MeshArrangementMode::Maximum) {
      const MeshAlignmentMode alignment = alignment_mode(mode);
      const float reference =
          bounds_coordinate(selection_bounds, axis, alignment);
      for (std::size_t index = 0; index < baseline.entries.size(); ++index) {
        const float delta = reference - bounds_coordinate(
            baseline.entries[index].bounds, axis, alignment);
        world_deltas[index] += axis_delta(axis, delta);
      }
      continue;
    }
    if (mode != MeshArrangementMode::Distribute) {
      return {.message = "Unknown object arrangement mode."};
    }
    if (baseline.entries.size() < 3u) {
      return {.message = "Select at least three object roots to distribute."};
    }

    std::vector<std::size_t> ordered;
    ordered.reserve(baseline.entries.size());
    double total_width = 0.0;
    for (std::size_t index = 0; index < baseline.entries.size(); ++index) {
      ordered.push_back(index);
      const SceneBounds &bounds = baseline.entries[index].bounds;
      total_width += static_cast<double>(axis_value(bounds.max, axis)) -
                     axis_value(bounds.min, axis);
    }
    std::ranges::sort(ordered, [&baseline, axis](std::size_t left_index,
                                               std::size_t right_index) {
      const auto &left = baseline.entries[left_index];
      const auto &right = baseline.entries[right_index];
      const float left_center = axis_value(bounds_center(left.bounds), axis);
      const float right_center = axis_value(bounds_center(right.bounds), axis);
      if (left_center != right_center) {
        return left_center < right_center;
      }
      return left.entity < right.entity;
    });

    const double selection_min = axis_value(selection_bounds.min, axis);
    const double selection_max = axis_value(selection_bounds.max, axis);
    const double gap = (selection_max - selection_min - total_width) /
                       static_cast<double>(baseline.entries.size() - 1u);
    double next_min = selection_min;
    for (const std::size_t index : ordered) {
      const SceneBounds &bounds = baseline.entries[index].bounds;
      const double target_min = axis_value(bounds.min, axis);
      const double target_max = axis_value(bounds.max, axis);
      const double width = target_max - target_min;
      const double next_max = next_min + width;
      const bool moves_outer_min =
          std::abs(target_min - selection_min) <= kArrangementEpsilon &&
          std::abs(next_min - target_min) > kArrangementEpsilon;
      const bool moves_outer_max =
          std::abs(target_max - selection_max) <= kArrangementEpsilon &&
          std::abs(next_max - target_max) > kArrangementEpsilon;
      if (next_min < selection_min - kArrangementEpsilon ||
          next_max > selection_max + kArrangementEpsilon || moves_outer_min ||
          moves_outer_max) {
        return {.message =
                    "Overlapping objects cannot retain equal spacing within their current bounds."};
      }
      world_deltas[index] +=
          axis_delta(axis, static_cast<float>(next_min - target_min));
      next_min += width + gap;
    }
  }

  MeshArrangementTransformPlan result;
  result.snapping_requested = snapping.enabled;
  if (snapping.enabled) {
    std::vector<double> candidates = snapping.candidate_grid_sizes;
    std::erase_if(candidates, [](double spacing) {
      return !std::isfinite(spacing) || spacing <= 0.0;
    });
    std::ranges::sort(candidates, [](double left, double right) {
      return left > right;
    });
    for (const double grid_size : candidates) {
      if (quantized_movement_preserves_plan(world_deltas, grid_size)) {
        result.used_grid_alignment = true;
        result.snap_grid_size = grid_size;
        break;
      }
    }
    result.grid_alignment_unavailable = !result.used_grid_alignment;
  }
  result.edits.reserve(baseline.entries.size());
  for (std::size_t index = 0; index < baseline.entries.size(); ++index) {
    const auto &entry = baseline.entries[index];
    const QVec3 world_delta = world_deltas[index];
    if (std::abs(world_delta.x) <= kArrangementEpsilon &&
        std::abs(world_delta.y) <= kArrangementEpsilon &&
        std::abs(world_delta.z) <= kArrangementEpsilon) {
      continue;
    }
    const QVec3 local_delta =
        transform_vector(entry.channel_from_world, world_delta);
    if (!finite_vector(local_delta)) {
      return {.message =
                  "An object parent transform cannot represent the world translation."};
    }
    TransformComponent after = entry.transform;
    after.location += local_delta;
    if (!scene_transform_is_finite(after)) {
      return {.message = "Object arrangement would produce an invalid transform."};
    }
    result.edits.push_back({
        .entity = entry.entity,
        .before = entry.transform,
        .after = after,
    });
  }
  result.ok = true;
  return result;
}

DocumentCommandResult
MeshArrangementService::align_selected_meshes(MeshArrangementAxis axis,
                                             MeshAlignmentMode alignment,
                                             MeshArrangementOptions options) {
  const auto axis_index = static_cast<std::size_t>(axis);
  if (axis_index >= 3u) {
    return document_command_invalid_request_result(
        "Object arrangement needs the X, Y, or Z axis.");
  }
  const MeshArrangementBaseline baseline = capture_baseline();
  MeshArrangementIntent intent;
  MeshArrangementMode mode = MeshArrangementMode::Center;
  if (alignment == MeshAlignmentMode::Minimum) {
    mode = MeshArrangementMode::Minimum;
  } else if (alignment == MeshAlignmentMode::Maximum) {
    mode = MeshArrangementMode::Maximum;
  }
  intent.axes[axis_index] = mode;
  MeshArrangementTransformPlan transforms = plan(
      baseline, intent, arrangement_snap_request(options.snap_to_grid));
  if (!transforms.ok) {
    return document_command_invalid_request_result(transforms.message);
  }
  auto result = document_commands_.set_transforms(std::move(transforms.edits),
                                                 "Align Meshes");
  if (result.ok && transforms.grid_alignment_unavailable) {
    result.message = mesh_arrangement_grid_alignment_warning();
  }
  return result;
}

DocumentCommandResult MeshArrangementService::distribute_selected_meshes(
    MeshArrangementAxis axis, MeshDistributionMode,
    MeshArrangementOptions options) {
  const auto axis_index = static_cast<std::size_t>(axis);
  if (axis_index >= 3u) {
    return document_command_invalid_request_result(
        "Object arrangement needs the X, Y, or Z axis.");
  }
  const MeshArrangementBaseline baseline = capture_baseline();
  MeshArrangementIntent intent;
  intent.axes[axis_index] = MeshArrangementMode::Distribute;
  MeshArrangementTransformPlan transforms = plan(
      baseline, intent, arrangement_snap_request(options.snap_to_grid));
  if (!transforms.ok) {
    return document_command_invalid_request_result(transforms.message);
  }
  auto result = document_commands_.set_transforms(std::move(transforms.edits),
                                                 "Distribute Meshes");
  if (result.ok && transforms.grid_alignment_unavailable) {
    result.message = mesh_arrangement_grid_alignment_warning();
  }
  return result;
}

} // namespace quader
