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
 * Applies Set Lock Command behavior as a command in the editor document command layer.
 */
class SetLockCommand final : public SceneCommand {
public:
    SetLockCommand(SceneDocument& scene, EntityId entity, bool before, bool after)
        : SceneCommand(scene, "Set Lock")
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
    DocumentCommandResult apply(bool locked) {
        return scene().setLocked(entity_, locked) ? documentCommandChangedResult() : documentCommandMissingEntityResult();
    }

    EntityId entity_ = InvalidEntityId;
    bool before_ = false;
    bool after_ = false;
};

} // namespace quader
