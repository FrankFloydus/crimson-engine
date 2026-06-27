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
#include <utility>
#include <vector>

namespace quader {

/**
 * Applies selected hierarchy duplicate behavior as one command in the editor document command layer.
 */
class DuplicateSelectedHierarchyCommand final : public SceneCommand {
public:
    explicit DuplicateSelectedHierarchyCommand(SceneDocument& scene)
        : SceneCommand(scene, "Duplicate Hierarchy") {}

    DuplicateSelectedHierarchyCommand(
            SceneDocument& scene,
            std::vector<DocumentEntitySnapshot> sourceSnapshots,
            std::unordered_map<EntityId, EntityId> existingClonesBySource)
        : SceneCommand(scene, "Duplicate Hierarchy")
        , capturedSources_(true)
        , sourceSnapshots_(std::move(sourceSnapshots))
        , existingClonesBySource_(std::move(existingClonesBySource)) {}

    [[nodiscard]] const std::vector<EntityId>& duplicateRoots() const {
        return duplicateRoots_;
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
        documentCommandApplySelection(scene(), beforeSelection_);
        return documentCommandChangedResult();
    }

    DocumentCommandResult redo() override {
        duplicateRoots_.clear();
        if (!capturedSources_) {
            DocumentCommandResult captured = captureSources();
            if (!captured.ok || !captured.changed) {
                return captured;
            }
        }
        if (sourceSnapshots_.empty()) {
            return documentCommandUnchangedResult();
        }

        beforeSelection_ = documentCommandCaptureSelection(scene());
        DocumentCommandResult restored = duplicateSnapshots_.empty()
                ? duplicateFromSources()
                : restoreDuplicates();
        if (!restored.ok) {
            destroyDuplicateRoots();
            documentCommandApplySelection(scene(), beforeSelection_);
            return restored;
        }

        documentCommandSelectOnly(scene(), duplicateRoots_);
        return documentCommandChangedResult();
    }

private:
    DocumentCommandResult captureSources() {
        capturedSources_ = true;
        const std::vector<EntityId> roots = documentCommandSelectedRootEntities(scene());
        if (roots.empty()) {
            return documentCommandUnchangedResult();
        }

        sourceSnapshots_.clear();
        sourceSnapshots_.reserve(roots.size());
        for (const EntityId root : roots) {
            std::optional<DocumentEntitySnapshot> snapshot =
                    documentCommandCaptureEntitySnapshot(scene(), root);
            if (!snapshot.has_value()) {
                sourceSnapshots_.clear();
                return documentCommandMissingEntityResult();
            }
            sourceSnapshots_.push_back(std::move(*snapshot));
        }
        return documentCommandChangedResult();
    }

    DocumentCommandResult duplicateFromSources() {
        duplicateSnapshots_.clear();
        duplicateSnapshots_.reserve(sourceSnapshots_.size());
        for (const DocumentEntitySnapshot& snapshot : sourceSnapshots_) {
            const EntityId parent = snapshot.originalParent != InvalidEntityId && scene().exists(snapshot.originalParent)
                    ? snapshot.originalParent
                    : InvalidEntityId;
            const EntityId duplicate = documentCommandDuplicateEntitySnapshotTree(
                    scene(),
                    snapshot,
                    parent,
                    false,
                    existingClonesBySource_.empty() ? nullptr : &existingClonesBySource_);
            if (duplicate == InvalidEntityId) {
                return documentCommandInvalidRequestResult("Selected hierarchy could not be duplicated");
            }
            duplicateRoots_.push_back(duplicate);
        }

        for (const EntityId duplicate : duplicateRoots_) {
            std::optional<DocumentEntitySnapshot> snapshot =
                    documentCommandCaptureEntitySnapshot(scene(), duplicate);
            if (!snapshot.has_value()) {
                return documentCommandInvalidRequestResult("Duplicated hierarchy snapshot could not be captured");
            }
            duplicateSnapshots_.push_back(std::move(*snapshot));
        }
        return documentCommandChangedResult();
    }

    DocumentCommandResult restoreDuplicates() {
        std::unordered_map<EntityId, EntityId> remappedEntities;
        for (const DocumentEntitySnapshot& snapshot : duplicateSnapshots_) {
            const EntityId parent = snapshot.originalParent != InvalidEntityId && scene().exists(snapshot.originalParent)
                    ? snapshot.originalParent
                    : InvalidEntityId;
            const EntityId restored = documentCommandRestoreEntitySnapshotTree(
                    scene(),
                    snapshot,
                    parent,
                    remappedEntities);
            if (restored == InvalidEntityId) {
                return documentCommandInvalidRequestResult("Duplicated hierarchy could not be restored");
            }
            duplicateRoots_.push_back(restored);
        }
        return documentCommandChangedResult();
    }

    void destroyDuplicateRoots() {
        for (auto it = duplicateRoots_.rbegin(); it != duplicateRoots_.rend(); ++it) {
            if (scene().exists(*it)) {
                (void)scene().destroyEntity(*it);
            }
        }
        duplicateRoots_.clear();
    }

    bool capturedSources_ = false;
    std::vector<DocumentEntitySnapshot> sourceSnapshots_;
    std::unordered_map<EntityId, EntityId> existingClonesBySource_;
    std::vector<DocumentEntitySnapshot> duplicateSnapshots_;
    std::vector<EntityId> duplicateRoots_;
    DocumentSelectionState beforeSelection_;
};

} // namespace quader
