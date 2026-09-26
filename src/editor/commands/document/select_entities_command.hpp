////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_result.hpp"
#include "editor/commands/document/scene_command.hpp"
#include "editor/selection/scene_selection_application.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {

/**
 * Applies Select Entities Command behavior as a command in the editor document command layer.
 */
class SelectEntitiesCommand final : public SceneCommand {
public:
  SelectEntitiesCommand(SceneDocument &scene, std::vector<EntityId> entities,
                        EntityId active = kInvalidEntityId,
                        SceneSelectionApplicationMode mode =
                            SceneSelectionApplicationMode::UndoableCommand,
                        std::string merge_key = {})
      : SceneCommand(scene, "Select Entities"),
        before_(capture_scene_selection(scene)),
        after_(make_scene_selection(scene, entities, active)), mode_(mode),
        merge_key_(std::move(merge_key)) {}

  DocumentCommandResult undo() override { return apply(before_); }

  DocumentCommandResult redo() override { return apply(after_); }

  bool merge_with(const IUndoableCommand &newer_command,
                  std::string_view merge_key) override {
    const auto *newer =
        dynamic_cast<const SelectEntitiesCommand *>(&newer_command);
    if (merge_key.empty() || merge_key_ != merge_key || newer == nullptr ||
        newer->merge_key_ != merge_key || &scene() != &newer->scene() ||
        mode_ != newer->mode_ ||
        !same_scene_selection(after_, newer->before_)) {
      return false;
    }
    after_ = newer->after_;
    return true;
  }

private:
  DocumentCommandResult apply(const SceneSelectionState &selection) {
    const bool changed = apply_scene_selection(scene(), selection, mode_);
    return changed ? document_command_changed_result()
                   : document_command_unchanged_result();
  }

  SceneSelectionState before_;
  SceneSelectionState after_;
  SceneSelectionApplicationMode mode_ =
      SceneSelectionApplicationMode::UndoableCommand;
  std::string merge_key_;
};

} // namespace quader
