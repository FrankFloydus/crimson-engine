////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/commands/document/scene_command.hpp"
#include "scene/scene_transform.hpp"

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {

/**
 * Applies Ungroup Selection Command behavior as a command in the editor document command layer.
 */
class UngroupSelectionCommand final : public SceneCommand {
public:
    explicit UngroupSelectionCommand(SceneDocument &scene)
        : SceneCommand(scene, "Ungroup Selection") {}

    [[nodiscard]] DocumentCommandResult preflight() const {
      const std::vector<EntityId> targets =
          current_groups_.empty()
              ? selected_ungroupable_roots()
              : document_command_normalize_existing_entities(scene(),
                                                             current_groups_);
      if (targets.empty()) {
        return document_command_unchanged_result();
      }
      const auto planned_groups = preflight_ungroup(targets);
      if (!planned_groups.has_value()) {
        return document_command_invalid_request_result(
            "Grouped entities cannot preserve their world transforms when "
            "ungrouped");
      }
      return planned_groups->empty() ? document_command_unchanged_result()
                                     : document_command_changed_result();
    }

    DocumentCommandResult undo() override {
        if (groups_.empty()) {
          return document_command_missing_entity_result();
        }

        std::unordered_map<EntityId, EntityId> remapped_entities;
        std::vector<EntityId> restored_groups;
        restored_groups.reserve(groups_.size());
        for (UngroupedEntity& group : groups_) {
          const EntityId parent =
              group.parent != kInvalidEntityId && scene().exists(group.parent)
                  ? group.parent
                  : kInvalidEntityId;
          const EntityId restored_group =
              document_command_create_entity_from_snapshot(
                  scene(), group.snapshot, parent, group.snapshot.name, true,
                  true);
          if (restored_group == kInvalidEntityId) {
            return document_command_missing_entity_result();
          }
          remapped_entities[group.snapshot.id] = restored_group;
          for (const DocumentEntityPlacement &child : group.children) {
            if (!scene().exists(child.entity)) {
              return document_command_missing_entity_result();
            }
            if (!scene().set_parent(child.entity, restored_group, false)) {
              return document_command_invalid_request_result(
                  "Entity cannot be reparented");
            }
            scene().set_transform(child.entity, child.transform);
          }
          group.snapshot.id = restored_group;
          restored_groups.push_back(restored_group);
        }
        document_command_apply_selection(
            scene(), document_command_remap_selection(before_selection_,
                                                      remapped_entities));
        current_groups_ = std::move(restored_groups);
        return document_command_changed_result();
    }

    DocumentCommandResult redo() override {
      const std::vector<EntityId> targets =
          current_groups_.empty()
              ? selected_ungroupable_roots()
              : document_command_normalize_existing_entities(scene(),
                                                             current_groups_);
      if (targets.empty()) {
        return document_command_unchanged_result();
      }

      std::optional<std::vector<UngroupedEntity>> planned_groups =
          preflight_ungroup(targets);
      if (!planned_groups.has_value()) {
        return document_command_invalid_request_result(
            "Grouped entities cannot preserve their world transforms when "
            "ungrouped");
      }
      if (planned_groups->empty()) {
        return document_command_unchanged_result();
      }
      const DocumentSelectionState before_selection =
          document_command_capture_selection(scene());
      std::vector<EntityId> selected_children;
      std::vector<SceneParentAssignment> parent_assignments;
      std::vector<SceneTransformAssignment> transform_assignments;
      std::vector<EntityId> group_entities;
      for (const UngroupedEntity &state : *planned_groups) {
        group_entities.push_back(state.snapshot.id);
        for (const DocumentEntityPlacement &child : state.children) {
          parent_assignments.push_back({
              .child = child.entity,
              .parent = state.parent,
          });
          selected_children.push_back(child.entity);
        }
        transform_assignments.insert(transform_assignments.end(),
                                     state.ungrouped_transforms.begin(),
                                     state.ungrouped_transforms.end());
      }

      if (scene().set_parents(parent_assignments)) {
        scene().set_transforms(transform_assignments);
        // Enroll group selection removal in the forward compensation scope;
        // restoring the selected group on a failed publication writes it back.
        for (const EntityId group : group_entities) {
          (void)scene().set_selected(group, false);
        }
        if (!scene().destroy_entities(group_entities)) {
          return document_command_missing_entity_result();
        }
      } else {
        for (const UngroupedEntity &state : *planned_groups) {
          const EntityId group = state.snapshot.id;
          for (const DocumentEntityPlacement &child : state.children) {
            if (!scene().set_parent(child.entity, state.parent, true)) {
              return document_command_invalid_request_result(
                  "Entity cannot be reparented");
            }
          }
          (void)scene().set_selected(group, false);
          if (!scene().destroy_entity(group)) {
            return document_command_missing_entity_result();
          }
        }
      }

        before_selection_ = before_selection;
        groups_ = std::move(*planned_groups);
        current_groups_.clear();
        document_command_select_only(scene(), selected_children);
        return document_command_changed_result();
    }

private:
    /**
     * Represents an Ungrouped Entity value used by the editor document command layer.
     */
    struct UngroupedEntity {
        DocumentEntitySnapshot snapshot;
        EntityId parent = kInvalidEntityId;
        std::vector<DocumentEntityPlacement> children;
        std::vector<SceneTransformAssignment> ungrouped_transforms;
    };

    std::optional<std::vector<UngroupedEntity>>
    preflight_ungroup(const std::vector<EntityId> &targets) const {
      std::vector<UngroupedEntity> result;
      result.reserve(targets.size());
      for (const EntityId group : targets) {
        if (!is_ungroupable(group)) {
          continue;
        }

        const std::optional<DocumentEntitySnapshot> snapshot =
            document_command_capture_entity_snapshot(scene(), group);
        if (!snapshot.has_value()) {
          return std::nullopt;
        }
        const EntityId parent = scene().parent(group);
        if (parent != kInvalidEntityId && !scene().exists(parent)) {
          return std::nullopt;
        }
        const QMat4 parent_world =
            parent == kInvalidEntityId
                ? scene_transform_identity_matrix()
                : scene().world_transform_matrix(parent);
        const std::vector<EntityId> children = scene().children(group);
        std::vector<DocumentEntityPlacement> child_placements =
            document_command_capture_placements(scene(), children);
        if (child_placements.size() != children.size()) {
          return std::nullopt;
        }
        std::vector<SceneTransformAssignment> ungrouped_transforms;
        ungrouped_transforms.reserve(children.size());
        for (const EntityId child : children) {
          const TransformComponent *transform = scene().transform(child);
          const std::optional<TransformComponent> local =
              transform == nullptr
                  ? std::nullopt
                  : try_scene_local_transform_preserving_world(
                        parent_world, scene().world_transform_matrix(child),
                        *transform);
          if (!local.has_value()) {
            return std::nullopt;
          }
          ungrouped_transforms.push_back({
              .entity = child,
              .transform = *local,
          });
        }
        result.push_back(UngroupedEntity{
            .snapshot = *snapshot,
            .parent = parent,
            .children = std::move(child_placements),
            .ungrouped_transforms = std::move(ungrouped_transforms),
        });
      }
      return result;
    }

    bool is_ungroupable(EntityId entity) const {
      return scene().exists(entity) && !scene().is_scene_root(entity) &&
             scene().kind(entity) == EntityKind::Empty &&
             scene().has_children(entity);
    }

    std::vector<EntityId> selected_ungroupable_roots() const {
      std::vector<EntityId> roots;
      for (const EntityId entity :
           document_command_selected_root_entities(scene())) {
        if (is_ungroupable(entity)) {
          roots.push_back(entity);
        }
      }
      return roots;
    }

    std::vector<UngroupedEntity> groups_;
    std::vector<EntityId> current_groups_;
    DocumentSelectionState before_selection_;
};

} // namespace quader
