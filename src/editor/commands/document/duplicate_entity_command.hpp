////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/commands/document/scene_command.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

namespace quader {

/**
 * Applies Duplicate Entity Command behavior as a command in the editor document command layer.
 */
class DuplicateEntityCommand final : public SceneCommand {
public:
    DuplicateEntityCommand(SceneDocument& scene, EntityId source, bool selectDuplicate = true)
        : SceneCommand(scene, "Duplicate Entity")
        , source_(source)
        , selectDuplicate_(selectDuplicate) {}

    [[nodiscard]] EntityId duplicateEntity() const {
        return duplicateRoots_.empty() ? InvalidEntityId : duplicateRoots_.front();
    }

    DocumentCommandResult undo() override {
        if (duplicateRoots_.empty()) {
            return documentCommandMissingEntityResult();
        }
        for (auto it = duplicateRoots_.rbegin(); it != duplicateRoots_.rend(); ++it) {
            if (scene().exists(*it) && !scene().destroyEntity(*it)) {
                return documentCommandMissingEntityResult();
            }
        }
        duplicateRoots_.clear();
        if (selectDuplicate_) {
            documentCommandApplySelection(scene(), beforeSelection_);
        }
        return documentCommandChangedResult();
    }

    DocumentCommandResult redo() override {
        beforeSelection_ = documentCommandCaptureSelection(scene());
        duplicateRoots_.clear();
        EntityId duplicate = InvalidEntityId;
        if (duplicateSnapshot_.has_value()) {
            std::unordered_map<EntityId, EntityId> remappedEntities;
            const EntityId parent = duplicateSnapshot_->originalParent != InvalidEntityId &&
                            scene().exists(duplicateSnapshot_->originalParent)
                    ? duplicateSnapshot_->originalParent
                    : InvalidEntityId;
            duplicate = documentCommandRestoreEntitySnapshotTree(
                    scene(),
                    *duplicateSnapshot_,
                    parent,
                    remappedEntities,
                    false);
        } else {
            if (source_ == InvalidEntityId || !scene().exists(source_)) {
                return documentCommandMissingEntityResult();
            }
            const std::optional<DocumentEntitySnapshot> snapshot =
                    documentCommandCaptureEntitySnapshot(scene(), source_);
            if (!snapshot.has_value()) {
                return documentCommandMissingEntityResult();
            }
            duplicate = documentCommandDuplicateEntitySnapshotTree(scene(), *snapshot, scene().parent(source_));
            if (duplicate != InvalidEntityId) {
                duplicateSnapshot_ = documentCommandCaptureEntitySnapshot(scene(), duplicate);
            }
        }
        if (duplicate == InvalidEntityId) {
            return documentCommandMissingEntityResult();
        }
        duplicateRoots_.push_back(duplicate);
        if (selectDuplicate_) {
            documentCommandSelectOnly(scene(), duplicateRoots_);
        }
        return documentCommandChangedResult();
    }

private:
    EntityId source_ = InvalidEntityId;
    bool selectDuplicate_ = true;
    std::vector<EntityId> duplicateRoots_;
    std::optional<DocumentEntitySnapshot> duplicateSnapshot_;
    DocumentSelectionState beforeSelection_;
};

} // namespace quader
