////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "diagnostics/profile.hpp"
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
        QDR_PROFILE_FUNCTION();
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
        QDR_PROFILE_FUNCTION();
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
        QDR_PROFILE_FUNCTION();
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
        QDR_PROFILE_FUNCTION();
        duplicateSnapshots_.clear();
        duplicateSnapshots_.reserve(sourceSnapshots_.size());
        SceneEntityNameAllocator nameAllocator(scene());
        for (const DocumentEntitySnapshot& snapshot : sourceSnapshots_) {
            QDR_PROFILE_SCOPE("Duplicate snapshot tree");
            const EntityId parent = snapshot.originalParent != InvalidEntityId && scene().exists(snapshot.originalParent)
                    ? snapshot.originalParent
                    : InvalidEntityId;
            const EntityId duplicate = documentCommandDuplicateEntitySnapshotTree(
                    scene(),
                    snapshot,
                    parent,
                    false,
                    existingClonesBySource_.empty() ? nullptr : &existingClonesBySource_,
                    &nameAllocator);
            if (duplicate == InvalidEntityId) {
                return documentCommandInvalidRequestResult("Selected hierarchy could not be duplicated");
            }
            duplicateRoots_.push_back(duplicate);
        }

        for (const EntityId duplicate : duplicateRoots_) {
            QDR_PROFILE_SCOPE("Capture duplicate snapshot");
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
        QDR_PROFILE_FUNCTION();
        std::unordered_map<EntityId, EntityId> remappedEntities;
        for (const DocumentEntitySnapshot& snapshot : duplicateSnapshots_) {
            QDR_PROFILE_SCOPE("Restore duplicate snapshot tree");
            const EntityId parent = snapshot.originalParent != InvalidEntityId && scene().exists(snapshot.originalParent)
                    ? snapshot.originalParent
                    : InvalidEntityId;
            const EntityId restored = restoreOrReuseDuplicateSnapshotTree(snapshot, parent, remappedEntities);
            if (restored == InvalidEntityId) {
                return documentCommandInvalidRequestResult("Duplicated hierarchy could not be restored");
            }
            duplicateRoots_.push_back(restored);
        }
        return documentCommandChangedResult();
    }

    EntityId restoreOrReuseDuplicateSnapshotTree(
            const DocumentEntitySnapshot& snapshot,
            EntityId parent,
            std::unordered_map<EntityId, EntityId>& remappedEntities) {
        EntityId restored = InvalidEntityId;
        if (scene().exists(snapshot.originalId)) {
            restored = snapshot.originalId;
            if (parent != InvalidEntityId && scene().exists(parent) && scene().parent(restored) != parent) {
                if (!scene().setParent(restored, parent, false)) {
                    return InvalidEntityId;
                }
            }
            documentCommandApplyEntitySnapshotCommonState(scene(), restored, snapshot, snapshot.name);
            documentCommandApplyEntitySnapshotComponents(scene(), restored, snapshot, true);
        } else {
            restored = documentCommandCreateEntityFromSnapshot(
                    scene(),
                    snapshot,
                    parent,
                    snapshot.name,
                    true,
                    true);
            if (restored == InvalidEntityId) {
                return InvalidEntityId;
            }
        }

        remappedEntities[snapshot.originalId] = restored;
        for (const DocumentEntitySnapshot& child : snapshot.children) {
            if (restoreOrReuseDuplicateSnapshotTree(child, restored, remappedEntities) == InvalidEntityId) {
                return InvalidEntityId;
            }
        }
        return restored;
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
