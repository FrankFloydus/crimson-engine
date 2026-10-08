////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/selection/selection_projection_service.hpp"

#include "assets/asset_registry.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "math/glm_bridge.hpp"
#include "scene/components.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <quader/modeling/neutral/polygon/document.hpp>

namespace quader {
namespace {

/**
 * Stores runtime object selection projection data derived from scene entities.
 */
struct RuntimeObjectSelectionProjection {
  std::vector<int> object_ids;
  int active_object_id = -1;
  bool has_input_entities = false;
  bool has_modeling_objects = false;
};

template <typename Id>
Id resolve_projected_active_selection(std::span<const Id> selected,
                                      Id requested, Id invalid) {
  if (requested != invalid && std::find(selected.begin(), selected.end(),
                                        requested) != selected.end()) {
    return requested;
  }
  return selected.empty() ? invalid : selected.back();
}

RuntimeObjectSelectionProjection runtime_object_selection_for_scene_entities(
    std::span<const EntityId> entities, EntityId active,
    const ModelingObjectRuntimeMap &object_registry) {
  RuntimeObjectSelectionProjection projection;
  projection.has_input_entities = !entities.empty();
  projection.object_ids.reserve(entities.size());
  for (const EntityId entity : entities) {
    const auto object = object_registry.modeling_object_for_entity(entity);
    if (object.has_value()) {
      projection.object_ids.push_back(*object);
    }
  }
  projection.has_modeling_objects = !projection.object_ids.empty();

  if (active != kInvalidEntityId) {
    const auto object = object_registry.modeling_object_for_entity(active);
    if (object.has_value()) {
      projection.active_object_id = *object;
    }
  }
  projection.active_object_id = resolve_projected_active_selection<int>(
      std::span<const int>(projection.object_ids.data(),
                           projection.object_ids.size()),
      projection.active_object_id, -1);
  return projection;
}

bool runtime_selection_matches(
    const ModelingSelectionProjectionSnapshot &selection,
    const RuntimeObjectSelectionProjection &projection) {
  std::vector<int> current_object_ids = selection.selected_object_ids();
  std::sort(current_object_ids.begin(), current_object_ids.end());
  std::vector<int> desired_object_ids = projection.object_ids;
  std::sort(desired_object_ids.begin(), desired_object_ids.end());
  return current_object_ids == desired_object_ids &&
         selection.active_object_id() == projection.active_object_id;
}

SelectionProjectionEditResult apply_runtime_object_selection(
    const ModelingSelectionProjectionSnapshot &selection,
    const RuntimeObjectSelectionProjection &projection,
    bool require_object_mode) {
  const bool mode_matches =
      !require_object_mode ||
      selection.selection_mode() == quader_editor_core::SelectionMode::Object;
  if (mode_matches && runtime_selection_matches(selection, projection)) {
    return {.changed = false, .selection = selection};
  }

  std::vector<quader_editor_core::PolygonObject> objects(
      selection.objects().begin(), selection.objects().end());
  std::vector<int> selected_ids;
  selected_ids.reserve(projection.object_ids.size());
  for (quader_editor_core::PolygonObject &object : objects) {
    object.selection = quader_poly::Selection{};
    object.component_edit_target = false;
    object.selected = std::find(projection.object_ids.begin(),
                                projection.object_ids.end(), object.id) !=
                      projection.object_ids.end();
    if (object.selected) {
      selected_ids.push_back(object.id);
    }
  }
  const int active_object_id = resolve_projected_active_selection<int>(
      selected_ids, projection.active_object_id, -1);
  return {
      .changed = true,
      .selection = selection.with_selection(
          std::move(objects), quader_editor_core::SelectionMode::Object,
          active_object_id, selection.selection_revision() + 1U),
  };
}

std::vector<EntityId> scene_entities_for_runtime_objects(
    std::span<const int> object_ids,
    const ModelingObjectRuntimeMap &object_registry) {
  std::vector<EntityId> entities;
  entities.reserve(object_ids.size());
  for (const int object_id : object_ids) {
    const std::optional<EntityId> entity =
        object_registry.entity_for_modeling_object(object_id);
    if (entity.has_value()) {
      entities.push_back(*entity);
    }
  }
  return entities;
}

EntityId active_scene_entity_for_runtime_object(
    int active_object_id, const std::unordered_set<EntityId> &desired_selection,
    const ModelingObjectRuntimeMap &object_registry) {
  const std::optional<EntityId> active_entity =
      object_registry.entity_for_modeling_object(active_object_id);
  if (active_entity.has_value() && desired_selection.contains(*active_entity)) {
    return *active_entity;
  }
  return kInvalidEntityId;
}

} // namespace

bool SelectionProjectionService::should_project_object_selection_from_scene(
    const ModelingSelectionProjectionSnapshot &selection,
    bool force_object_mode) const {
  return force_object_mode ||
         selection.selection_mode() ==
             quader_editor_core::SelectionMode::Object;
}

SelectionProjectionEditResult
SelectionProjectionService::project_object_selection_from_scene(
    const ModelingSelectionProjectionSnapshot &selection,
    const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_registry,
    bool force_object_mode) const {
  if (!should_project_object_selection_from_scene(selection,
                                                  force_object_mode)) {
    return {.changed = false, .selection = selection};
  }

  const std::vector<EntityId> selected_entities = scene.selected_entities();
  const RuntimeObjectSelectionProjection projection =
      runtime_object_selection_for_scene_entities(
          selected_entities, scene.primary_selection(), object_registry);
  return apply_runtime_object_selection(selection, projection,
                                        force_object_mode);
}

RuntimeObjectSelectionProjectionResult
SelectionProjectionService::project_object_selection_for_scene_entities(
    const ModelingSelectionProjectionSnapshot &selection,
    std::span<const EntityId> entities, EntityId active,
    const ModelingObjectRuntimeMap &object_registry) const {
  RuntimeObjectSelectionProjectionResult result;

  const RuntimeObjectSelectionProjection projection =
      runtime_object_selection_for_scene_entities(entities, active,
                                                  object_registry);
  result.has_modeling_objects = projection.has_modeling_objects;
  SelectionProjectionEditResult edit =
      apply_runtime_object_selection(selection, projection, true);
  result.changed = edit.changed;
  result.selection = std::move(edit.selection);
  return result;
}

SelectionProjectionEditResult
SelectionProjectionService::promote_object_selection_for_tool(
    const ModelingSelectionProjectionSnapshot &selection,
    const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_registry) const {
  const std::vector<EntityId> selected_entities = scene.selected_entities();
  const RuntimeObjectSelectionProjection projection =
      runtime_object_selection_for_scene_entities(
          selected_entities, scene.primary_selection(), object_registry);
  if (projection.has_input_entities) {
    return apply_runtime_object_selection(selection, projection, true);
  }

  const std::vector<int> current_object_ids = selection.selected_object_ids();
  if (current_object_ids.empty()) {
    return {.changed = false, .selection = selection};
  }
  const int active_object_id = selection.active_object_id() > 0
                                   ? selection.active_object_id()
                                   : current_object_ids.back();
  return apply_runtime_object_selection(
      selection,
      {.object_ids = current_object_ids,
       .active_object_id = active_object_id,
       .has_modeling_objects = true},
      true);
}

bool SelectionProjectionService::project_scene_selection_from_modeling(
    const ModelingSelectionProjectionSnapshot &selection,
    SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_registry) const {
  const std::vector<int> selected_object_ids = selection.selected_object_ids();
  const std::vector<EntityId> selected_entities =
      scene_entities_for_runtime_objects(selected_object_ids, object_registry);
  const std::unordered_set<EntityId> desired_selection(
      selected_entities.begin(), selected_entities.end());
  const EntityId active = active_scene_entity_for_runtime_object(
      selection.active_object_id(), desired_selection, object_registry);
  return apply_scene_selection(
      scene, make_scene_selection(scene, selected_entities, active),
      SceneSelectionApplicationMode::RuntimeSync);
}

bool SelectionProjectionService::restore_scene_selection(
    SceneDocument &scene, std::span<const EntityId> selected,
    EntityId active) const {
  return apply_scene_selection(scene, make_scene_selection(scene, selected, active),
                               SceneSelectionApplicationMode::RuntimeSync);
}

bool SelectionProjectionService::add_scene_selection(
    SceneDocument &scene, std::span<const EntityId> selected,
    EntityId active) const {
  return quader::add_scene_selection(
      scene, selected, active, SceneSelectionApplicationMode::RuntimeSync);
}

std::optional<QVec3> SelectionProjectionService::world_selection_center(
    const ModelingSelectionProjectionSnapshot &selection,
    const SceneDocument &scene, const AssetRegistry &assets,
    const ModelingObjectRuntimeMap &object_registry) const {
  if (const std::optional<SceneBounds> bounds =
          world_selection_bounds(selection, scene, assets, object_registry)) {
    return bounds_center(*bounds);
  }

  const std::optional<quader::QVec3> center = selection.selection_center();
  return center ? std::optional<QVec3>{*center} : std::nullopt;
}

std::optional<SceneBounds> SelectionProjectionService::world_selection_bounds(
    const ModelingSelectionProjectionSnapshot &selection,
    const SceneDocument &scene, const AssetRegistry &assets,
    const ModelingObjectRuntimeMap &object_registry,
    SelectionBoundsWorkStats *work_stats) const {
  if (work_stats != nullptr) {
    *work_stats = {};
  }
  if (selection.selection_mode() ==
      quader_editor_core::SelectionMode::Object) {
    if (const std::optional<SceneBounds> bounds =
            selected_world_bounds(scene, assets)) {
      return bounds;
    }
  }

  const auto world_transform = [&](int object_id) -> std::optional<QMat4> {
    const std::optional<EntityId> entity =
        object_registry.entity_for_modeling_object(object_id);
    if (entity.has_value() && !scene.is_effectively_visible(*entity)) {
      return std::nullopt;
    }
    std::optional<QMat4> world_from_object{QMat4{}};
    if (entity.has_value() && scene.transform(*entity) != nullptr) {
      world_from_object = scene.world_transform_matrix(*entity);
      if (work_stats != nullptr) {
        ++work_stats->world_transform_evaluation_count;
      }
    }
    return world_from_object;
  };
  if (const auto bounds = component_selection_bounds(selection, world_transform,
                                                     {}, work_stats)) {
    return bounds;
  }
  const quader_editor_core::Bounds3 runtime_bounds = selection.selection_bounds();
  if (!runtime_bounds.empty) {
    return SceneBounds{.min = runtime_bounds.min, .max = runtime_bounds.max};
  }
  return std::nullopt;
}

std::optional<SceneBounds> SelectionProjectionService::component_selection_bounds(
    const ModelingSelectionProjectionSnapshot &selection,
    const std::function<std::optional<QMat4>(int)> &world_transform,
    const std::function<QVec3(QVec3)> &reference_position,
    SelectionBoundsWorkStats *work_stats) const {
  if (work_stats != nullptr) {
    *work_stats = {};
  }

  std::optional<SceneBounds> bounds;
  for (const quader_editor_core::PolygonObject &object : selection.objects()) {
    if (!object.selected || object.selection.empty()) {
      continue;
    }
    if (work_stats != nullptr) {
      ++work_stats->selected_object_visit_count;
    }
    const std::optional<QMat4> world_from_object = world_transform(object.id);
    if (!world_from_object.has_value()) {
      continue;
    }

    const std::vector<quader_poly::ElementId> vertex_ids =
        quader_poly::selected_vertex_ids(object.document, object.selection);
    if (vertex_ids.empty()) {
      continue;
    }
    std::unordered_map<quader_poly::ElementId, const quader_poly::Vertex *>
        vertices_by_id;
    vertices_by_id.reserve(object.document.vertices.size());
    for (const quader_poly::Vertex &vertex : object.document.vertices) {
      if (work_stats != nullptr) {
        ++work_stats->vertex_index_visit_count;
      }
      // Preserve find_vertex's first-record behavior for malformed duplicate
      // IDs while making every selected-ID lookup constant expected time.
      vertices_by_id.emplace(vertex.id, &vertex);
    }

    for (const quader_poly::ElementId vertex_id : vertex_ids) {
      if (work_stats != nullptr) {
        ++work_stats->selected_vertex_lookup_count;
      }
      const auto vertex = vertices_by_id.find(vertex_id);
      if (vertex == vertices_by_id.end() || vertex->second == nullptr) {
        continue;
      }
      const QVec3 world =
          transform_point(*world_from_object, vertex->second->position);
      const QVec3 point = reference_position ? reference_position(world) : world;
      if (work_stats != nullptr) {
        ++work_stats->included_point_count;
      }
      if (!bounds) {
        bounds = SceneBounds{.min = point, .max = point};
      } else {
        include_point(*bounds, point);
      }
    }
  }
  return bounds;
}

} // namespace quader
