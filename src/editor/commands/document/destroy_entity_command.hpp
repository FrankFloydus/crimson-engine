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

namespace quader {

/**
 * Applies Destroy Entity Command behavior as a command in the editor document command layer.
 */
class DestroyEntityCommand final : public SceneCommand {
public:
    DestroyEntityCommand(SceneDocument& scene, EntityId entity)
        : SceneCommand(scene, "Destroy Entity")
        , entity_(entity) {}

    [[nodiscard]] EntityId entity() const { return entity_; }

    DocumentCommandResult undo() override {
        if (!snapshot_.has_value()) {
          return document_command_missing_entity_result();
        }

        std::unordered_map<EntityId, EntityId> remapped_entities;
        if (orphan_definition_snapshot_.has_value()) {
          const EntityId definition_parent =
              orphan_definition_snapshot_->original_parent !=
                          kInvalidEntityId &&
                      scene().exists(
                          orphan_definition_snapshot_->original_parent)
                  ? orphan_definition_snapshot_->original_parent
                  : kInvalidEntityId;
          definition_ = document_command_restore_entity_snapshot_tree(
              scene(), *orphan_definition_snapshot_, definition_parent,
              remapped_entities);
          if (definition_ == kInvalidEntityId || !scene().exists(definition_)) {
            return document_command_missing_entity_result();
          }
        }
        const EntityId parent =
            snapshot_->original_parent != kInvalidEntityId &&
                    scene().exists(snapshot_->original_parent)
                ? snapshot_->original_parent
                : kInvalidEntityId;
        entity_ = document_command_restore_entity_snapshot_tree(
            scene(), *snapshot_, parent, remapped_entities);
        if (entity_ == kInvalidEntityId || !scene().exists(entity_)) {
          if (definition_ != kInvalidEntityId && scene().exists(definition_)) {
            (void)scene().destroy_entity(definition_);
            definition_ = kInvalidEntityId;
          }
          return document_command_missing_entity_result();
        }
        document_command_apply_selection(
            scene(), document_command_remap_selection(before_selection_,
                                                      remapped_entities));
        return document_command_changed_result();
    }

    DocumentCommandResult redo() override {
      if (entity_ == kInvalidEntityId || !scene().exists(entity_)) {
        return document_command_missing_entity_result();
      }

      before_selection_ = document_command_capture_selection(scene());
      snapshot_ = document_command_capture_entity_snapshot(scene(), entity_);
      if (!snapshot_.has_value()) {
        return document_command_missing_entity_result();
      }
      orphan_definition_snapshot_.reset();
      definition_ = kInvalidEntityId;
      if (const InstanceOccurrenceComponent *occurrence =
              scene().instance_occurrence(entity_);
          occurrence != nullptr &&
          scene().instance_occurrences_for_definition(occurrence->definition)
                  .size() == 1u) {
        definition_ = occurrence->definition;
        orphan_definition_snapshot_ =
            document_command_capture_entity_snapshot(scene(), definition_);
        if (!orphan_definition_snapshot_.has_value()) {
          return document_command_missing_entity_result();
        }
      }
      if (!scene().destroy_entity(entity_)) {
        return document_command_missing_entity_result();
      }
      if (definition_ != kInvalidEntityId && scene().exists(definition_) &&
          !scene().destroy_entity(definition_)) {
        return document_command_missing_entity_result();
      }
      entity_ = kInvalidEntityId;
      definition_ = kInvalidEntityId;
      return document_command_changed_result();
    }

private:
  EntityId entity_ = kInvalidEntityId;
  EntityId definition_ = kInvalidEntityId;
  std::optional<DocumentEntitySnapshot> snapshot_;
  std::optional<DocumentEntitySnapshot> orphan_definition_snapshot_;
  DocumentSelectionState before_selection_;
};

} // namespace quader
