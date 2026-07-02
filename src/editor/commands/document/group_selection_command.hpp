////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/commands/document/scene_command.hpp"

#include <string>
#include <utility>
#include <vector>

namespace quader {

/**
 * Applies Group Selection Command behavior as a command in the editor document command layer.
 */
class GroupSelectionCommand final : public SceneCommand {
public:
  GroupSelectionCommand(
      SceneDocument &scene, std::string group_name = {},
      TransformComponent group_transform = TransformComponent{})
      : SceneCommand(scene, "Group Selection"),
        group_name_(std::move(group_name)), group_transform_(group_transform) {}

  [[nodiscard]] EntityId group_entity() const { return group_; }

  DocumentCommandResult undo() override {
    if (group_ == kInvalidEntityId || !scene().exists(group_)) {
      return document_command_missing_entity_result();
    }

    for (const DocumentEntityPlacement &placement : root_placements_) {
      DocumentCommandResult result =
          document_command_apply_placement(scene(), placement);
      if (!result.ok) {
        return result;
      }
    }
    if (!scene().destroy_entity(group_)) {
      return document_command_missing_entity_result();
    }
    document_command_apply_selection(scene(), before_selection_);
    return document_command_changed_result();
  }

    DocumentCommandResult redo() override {
      if (!captured_roots_) {
        roots_ = document_command_selected_root_entities(scene());
        captured_roots_ = true;
      }
      roots_ = document_command_normalize_existing_entities(scene(), roots_);
      if (roots_.empty()) {
        return document_command_unchanged_result();
      }

      before_selection_ = document_command_capture_selection(scene());
      root_placements_ = document_command_capture_placements(scene(), roots_);
      const EntityId common_parent =
          document_command_common_parent(scene(), roots_);
      const std::string name =
          group_name_.empty() ? std::string("Group") : group_name_;
      group_ = scene().create_entity_with_id(group_, name, EntityKind::Empty,
                                             group_object_id_);
      if (group_ == kInvalidEntityId) {
        return document_command_invalid_request_result(
            "Group entity cannot be created");
      }
      if (!group_uuid_.empty() &&
          !scene().set_entity_uuid(group_, group_uuid_)) {
        scene().destroy_entity(group_);
        group_ = kInvalidEntityId;
        return document_command_invalid_request_result(
            "Group entity identity cannot be restored");
      }
      if (group_uuid_.empty()) {
        group_uuid_ = scene().uuid(group_);
      }
      group_object_id_ = scene().editor_object_id(group_);
      scene().set_transform(group_, group_transform_);
      if (common_parent != kInvalidEntityId &&
          !scene().set_parent(group_, common_parent, true)) {
        scene().destroy_entity(group_);
        return document_command_invalid_request_result(
            "Group entity cannot be reparented");
      }
        for (const EntityId root : roots_) {
          if (!scene().set_parent(root, group_, true)) {
            return document_command_invalid_request_result(
                "Selected entity cannot be grouped");
          }
        }
        document_command_select_only(scene(), {group_});
        return document_command_changed_result();
    }

private:
  std::string group_name_;
  TransformComponent group_transform_;
  bool captured_roots_ = false;
  std::vector<EntityId> roots_;
  std::vector<DocumentEntityPlacement> root_placements_;
  DocumentSelectionState before_selection_;
  EntityId group_ = kInvalidEntityId;
  std::string group_uuid_;
  EditorObjectId group_object_id_ = kInvalidEditorObjectId;
};

} // namespace quader
