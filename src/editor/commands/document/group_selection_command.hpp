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
#include <vector>

namespace quader {

/**
 * Applies Group Selection Command behavior as a command in the editor document command layer.
 */
class GroupSelectionCommand final : public SceneCommand {
public:
    GroupSelectionCommand(
            SceneDocument& scene,
            std::string groupName = {},
            TransformComponent groupTransform = TransformComponent{})
        : SceneCommand(scene, "Group Selection")
        , groupName_(std::move(groupName))
        , groupTransform_(groupTransform) {}

    [[nodiscard]] EntityId groupEntity() const { return group_; }

    DocumentCommandResult undo() override {
        if (group_ == InvalidEntityId || !scene().exists(group_)) {
            return documentCommandMissingEntityResult();
        }

        for (const DocumentEntityPlacement& placement : rootPlacements_) {
            DocumentCommandResult result = documentCommandApplyPlacement(scene(), placement);
            if (!result.ok) {
                return result;
            }
        }
        if (!scene().destroyEntity(group_)) {
            return documentCommandMissingEntityResult();
        }
        documentCommandApplySelection(scene(), beforeSelection_);
        return documentCommandChangedResult();
    }

    DocumentCommandResult redo() override {
        if (!capturedRoots_) {
            roots_ = documentCommandSelectedRootEntities(scene());
            capturedRoots_ = true;
        }
        roots_ = documentCommandNormalizeExistingEntities(scene(), roots_);
        if (roots_.empty()) {
            return documentCommandUnchangedResult();
        }

        beforeSelection_ = documentCommandCaptureSelection(scene());
        rootPlacements_ = documentCommandCapturePlacements(scene(), roots_);
        const EntityId commonParent = documentCommandCommonParent(scene(), roots_);
        const std::string name = groupName_.empty() ? std::string("Group") : groupName_;
        group_ = scene().createEntityWithId(group_, name, EntityKind::Empty, groupObjectId_);
        if (group_ == InvalidEntityId) {
            return documentCommandInvalidRequestResult("Group entity cannot be created");
        }
        if (!groupUuid_.empty() && !scene().setEntityUuid(group_, groupUuid_)) {
            scene().destroyEntity(group_);
            group_ = InvalidEntityId;
            return documentCommandInvalidRequestResult("Group entity identity cannot be restored");
        }
        if (groupUuid_.empty()) {
            groupUuid_ = scene().uuid(group_);
        }
        groupObjectId_ = scene().editorObjectId(group_);
        scene().setTransform(group_, groupTransform_);
        if (commonParent != InvalidEntityId && !scene().setParent(group_, commonParent, true)) {
            scene().destroyEntity(group_);
            return documentCommandInvalidRequestResult("Group entity cannot be reparented");
        }
        for (const EntityId root : roots_) {
            if (!scene().setParent(root, group_, true)) {
                return documentCommandInvalidRequestResult("Selected entity cannot be grouped");
            }
        }
        documentCommandSelectOnly(scene(), {group_});
        return documentCommandChangedResult();
    }

private:
    std::string groupName_;
    TransformComponent groupTransform_;
    bool capturedRoots_ = false;
    std::vector<EntityId> roots_;
    std::vector<DocumentEntityPlacement> rootPlacements_;
    DocumentSelectionState beforeSelection_;
    EntityId group_ = InvalidEntityId;
    std::string groupUuid_;
    EditorObjectId groupObjectId_ = InvalidEditorObjectId;
};

} // namespace quader
