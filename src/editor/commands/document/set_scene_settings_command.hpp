////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document/scene_command.hpp"

#include <string_view>
#include <utility>

namespace quader {

/**
 * Owns undoable scene-root render settings edits for the document command layer.
 */
class SetSceneSettingsCommand final : public SceneCommand {
public:
    SetSceneSettingsCommand(SceneDocument& scene, EntityId entity, SceneSettingsComponent before, SceneSettingsComponent after)
        : SceneCommand(scene, "Set Scene Settings")
        , entity_(entity)
        , before_(std::move(before))
        , after_(std::move(after)) {}

    DocumentCommandResult undo() override {
        return apply(before_);
    }

    DocumentCommandResult redo() override {
        if (documentCommandSameSceneSettings(before_, after_)) {
            return documentCommandUnchangedResult();
        }
        return apply(after_);
    }

    bool mergeWith(const IUndoableCommand& newerCommand, std::string_view mergeKey) override {
        if (mergeKey.empty()) {
            return false;
        }
        const auto* newer = dynamic_cast<const SetSceneSettingsCommand*>(&newerCommand);
        if (newer == nullptr || &newer->scene() != &scene() || newer->entity_ != entity_) {
            return false;
        }
        after_ = newer->after_;
        return true;
    }

private:
    DocumentCommandResult apply(const SceneSettingsComponent& settings) {
        if (!scene().exists(entity_)) {
            return documentCommandMissingEntityResult();
        }
        if (!scene().setSceneSettings(entity_, settings)) {
            return documentCommandInvalidRequestResult("Scene settings cannot be changed");
        }
        return documentCommandChangedResult();
    }

    EntityId entity_ = InvalidEntityId;
    SceneSettingsComponent before_;
    SceneSettingsComponent after_;
};

} // namespace quader
