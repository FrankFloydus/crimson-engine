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
 * Applies Ungroup Selection Command behavior as a command in the editor document command layer.
 */
class UngroupSelectionCommand final : public SceneCommand {
public:
    explicit UngroupSelectionCommand(SceneDocument& scene)
        : SceneCommand(scene, "Ungroup Selection") {}

    DocumentCommandResult undo() override {
        if (groups_.empty()) {
            return documentCommandMissingEntityResult();
        }

        std::unordered_map<EntityId, EntityId> remappedEntities;
        std::vector<EntityId> restoredGroups;
        restoredGroups.reserve(groups_.size());
        for (UngroupedEntity& group : groups_) {
            const EntityId parent = group.parent != InvalidEntityId && scene().exists(group.parent)
                    ? group.parent
                    : InvalidEntityId;
            const EntityId restoredGroup = documentCommandCreateEntityFromSnapshot(
                    scene(),
                    group.snapshot,
                    parent,
                    group.snapshot.name,
                    true,
                    true);
            if (restoredGroup == InvalidEntityId) {
                return documentCommandMissingEntityResult();
            }
            remappedEntities[group.snapshot.originalId] = restoredGroup;
            for (const DocumentEntityPlacement& child : group.children) {
                if (!scene().exists(child.entity)) {
                    return documentCommandMissingEntityResult();
                }
                if (!scene().setParent(child.entity, restoredGroup, false)) {
                    return documentCommandInvalidRequestResult("Entity cannot be reparented");
                }
                scene().setTransform(child.entity, child.transform);
            }
            group.snapshot.originalId = restoredGroup;
            restoredGroups.push_back(restoredGroup);
        }
        documentCommandApplySelection(scene(), documentCommandRemapSelection(beforeSelection_, remappedEntities));
        currentGroups_ = std::move(restoredGroups);
        return documentCommandChangedResult();
    }

    DocumentCommandResult redo() override {
        const std::vector<EntityId> targets = currentGroups_.empty()
                ? selectedUngroupableRoots()
                : documentCommandNormalizeExistingEntities(scene(), currentGroups_);
        if (targets.empty()) {
            return documentCommandUnchangedResult();
        }

        beforeSelection_ = documentCommandCaptureSelection(scene());
        groups_.clear();
        std::vector<EntityId> selectedChildren;
        for (const EntityId group : targets) {
            if (!isUngroupable(group)) {
                continue;
            }

            std::optional<DocumentEntitySnapshot> snapshot =
                    documentCommandCaptureEntitySnapshot(scene(), group);
            if (!snapshot.has_value()) {
                return documentCommandMissingEntityResult();
            }
            const EntityId parent = scene().parent(group);
            const std::vector<EntityId> children = scene().children(group);
            UngroupedEntity state{
                    .snapshot = *snapshot,
                    .parent = parent,
                    .children = documentCommandCapturePlacements(scene(), children),
            };

            for (const EntityId child : children) {
                if (!scene().setParent(child, parent, true)) {
                    return documentCommandInvalidRequestResult("Entity cannot be reparented");
                }
                selectedChildren.push_back(child);
            }
            if (!scene().destroyEntity(group)) {
                return documentCommandMissingEntityResult();
            }
            groups_.push_back(std::move(state));
        }

        if (groups_.empty()) {
            return documentCommandUnchangedResult();
        }

        currentGroups_.clear();
        documentCommandSelectOnly(scene(), selectedChildren);
        return documentCommandChangedResult();
    }

private:
    /**
     * Represents an Ungrouped Entity value used by the editor document command layer.
     */
    struct UngroupedEntity {
        DocumentEntitySnapshot snapshot;
        EntityId parent = InvalidEntityId;
        std::vector<DocumentEntityPlacement> children;
    };

    bool isUngroupable(EntityId entity) const {
        return scene().exists(entity) &&
                !scene().isSceneRoot(entity) &&
                scene().kind(entity) == EntityKind::Empty &&
                scene().hasChildren(entity);
    }

    std::vector<EntityId> selectedUngroupableRoots() const {
        std::vector<EntityId> roots;
        for (const EntityId entity : documentCommandSelectedRootEntities(scene())) {
            if (isUngroupable(entity)) {
                roots.push_back(entity);
            }
        }
        return roots;
    }

    std::vector<UngroupedEntity> groups_;
    std::vector<EntityId> currentGroups_;
    DocumentSelectionState beforeSelection_;
};

} // namespace quader
