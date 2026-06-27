////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/SceneCommand.h"

#include <optional>

namespace quader {

/**
 * Applies Set Material Command behavior as a command in the editor document command layer.
 */
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
        if (before_.has_value() && *before_ == after_) {
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
