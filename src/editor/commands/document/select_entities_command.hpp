////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/commands/document/scene_command.hpp"

#include <vector>

namespace quader {

/**
 * Applies Select Entities Command behavior as a command in the editor document command layer.
 */
class SelectEntitiesCommand final : public SceneCommand {
public:
  SelectEntitiesCommand(SceneDocument &scene, std::vector<EntityId> entities,
                        EntityId active = kInvalidEntityId)
      : SceneCommand(scene, "Select Entities"),
        before_(document_command_capture_selection(scene)),
        after_(document_command_selection_for(scene, entities, active)) {}

  DocumentCommandResult undo() override {
    document_command_apply_selection(scene(), before_);
    return document_command_changed_result();
  }

    DocumentCommandResult redo() override {
      if (document_command_same_selection(before_, after_)) {
        return document_command_unchanged_result();
      }
      document_command_apply_selection(scene(), after_);
      return document_command_changed_result();
    }

private:
    DocumentSelectionState before_;
    DocumentSelectionState after_;
};

} // namespace quader
