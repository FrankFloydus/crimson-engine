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
 * Applies Set Visibility Command behavior as a command in the editor document command layer.
 */
class SetVisibilityCommand final : public SceneCommand {
public:
    SetVisibilityCommand(SceneDocument& scene, EntityId entity, bool before, bool after)
        : SceneCommand(scene, "Set Visibility")
        , entity_(entity)
        , before_(before)
        , after_(after) {}

    DocumentCommandResult undo() override {
        return apply(before_);
    }

    DocumentCommandResult redo() override {
        if (before_ == after_) {
            return documentCommandUnchangedResult();
        }
        return apply(after_);
    }

private:
    DocumentCommandResult apply(bool visible) {
        return scene().setVisible(entity_, visible) ? documentCommandChangedResult() : documentCommandMissingEntityResult();
    }

    EntityId entity_ = InvalidEntityId;
    bool before_ = true;
    bool after_ = true;
};

} // namespace quader
