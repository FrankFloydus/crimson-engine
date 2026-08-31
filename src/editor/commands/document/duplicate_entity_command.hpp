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
#include <vector>

namespace quader {

/**
 * Applies Duplicate Entity Command behavior as a command in the editor document command layer.
 */
class DuplicateEntityCommand final : public SceneCommand {
public:
  DuplicateEntityCommand(SceneDocument &scene, EntityId source,
                         bool select_duplicate = true)
      : SceneCommand(scene, "Duplicate Entity"), source_(source),
        select_duplicate_(select_duplicate) {}

  [[nodiscard]] EntityId duplicate_entity() const {
    return duplicate_roots_.empty() ? kInvalidEntityId
                                    : duplicate_roots_.front();
  }

    DocumentCommandResult undo() override {
      if (duplicate_roots_.empty()) {
        return document_command_missing_entity_result();
      }
      for (auto it = duplicate_roots_.rbegin(); it != duplicate_roots_.rend();
           ++it) {
        if (scene().exists(*it) && !scene().destroy_entity(*it)) {
          return document_command_missing_entity_result();
        }
      }
      duplicate_roots_.clear();
      if (select_duplicate_) {
        document_command_apply_selection(scene(), before_selection_);
      }
      return document_command_changed_result();
    }

    DocumentCommandResult redo() override {
      before_selection_ = document_command_capture_selection(scene());
      duplicate_roots_.clear();
      EntityId duplicate = kInvalidEntityId;
      if (duplicate_snapshot_.has_value()) {
        std::unordered_map<EntityId, EntityId> remapped_entities;
        const EntityId parent =
            duplicate_snapshot_->parent != kInvalidEntityId &&
                    scene().exists(duplicate_snapshot_->parent)
                ? duplicate_snapshot_->parent
                : kInvalidEntityId;
        duplicate = document_command_restore_entity_snapshot_tree(
            scene(), *duplicate_snapshot_, parent, remapped_entities, false);
      } else {
        if (source_ == kInvalidEntityId || !scene().exists(source_)) {
          return document_command_missing_entity_result();
        }
        const std::optional<DocumentEntitySnapshot> snapshot =
            document_command_capture_entity_snapshot(scene(), source_);
        if (!snapshot.has_value()) {
          return document_command_missing_entity_result();
        }
        duplicate = document_command_duplicate_entity_snapshot_tree(
            scene(), *snapshot, scene().parent(source_));
        if (duplicate != kInvalidEntityId) {
          duplicate_snapshot_ =
              document_command_capture_entity_snapshot(scene(), duplicate);
        }
      }
      if (duplicate == kInvalidEntityId) {
        return document_command_missing_entity_result();
      }
      duplicate_roots_.push_back(duplicate);
      if (select_duplicate_) {
        document_command_select_only(scene(), duplicate_roots_);
      }
      return document_command_changed_result();
    }

private:
  EntityId source_ = kInvalidEntityId;
  bool select_duplicate_ = true;
  std::vector<EntityId> duplicate_roots_;
  std::optional<DocumentEntitySnapshot> duplicate_snapshot_;
  DocumentSelectionState before_selection_;
};

} // namespace quader
