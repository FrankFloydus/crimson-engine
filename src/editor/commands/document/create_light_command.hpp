////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/commands/document/scene_command.hpp"

#include <string>
#include <utility>

namespace quader {

/**
 * Applies Create Light Command behavior as a command in the editor document command layer.
 */
class CreateLightCommand final : public SceneCommand {
public:
    CreateLightCommand(
            SceneDocument& scene,
            std::string label,
            std::string name,
            LightComponent light,
            EntityId parent = InvalidEntityId,
            TransformComponent transform = TransformComponent{},
            bool selectCreated = false)
        : SceneCommand(scene, std::move(label))
        , name_(std::move(name))
        , light_(light)
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
        entity_ = scene().createEntityWithId(entity_, name_, EntityKind::Light, editorObjectId_);
        if (entity_ == InvalidEntityId) {
            return documentCommandInvalidRequestResult("Light entity cannot be created");
        }
        if (!uuid_.empty() && !scene().setEntityUuid(entity_, uuid_)) {
            scene().destroyEntity(entity_);
            entity_ = InvalidEntityId;
            return documentCommandInvalidRequestResult("Light identity cannot be restored");
        }
        if (uuid_.empty()) {
            uuid_ = scene().uuid(entity_);
        }
        editorObjectId_ = scene().editorObjectId(entity_);
        if (parent_ != InvalidEntityId && !scene().setParent(entity_, parent_, false)) {
            scene().destroyEntity(entity_);
            entity_ = InvalidEntityId;
            return documentCommandInvalidRequestResult("Light cannot be reparented");
        }
        scene().setTransform(entity_, transform_);
        scene().setLight(entity_, light_);
        if (selectCreated_) {
            documentCommandSelectOnly(scene(), {entity_});
        }
        return documentCommandChangedResult();
    }

private:
    std::string name_;
    LightComponent light_;
    EntityId parent_ = InvalidEntityId;
    TransformComponent transform_;
    bool selectCreated_ = false;
    EntityId entity_ = InvalidEntityId;
    std::string uuid_;
    EditorObjectId editorObjectId_ = InvalidEditorObjectId;
    DocumentSelectionState beforeSelection_;
};

} // namespace quader
