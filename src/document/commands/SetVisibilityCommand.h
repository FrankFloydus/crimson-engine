#pragma once

#include "document/commands/SceneCommand.h"

namespace quader {

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
