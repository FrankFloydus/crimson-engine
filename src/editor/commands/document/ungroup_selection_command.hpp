////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/commands/document/scene_command.hpp"

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
    explicit UngroupSelectionCommand(SceneDocument& scene)
        : SceneCommand(scene, "Ungroup Selection") {}

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
          remapped_entities[group.snapshot.original_id] = restored_group;
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
          group.snapshot.original_id = restored_group;
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

      before_selection_ = document_command_capture_selection(scene());
      groups_.clear();
      std::vector<EntityId> selected_children;
      for (const EntityId group : targets) {
        if (!is_ungroupable(group)) {
          continue;
        }

        std::optional<DocumentEntitySnapshot> snapshot =
            document_command_capture_entity_snapshot(scene(), group);
        if (!snapshot.has_value()) {
          return document_command_missing_entity_result();
        }
        const EntityId parent = scene().parent(group);
        const std::vector<EntityId> children = scene().children(group);
        UngroupedEntity state{
            .snapshot = *snapshot,
            .parent = parent,
            .children = document_command_capture_placements(scene(), children),
        };

        for (const EntityId child : children) {
          if (!scene().set_parent(child, parent, true)) {
            return document_command_invalid_request_result(
                "Entity cannot be reparented");
          }
          selected_children.push_back(child);
        }
        if (!scene().destroy_entity(group)) {
          return document_command_missing_entity_result();
        }
        groups_.push_back(std::move(state));
      }

        if (groups_.empty()) {
          return document_command_unchanged_result();
        }

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
    };

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
