////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "document/commands/DocumentCommandEdits.h"
#include "document/commands/SceneCommand.h"

namespace quader {

/**
 * Applies Reparent Entity Command behavior as a command in the editor document command layer.
 */
class ReparentEntityCommand final : public SceneCommand {
public:
    ReparentEntityCommand(
            SceneDocument& scene,
            EntityId entity,
            EntityId parent,
            bool preserveWorldTransform = true)
        : SceneCommand(scene, "Reparent Entity")
        , entity_(entity)
        , beforeParent_(scene.parent(entity))
        , afterParent_(parent)
        , preserveWorldTransform_(preserveWorldTransform) {
        if (const TransformComponent* transform = scene.transform(entity)) {
            beforeTransform_ = *transform;
            afterTransform_ = *transform;
        }
    }

    DocumentCommandResult undo() override {
        return apply(beforeParent_, beforeTransform_);
    }

    DocumentCommandResult redo() override {
        if (!scene().exists(entity_)) {
            return documentCommandMissingEntityResult();
        }
        if (afterParent_ != InvalidEntityId && !scene().exists(afterParent_)) {
            return documentCommandInvalidRequestResult("Parent entity does not exist");
        }
        if (beforeParent_ == afterParent_ && !capturedAfterTransform_) {
            return documentCommandUnchangedResult();
        }
        if (capturedAfterTransform_) {
            return apply(afterParent_, afterTransform_);
        }
        if (!scene().setParent(entity_, afterParent_, preserveWorldTransform_)) {
            return documentCommandInvalidRequestResult("Entity cannot be reparented");
        }
        if (const TransformComponent* transform = scene().transform(entity_)) {
            afterTransform_ = *transform;
            capturedAfterTransform_ = true;
        }
        return documentCommandChangedResult();
    }

private:
    DocumentCommandResult apply(EntityId parent, const TransformComponent& transform) {
        if (!scene().exists(entity_)) {
            return documentCommandMissingEntityResult();
        }
        if (parent != InvalidEntityId && !scene().exists(parent)) {
            return documentCommandInvalidRequestResult("Parent entity does not exist");
        }
        if (!scene().setParent(entity_, parent, false)) {
            return documentCommandInvalidRequestResult("Entity cannot be reparented");
        }
        scene().setTransform(entity_, transform);
        return documentCommandChangedResult();
    }

    EntityId entity_ = InvalidEntityId;
    EntityId beforeParent_ = InvalidEntityId;
    EntityId afterParent_ = InvalidEntityId;
    TransformComponent beforeTransform_;
    TransformComponent afterTransform_;
    bool preserveWorldTransform_ = true;
    bool capturedAfterTransform_ = false;
};

} // namespace quader
