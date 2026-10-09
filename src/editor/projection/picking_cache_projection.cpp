////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/picking_cache_projection.hpp"

#include "diagnostics/profile.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_mesh_occurrence_projection.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_map>

namespace quader {
namespace {

constexpr std::uint64_t kPickGeometryHashSeed = 1469598103934665603ull;
constexpr std::uint64_t kPickGeometryHashPrime = 1099511628211ull;

std::uint64_t mix_pick_geometry_hash(std::uint64_t hash, std::uint64_t value) {
  return (hash ^ value) * kPickGeometryHashPrime;
}

std::uint64_t mix_pick_geometry_hash(std::uint64_t hash, int value) {
  return mix_pick_geometry_hash(
      hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

std::uint64_t mix_pick_geometry_hash(std::uint64_t hash, std::uint32_t value) {
  return mix_pick_geometry_hash(hash, static_cast<std::uint64_t>(value));
}

std::uint64_t mix_pick_geometry_hash(std::uint64_t hash, bool value) {
  return mix_pick_geometry_hash(hash, value ? 1ull : 0ull);
}

std::uint64_t mix_pick_geometry_hash(std::uint64_t hash, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return mix_pick_geometry_hash(hash, bits);
}

std::uint64_t mix_pick_geometry_hash(std::uint64_t hash, quader::QVec3 value) {
  hash = mix_pick_geometry_hash(hash, value.x);
  hash = mix_pick_geometry_hash(hash, value.y);
  hash = mix_pick_geometry_hash(hash, value.z);
  return hash;
}

void include_pick_bounds_point(quader_editor_core::PickObjectBounds &bounds,
                               quader::QVec3 point) {
  if (bounds.empty) {
    bounds.min = point;
    bounds.max = point;
    bounds.empty = false;
    return;
  }
  bounds.min.x = std::min(bounds.min.x, point.x);
  bounds.min.y = std::min(bounds.min.y, point.y);
  bounds.min.z = std::min(bounds.min.z, point.z);
  bounds.max.x = std::max(bounds.max.x, point.x);
  bounds.max.y = std::max(bounds.max.y, point.y);
  bounds.max.z = std::max(bounds.max.z, point.z);
}

bool append_picking_participant(
    ModelingPickingProjection &projection,
    quader_editor_core::PolygonObject object,
    const QMat4 &world_from_object) {
  const std::optional<QMat4> object_from_world =
      try_scene_matrix_inverse(world_from_object);
  if (!object_from_world.has_value()) {
    return false;
  }
  const int object_id = object.id;
  projection.objects.push_back(std::move(object));
  projection.object_transforms.push_back({
      .object_id = object_id,
      .world_from_object = world_from_object,
      .object_from_world = *object_from_world,
  });
  return true;
}

} // namespace

ModelingPickingProjection
PickingCacheProjection::scene_transformed_objects_for_picking(
    std::span<const quader_editor_core::PolygonObject> objects,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_map,
    ModelingInstanceEditScope instance_edit_scope,
    ModelingSelectionMode app_selection_mode,
    const std::function<int(EntityId)> &instance_participant_id,
    const std::function<bool(EntityId)> &entity_visible) {
  QDR_PROFILE_FUNCTION();
  ModelingPickingProjection projection;
  projection.objects.reserve(objects.size());
  projection.object_transforms.reserve(objects.size());

  const std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
      occurrences = scene.mesh_occurrence_projection();
  std::unordered_map<EntityId, const SceneMeshOccurrenceGeometryGroup *>
      formal_group_by_geometry;
  formal_group_by_geometry.reserve(
      occurrences->formal_geometry_groups().size());
  for (const SceneMeshOccurrenceGeometryGroup &group :
       occurrences->formal_geometry_groups()) {
    formal_group_by_geometry.emplace(group.geometry_entity, &group);
  }

  const bool edit_scope_valid =
      instance_edit_scope.active() &&
      (!entity_visible || entity_visible(instance_edit_scope.occurrence)) &&
      scene.is_instance_definition_entity(instance_edit_scope.definition);

  for (const quader_editor_core::PolygonObject &source : objects) {
    const std::optional<EntityId> entity =
        object_map.entity_for_modeling_object(source.id);
    if (!entity.has_value()) {
      continue;
    }

    const EntityId definition =
        scene.containing_instance_definition(*entity);
    const bool definition_member =
        definition != kInvalidEntityId && definition != *entity;

    if (instance_edit_scope.active()) {
      if (!edit_scope_valid || !definition_member ||
          definition != instance_edit_scope.definition ||
          (entity_visible && !entity_visible(*entity))) {
        continue;
      }
      const SceneMeshOccurrenceProjectionRecord *occurrence = occurrences->find(
          {.display_entity = instance_edit_scope.occurrence,
           .definition_entity = definition,
           .geometry_entity = *entity});
      if (occurrence == nullptr || !occurrence->selectable ||
          !occurrence->effectively_selectable) {
        continue;
      }
      quader_editor_core::PolygonObject object = source;
      (void)append_picking_participant(projection, std::move(object),
                                       occurrence->world_from_geometry);
      continue;
    }

    if (definition_member) {
      // Formal occurrences remain atomic scene targets in both scene-owned
      // selection modes. Group mode promotes the winning occurrence only
      // after picking; component modes must not expose definition topology
      // outside instance edit.
      const bool scene_selection_mode =
          app_selection_mode == ModelingSelectionMode::Object ||
          app_selection_mode == ModelingSelectionMode::Group;
      if (!scene_selection_mode || !instance_participant_id) {
        continue;
      }
      const auto group = formal_group_by_geometry.find(*entity);
      if (group == formal_group_by_geometry.end() ||
          group->second->definition_entity != definition) {
        continue;
      }
      for (const std::size_t participant_index :
           group->second->participant_indices) {
        const SceneMeshOccurrenceProjectionRecord *occurrence =
            occurrences->record(participant_index);
        if (occurrence == nullptr || !occurrence->selectable ||
            !occurrence->effectively_selectable ||
            (entity_visible &&
             !entity_visible(occurrence->display_entity))) {
          continue;
        }
        const int participant_id =
            instance_participant_id(occurrence->display_entity);
        if (participant_id <= 0) {
          continue;
        }
        quader_editor_core::PolygonObject object = source;
        object.id = participant_id;
        object.selected =
            scene.selection(occurrence->display_entity) != nullptr &&
            scene.selection(occurrence->display_entity)->selected;
        object.component_edit_target = false;
        object.selection = {};
        if (append_picking_participant(projection, std::move(object),
                                       occurrence->world_from_geometry)) {
          projection.scene_target_by_participant.insert_or_assign(
              participant_id, occurrence->display_entity);
        }
      }
      continue;
    }

    const SceneMeshOccurrenceProjectionRecord *occurrence = occurrences->find(
        {.display_entity = *entity,
         .definition_entity = kInvalidEntityId,
         .geometry_entity = *entity});
    if (occurrence == nullptr ||
        (entity_visible && !entity_visible(*entity)) ||
        !occurrence->selectable || !occurrence->effectively_selectable) {
      continue;
    }
    quader_editor_core::PolygonObject object = source;
    (void)append_picking_participant(
        projection, std::move(object), occurrence->world_from_geometry);
  }
  return projection;
}

quader_editor_core::PickObjectBounds
PickingCacheProjection::build_object_pick_bounds(
    const quader_editor_core::PolygonObject &object) {
  QDR_PROFILE_FUNCTION();
  quader_editor_core::PickObjectBounds bounds;
  bounds.object_id = object.id;
  for (const quader_poly::Vertex &vertex : object.document.vertices) {
    include_pick_bounds_point(bounds, vertex.position);
  }
  return bounds;
}

quader_editor_core::PickObjectBounds
PickingCacheProjection::transform_object_pick_bounds(
    const quader_editor_core::PickObjectBounds &local_bounds,
    int participant_id, const QMat4 &world_from_object) {
  quader_editor_core::PickObjectBounds world_bounds;
  world_bounds.object_id = participant_id;
  if (local_bounds.empty) {
    return world_bounds;
  }
  const std::array<QVec3, 8> corners{{
      {local_bounds.min.x, local_bounds.min.y, local_bounds.min.z},
      {local_bounds.max.x, local_bounds.min.y, local_bounds.min.z},
      {local_bounds.min.x, local_bounds.max.y, local_bounds.min.z},
      {local_bounds.max.x, local_bounds.max.y, local_bounds.min.z},
      {local_bounds.min.x, local_bounds.min.y, local_bounds.max.z},
      {local_bounds.max.x, local_bounds.min.y, local_bounds.max.z},
      {local_bounds.min.x, local_bounds.max.y, local_bounds.max.z},
      {local_bounds.max.x, local_bounds.max.y, local_bounds.max.z},
  }};
  for (const QVec3 corner : corners) {
    include_pick_bounds_point(
        world_bounds, transform_point(world_from_object, corner));
  }
  return world_bounds;
}

std::uint64_t PickingCacheProjection::local_face_geometry_signature(
    const quader_editor_core::PolygonObject &object,
    quader_poly::PickOptions pick_options) {
  QDR_PROFILE_FUNCTION();
  std::uint64_t hash = kPickGeometryHashSeed;
  hash = mix_pick_geometry_hash(hash, pick_options.has_hidden_material_slot);
  hash = mix_pick_geometry_hash(hash, pick_options.hidden_material_slot);
  hash = mix_pick_geometry_hash(
      hash, static_cast<std::uint64_t>(object.document.vertices.size()));
  hash = mix_pick_geometry_hash(
      hash, static_cast<std::uint64_t>(object.document.faces.size()));
  for (const quader_poly::Vertex &vertex : object.document.vertices) {
    hash = mix_pick_geometry_hash(hash, vertex.id);
    hash = mix_pick_geometry_hash(hash, vertex.position);
  }
  for (const quader_poly::Face &face : object.document.faces) {
    hash = mix_pick_geometry_hash(hash, face.id);
    hash = mix_pick_geometry_hash(hash, face.material_slot);
    hash = mix_pick_geometry_hash(
        hash, static_cast<std::uint64_t>(face.vertices.size()));
    for (const quader_poly::ElementId vertex_id : face.vertices) {
      hash = mix_pick_geometry_hash(hash, vertex_id);
    }
  }
  return hash;
}

std::uint64_t PickingCacheProjection::object_face_geometry_signature(
    const quader_editor_core::PolygonObject &object,
    quader_poly::PickOptions pick_options) {
  QDR_PROFILE_FUNCTION();
  std::uint64_t hash = mix_pick_geometry_hash(kPickGeometryHashSeed, object.id);
  hash = mix_pick_geometry_hash(hash, static_cast<int>(pick_options.mode));
  hash = mix_pick_geometry_hash(hash, pick_options.vertex_radius);
  hash = mix_pick_geometry_hash(hash, pick_options.edge_radius);
  hash = mix_pick_geometry_hash(hash, pick_options.pick_backfaces);
  hash = mix_pick_geometry_hash(hash,
                                pick_options.prefer_front_facing_faces);
  return mix_pick_geometry_hash(
      hash, local_face_geometry_signature(object, pick_options));
}

} // namespace quader
