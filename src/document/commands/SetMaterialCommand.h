#pragma once

#include "document/commands/SceneCommand.h"

#include <optional>

namespace quader {

class SetMaterialCommand final : public SceneCommand {
public:
    SetMaterialCommand(
            SceneDocument& scene,
            EntityId entity,
            std::optional<MaterialComponent> before,
            MaterialComponent after)
        : SceneCommand(scene, "Set Material")
        , entity_(entity)
        , before_(before)
        , after_(after) {}

    DocumentCommandResult undo() override {
        return apply(before_);
    }

    DocumentCommandResult redo() override {
        if (before_.has_value() && before_->material == after_.material) {
            return documentCommandUnchangedResult();
        }
        return apply(after_);
    }

private:
    DocumentCommandResult apply(const std::optional<MaterialComponent>& material) {
        if (!scene().exists(entity_)) {
            return documentCommandMissingEntityResult();
        }
        if (!material.has_value()) {
            scene().clearMaterial(entity_);
            return documentCommandChangedResult();
        }
        scene().setMaterial(entity_, *material);
        return documentCommandChangedResult();
    }

    DocumentCommandResult apply(MaterialComponent material) {
        if (!scene().exists(entity_)) {
            return documentCommandMissingEntityResult();
        }
        scene().setMaterial(entity_, material);
        return documentCommandChangedResult();
    }

    EntityId entity_ = InvalidEntityId;
    std::optional<MaterialComponent> before_;
    MaterialComponent after_;
};

} // namespace quader
