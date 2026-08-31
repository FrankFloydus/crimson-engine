////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/scene_mesh_occurrence_projection.hpp"

#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_transform.hpp"

#include <optional>
#include <utility>

namespace quader {
namespace {

std::size_t hash_combine(std::size_t seed, EntityId value) noexcept {
  constexpr std::size_t kMix =
      sizeof(std::size_t) >= sizeof(std::uint64_t)
          ? static_cast<std::size_t>(0x9e3779b97f4a7c15ULL)
          : static_cast<std::size_t>(0x9e3779b9UL);
  return seed ^ (std::hash<EntityId>{}(value) + kMix + (seed << 6u) +
                 (seed >> 2u));
}

bool scene_hierarchy_segment_is_locked(
    const SceneDocument &document, EntityId entity,
    EntityId exclusive_ancestor) {
  if (!document.exists(entity) || !document.exists(exclusive_ancestor)) {
    return true;
  }
  EntityId current = entity;
  while (current != kInvalidEntityId && current != exclusive_ancestor) {
    if (const LockComponent *lock = document.lock(current);
        lock != nullptr && lock->locked) {
      return true;
    }
    const SceneLayerId layer_id = document.entity_layer(current);
    if (layer_id != kInvalidSceneLayerId) {
      const SceneLayerRecord *layer = document.layer(layer_id);
      if (layer == nullptr || layer->locked) {
        return true;
      }
    }
    current = document.parent(current);
  }
  return current != exclusive_ancestor;
}

struct DefinitionMemberProjection final {
  EntityId entity = kInvalidEntityId;
  QMat4 definition_from_geometry;
  bool selectable = true;
  bool effectively_locked = false;
  std::size_t geometry_group_index = 0u;
};

struct DefinitionProjection final {
  EntityId entity = kInvalidEntityId;
  std::vector<DefinitionMemberProjection> members;
  std::vector<EntityId> visible_occurrences;
};

} // namespace

std::size_t SceneMeshOccurrenceProjectionSnapshot::IdentityHash::operator()(
    const SceneMeshOccurrenceIdentity &identity) const noexcept {
  std::size_t hash = hash_combine(0u, identity.display_entity);
  hash = hash_combine(hash, identity.definition_entity);
  return hash_combine(hash, identity.geometry_entity);
}

std::span<const SceneMeshOccurrenceProjectionRecord>
SceneMeshOccurrenceProjectionSnapshot::records_for_display(
    EntityId display_entity) const noexcept {
  const auto found = display_ranges_.find(display_entity);
  if (found == display_ranges_.end() ||
      found->second.begin > records_.size() ||
      found->second.count > records_.size() - found->second.begin) {
    return {};
  }
  return {records_.data() + found->second.begin, found->second.count};
}

const SceneMeshOccurrenceProjectionRecord *
SceneMeshOccurrenceProjectionSnapshot::find(
    SceneMeshOccurrenceIdentity identity) const noexcept {
  const auto found = index_by_identity_.find(identity);
  return found != index_by_identity_.end() ? record(found->second) : nullptr;
}

const SceneMeshOccurrenceProjectionRecord *
SceneMeshOccurrenceProjectionSnapshot::record(std::size_t index) const
    noexcept {
  return index < records_.size() ? &records_[index] : nullptr;
}

std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
SceneDocument::mesh_occurrence_projection() const {
  const std::shared_ptr<const SceneHierarchyProjectionSnapshot> hierarchy =
      hierarchy_projection();
  (void)hierarchy;
  const MeshOccurrenceProjectionCacheKey key{
      .restore_epoch = spatial_projection_restore_epoch_,
      .hierarchy_build_count =
          spatial_projection_cache_stats_.hierarchy_build_count,
      .visibility_revision = visibility_revision_,
      .mesh_binding_revision = mesh_binding_revision_,
  };
  if (mesh_occurrence_projection_cache_ != nullptr &&
      mesh_occurrence_projection_cache_key_ == key) {
    ++spatial_projection_cache_stats_.mesh_occurrence_cache_hit_count;
    return mesh_occurrence_projection_cache_;
  }

  SceneMeshOccurrenceProjectionWorkStats work;
  mesh_occurrence_projection_cache_ =
      std::make_shared<SceneMeshOccurrenceProjectionSnapshot>(
          build_scene_mesh_occurrence_projection(*this, &work));
  mesh_occurrence_projection_cache_key_ = key;
  ++spatial_projection_cache_stats_.mesh_occurrence_build_count;
  return mesh_occurrence_projection_cache_;
}

SceneMeshOccurrenceProjectionSnapshot build_scene_mesh_occurrence_projection(
    const SceneDocument &document,
    SceneMeshOccurrenceProjectionWorkStats *work_stats) {
  SceneMeshOccurrenceProjectionWorkStats work;
  SceneMeshOccurrenceProjectionSnapshot snapshot;
  const std::vector<EntityId> entities = document.entities();
  const std::shared_ptr<const SceneHierarchyProjectionSnapshot> hierarchy =
      document.hierarchy_projection();

  std::vector<DefinitionProjection> definitions;
  std::unordered_map<EntityId, std::size_t> definition_by_occurrence;
  definitions.reserve(entities.size());
  definition_by_occurrence.reserve(entities.size());

  for (const EntityId definition : entities) {
    if (!document.is_instance_definition_entity(definition)) {
      continue;
    }
    const SceneHierarchyProjectionRecord *definition_state =
        hierarchy->find(definition);
    if (definition_state == nullptr) {
      continue;
    }
    const std::optional<QMat4> definition_from_world =
        try_scene_matrix_inverse(definition_state->world_transform);
    if (!definition_from_world.has_value()) {
      continue;
    }

    DefinitionProjection projected_definition{.entity = definition};
    for (const EntityId member : document.descendants(definition, false)) {
      if (document.containing_instance_definition(member) != definition ||
          document.mesh(member) == nullptr ||
          !scene_hierarchy_segment_is_visible(document, member, definition)) {
        continue;
      }
      const SceneHierarchyProjectionRecord *member_state =
          hierarchy->find(member);
      if (member_state == nullptr) {
        continue;
      }
      const std::size_t group_index =
          snapshot.formal_geometry_groups_.size();
      const QMat4 definition_from_geometry = scene_transform_multiply(
          *definition_from_world, member_state->world_transform);
      snapshot.formal_geometry_groups_.push_back({
          .definition_entity = definition,
          .geometry_entity = member,
          .definition_from_geometry = definition_from_geometry,
      });
      projected_definition.members.push_back({
          .entity = member,
          .definition_from_geometry = definition_from_geometry,
          .selectable =
              scene_hierarchy_segment_is_selectable_ignoring_visibility(
                  document, member, definition),
          .effectively_locked = scene_hierarchy_segment_is_locked(
              document, member, definition),
          .geometry_group_index = group_index,
      });
    }
    if (projected_definition.members.empty()) {
      continue;
    }

    for (const EntityId occurrence :
         document.instance_occurrences_for_definition(definition)) {
      const SceneHierarchyProjectionRecord *occurrence_state =
          hierarchy->find(occurrence);
      if (occurrence_state == nullptr ||
          !occurrence_state->effectively_visible) {
        continue;
      }
      projected_definition.visible_occurrences.push_back(occurrence);
    }
    if (projected_definition.visible_occurrences.empty()) {
      snapshot.formal_geometry_groups_.resize(
          snapshot.formal_geometry_groups_.size() -
          projected_definition.members.size());
      continue;
    }

    const std::size_t definition_index = definitions.size();
    for (const EntityId occurrence :
         projected_definition.visible_occurrences) {
      definition_by_occurrence.emplace(occurrence, definition_index);
    }
    definitions.push_back(std::move(projected_definition));
  }

  snapshot.records_.reserve(entities.size());
  snapshot.ordinary_placement_indices_.reserve(entities.size());
  snapshot.display_ranges_.reserve(entities.size());
  snapshot.index_by_identity_.reserve(entities.size());

  const auto append_record = [&](SceneMeshOccurrenceProjectionRecord record) {
    const std::size_t index = snapshot.records_.size();
    const EntityId display = record.display_entity;
    snapshot.index_by_identity_.emplace(record.identity(), index);
    snapshot.records_.push_back(std::move(record));
    auto [range, inserted] = snapshot.display_ranges_.try_emplace(
        display, SceneMeshOccurrenceProjectionSnapshot::DisplayRange{
                     .begin = index,
                 });
    if (!inserted && range->second.begin + range->second.count != index) {
      return index;
    }
    ++range->second.count;
    return index;
  };

  for (const EntityId entity : entities) {
    const auto formal = definition_by_occurrence.find(entity);
    if (formal != definition_by_occurrence.end()) {
      const DefinitionProjection &definition = definitions[formal->second];
      const SceneHierarchyProjectionRecord *occurrence_state =
          hierarchy->find(entity);
      if (occurrence_state == nullptr) {
        continue;
      }
      const bool occurrence_selectable =
          scene_hierarchy_segment_is_selectable_ignoring_visibility(document,
                                                                     entity);
      for (const DefinitionMemberProjection &member : definition.members) {
        const std::size_t record_index = append_record({
            .display_entity = entity,
            .geometry_entity = member.entity,
            .definition_entity = definition.entity,
            .world_from_geometry = scene_transform_multiply(
                occurrence_state->world_transform,
                member.definition_from_geometry),
            .selectable = occurrence_selectable && member.selectable,
            .effectively_locked = occurrence_state->effectively_locked ||
                                  member.effectively_locked,
        });
        snapshot.formal_geometry_groups_[member.geometry_group_index]
            .participant_indices.push_back(record_index);
        ++work.formal_placement_count;
        ++work.world_transform_multiply_count;
      }
      continue;
    }

    const SceneHierarchyProjectionRecord *state = hierarchy->find(entity);
    if (state == nullptr || !state->effectively_visible ||
        document.mesh(entity) == nullptr ||
        document.is_instance_definition_entity(entity) ||
        document.is_instance_definition_member(entity) ||
        document.instance_occurrence(entity) != nullptr) {
      continue;
    }
    snapshot.ordinary_placement_indices_.push_back(append_record({
        .display_entity = entity,
        .geometry_entity = entity,
        .world_from_geometry = state->world_transform,
        .selectable =
            scene_hierarchy_segment_is_selectable_ignoring_visibility(document,
                                                                       entity),
        .effectively_locked = state->effectively_locked,
    }));
    ++work.ordinary_placement_count;
  }

  for (const DefinitionProjection &definition : definitions) {
    for (const EntityId occurrence : definition.visible_occurrences) {
      for (const DefinitionMemberProjection &member : definition.members) {
        const SceneMeshOccurrenceProjectionRecord *record = snapshot.find({
            .display_entity = occurrence,
            .definition_entity = definition.entity,
            .geometry_entity = member.entity,
        });
        if (record != nullptr) {
          snapshot.formal_placement_indices_.push_back(
              static_cast<std::size_t>(record - snapshot.records_.data()));
        }
      }
    }
  }

  work.formal_geometry_group_count =
      snapshot.formal_geometry_groups_.size();
  if (work_stats != nullptr) {
    *work_stats = work;
  }
  return snapshot;
}

} // namespace quader
