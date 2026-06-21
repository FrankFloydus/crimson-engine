#pragma once

#include "document/commands/SceneCommand.h"

namespace quader {

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
