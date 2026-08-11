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

inline CommandResult
modeling_memento_command_result(ModelingCommandResult result) {
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
          return command_invalid_result(
              "Modeling command has not been executed");
        }
        return modeling_memento_command_result(
            modeling_.restore_history_memento(before_));
    }

    CommandResult redo() override {
        if (executed_) {
          return modeling_memento_command_result(
              modeling_.restore_history_memento(after_));
        }

        before_ = capture_before_memento();
        CommandResult result =
            modeling_memento_command_result(execute_initial());
        if (result.ok && result.changed) {
          after_ = capture_after_memento();
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

    [[nodiscard]] virtual ModelingSessionMemento capture_before_memento() {
      return modeling_.capture_history_memento();
    }

    [[nodiscard]] virtual ModelingSessionMemento capture_after_memento() {
      return modeling_.capture_history_memento();
    }

private:
  virtual ModelingCommandResult execute_initial() = 0;

  ModelingSessionService &modeling_;
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
            ModelingSessionMemento after,
            bool preserve_scene_selection = false)
        : modeling_(modeling)
        , label_(std::move(label))
        , before_(std::move(before))
        , after_(std::move(after))
        , preserve_scene_selection_(preserve_scene_selection) {}

    [[nodiscard]] const std::string& label() const override { return label_; }

    CommandResult undo() override {
      return modeling_memento_command_result(restore(before_));
    }

    CommandResult redo() override {
      return modeling_memento_command_result(restore(after_));
    }

private:
    ModelingCommandResult restore(const ModelingSessionMemento &memento) {
      return preserve_scene_selection_
                 ? modeling_.restore_history_memento_preserving_scene_selection(
                       memento)
                 : modeling_.restore_history_memento(memento);
    }

    ModelingSessionService& modeling_;
    std::string label_;
    ModelingSessionMemento before_;
    ModelingSessionMemento after_;
    bool preserve_scene_selection_ = false;
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
  ModelingCommandResult execute_initial() override {
    created_ = modeling().create_box_from_bounds(request_);
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
  ModelingCommandResult execute_initial() override {
    created_ = modeling().create_mesh(std::move(request_));
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
 * Creates one or more source-inclusive linear arrays as one history item.
 */
class LinearArrayHistoryCommand final : public ModelingMementoCommand {
public:
  LinearArrayHistoryCommand(ModelingSessionService &modeling,
                            ModelingLinearArrayRequest request)
      : ModelingMementoCommand(modeling, "Linear Array"),
        request_(std::move(request)) {}

  [[nodiscard]] const ModelingLinearArrayResult &created() const {
    return created_;
  }

private:
  ModelingCommandResult execute_initial() override {
    created_ = modeling().create_linear_array(request_);
    return {
        .ok = created_.ok,
        .changed = created_.changed,
        .message = created_.message,
    };
  }

  ModelingLinearArrayRequest request_;
  ModelingLinearArrayResult created_;
};

/**
 * Creates one or more source-inclusive radial arrays as one history item.
 */
class RadialArrayHistoryCommand final : public ModelingMementoCommand {
public:
  RadialArrayHistoryCommand(ModelingSessionService &modeling,
                            ModelingRadialArrayRequest request)
      : ModelingMementoCommand(modeling, "Radial Array"),
        request_(std::move(request)) {}

  [[nodiscard]] const ModelingRadialArrayResult &created() const {
    return created_;
  }

private:
  ModelingCommandResult execute_initial() override {
    created_ = modeling().create_radial_array(request_);
    return {
        .ok = created_.ok,
        .changed = created_.changed,
        .message = created_.message,
    };
  }

  ModelingRadialArrayRequest request_;
  ModelingRadialArrayResult created_;
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
  ModelingCommandResult execute_initial() override {
    return modeling().move_selection(delta_);
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
  ModelingCommandResult execute_initial() override {
    return modeling().transform_selection(transform_, pivot_);
  }

    ModelingTransform3 transform_;
    QVec3 pivot_;
};

/**
 * Applies Assign Material Slot To Selection History Command behavior as a command in the editor coordination layer.
 */
class AssignMaterialSlotToSelectionHistoryCommand final : public ModelingMementoCommand {
public:
  AssignMaterialSlotToSelectionHistoryCommand(ModelingSessionService &modeling,
                                              std::uint32_t material_slot)
      : ModelingMementoCommand(modeling, "Assign Material Slot"),
        material_slot_(material_slot) {}

private:
  ModelingCommandResult execute_initial() override {
    return modeling().assign_material_slot_to_selection(material_slot_);
  }

  std::uint32_t material_slot_ = 0;
};

/**
 * Applies Assign Material Slot To Face History Command behavior as a command in the editor coordination layer.
 */
class AssignMaterialSlotToFaceHistoryCommand final : public ModelingMementoCommand {
public:
  AssignMaterialSlotToFaceHistoryCommand(ModelingSessionService &modeling,
                                         int modeling_object_id, int face,
                                         std::uint32_t material_slot)
      : ModelingMementoCommand(modeling, "Assign Face Material Slot"),
        modeling_object_id_(modeling_object_id), face_(face),
        material_slot_(material_slot) {}

private:
  ModelingCommandResult execute_initial() override {
    return modeling().assign_material_slot_to_face(modeling_object_id_, face_,
                                                   material_slot_);
  }

  int modeling_object_id_ = -1;
  int face_ = -1;
  std::uint32_t material_slot_ = 0;
};

/**
 * Applies Paste Selection History Command behavior as a command in the editor coordination layer.
 */
class PasteSelectionHistoryCommand final : public ModelingMementoCommand {
public:
    explicit PasteSelectionHistoryCommand(ModelingSessionService& modeling)
        : ModelingMementoCommand(modeling, "Paste Selection") {}

private:
  ModelingCommandResult execute_initial() override {
    return modeling().paste_selection();
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
  ModelingCommandResult execute_initial() override {
    return modeling().repeat_last_action();
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
  ModelingCommandResult execute_initial() override {
    return modeling().execute_instant_operation(operation_);
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
  ModelingCommandResult execute_initial() override {
    return modeling().execute_instant_operation(
        ModelingInstantOperationId::DuplicateMeshes);
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
  ModelingCommandResult execute_initial() override {
    return modeling().mirror_selection(axis_);
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
  ModelingCommandResult execute_initial() override {
    return modeling().flip_selection(orientation_);
  }

    ModelingFlipOrientation orientation_;
};

/**
 * Applies Apply Active Material History Command behavior as a command in the editor coordination layer.
 */
class ApplyActiveMaterialHistoryCommand final : public ModelingMementoCommand {
public:
  explicit ApplyActiveMaterialHistoryCommand(ModelingSessionService &modeling,
                                             std::uint32_t material_slot = 0)
      : ModelingMementoCommand(modeling, "Apply Material"),
        material_slot_(material_slot) {}

  ApplyActiveMaterialHistoryCommand(ModelingSessionService &modeling,
                                    MaterialReference material)
      : ModelingMementoCommand(modeling, "Apply Material"),
        material_(material) {}

private:
  ModelingCommandResult execute_initial() override {
    return material_.valid()
               ? modeling().apply_active_material(material_)
               : modeling().apply_active_material(material_slot_);
  }

  std::uint32_t material_slot_ = 0;
  MaterialReference material_;
};

/**
 * Normalizes one mesh to the resolved material sampled from one of its faces.
 * Redo restores the captured post-execution memento and therefore never
 * resamples material state that may have changed since the initial gesture.
 */
class ApplyFaceMaterialToMeshHistoryCommand final
    : public ModelingMementoCommand {
public:
  ApplyFaceMaterialToMeshHistoryCommand(ModelingSessionService &modeling,
                                        int modeling_object_id, int face_id)
      : ModelingMementoCommand(modeling, "Apply Face Material to Mesh"),
        modeling_object_id_(modeling_object_id), face_id_(face_id) {}

private:
  ModelingCommandResult execute_initial() override {
    return modeling().apply_face_material_to_mesh(modeling_object_id_,
                                                  face_id_);
  }

  int modeling_object_id_ = kInvalidModelingObjectId;
  int face_id_ = quader_poly::kInvalidElementId;
};

/**
 * Applies Cut Selected Meshes By Hovered Face Plane History Command behavior as
 * a command in the editor coordination layer.
 */
class CutSelectedMeshesByHoveredFacePlaneHistoryCommand final
    : public ModelingMementoCommand {
public:
  explicit CutSelectedMeshesByHoveredFacePlaneHistoryCommand(
      ModelingSessionService &modeling)
      : ModelingMementoCommand(modeling, "Cut By Face Plane") {}

private:
  ModelingCommandResult execute_initial() override {
    return modeling().cut_selected_meshes_by_hovered_face_plane();
  }
};

/**
 * Applies Commit Transform Preview History Command behavior as a command in the editor coordination layer.
 */
class CommitTransformPreviewHistoryCommand final : public ModelingMementoCommand {
public:
    explicit CommitTransformPreviewHistoryCommand(ModelingSessionService& modeling)
        : ModelingMementoCommand(modeling, "Transform Preview") {}

private:
  ModelingSessionMemento capture_before_memento() override {
    ModelingSessionMemento preview_history =
        modeling().capture_active_preview_history_memento();
    return preview_history.valid() ? std::move(preview_history)
                                   : modeling().capture_history_memento();
  }

  ModelingCommandResult execute_initial() override {
    return modeling().commit_transform_preview();
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
  ModelingCommandResult execute_initial() override {
    return modeling().commit_active_operation_preview();
  }
};

} // namespace quader
