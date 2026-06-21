#pragma once

#include "document/commands/DocumentCommandSceneHelpers.h"
#include "document/commands/SceneCommand.h"

#include <string>
#include <utility>

namespace quader {

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
        entity_ = InvalidEntityId;
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
        entity_ = scene().createEntity(name_, kind_);
        if (parent_ != InvalidEntityId && !scene().setParent(entity_, parent_, false)) {
            scene().destroyEntity(entity_);
            entity_ = InvalidEntityId;
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
    DocumentSelectionState beforeSelection_;
};

} // namespace quader
