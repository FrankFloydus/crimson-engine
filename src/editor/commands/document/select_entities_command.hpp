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
                            SceneSelectionApplicationMode::UndoableCommand)
      : SceneCommand(scene, "Select Entities"),
        before_(capture_scene_selection(scene)),
        after_(make_scene_selection(scene, entities, active)), mode_(mode) {}

  DocumentCommandResult undo() override {
    apply_scene_selection(scene(), before_, mode_);
    return document_command_changed_result();
  }

    DocumentCommandResult redo() override {
      if (same_scene_selection(before_, after_)) {
        return document_command_unchanged_result();
      }
      apply_scene_selection(scene(), after_, mode_);
      return document_command_changed_result();
    }

private:
    SceneSelectionState before_;
    SceneSelectionState after_;
    SceneSelectionApplicationMode mode_ =
        SceneSelectionApplicationMode::UndoableCommand;
};

} // namespace quader
