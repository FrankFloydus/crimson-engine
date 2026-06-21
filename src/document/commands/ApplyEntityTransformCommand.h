#pragma once

#include "document/commands/DocumentCommandEdits.h"
#include "document/commands/SceneCommand.h"

#include <string>
#include <utility>

namespace quader {

class ApplyEntityTransformCommand final : public SceneCommand {
public:
    ApplyEntityTransformCommand(
            SceneDocument& scene,
            EntityId entity,
            TransformComponent before,
            TransformComponent after,
            std::string label = "Apply Entity Transform")
        : SceneCommand(scene, std::move(label))
        , entity_(entity)
        , before_(before)
        , after_(after) {}

    DocumentCommandResult undo() override {
        return apply(before_);
    }

    DocumentCommandResult redo() override {
        if (documentCommandSameTransform(before_, after_)) {
            return documentCommandUnchangedResult();
        }
        return apply(after_);
    }

private:
    DocumentCommandResult apply(const TransformComponent& transform) {
        if (!scene().exists(entity_)) {
            return documentCommandMissingEntityResult();
        }
        scene().setTransform(entity_, transform);
        return documentCommandChangedResult();
    }

    EntityId entity_ = InvalidEntityId;
    TransformComponent before_;
    TransformComponent after_;
};

} // namespace quader
