////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/DocumentCommandSceneHelpers.h"
#include "editor/commands/document/SceneCommand.h"

#include <string>
#include <utility>

namespace quader {

/**
 * Applies Create Entity Command behavior as a command in the editor document command layer.
 */
class CreateEntityCommand final : public SceneCommand {
public:
    CreateEntityCommand(
            SceneDocument& scene,
            std::string name,
            EntityKind kind = EntityKind::Generic,
            EntityId parent = InvalidEntityId,
            TransformComponent transform = TransformComponent{},
            bool selectCreated = false)
        : SceneCommand(scene, "Create Entity")
        , name_(std::move(name))
        , kind_(kind)
        , parent_(parent)
        , transform_(transform)
        , selectCreated_(selectCreated) {}

    [[nodiscard]] EntityId entity() const { return entity_; }

    DocumentCommandResult undo() override {
        if (entity_ == InvalidEntityId || !scene().exists(entity_)) {
            return documentCommandMissingEntityResult();
        }
        if (!scene().destroyEntity(entity_)) {
            return documentCommandMissingEntityResult();
        }
        if (selectCreated_) {
            documentCommandApplySelection(scene(), beforeSelection_);
        }
        return documentCommandChangedResult();
    }

    DocumentCommandResult redo() override {
        if (entity_ != InvalidEntityId && scene().exists(entity_)) {
            return documentCommandUnchangedResult();
        }
        if (parent_ != InvalidEntityId && !scene().exists(parent_)) {
            return documentCommandInvalidRequestResult("Parent entity does not exist");
        }

        beforeSelection_ = documentCommandCaptureSelection(scene());
        entity_ = scene().createEntityWithId(entity_, name_, kind_, editorObjectId_);
        if (entity_ == InvalidEntityId) {
            return documentCommandInvalidRequestResult("Entity cannot be created");
        }
        editorObjectId_ = scene().editorObjectId(entity_);
        if (parent_ != InvalidEntityId && !scene().setParent(entity_, parent_, false)) {
            scene().destroyEntity(entity_);
            return documentCommandInvalidRequestResult("Entity cannot be reparented");
        }
        scene().setTransform(entity_, transform_);
        if (selectCreated_) {
            documentCommandSelectOnly(scene(), {entity_});
        }
        return documentCommandChangedResult();
    }

private:
    std::string name_;
    EntityKind kind_ = EntityKind::Generic;
    EntityId parent_ = InvalidEntityId;
    TransformComponent transform_;
    bool selectCreated_ = false;
    EntityId entity_ = InvalidEntityId;
    EditorObjectId editorObjectId_ = InvalidEditorObjectId;
    DocumentSelectionState beforeSelection_;
};

} // namespace quader
