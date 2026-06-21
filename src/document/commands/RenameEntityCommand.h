#pragma once

#include "document/commands/SceneCommand.h"

#include <string>
#include <utility>

namespace quader {

class RenameEntityCommand final : public SceneCommand {
public:
    RenameEntityCommand(SceneDocument& scene, EntityId entity, std::string before, std::string after)
        : SceneCommand(scene, "Rename Entity")
        , entity_(entity)
        , before_(std::move(before))
        , after_(std::move(after)) {}

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
    DocumentCommandResult apply(const std::string& name) {
        return scene().renameEntity(entity_, name) ? documentCommandChangedResult() : documentCommandMissingEntityResult();
    }

    EntityId entity_ = InvalidEntityId;
    std::string before_;
    std::string after_;
};

} // namespace quader
