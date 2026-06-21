#pragma once

#include "document/commands/DocumentCommandSceneHelpers.h"
#include "document/commands/SceneCommand.h"

#include <optional>
#include <unordered_map>

namespace quader {

class DestroyEntityCommand final : public SceneCommand {
public:
    DestroyEntityCommand(SceneDocument& scene, EntityId entity)
        : SceneCommand(scene, "Destroy Entity")
        , entity_(entity) {}

    [[nodiscard]] EntityId entity() const { return entity_; }

    DocumentCommandResult undo() override {
        if (!snapshot_.has_value()) {
            return documentCommandMissingEntityResult();
        }

        std::unordered_map<EntityId, EntityId> remappedEntities;
        const EntityId parent = snapshot_->originalParent != InvalidEntityId && scene().exists(snapshot_->originalParent)
                ? snapshot_->originalParent
                : InvalidEntityId;
        entity_ = documentCommandRestoreEntitySnapshotTree(scene(), *snapshot_, parent, remappedEntities);
        documentCommandApplySelection(scene(), documentCommandRemapSelection(beforeSelection_, remappedEntities));
        return documentCommandChangedResult();
    }

    DocumentCommandResult redo() override {
        if (entity_ == InvalidEntityId || !scene().exists(entity_)) {
            return documentCommandMissingEntityResult();
        }

        beforeSelection_ = documentCommandCaptureSelection(scene());
        snapshot_ = documentCommandCaptureEntitySnapshot(scene(), entity_);
        if (!snapshot_.has_value()) {
            return documentCommandMissingEntityResult();
        }
        if (!scene().destroyEntity(entity_)) {
            return documentCommandMissingEntityResult();
        }
        entity_ = InvalidEntityId;
        return documentCommandChangedResult();
    }

private:
    EntityId entity_ = InvalidEntityId;
    std::optional<DocumentEntitySnapshot> snapshot_;
    DocumentSelectionState beforeSelection_;
};

} // namespace quader
