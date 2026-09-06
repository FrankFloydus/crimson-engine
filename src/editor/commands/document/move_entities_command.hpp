////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/scene_command.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {

/** Moves an authored block of hierarchy roots as one atomic history entry. */
class MoveEntitiesCommand final : public SceneCommand {
public:
  MoveEntitiesCommand(SceneDocument &scene, std::vector<EntityId> roots,
                      EntityId parent, EntityId before_sibling)
      : SceneCommand(scene, "Move Entities"), roots_(std::move(roots)),
        parent_(parent), before_sibling_(before_sibling) {}

  DocumentCommandResult undo() override {
    return apply(before_, before_layers_);
  }

  DocumentCommandResult redo() override {
    if (!prepared_) {
      const DocumentCommandResult prepared = prepare();
      if (!prepared.ok || !prepared.changed) {
        return prepared;
      }
      prepared_ = true;
    }
    return apply(after_, after_layers_);
  }

private:
  /** Retains only placement and local transform for a moved root. */
  struct Placement {
    EntityId entity = kInvalidEntityId;
    EntityId parent = kInvalidEntityId;
    std::size_t ordinal = 0u;
    TransformComponent transform;
  };

  DocumentCommandResult prepare() {
    const std::unordered_set<EntityId> moved(roots_.begin(), roots_.end());
    const std::vector<EntityId> siblings = scene().children(parent_);
    std::size_t insertion = 0u;
    for (const EntityId sibling : siblings) {
      if (sibling == before_sibling_) {
        break;
      }
      if (!moved.contains(sibling)) {
        ++insertion;
      }
    }

    bool changed = false;
    const QMat4 parent_world = scene().world_transform_matrix(parent_);
    const EntityId target_definition =
        scene().containing_instance_definition(parent_);
    for (std::size_t index = 0u; index < roots_.size(); ++index) {
      const EntityId entity = roots_[index];
      const TransformComponent *transform = scene().transform(entity);
      const std::optional<std::size_t> ordinal =
          scene().sibling_ordinal(entity);
      if (transform == nullptr || !ordinal.has_value()) {
        return document_command_invalid_request_result(
            "Moved entity has no authored hierarchy placement");
      }
      const EntityId previous_parent = scene().parent(entity);
      const EntityId previous_definition =
          scene().containing_instance_definition(entity);
      // The scene owner permits extracting live-definition members only from
      // its top level. Reject an insertion whose inverse would violate that
      // invariant, instead of committing an entry that undo cannot replay.
      if (target_definition != kInvalidEntityId &&
          target_definition != previous_definition &&
          parent_ != target_definition &&
          !scene().instance_occurrences_for_definition(target_definition).empty()) {
        return document_command_invalid_request_result(
            "Content cannot be inserted into a nested live instance group");
      }
      const std::optional<TransformComponent> local =
          previous_parent == parent_
              ? std::optional<TransformComponent>(*transform)
              : try_scene_local_transform_preserving_world(
                    parent_world, scene().world_transform_matrix(entity),
                    *transform);
      if (!local.has_value()) {
        return document_command_invalid_request_result(
            "World transform cannot be preserved at the destination");
      }
      before_.push_back({entity, previous_parent, *ordinal, *transform});
      after_.push_back({entity, parent_, insertion + index, *local});
      changed = changed || previous_parent != parent_ ||
                *ordinal != insertion + index;

      const bool removes_layer =
          target_definition != kInvalidEntityId &&
          previous_definition != target_definition;
      for (const EntityId descendant : scene().descendants(entity, true)) {
        participants_.push_back(descendant);
        const SceneLayerId layer = scene().entity_layer(descendant);
        before_layers_.push_back({descendant, layer});
        after_layers_.push_back(
            {descendant, removes_layer ? kInvalidSceneLayerId : layer});
      }
    }
    return changed ? document_command_changed_result()
                   : document_command_unchanged_result();
  }

  DocumentCommandResult apply(
      const std::vector<Placement> &placements,
      const std::vector<SceneLayerAssignment> &layers) {
    // Checkpoints live only for this transition. Ordinary undo/redo uses the
    // scene mutators and publishes fresh revisions; only failure rewinds them.
    const SceneModelingProjectionCheckpoint checkpoint =
        scene().capture_modeling_projection_checkpoint(
            participants_, SceneModelingProjectionDomain::Structure |
                               SceneModelingProjectionDomain::Selection |
                               SceneModelingProjectionDomain::MeshBindings |
                               SceneModelingProjectionDomain::CurveBindings);
    if (!checkpoint.valid()) {
      return document_command_invalid_request_result(
          "Hierarchy move cannot capture its participants");
    }
    const auto fail = [&]() {
      return document_command_invalid_request_result(
          scene().restore_modeling_projection_checkpoint(checkpoint)
              ? "Entities cannot be moved to the requested hierarchy"
              : "Hierarchy move failed and could not restore its participants");
    };
    for (const Placement &placement : placements) {
      if (!scene().set_parent(placement.entity, placement.parent, false) ||
          !scene().set_transform(placement.entity, placement.transform)) {
        return fail();
      }
    }

    // First park only the moved roots at the end. Inserting them by authored
    // ordinal then preserves every untouched sibling, including scattered
    // original positions on undo, without mutating unrelated entities.
    for (const Placement &placement : placements) {
      const HierarchyComponent *parent = scene().hierarchy(placement.parent);
      if (parent == nullptr || parent->children.empty() ||
          !scene().set_sibling_ordinal(placement.entity,
                                       parent->children.size() - 1u)) {
        return fail();
      }
    }
    std::vector<Placement> ordered = placements;
    std::ranges::sort(ordered, [](const Placement &left, const Placement &right) {
      return left.parent != right.parent ? left.parent < right.parent
                                         : left.ordinal < right.ordinal;
    });
    for (const Placement &placement : ordered) {
      if (!scene().set_sibling_ordinal(placement.entity, placement.ordinal)) {
        return fail();
      }
    }
    const bool layers_changed = std::ranges::any_of(
        layers, [this](const SceneLayerAssignment &assignment) {
          return scene().entity_layer(assignment.entity) != assignment.layer_id;
        });
    if (layers_changed) {
      const std::unordered_set<EntityId> participants(participants_.begin(),
                                                     participants_.end());
      SceneLayerState state = scene().layer_state();
      std::erase_if(state.assignments,
                    [&participants](const SceneLayerAssignment &assignment) {
                      return participants.contains(assignment.entity);
                    });
      for (const SceneLayerAssignment &assignment : layers) {
        if (assignment.layer_id != kInvalidSceneLayerId) {
          state.assignments.push_back(assignment);
        }
      }
      // This owner operation validates all memberships before publishing.
      // Keep it last: its scene-wide layer revision publication cannot be
      // compensated by the bounded hierarchy participant checkpoint.
      if (!scene().replace_layer_state(std::move(state))) {
        return fail();
      }
    }
    return document_command_changed_result();
  }

  std::vector<EntityId> roots_;
  EntityId parent_ = kInvalidEntityId;
  EntityId before_sibling_ = kInvalidEntityId;
  bool prepared_ = false;
  std::vector<Placement> before_;
  std::vector<Placement> after_;
  std::vector<EntityId> participants_;
  std::vector<SceneLayerAssignment> before_layers_;
  std::vector<SceneLayerAssignment> after_layers_;
};

} // namespace quader
