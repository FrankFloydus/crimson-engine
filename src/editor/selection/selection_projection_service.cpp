////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/selection/selection_projection_service.hpp"

#include "assets/asset_registry.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "math/glm_bridge.hpp"
#include "scene/components.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include <editor/modeling/runtime/session.hpp>
#include <mesh/polygon/document.hpp>

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
    const quader_editor_core::EditorSession &session,
    const RuntimeObjectSelectionProjection &projection) {
  std::vector<int> current_object_ids = session.selected_object_ids();
  std::sort(current_object_ids.begin(), current_object_ids.end());
  std::vector<int> desired_object_ids = projection.object_ids;
  std::sort(desired_object_ids.begin(), desired_object_ids.end());
  return current_object_ids == desired_object_ids &&
         session.active_object_id() == projection.active_object_id;
}

bool apply_runtime_object_selection(
    quader_editor_core::EditorSession &session,
    const RuntimeObjectSelectionProjection &projection,
    bool require_object_mode) {
  bool changed = false;
  if (require_object_mode &&
      session.selection_mode() != quader_editor_core::SelectionMode::Object) {
    session.set_selection_mode(quader_editor_core::SelectionMode::Object);
    changed = true;
  }
  if ((!require_object_mode ||
       session.selection_mode() == quader_editor_core::SelectionMode::Object) &&
      runtime_selection_matches(session, projection)) {
    return changed;
  }
  session.set_selected_object_ids(projection.object_ids,
                                  projection.active_object_id);
  return true;
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

bool apply_exact_scene_selection(SceneDocument &scene,
                                 std::span<const EntityId> selected,
                                 EntityId active) {
  const std::uint64_t revision_before = scene.revision();
  std::unordered_set<EntityId> desired;
  desired.reserve(selected.size());
  std::vector<EntityId> ordered_selected;
  ordered_selected.reserve(selected.size());

  for (const EntityId entity : selected) {
    if (scene.exists(entity) && !desired.contains(entity)) {
      desired.insert(entity);
      ordered_selected.push_back(entity);
    }
  }

  for (const EntityId entity : scene.selected_entities()) {
    if (!desired.contains(entity)) {
      scene.set_selected(entity, false);
    }
  }
  for (const EntityId entity : ordered_selected) {
    const SelectionComponent *selection = scene.selection(entity);
    if (selection == nullptr || !selection->selected) {
      scene.set_selected(entity, true);
    }
  }

  const EntityId resolved_active = resolve_projected_active_selection<EntityId>(
      std::span<const EntityId>(ordered_selected.data(),
                                ordered_selected.size()),
      active, kInvalidEntityId);
  if (resolved_active != kInvalidEntityId) {
    scene.set_active_selection(resolved_active);
  }

  return scene.revision() != revision_before;
}

QVec3 world_point_for_modeling_object(
    int object_id, QVec3 point, const ModelingObjectRuntimeMap &object_registry,
    const SceneDocument &scene) {
  const std::optional<EntityId> entity =
      object_registry.entity_for_modeling_object(object_id);
  if (!entity.has_value()) {
    return point;
  }
  const TransformComponent *transform = scene.transform(*entity);
  if (transform == nullptr) {
    return point;
  }
  return transform_point(scene.world_transform_matrix(*entity), point);
}

} // namespace

bool SelectionProjectionService::should_project_object_selection_from_scene(
    const quader_editor_core::EditorSession &session,
    bool force_object_mode) const {
  return force_object_mode ||
         session.selection_mode() == quader_editor_core::SelectionMode::Object;
}

bool SelectionProjectionService::project_object_selection_from_scene(
    quader_editor_core::EditorSession &session, const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_registry,
    bool force_object_mode) const {
  if (!should_project_object_selection_from_scene(session, force_object_mode)) {
    return false;
  }

  const std::vector<EntityId> selected_entities = scene.selected_entities();
  const RuntimeObjectSelectionProjection projection =
      runtime_object_selection_for_scene_entities(
          selected_entities, scene.primary_selection(), object_registry);
  return apply_runtime_object_selection(session, projection, force_object_mode);
}

RuntimeObjectSelectionProjectionResult
SelectionProjectionService::project_object_selection_for_scene_entities(
    quader_editor_core::EditorSession &session,
    std::span<const EntityId> entities, EntityId active,
    const ModelingObjectRuntimeMap &object_registry) const {
  RuntimeObjectSelectionProjectionResult result;

  const RuntimeObjectSelectionProjection projection =
      runtime_object_selection_for_scene_entities(entities, active,
                                                  object_registry);
  result.has_modeling_objects = projection.has_modeling_objects;
  result.changed = apply_runtime_object_selection(session, projection, true);
  return result;
}

bool SelectionProjectionService::promote_object_selection_for_tool(
    quader_editor_core::EditorSession &session, const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_registry) const {
  const std::vector<EntityId> selected_entities = scene.selected_entities();
  const RuntimeObjectSelectionProjection projection =
      runtime_object_selection_for_scene_entities(
          selected_entities, scene.primary_selection(), object_registry);
  if (projection.has_input_entities) {
    return apply_runtime_object_selection(session, projection, true);
  }

  const std::vector<int> current_object_ids = session.selected_object_ids();
  if (current_object_ids.empty()) {
    return false;
  }
  const int active_object_id = session.active_object_id() > 0
                                   ? session.active_object_id()
                                   : current_object_ids.back();
  if (session.selection_mode() != quader_editor_core::SelectionMode::Object) {
    session.set_selection_mode(quader_editor_core::SelectionMode::Object);
  }
  session.set_selected_object_ids(current_object_ids, active_object_id);
  return true;
}

bool SelectionProjectionService::project_scene_selection_from_modeling(
    const quader_editor_core::EditorSession &session, SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_registry) const {
  const std::vector<int> selected_object_ids = session.selected_object_ids();
  const std::vector<EntityId> selected_entities =
      scene_entities_for_runtime_objects(selected_object_ids, object_registry);
  const std::unordered_set<EntityId> desired_selection(
      selected_entities.begin(), selected_entities.end());
  const EntityId active = active_scene_entity_for_runtime_object(
      session.active_object_id(), desired_selection, object_registry);
  return apply_exact_scene_selection(scene, selected_entities, active);
}

bool SelectionProjectionService::restore_scene_selection(
    SceneDocument &scene, std::span<const EntityId> selected,
    EntityId active) const {
  return apply_exact_scene_selection(scene, selected, active);
}

bool SelectionProjectionService::add_scene_selection(
    SceneDocument &scene, std::span<const EntityId> selected,
    EntityId active) const {
  const std::uint64_t revision_before = scene.revision();
  for (const EntityId entity : selected) {
    if (scene.exists(entity)) {
      scene.set_selected(entity, true);
    }
  }
  if (active != kInvalidEntityId && scene.exists(active)) {
    scene.set_active_selection(active);
  }
  return scene.revision() != revision_before;
}

std::optional<QVec3> SelectionProjectionService::world_selection_center(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, const AssetRegistry &assets,
    const ModelingObjectRuntimeMap &object_registry) const {
  if (session.selection_mode() == quader_editor_core::SelectionMode::Object) {
    if (const std::optional<SceneBounds> bounds =
            selected_world_bounds(scene, assets)) {
      return bounds_center(*bounds);
    }
  }

  std::optional<SceneBounds> bounds;
  for (const quader_editor_core::PolygonObject &object : session.objects()) {
    if (!object.selected || object.selection.empty()) {
      continue;
    }
    const std::optional<EntityId> entity =
        object_registry.entity_for_modeling_object(object.id);
    if (entity.has_value() && !scene.is_effectively_visible(*entity)) {
      continue;
    }

    const std::vector<quader_poly::ElementId> vertex_ids =
        quader_poly::selected_vertex_ids(object.document, object.selection);
    for (const quader_poly::ElementId vertex_id : vertex_ids) {
      const quader_poly::Vertex *vertex =
          quader_poly::find_vertex(object.document, vertex_id);
      if (vertex == nullptr) {
        continue;
      }
      const QVec3 world = world_point_for_modeling_object(
          object.id, vertex->position, object_registry, scene);
      if (!bounds) {
        bounds = SceneBounds{.min = world, .max = world};
      } else {
        include_point(*bounds, world);
      }
    }
  }
  if (bounds) {
    return bounds_center(*bounds);
  }

  const std::optional<quader::QVec3> center = session.selection_center();
  return center ? std::optional<QVec3>{*center} : std::nullopt;
}

} // namespace quader
