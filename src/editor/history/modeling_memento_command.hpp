////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/i_undoable_command.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace quader {

inline CommandResult modelingMementoCommandResult(ModelingCommandResult result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message),
    };
}

/**
 * Applies Modeling Memento Command behavior as a command in the editor coordination layer.
 */
class ModelingMementoCommand : public IUndoableCommand {
public:
    [[nodiscard]] const std::string& label() const override { return label_; }

    CommandResult undo() override {
        if (!executed_) {
            return commandInvalidResult("Modeling command has not been executed");
        }
        return modelingMementoCommandResult(modeling_.restoreHistoryMemento(before_));
    }

    CommandResult redo() override {
        if (executed_) {
            return modelingMementoCommandResult(modeling_.restoreHistoryMemento(after_));
        }

        before_ = captureBeforeMemento();
        CommandResult result = modelingMementoCommandResult(executeInitial());
        if (result.ok && result.changed) {
            after_ = captureAfterMemento();
            executed_ = true;
        }
        return result;
    }

protected:
    ModelingMementoCommand(ModelingSessionService& modeling, std::string label, bool executed = false)
        : modeling_(modeling)
        , label_(std::move(label))
        , executed_(executed) {}

    [[nodiscard]] ModelingSessionService& modeling() { return modeling_; }
    [[nodiscard]] const ModelingSessionService& modeling() const { return modeling_; }

    [[nodiscard]] virtual ModelingSessionMemento captureBeforeMemento() {
        return modeling_.captureHistoryMemento();
    }

    [[nodiscard]] virtual ModelingSessionMemento captureAfterMemento() {
        return modeling_.captureHistoryMemento();
    }

private:
    virtual ModelingCommandResult executeInitial() = 0;

    ModelingSessionService& modeling_;
    std::string label_;
    ModelingSessionMemento before_;
    ModelingSessionMemento after_;
    bool executed_ = false;
};

/**
 * Applies Recorded Modeling Memento Command behavior as a command in the editor coordination layer.
 */
class RecordedModelingMementoCommand final : public IUndoableCommand {
public:
    RecordedModelingMementoCommand(
            ModelingSessionService& modeling,
            std::string label,
            ModelingSessionMemento before,
            ModelingSessionMemento after)
        : modeling_(modeling)
        , label_(std::move(label))
        , before_(std::move(before))
        , after_(std::move(after)) {}

    [[nodiscard]] const std::string& label() const override { return label_; }

    CommandResult undo() override {
        return modelingMementoCommandResult(modeling_.restoreHistoryMemento(before_));
    }

    CommandResult redo() override {
        return modelingMementoCommandResult(modeling_.restoreHistoryMemento(after_));
    }

private:
    ModelingSessionService& modeling_;
    std::string label_;
    ModelingSessionMemento before_;
    ModelingSessionMemento after_;
};

/**
 * Applies Create Box From Bounds History Command behavior as a command in the editor coordination layer.
 */
class CreateBoxFromBoundsHistoryCommand final : public ModelingMementoCommand {
public:
    CreateBoxFromBoundsHistoryCommand(ModelingSessionService& modeling, ModelingCreateBoxRequest request)
        : ModelingMementoCommand(modeling, "Create Box")
        , request_(std::move(request)) {}

    [[nodiscard]] const ModelingCreateBoxResult& created() const { return created_; }

private:
    ModelingCommandResult executeInitial() override {
        created_ = modeling().createBoxFromBounds(request_);
        return {
                .ok = created_.ok,
                .changed = created_.changed,
                .message = created_.message,
        };
    }

    ModelingCreateBoxRequest request_;
    ModelingCreateBoxResult created_;
};

/**
 * Applies Create Mesh History Command behavior as a command in the editor coordination layer.
 */
class CreateMeshHistoryCommand final : public ModelingMementoCommand {
public:
    CreateMeshHistoryCommand(ModelingSessionService& modeling, ModelingCreateMeshRequest request)
        : ModelingMementoCommand(modeling, "Create Mesh")
        , request_(std::move(request)) {}

    [[nodiscard]] const ModelingCreateBoxResult& created() const { return created_; }

private:
    ModelingCommandResult executeInitial() override {
        created_ = modeling().createMesh(std::move(request_));
        return {
                .ok = created_.ok,
                .changed = created_.changed,
                .message = created_.message,
        };
    }

    ModelingCreateMeshRequest request_;
    ModelingCreateBoxResult created_;
};

/**
 * Applies Move Selection History Command behavior as a command in the editor coordination layer.
 */
class MoveSelectionHistoryCommand final : public ModelingMementoCommand {
public:
    MoveSelectionHistoryCommand(ModelingSessionService& modeling, QVec3 delta)
        : ModelingMementoCommand(modeling, "Move Selection")
        , delta_(delta) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().moveSelection(delta_);
    }

    QVec3 delta_;
};

/**
 * Applies Transform Selection History Command behavior as a command in the editor coordination layer.
 */
class TransformSelectionHistoryCommand final : public ModelingMementoCommand {
public:
    TransformSelectionHistoryCommand(
            ModelingSessionService& modeling,
            ModelingTransform3 transform,
            QVec3 pivot)
        : ModelingMementoCommand(modeling, "Transform Selection")
        , transform_(transform)
        , pivot_(pivot) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().transformSelection(transform_, pivot_);
    }

    ModelingTransform3 transform_;
    QVec3 pivot_;
};

/**
 * Applies Assign Material Slot To Selection History Command behavior as a command in the editor coordination layer.
 */
class AssignMaterialSlotToSelectionHistoryCommand final : public ModelingMementoCommand {
public:
    AssignMaterialSlotToSelectionHistoryCommand(ModelingSessionService& modeling, std::uint32_t materialSlot)
        : ModelingMementoCommand(modeling, "Assign Material Slot")
        , materialSlot_(materialSlot) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().assignMaterialSlotToSelection(materialSlot_);
    }

    std::uint32_t materialSlot_ = 0;
};

/**
 * Applies Assign Material Slot To Face History Command behavior as a command in the editor coordination layer.
 */
class AssignMaterialSlotToFaceHistoryCommand final : public ModelingMementoCommand {
public:
    AssignMaterialSlotToFaceHistoryCommand(
            ModelingSessionService& modeling,
            int modelingObjectId,
            int face,
            std::uint32_t materialSlot)
        : ModelingMementoCommand(modeling, "Assign Face Material Slot")
        , modelingObjectId_(modelingObjectId)
        , face_(face)
        , materialSlot_(materialSlot) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().assignMaterialSlotToFace(modelingObjectId_, face_, materialSlot_);
    }

    int modelingObjectId_ = -1;
    int face_ = -1;
    std::uint32_t materialSlot_ = 0;
};

/**
 * Applies Paste Selection History Command behavior as a command in the editor coordination layer.
 */
class PasteSelectionHistoryCommand final : public ModelingMementoCommand {
public:
    explicit PasteSelectionHistoryCommand(ModelingSessionService& modeling)
        : ModelingMementoCommand(modeling, "Paste Selection") {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().pasteSelection();
    }
};

/**
 * Applies Repeat Last Action History Command behavior as a command in the editor coordination layer.
 */
class RepeatLastActionHistoryCommand final : public ModelingMementoCommand {
public:
    explicit RepeatLastActionHistoryCommand(ModelingSessionService& modeling)
        : ModelingMementoCommand(modeling, "Repeat Last Action") {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().repeatLastAction();
    }
};

/**
 * Applies Instant Modeling Memento Command behavior as a command in the editor coordination layer.
 */
class InstantModelingMementoCommand final : public ModelingMementoCommand {
public:
    InstantModelingMementoCommand(
            ModelingSessionService& modeling,
            ModelingInstantOperationId operation,
            std::string label)
        : ModelingMementoCommand(modeling, std::move(label))
        , operation_(operation) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().executeInstantOperation(operation_);
    }

    ModelingInstantOperationId operation_;
};

/**
 * Applies Duplicate Meshes History Command behavior as a command in the editor coordination layer.
 */
class DuplicateMeshesHistoryCommand final : public ModelingMementoCommand {
public:
    explicit DuplicateMeshesHistoryCommand(ModelingSessionService& modeling)
        : ModelingMementoCommand(modeling, "Duplicate Meshes") {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().executeInstantOperation(ModelingInstantOperationId::DuplicateMeshes);
    }
};

/**
 * Applies Mirror Selection History Command behavior as a command in the editor coordination layer.
 */
class MirrorSelectionHistoryCommand final : public ModelingMementoCommand {
public:
    MirrorSelectionHistoryCommand(ModelingSessionService& modeling, ModelingMirrorAxis axis, std::string label)
        : ModelingMementoCommand(modeling, std::move(label))
        , axis_(axis) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().mirrorSelection(axis_);
    }

    ModelingMirrorAxis axis_;
};

/**
 * Applies Flip Selection History Command behavior as a command in the editor coordination layer.
 */
class FlipSelectionHistoryCommand final : public ModelingMementoCommand {
public:
    FlipSelectionHistoryCommand(
            ModelingSessionService& modeling,
            ModelingFlipOrientation orientation,
            std::string label)
        : ModelingMementoCommand(modeling, std::move(label))
        , orientation_(orientation) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().flipSelection(orientation_);
    }

    ModelingFlipOrientation orientation_;
};

/**
 * Applies Apply Active Material History Command behavior as a command in the editor coordination layer.
 */
class ApplyActiveMaterialHistoryCommand final : public ModelingMementoCommand {
public:
    explicit ApplyActiveMaterialHistoryCommand(
            ModelingSessionService& modeling,
            std::uint32_t materialSlot = 0)
        : ModelingMementoCommand(modeling, "Apply Material")
        , materialSlot_(materialSlot) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().applyActiveMaterial(materialSlot_);
    }

    std::uint32_t materialSlot_ = 0;
};

/**
 * Applies Commit Transform Preview History Command behavior as a command in the editor coordination layer.
 */
class CommitTransformPreviewHistoryCommand final : public ModelingMementoCommand {
public:
    explicit CommitTransformPreviewHistoryCommand(ModelingSessionService& modeling)
        : ModelingMementoCommand(modeling, "Transform Preview") {}

private:
    ModelingSessionMemento captureBeforeMemento() override {
        ModelingSessionMemento previewHistory = modeling().captureActivePreviewHistoryMemento();
        return previewHistory.valid() ? std::move(previewHistory) : modeling().captureHistoryMemento();
    }

    ModelingCommandResult executeInitial() override {
        return modeling().commitTransformPreview();
    }
};

/**
 * Applies Commit Active Operation Preview History Command behavior as a command in the editor coordination layer.
 */
class CommitActiveOperationPreviewHistoryCommand final : public ModelingMementoCommand {
public:
    explicit CommitActiveOperationPreviewHistoryCommand(ModelingSessionService& modeling)
        : ModelingMementoCommand(modeling, "Commit Operation Preview") {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().commitActiveOperationPreview();
    }
};

} // namespace quader
