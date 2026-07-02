////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document/scene_command.hpp"

#include <string_view>
#include <utility>

namespace quader {

/**
 * Owns undoable scene-root render settings edits for the document command layer.
 */
class SetSceneSettingsCommand final : public SceneCommand {
public:
    SetSceneSettingsCommand(SceneDocument& scene, EntityId entity, SceneSettingsComponent before, SceneSettingsComponent after)
        : SceneCommand(scene, "Set Scene Settings")
        , entity_(entity)
        , before_(std::move(before))
        , after_(std::move(after)) {}

    DocumentCommandResult undo() override {
        return apply(before_);
    }

    DocumentCommandResult redo() override {
      if (document_command_same_scene_settings(before_, after_)) {
        return document_command_unchanged_result();
      }
        return apply(after_);
    }

    bool merge_with(const IUndoableCommand &newer_command,
                    std::string_view merge_key) override {
      if (merge_key.empty()) {
        return false;
      }
      const auto *newer =
          dynamic_cast<const SetSceneSettingsCommand *>(&newer_command);
      if (newer == nullptr || &newer->scene() != &scene() ||
          newer->entity_ != entity_) {
        return false;
      }
      after_ = newer->after_;
      return true;
    }

private:
    DocumentCommandResult apply(const SceneSettingsComponent& settings) {
        if (!scene().exists(entity_)) {
          return document_command_missing_entity_result();
        }
        if (!scene().set_scene_settings(entity_, settings)) {
          return document_command_invalid_request_result(
              "Scene settings cannot be changed");
        }
        return document_command_changed_result();
    }

    EntityId entity_ = kInvalidEntityId;
    SceneSettingsComponent before_;
    SceneSettingsComponent after_;
};

} // namespace quader
