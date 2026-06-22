////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/DocumentCommandEdits.h"
#include "editor/commands/document/SceneCommand.h"

#include <string>
#include <utility>

namespace quader {

/**
 * Applies Apply Entity Transform Command behavior as a command in the editor document command layer.
 */
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
