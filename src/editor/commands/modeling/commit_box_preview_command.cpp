////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/commit_box_preview_command.hpp"

#include "editor/commands/document/set_material_command.hpp"
#include "scene/scene_document.hpp"

#include <array>
#include <optional>
#include <utility>

namespace quader {
namespace {

CommandResult commandResultFrom(ModelingCommandResult result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message),
    };
}

CommandResult commandResultFrom(const ModelingCreateBoxResult& result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = result.message,
    };
}

} // namespace

CommitBoxPreviewCommand::CommitBoxPreviewCommand(
        ModelingSessionService& modeling,
        SceneDocument& scene,
        std::string name,
        AssetId defaultMaterial)
    : modeling_(modeling)
    , scene_(scene)
    , name_(std::move(name))
    , defaultMaterial_(defaultMaterial) {}

const std::string& CommitBoxPreviewCommand::label() const {
    return label_;
}

const ModelingCreateBoxResult& CommitBoxPreviewCommand::committed() const {
    return committed_;
}

CommandResult CommitBoxPreviewCommand::redo() {
    if (!executed_) {
        return executeInitial();
    }
    return redoRecorded();
}

CommandResult CommitBoxPreviewCommand::undo() {
    if (!executed_) {
        return commandInvalidResult("Box preview commit has not been executed");
    }

    const CommandResult material = undoMaterial();
    if (!material.ok) {
        return material;
    }

    CommandResult restore = commandResultFrom(modeling_.restoreHistoryMemento(before_));
    if (!restore.ok) {
        return restore;
    }

    CommandResult cancelPreview = cancelRestoredPreview();
    if (!cancelPreview.ok) {
        return cancelPreview;
    }

    return {
            .ok = true,
            .changed = material.changed || restore.changed || cancelPreview.changed,
    };
}

CommandResult CommitBoxPreviewCommand::executeInitial() {
    before_ = modeling_.captureHistoryMemento();
    committed_ = modeling_.commitBox(name_);
    CommandResult result = commandResultFrom(committed_);
    if (!result.ok || !result.changed) {
        return result;
    }

    const std::array<EntityId, 1> selected{committed_.object.entity};
    const CommandResult selection = commandResultFrom(
            modeling_.setObjectSelection(selected, committed_.object.entity));
    if (!selection.ok) {
        return rollbackInitial(selection);
    }

    if (defaultMaterial_ != InvalidAssetId) {
        std::optional<MaterialComponent> beforeMaterial;
        if (const MaterialComponent* material = scene_.material(committed_.object.entity)) {
            beforeMaterial = *material;
        }
        materialCommand_ = std::make_unique<SetMaterialCommand>(
                scene_,
                committed_.object.entity,
                beforeMaterial,
                MaterialComponent{.material = defaultMaterial_});
        const CommandResult material = materialCommand_->redo();
        if (!material.ok) {
            return rollbackInitial(material);
        }
        if (!material.changed) {
            materialCommand_.reset();
        }
    }

    after_ = modeling_.captureHistoryMemento();
    executed_ = true;
    return {
            .ok = true,
            .changed = true,
    };
}

CommandResult CommitBoxPreviewCommand::redoRecorded() {
    CommandResult restore = commandResultFrom(modeling_.restoreHistoryMemento(after_));
    if (!restore.ok) {
        return restore;
    }

    CommandResult material = redoMaterial();
    if (!material.ok) {
        (void)modeling_.restoreHistoryMemento(before_);
        (void)cancelRestoredPreview();
        return material;
    }

    return {
            .ok = true,
            .changed = restore.changed || material.changed,
    };
}

CommandResult CommitBoxPreviewCommand::undoMaterial() {
    if (!materialCommand_) {
        return commandUnchangedResult();
    }
    return materialCommand_->undo();
}

CommandResult CommitBoxPreviewCommand::redoMaterial() {
    if (!materialCommand_) {
        return commandUnchangedResult();
    }
    return materialCommand_->redo();
}

CommandResult CommitBoxPreviewCommand::rollbackInitial(CommandResult failure) {
    (void)undoMaterial();
    (void)modeling_.restoreHistoryMemento(before_);
    (void)cancelRestoredPreview();
    return failure;
}

CommandResult CommitBoxPreviewCommand::cancelRestoredPreview() {
    return commandResultFrom(modeling_.cancelBox());
}

} // namespace quader
