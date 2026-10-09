////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/scene_command.hpp"

namespace quader {

/**
 * Applies Set Selectable Command behavior as a command in the editor document command layer.
 */
class SetSelectableCommand final : public SceneCommand {
public:
    SetSelectableCommand(SceneDocument& scene, EntityId entity, bool before, bool after)
        : SceneCommand(scene, "Set Selectable")
        , entity_(entity)
        , before_(before)
        , after_(after) {}

    DocumentCommandResult undo() override {
        return apply(before_);
    }

    DocumentCommandResult redo() override {
        if (before_ == after_) {
          return document_command_unchanged_result();
        }
        return apply(after_);
    }

private:
    DocumentCommandResult apply(bool selectable) {
      return scene().set_selectable(entity_, selectable)
                 ? document_command_changed_result()
                 : document_command_missing_entity_result();
    }

    EntityId entity_ = kInvalidEntityId;
    bool before_ = true;
    bool after_ = true;
};

} // namespace quader
