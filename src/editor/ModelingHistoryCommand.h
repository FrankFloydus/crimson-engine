#pragma once

#include "core/IUndoableCommand.h"
#include "modeling/ModelingSessionService.h"

#include <cstdint>
#include <string>
#include <utility>

namespace quader {

inline CommandResult modelingHistoryCommandResult(ModelingCommandResult result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message),
    };
}

class ModelingHistoryCommand : public IUndoableCommand {
public:
    [[nodiscard]] const std::string& label() const override { return label_; }

    CommandResult undo() override {
        if (!executed_) {
            return commandInvalidResult("Modeling command has not been executed");
        }
        return modelingHistoryCommandResult(modeling_.undo());
    }

    CommandResult redo() override {
        if (executed_) {
            return modelingHistoryCommandResult(modeling_.redo());
        }

        CommandResult result = modelingHistoryCommandResult(executeInitial());
        if (result.ok && result.changed) {
            executed_ = true;
        }
        return result;
    }

protected:
    ModelingHistoryCommand(ModelingSessionService& modeling, std::string label)
        : modeling_(modeling)
        , label_(std::move(label)) {}

    [[nodiscard]] ModelingSessionService& modeling() { return modeling_; }
    [[nodiscard]] const ModelingSessionService& modeling() const { return modeling_; }

private:
    virtual ModelingCommandResult executeInitial() = 0;

    ModelingSessionService& modeling_;
    std::string label_;
    bool executed_ = false;
};

class DeleteSelectionHistoryCommand final : public ModelingHistoryCommand {
public:
    explicit DeleteSelectionHistoryCommand(ModelingSessionService& modeling)
        : ModelingHistoryCommand(modeling, "Delete Selection") {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().deleteSelection();
    }
};

class PasteSelectionHistoryCommand final : public ModelingHistoryCommand {
public:
    explicit PasteSelectionHistoryCommand(ModelingSessionService& modeling)
        : ModelingHistoryCommand(modeling, "Paste Selection") {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().pasteSelection();
    }
};

class RepeatLastActionHistoryCommand final : public ModelingHistoryCommand {
public:
    explicit RepeatLastActionHistoryCommand(ModelingSessionService& modeling)
        : ModelingHistoryCommand(modeling, "Repeat Last Action") {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().repeatLastAction();
    }
};

class InstantModelingHistoryCommand final : public ModelingHistoryCommand {
public:
    InstantModelingHistoryCommand(
            ModelingSessionService& modeling,
            ModelingInstantOperationId operation,
            std::string label)
        : ModelingHistoryCommand(modeling, std::move(label))
        , operation_(operation) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().executeInstantOperation(operation_);
    }

    ModelingInstantOperationId operation_;
};

class DuplicateMeshesHistoryCommand final : public ModelingHistoryCommand {
public:
    explicit DuplicateMeshesHistoryCommand(ModelingSessionService& modeling)
        : ModelingHistoryCommand(modeling, "Duplicate Meshes") {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().executeInstantOperation(ModelingInstantOperationId::DuplicateMeshes);
    }
};

class MirrorSelectionHistoryCommand final : public ModelingHistoryCommand {
public:
    MirrorSelectionHistoryCommand(ModelingSessionService& modeling, ModelingMirrorAxis axis, std::string label)
        : ModelingHistoryCommand(modeling, std::move(label))
        , axis_(axis) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().mirrorSelection(axis_);
    }

    ModelingMirrorAxis axis_;
};

class FlipSelectionHistoryCommand final : public ModelingHistoryCommand {
public:
    FlipSelectionHistoryCommand(
            ModelingSessionService& modeling,
            ModelingFlipOrientation orientation,
            std::string label)
        : ModelingHistoryCommand(modeling, std::move(label))
        , orientation_(orientation) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().flipSelection(orientation_);
    }

    ModelingFlipOrientation orientation_;
};

class ApplyActiveMaterialHistoryCommand final : public ModelingHistoryCommand {
public:
    explicit ApplyActiveMaterialHistoryCommand(
            ModelingSessionService& modeling,
            std::uint32_t materialSlot = 0)
        : ModelingHistoryCommand(modeling, "Apply Material")
        , materialSlot_(materialSlot) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().applyActiveMaterial(materialSlot_);
    }

    std::uint32_t materialSlot_ = 0;
};

class CommitTransformPreviewHistoryCommand final : public ModelingHistoryCommand {
public:
    explicit CommitTransformPreviewHistoryCommand(ModelingSessionService& modeling)
        : ModelingHistoryCommand(modeling, "Transform Preview") {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().commitTransformPreview();
    }
};

class CommitActiveOperationPreviewHistoryCommand final : public ModelingHistoryCommand {
public:
    explicit CommitActiveOperationPreviewHistoryCommand(ModelingSessionService& modeling)
        : ModelingHistoryCommand(modeling, "Commit Operation Preview") {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().commitActiveOperationPreview();
    }
};

} // namespace quader
