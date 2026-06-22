////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/DocumentCommandSceneHelpers.h"
#include "editor/commands/document/SceneCommand.h"

#include <optional>
#include <unordered_map>

namespace quader {

/**
 * Applies Destroy Entity Command behavior as a command in the editor document command layer.
 */
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
        if (entity_ == InvalidEntityId || !scene().exists(entity_)) {
            return documentCommandMissingEntityResult();
        }
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
