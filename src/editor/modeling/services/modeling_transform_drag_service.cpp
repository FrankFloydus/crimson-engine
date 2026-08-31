////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_transform_drag_service.hpp"

#include "editor/modeling/modeling_viewport_camera_adapter.hpp"
#include "editor/modeling/modeling_result_adapters.hpp"
#include "editor/modeling/runtime/editor_runtime.hpp"

#include "core/i_undoable_command.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "math/glm_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <editor/interactions/move_tool.hpp>
#include <editor/interactions/rotate_tool.hpp>
#include <editor/interactions/scale_tool.hpp>
#include <editor/interactions/tool_transform_math.hpp>
#include <editor/interactions/transform_drag.hpp>

namespace quader {
namespace {

bool selected_scene_targets_are_instance_occurrences(
    const SceneDocument &scene) {
  const std::vector<EntityId> selected = scene.selected_entities();
  return !selected.empty() &&
         std::all_of(selected.begin(), selected.end(), [&](EntityId entity) {
           return scene.instance_occurrence(entity) != nullptr &&
                  !scene.is_effectively_locked(entity);
         });
}

bool selected_scene_targets_support_structural_duplicate(
    const SceneDocument &scene) {
  const std::vector<EntityId> selected = scene.selected_entities();
  return !selected.empty() &&
         std::all_of(selected.begin(), selected.end(), [&](EntityId entity) {
           return scene.exists(entity) && !scene.is_scene_root(entity) &&
                  scene.transform(entity) != nullptr &&
                  !scene.is_effectively_locked(entity);
         });
}

bool selected_scene_targets_are_sdk_modeling_objects(
    const SceneDocument &scene) {
  const std::vector<EntityId> selected = scene.selected_entities();
  return !selected.empty() &&
         std::all_of(selected.begin(), selected.end(), [&](EntityId entity) {
           const ModelingObjectComponent *modeling =
               scene.modeling_object(entity);
           return modeling != nullptr && modeling->sdk_owned &&
                  scene.instance_occurrence(entity) == nullptr &&
                  !scene.is_effectively_locked(entity);
         });
}

quader::QVec3 to_runtime(QVec3 value) { return {value.x, value.y, value.z}; }

quader::QVec2 to_runtime(QVec2 value) { return {value.x, value.y}; }

quader_poly::Transform3 to_runtime(const ModelingTransform3 &transform) {
  return {
      .x_axis = to_runtime(transform.x_axis),
      .y_axis = to_runtime(transform.y_axis),
      .z_axis = to_runtime(transform.z_axis),
      .origin = to_runtime(transform.origin),
  };
}

quader_editor_core::SelectionMode
to_runtime(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return quader_editor_core::SelectionMode::Vertex;
  case ModelingSelectionMode::Edge:
    return quader_editor_core::SelectionMode::Edge;
  case ModelingSelectionMode::Face:
    return quader_editor_core::SelectionMode::Face;
  case ModelingSelectionMode::Object:
  case ModelingSelectionMode::Group:
    return quader_editor_core::SelectionMode::Object;
  }
  return quader_editor_core::SelectionMode::Object;
}

quader_poly::Ray to_runtime(const ModelingRay &ray) {
  return {
      .origin = to_runtime(ray.origin),
      .direction = to_runtime(normalize(ray.direction)),
  };
}

QMat4 matrix_from_runtime_transform(const quader_poly::Transform3 &transform) {
  glm::mat4 matrix{1.0f};
  matrix[0] = glm::vec4{to_glm(transform.x_axis), 0.0f};
  matrix[1] = glm::vec4{to_glm(transform.y_axis), 0.0f};
  matrix[2] = glm::vec4{to_glm(transform.z_axis), 0.0f};
  matrix[3] = glm::vec4{to_glm(transform.origin), 1.0f};
  return from_glm(matrix);
}

std::vector<int> selected_modeling_object_ids(
    const ModelingTransformDragServiceContext &context) {
  std::vector<int> object_ids;
  for (const quader_editor_core::PolygonObject &object :
       context.port.cached_scene_transformed_objects_for_picking()) {
    if (object.selected) {
      object_ids.push_back(object.id);
    }
  }
  return object_ids;
}

std::string
transform_drag_history_label(quader_editor_core::ViewportInteractionTool tool) {
  switch (tool) {
  case quader_editor_core::ViewportInteractionTool::Move:
    return "Move Selection";
  case quader_editor_core::ViewportInteractionTool::Rotate:
    return "Rotate Selection";
  case quader_editor_core::ViewportInteractionTool::Scale:
    return "Scale Selection";
  default:
    return "Transform Selection";
  }
}

/**
 * Records an already-committed modeling preview as an editor history command.
 */
class RecordedModelingPreviewMementoCommand final : public IUndoableCommand {
public:
  using Restore =
      std::function<ModelingCommandResult(const ModelingSessionMemento &)>;

  RecordedModelingPreviewMementoCommand(Restore restore, std::string label,
                                        ModelingSessionMemento before,
                                        ModelingSessionMemento after)
      : restore_(std::move(restore)), label_(std::move(label)),
        before_(std::move(before)), after_(std::move(after)) {}

  [[nodiscard]] const std::string &label() const override { return label_; }

  CommandResult undo() override {
    return command_result_from_modeling(restore_(before_));
  }

  CommandResult redo() override {
    return command_result_from_modeling(restore_(after_));
  }

private:
  Restore restore_;
  std::string label_;
  ModelingSessionMemento before_;
  ModelingSessionMemento after_;
};

/// Records a topology-stable transform as exact sparse authored positions.
class RecordedModelingTransformDeltaCommand final : public IUndoableCommand {
public:
  using Apply = std::function<ModelingCommandResult(
      const ModelingSdkTransformHistoryDelta &, bool)>;

  RecordedModelingTransformDeltaCommand(
      Apply apply, std::string label, ModelingSdkTransformHistoryDelta delta)
      : apply_(std::move(apply)), label_(std::move(label)),
        delta_(std::move(delta)) {}

  [[nodiscard]] const std::string &label() const override { return label_; }

  CommandResult undo() override {
    return command_result_from_modeling(apply_(delta_, false));
  }

  CommandResult redo() override {
    return command_result_from_modeling(apply_(delta_, true));
  }

private:
  Apply apply_;
  std::string label_;
  ModelingSdkTransformHistoryDelta delta_;
};

quader_editor_core::TransformAxis to_runtime(ModelingTransformAxis axis) {
  switch (axis) {
  case ModelingTransformAxis::X:
    return quader_editor_core::TransformAxis::X;
  case ModelingTransformAxis::Y:
    return quader_editor_core::TransformAxis::Y;
  case ModelingTransformAxis::Z:
    return quader_editor_core::TransformAxis::Z;
  case ModelingTransformAxis::XY:
    return quader_editor_core::TransformAxis::Xy;
  case ModelingTransformAxis::XZ:
    return quader_editor_core::TransformAxis::Xz;
  case ModelingTransformAxis::YZ:
    return quader_editor_core::TransformAxis::Yz;
  case ModelingTransformAxis::All:
    return quader_editor_core::TransformAxis::All;
  case ModelingTransformAxis::None:
    return quader_editor_core::TransformAxis::None;
  }
  return quader_editor_core::TransformAxis::None;
}

quader_editor_core::TransformSpace to_runtime(ModelingTransformSpace space) {
  return space == ModelingTransformSpace::Local
             ? quader_editor_core::TransformSpace::Local
             : quader_editor_core::TransformSpace::Global;
}

ModelingTransformAxis from_runtime(quader_editor_core::TransformAxis axis) {
  switch (axis) {
  case quader_editor_core::TransformAxis::X:
    return ModelingTransformAxis::X;
  case quader_editor_core::TransformAxis::Y:
    return ModelingTransformAxis::Y;
  case quader_editor_core::TransformAxis::Z:
    return ModelingTransformAxis::Z;
  case quader_editor_core::TransformAxis::Xy:
    return ModelingTransformAxis::XY;
  case quader_editor_core::TransformAxis::Xz:
    return ModelingTransformAxis::XZ;
  case quader_editor_core::TransformAxis::Yz:
    return ModelingTransformAxis::YZ;
  case quader_editor_core::TransformAxis::All:
    return ModelingTransformAxis::All;
  case quader_editor_core::TransformAxis::None:
    return ModelingTransformAxis::None;
  }
  return ModelingTransformAxis::None;
}

bool transform_drag_axis_supports_vertex_snap(
    quader_editor_core::TransformAxis axis) {
  switch (axis) {
  case quader_editor_core::TransformAxis::X:
  case quader_editor_core::TransformAxis::Y:
  case quader_editor_core::TransformAxis::Z:
    return true;
  case quader_editor_core::TransformAxis::Xy:
  case quader_editor_core::TransformAxis::Xz:
  case quader_editor_core::TransformAxis::Yz:
  case quader_editor_core::TransformAxis::All:
  case quader_editor_core::TransformAxis::None:
    return false;
  }
  return false;
}

bool transform_drag_can_use_vertex_snap(
    const quader_editor_core::TransformDragState &drag) {
  return drag.active &&
         drag.tool == quader_editor_core::ViewportInteractionTool::Move &&
         drag.move.active &&
         transform_drag_axis_supports_vertex_snap(drag.axis);
}

bool transform_drag_has_stationary_vertex_snap_target(
    const ModelingSelectionProjectionSnapshot &session) {
  if (session.selection_mode() !=
      quader_editor_core::SelectionMode::Object) {
    return true;
  }
  return std::any_of(
      session.objects().begin(), session.objects().end(),
      [](const quader_editor_core::PolygonObject &object) {
        return !object.selected;
      });
}

quader_editor_core::ViewportInteractionTool
to_runtime(ModelingTransformTool tool) {
  switch (tool) {
  case ModelingTransformTool::Move:
    return quader_editor_core::ViewportInteractionTool::Move;
  case ModelingTransformTool::Rotate:
    return quader_editor_core::ViewportInteractionTool::Rotate;
  case ModelingTransformTool::Scale:
    return quader_editor_core::ViewportInteractionTool::Scale;
  }
  return quader_editor_core::ViewportInteractionTool::Unknown;
}

ModelingTransformTool
from_runtime(quader_editor_core::ViewportInteractionTool tool) {
  switch (tool) {
  case quader_editor_core::ViewportInteractionTool::Move:
    return ModelingTransformTool::Move;
  case quader_editor_core::ViewportInteractionTool::Rotate:
    return ModelingTransformTool::Rotate;
  case quader_editor_core::ViewportInteractionTool::Scale:
    return ModelingTransformTool::Scale;
  default:
    return ModelingTransformTool::Move;
  }
}

quader_editor_core::TransformDragOperation
to_runtime(ModelingTransformOperation operation) {
  switch (operation) {
  case ModelingTransformOperation::Transform:
    return quader_editor_core::TransformDragOperation::Transform;
  case ModelingTransformOperation::Extrude:
    return quader_editor_core::TransformDragOperation::Extrude;
  case ModelingTransformOperation::Inset:
    return quader_editor_core::TransformDragOperation::Inset;
  }
  return quader_editor_core::TransformDragOperation::Transform;
}

ModelingTransformOperation
from_runtime(quader_editor_core::TransformDragOperation operation) {
  switch (operation) {
  case quader_editor_core::TransformDragOperation::Transform:
    return ModelingTransformOperation::Transform;
  case quader_editor_core::TransformDragOperation::Extrude:
    return ModelingTransformOperation::Extrude;
  case quader_editor_core::TransformDragOperation::Inset:
    return ModelingTransformOperation::Inset;
  }
  return ModelingTransformOperation::Transform;
}

QVec3 from_runtime(quader::QVec3 value) { return {value.x, value.y, value.z}; }

ModelingTransform3 from_runtime(quader_poly::Transform3 value) {
  return {
      .x_axis = from_runtime(value.x_axis),
      .y_axis = from_runtime(value.y_axis),
      .z_axis = from_runtime(value.z_axis),
      .origin = from_runtime(value.origin),
  };
}

} // namespace

ModelingTransformDragPreview
ModelingTransformDragService::begin_transform_drag(
    ModelingTransformDragServiceContext context,
    const ModelingTransformDragBeginRequest &request) {
  QDR_PROFILE_FUNCTION();
  if (context.runtime.transform_drag_active() ||
      context.port.portable_modeling_preview_active() ||
      scene_duplicate_transaction_.has_value() ||
      structural_duplicate_scalar_checkpoint_.has_value()) {
    QDR_PROFILE_SCOPE("Modeling transform drag cancel active preview");
    const ModelingCommandResult canceled = cancel_transform_drag(context);
    if (!canceled.ok) {
      return {
          .ok = false,
          .active = false,
          .changed = canceled.changed,
          .message = canceled.message.empty()
                         ? "Could not cancel the preceding transform drag."
                         : canceled.message,
      };
    }
  }
  const auto fail_after_cancel =
      [this, &context](std::string message,
                       bool prior_changed = false)
      -> ModelingTransformDragPreview {
    const bool was_preview_driven = controller_.preview_driven();
    const ModelingCommandResult canceled = cancel_transform_drag(context);
    if (!canceled.ok) {
      message += "; rollback failed: ";
      message += canceled.message.empty() ? "unknown transform rollback failure"
                                          : canceled.message;
    }
    return {
        .ok = false,
        .active = false,
        .changed = prior_changed || !canceled.ok,
        .preview_driven = was_preview_driven,
        .message = std::move(message),
    };
  };
  bool portable_modeling_selection =
      context.port.has_portable_modeling_selection();
  if ((!portable_modeling_selection &&
       !context.port.has_scene_transform_selection()) ||
      request.axis == ModelingTransformAxis::None) {
    return transform_drag_preview(context, false);
  }

  const bool runtime_has_selection = portable_modeling_selection;
  const bool scene_transform_selection =
      !runtime_has_selection && context.port.has_scene_transform_selection();
  const ModelingSelectionMode selection_mode =
      context.port.portable_modeling_selection_mode();
  const bool duplicate_structural_selection =
      request.duplicate_selected &&
      (selection_mode == ModelingSelectionMode::Object ||
       selection_mode == ModelingSelectionMode::Group);
  if (duplicate_structural_selection &&
      !selected_scene_targets_support_structural_duplicate(context.scene)) {
    return {
        .ok = false,
        .message = "This scene selection does not support drag duplication.",
    };
  }
  const quader_editor_core::SelectionMode drag_selection_mode =
      scene_transform_selection
          ? quader_editor_core::SelectionMode::Object
          : (portable_modeling_selection
                 ? to_runtime(context.port.portable_modeling_selection_mode())
                 : context.session.selection_mode());
  const quader_editor_core::ViewportInteractionTool tool =
      to_runtime(request.tool);
  const quader_editor_core::TransformDragOperation operation =
      quader_editor_core::sanitize_transform_drag_operation(
          to_runtime(request.operation), tool, drag_selection_mode);
  const quader_editor_core::TransformAxis axis = to_runtime(request.axis);
  const quader::QVec3 pivot = to_runtime(request.pivot);
  TransformDragPreviewRouting preview_routing =
      TransformDragController::preview_routing(
          request.duplicate_selected,
          drag_selection_mode == quader_editor_core::SelectionMode::Object,
          operation == quader_editor_core::TransformDragOperation::Transform,
          scene_transform_selection);
  if (portable_modeling_selection) {
    preview_routing.preview_driven = true;
    preview_routing.needs_runtime_preview_transaction = true;
    preview_routing.scene_transform_preview_eligible = false;
  }

  std::vector<ModelingStructuralEntityClone> structural_modeling_clones;
  if (duplicate_structural_selection) {
    if (context.structural_duplicate == nullptr || context.history == nullptr) {
      return {
          .ok = false,
          .message = "Scene drag duplication requires structural lifecycle "
                     "and history ownership.",
      };
    }
    const bool duplicate_instances =
        selected_scene_targets_are_instance_occurrences(context.scene);
    const std::string duplicate_label =
        duplicate_instances ? "Duplicate Instance" : "Duplicate Selection";
    scene_duplicate_transaction_.emplace(
        context.history->begin_transaction(duplicate_label));
    if (!scene_duplicate_transaction_->active()) {
      scene_duplicate_transaction_.reset();
      return {
          .ok = false,
          .message = "Could not start scene drag duplication while "
                     "another editor transaction is active.",
      };
    }

    structural_duplicate_scalar_checkpoint_.emplace(
        context.scene.capture_scalar_compensation_checkpoint());
    if (!structural_duplicate_scalar_checkpoint_->valid()) {
      const CommandResult canceled = scene_duplicate_transaction_->cancel();
      scene_duplicate_transaction_.reset();
      structural_duplicate_scalar_checkpoint_.reset();
      return {
          .ok = false,
          .changed = !canceled.ok,
          .message = canceled.ok
                         ? "Scene scalar compensation is unavailable for "
                           "drag duplication."
                         : "Scene scalar compensation is unavailable for "
                           "drag duplication; empty transaction cleanup "
                           "failed: " +
                               canceled.message,
      };
    }
    structural_duplicate_selection_before_.emplace(
        capture_scene_selection(context.scene));

    ModelingStructuralDuplicateResult duplicated =
        (*context.structural_duplicate)(
            *scene_duplicate_transaction_,
            context.port.portable_modeling_selection_mode(), duplicate_label);
    if (!duplicated.ok || !duplicated.changed) {
      return fail_after_cancel(
          duplicated.message.empty()
              ? "Could not duplicate the scene selection."
              : std::move(duplicated.message),
          duplicated.changed);
    }
    structural_modeling_clones = std::move(duplicated.modeling_objects);
    structural_duplicate_active_ = true;
    context.port.refresh_transform_drag_projection();
    portable_modeling_selection =
        context.port.has_portable_modeling_selection();
    context.port.invalidate_selection_overlay_caches();
  }

  {
    QDR_PROFILE_SCOPE("Modeling transform drag begin runtime state");
    context.runtime.begin_transform_drag_state(
        tool, operation, axis, to_runtime(request.space),
        to_runtime(request.orientation), pivot);
  }
  {
    QDR_PROFILE_SCOPE("Modeling transform drag configure state");
    quader_editor_core::TransformDragState drag =
        context.runtime.transform_drag_state();
    drag.scene_transform_preview_eligible =
        preview_routing.scene_transform_preview_eligible;
    context.runtime.set_transform_drag_state(drag);
  }
  {
    QDR_PROFILE_SCOPE("Modeling transform drag set preview routing");
    controller_.set_preview_driven(preview_routing.preview_driven,
                                   request.duplicate_selected);
  }

  if (preview_routing.needs_runtime_preview_transaction) {
    QDR_PROFILE_SCOPE("Modeling transform drag runtime preview transaction");
    if (!portable_modeling_selection) {
      const ModelingCommandResult canceled = cancel_transform_drag(context);
      return {.ok = false,
              .changed = !canceled.ok,
              .message = !canceled.ok
                             ? "Modeling transform preview requires an SDK "
                               "selection; rollback failed: " +
                                   canceled.message
                             : "Modeling transform preview requires an SDK "
                               "selection."};
    }
    const ModelingCommandResult portable_begin =
        context.port.begin_portable_transform_preview(
            request.duplicate_selected && !duplicate_structural_selection,
            operation ==
                    quader_editor_core::TransformDragOperation::Transform &&
                !request.duplicate_selected,
            structural_modeling_clones);
    const quader_editor_core::CommandResult begin_preview{
        .ok = portable_begin.ok,
        .changed = portable_begin.changed,
        .message = portable_begin.message,
    };
    if (!begin_preview.ok) {
      const ModelingCommandResult canceled = cancel_transform_drag(context);
      return {
          .ok = false,
          .changed = !canceled.ok,
          .message = begin_preview.message.empty()
                         ? (!canceled.ok
                                ? "Could not start transform preview; "
                                  "rollback failed: " + canceled.message
                                : "Could not start transform preview.")
                         : (!canceled.ok
                                ? begin_preview.message +
                                      "; rollback failed: " + canceled.message
                                : begin_preview.message),
      };
    }
    // A topology-stable transform does not change semantic selection. Its
    // retained overlay is driven by the transient position revision, so keep
    // the expensive semantic/projected bases alive for the entire drag.
    if (operation !=
            quader_editor_core::TransformDragOperation::Transform ||
        request.duplicate_selected || begin_preview.changed) {
      QDR_PROFILE_SCOPE("Modeling transform drag invalidate overlay caches");
      context.port.invalidate_selection_overlay_caches();
    }
    if (begin_preview.changed) {
      {
        QDR_PROFILE_SCOPE(
            "Modeling transform drag sync scene selection after begin");
        context.port.sync_scene_selection_from_modeling();
      }
    }
  }

  if (preview_routing.scene_transform_preview_eligible && !([&]() {
        QDR_PROFILE_SCOPE(
            "Modeling transform drag begin scene transform preview");
        return scene_transform_drag_service_.begin(
            context.scene, request.pivot, true, "Transform Selection",
            context.port.scene_selection_display_from_scene(),
            &context.assets,
            context.document_commands != nullptr
                ? context.document_commands->texture_lock_settings()
                : TextureLockSettings{});
      }())) {
    return fail_after_cancel("Could not start scene transform preview.");
  }

  const quader_editor_core::ViewportCameraState camera =
      modeling_runtime_camera(request.camera);
  const quader::QVec2 screen_position = to_runtime(request.screen_position);
  const quader::QVec2 screen_pivot = to_runtime(request.screen_pivot);
  const quader::QVec2 screen_tip = to_runtime(request.screen_tip);

  if (tool == quader_editor_core::ViewportInteractionTool::Move) {
    quader_editor_core::MoveBeginInput begin;
    begin.axis = axis;
    begin.orientation = to_runtime(request.orientation);
    begin.has_world_direction = request.has_world_direction;
    begin.world_direction = to_runtime(request.world_direction);
    begin.center_local = {};
    begin.has_world_center = true;
    begin.world_center = pivot;
    begin.has_screen_axis = true;
    begin.screen_pivot = screen_pivot;
    begin.screen_tip = screen_tip;
    begin.screen_position = screen_position;
    begin.has_start_ray = true;
    begin.start_ray = to_runtime(request.ray);
    begin.has_camera = true;
    begin.camera_transform = camera.transform;
    const quader_editor_core::MoveBeginResult begin_result = [&]() {
      QDR_PROFILE_SCOPE("Modeling transform drag begin move");
      return quader_editor_core::begin_move_drag(begin);
    }();
    if (!begin_result.ok) {
      return fail_after_cancel(begin_result.message.empty()
                                   ? "Move drag failed."
                                   : begin_result.message);
    }
    context.runtime.set_transform_drag_move_state(begin_result.state);
  } else if (tool == quader_editor_core::ViewportInteractionTool::Rotate) {
    quader_editor_core::RotateBeginInput begin;
    begin.axis = axis;
    begin.orientation = to_runtime(request.orientation);
    begin.object_ids = portable_modeling_selection
                           ? selected_modeling_object_ids(context)
                           : context.session.selected_object_ids();
    begin.pivot = pivot;
    begin.screen_position = screen_position;
    begin.has_screen_pivot = true;
    begin.screen_pivot = screen_pivot;
    begin.has_screen_tip = true;
    begin.screen_tip = screen_tip;
    begin.has_world_direction = request.has_world_direction;
    begin.world_direction = to_runtime(request.world_direction);
    begin.has_world_pivot = true;
    begin.world_pivot = pivot;
    begin.has_camera = true;
    begin.camera_transform = camera.transform;
    const quader_editor_core::RotateBeginResult begin_result = [&]() {
      QDR_PROFILE_SCOPE("Modeling transform drag begin rotate");
      return quader_editor_core::begin_rotate_drag(begin);
    }();
    if (!begin_result.ok) {
      return fail_after_cancel(begin_result.message.empty()
                                   ? "Rotate drag failed."
                                   : begin_result.message);
    }
    context.runtime.set_transform_drag_rotate_state(begin_result.state);
  } else if (tool == quader_editor_core::ViewportInteractionTool::Scale) {
    quader_editor_core::ScaleBeginInput begin;
    begin.axis = axis;
    begin.orientation = to_runtime(request.orientation);
    begin.object_ids = portable_modeling_selection
                           ? selected_modeling_object_ids(context)
                           : context.session.selected_object_ids();
    begin.pivot = pivot;
    begin.screen_position = screen_position;
    begin.has_screen_pivot = true;
    begin.screen_pivot = screen_pivot;
    begin.has_screen_tip = true;
    begin.screen_tip = screen_tip;
    const quader_editor_core::Bounds3 bounds = [&]() {
      QDR_PROFILE_SCOPE("Modeling transform drag scale bounds");
      return context.port.transform_drag_bounds();
    }();
    begin.bounds.has_bounds = !bounds.empty;
    begin.bounds.min = bounds.min;
    begin.bounds.max = bounds.max;
    const quader_editor_core::ScaleBeginResult begin_result = [&]() {
      QDR_PROFILE_SCOPE("Modeling transform drag begin scale");
      return quader_editor_core::begin_scale_drag(begin);
    }();
    if (!begin_result.ok) {
      return fail_after_cancel(begin_result.message.empty()
                                   ? "Scale drag failed."
                                   : begin_result.message);
    }
    context.runtime.set_transform_drag_scale_state(begin_result.state);
  } else {
    return fail_after_cancel(
        "Transform drag only supports move, rotate, or scale.");
  }

  {
    QDR_PROFILE_SCOPE("Modeling transform drag first preview apply");
    return apply_transform_drag_preview(
        context, {
                     .screen_position = request.screen_position,
                     .ray = request.ray,
                     .camera = request.camera,
                     .grid_size = request.grid_size,
                     .vertex_snap_radius = request.vertex_snap_radius,
                     .grid_snap_enabled = request.grid_snap_enabled,
                     .rotation_snap_degrees =
                         request.rotation_snap_degrees,
                     .precision = request.precision,
                 });
  }
}

ModelingTransformDragPreview
ModelingTransformDragService::update_transform_drag(
    ModelingTransformDragServiceContext context,
    const ModelingTransformDragUpdateRequest &request) {
  QDR_PROFILE_FUNCTION();
  if (!context.runtime.transform_drag_active()) {
    if (context.port.portable_modeling_preview_active() ||
        scene_transform_drag_service_.preview_active() ||
        scene_duplicate_transaction_.has_value() ||
        structural_duplicate_scalar_checkpoint_.has_value()) {
      const ModelingCommandResult canceled = cancel_transform_drag(context);
      return {
          .ok = false,
          .active = false,
          .changed = !canceled.ok,
          .message = canceled.ok
                         ? "Transform drag state ended before its preview."
                         : "Transform drag state ended before its preview; "
                           "rollback failed: " +
                               canceled.message,
      };
    }
    return transform_drag_preview(context, false);
  }
  return apply_transform_drag_preview(context, request);
}

ModelingTransformDragPreview
ModelingTransformDragService::commit_transform_drag(
    ModelingTransformDragServiceContext context, EditorHistoryService *history) {
  QDR_PROFILE_FUNCTION();
  const bool was_preview_driven = controller_.preview_driven();
  const bool scene_duplicate_transaction_active =
      scene_duplicate_transaction_.has_value() &&
      scene_duplicate_transaction_->active();
  if (scene_duplicate_transaction_active && history == nullptr) {
    const ModelingCommandResult canceled = cancel_transform_drag(context);
    return {
        .ok = false,
        .changed = canceled.changed,
        .preview_driven = was_preview_driven,
        .message = canceled.ok
                       ? "Linked instance drag duplication requires editor "
                         "history when committing."
                       : canceled.message,
    };
  }
  const bool finish_preview_transaction =
      was_preview_driven ||
      (controller_.duplicate_selected() &&
       !scene_duplicate_transaction_active);
  const quader_editor_core::TransformDragState repeat_drag =
      context.runtime.transform_drag_state();
  const bool repeatable_scene_modeling_transform =
      repeat_drag.scene_transform_preview_eligible &&
      selected_scene_targets_are_sdk_modeling_objects(context.scene);
  const bool sparse_transform_history =
      finish_preview_transaction && history != nullptr &&
      context.port.transient_transform_preview_active() &&
      !controller_.duplicate_selected() &&
      repeat_drag.operation ==
          quader_editor_core::TransformDragOperation::Transform;
  ModelingSessionMemento preview_before =
      finish_preview_transaction && history != nullptr &&
              context.port.portable_modeling_preview_active() &&
              !sparse_transform_history
          ? context.port.capture_active_preview_history_memento()
          : ModelingSessionMemento();
  bool changed = false;

  if (finish_preview_transaction) {
    const bool portable_preview =
        context.port.portable_modeling_preview_active();
    if (!portable_preview) {
      const ModelingCommandResult rollback = cancel_transform_drag(context);
      return {
          .ok = false,
          .changed = !rollback.ok,
          .preview_driven = was_preview_driven,
          .message = rollback.ok
                         ? "Portable transform preview is no longer active."
                         : "Portable transform preview is no longer active; "
                           "rollback failed: " +
                               rollback.message,
      };
    }
    const ModelingCommandResult committed =
        context.port.commit_portable_translate_preview();
    if (!committed.ok) {
      const ModelingCommandResult rollback = cancel_transform_drag(context);
      return {
          .ok = false,
          .changed = !rollback.ok,
          .preview_driven = was_preview_driven,
          .message = !rollback.ok
                         ? (committed.message.empty()
                                ? "Could not commit portable transform "
                                  "preview; rollback failed: " +
                                      rollback.message
                                : committed.message + "; rollback failed: " +
                                      rollback.message)
                         : (committed.message.empty()
                                ? "Could not commit portable transform "
                                  "preview."
                                : committed.message),
      };
    }
    changed = committed.changed;
    context.port.invalidate_selection_overlay_caches();
    if (changed && history != nullptr && sparse_transform_history) {
      QDR_PROFILE_SCOPE("Modeling transform drag record sparse history");
      ModelingSdkTransformHistoryDelta delta =
          context.port.take_committed_transform_history_delta();
      if (!delta.valid()) {
        clear_runtime_state(context);
        return {
            .ok = false,
            .changed = true,
            .preview_driven = was_preview_driven,
            .message = "Committed transform did not publish sparse history.",
        };
      }
      ModelingTransformDragPort *port = &context.port;
      const ModelingSdkTransformHistoryDelta rollback_delta = delta;
      const CommandResult recorded = history->record_executed(
          std::make_unique<RecordedModelingTransformDeltaCommand>(
              [port](const ModelingSdkTransformHistoryDelta &history_delta,
                     bool use_after) {
                return port->apply_transform_history_delta(history_delta,
                                                           use_after);
              },
              transform_drag_history_label(repeat_drag.tool),
              std::move(delta)));
      if (!recorded.ok) {
        QDR_PROFILE_SCOPE(
            "Modeling transform drag rollback sparse history failure");
        const ModelingCommandResult restored =
            context.port.apply_transform_history_delta(rollback_delta, false);
        clear_runtime_state(context);
        return {
            .ok = false,
            .changed = !restored.ok,
            .preview_driven = was_preview_driven,
            .message = !restored.ok
                           ? (restored.message.empty()
                                  ? "Could not roll back a sparse transform "
                                    "after history recording failed."
                                  : restored.message)
                           : (recorded.message.empty()
                                  ? "Could not record transform drag history."
                                  : recorded.message),
        };
      }
    } else if (changed && history != nullptr && preview_before.valid()) {
      QDR_PROFILE_SCOPE("Modeling transform drag record snapshot history");
      const ModelingSessionMemento preview_after =
          context.port.capture_history_memento();
      const ModelingSessionMemento rollback_before = preview_before;
      ModelingTransformDragPort *port = &context.port;
      const CommandResult recorded = history->record_executed(
          std::make_unique<RecordedModelingPreviewMementoCommand>(
              [port](const ModelingSessionMemento &memento) {
                return port->restore_history_memento(memento);
              },
              transform_drag_history_label(repeat_drag.tool),
              preview_before, preview_after));
      if (!recorded.ok) {
        ModelingCommandResult restored{
            .ok = true,
            .changed = false,
        };
        ModelingCommandResult structural_rollback{
            .ok = true,
            .changed = false,
        };
        if (structural_duplicate_scalar_checkpoint_.has_value()) {
          structural_rollback = cancel_transform_drag(context);
        } else {
          restored = context.port.restore_history_memento(rollback_before);
          structural_rollback = cancel_transform_drag(context);
        }
        return {
            .ok = false,
            .changed = !restored.ok || !structural_rollback.ok,
            .preview_driven = was_preview_driven,
            .message = !structural_rollback.ok
                           ? (recorded.message.empty()
                                  ? "Could not record transform drag history; "
                                    "structural rollback failed: " +
                                        structural_rollback.message
                                  : recorded.message +
                                        "; structural rollback failed: " +
                                        structural_rollback.message)
                           : (!restored.ok
                                  ? (restored.message.empty()
                                         ? "Could not roll back a transform "
                                           "drag after history recording "
                                           "failed."
                                         : restored.message)
                                  : (recorded.message.empty()
                                         ? "Could not record transform drag "
                                           "history."
                                         : recorded.message)),
        };
      }
    }
  }

  if (scene_transform_drag_service_.preview_active()) {
    const DocumentCommandResult scene_commit = [&]() {
      QDR_PROFILE_SCOPE(
          "Modeling transform drag commit scene transform preview");
      return history != nullptr ? scene_transform_drag_service_.commit(*history)
                                : scene_transform_drag_service_.accept();
    }();
    if (!scene_commit.ok) {
      const ModelingCommandResult rollback = cancel_transform_drag(context);
      return {
          .ok = false,
          .changed = !rollback.ok,
          .preview_driven = was_preview_driven,
          .message = !rollback.ok
                         ? (scene_commit.message.empty()
                                ? "Could not commit scene transform preview; "
                                  "rollback failed: " +
                                      rollback.message
                                : scene_commit.message + "; rollback failed: " +
                                      rollback.message)
                         : (scene_commit.message.empty()
                                ? "Could not commit scene transform preview."
                                : scene_commit.message),
      };
    }
    changed = changed || scene_commit.changed;
    if (scene_commit.changed) {
      context.port.invalidate_selection_overlay_caches();
    }
  }

  if (scene_duplicate_transaction_.has_value()) {
    if (!structural_duplicate_scalar_checkpoint_.has_value() ||
        !structural_duplicate_scalar_checkpoint_->valid() ||
        !structural_duplicate_selection_before_.has_value()) {
      const CommandResult transaction_commit =
          scene_duplicate_transaction_->commit();
      scene_duplicate_transaction_.reset();
      if (structural_duplicate_scalar_checkpoint_.has_value()) {
        (void)context.scene.discard_scalar_compensation_checkpoint(
            *structural_duplicate_scalar_checkpoint_);
      }
      structural_duplicate_scalar_checkpoint_.reset();
      structural_duplicate_selection_before_.reset();
      clear_runtime_state(context);
      return {
          .ok = false,
          .changed = true,
          .preview_driven = was_preview_driven,
          .message = transaction_commit.ok
                         ? "Structural drag scalar compensation state is "
                           "unavailable."
                         : "Structural drag scalar compensation state is "
                           "unavailable; applied duplicate state could not "
                           "be retained in history: " +
                               transaction_commit.message,
      };
    }

    if (!context.scene.acknowledge_scalar_compensation_checkpoint(
            *structural_duplicate_scalar_checkpoint_)) {
      const CommandResult transaction_commit =
          scene_duplicate_transaction_->commit();
      scene_duplicate_transaction_.reset();
      (void)context.scene.discard_scalar_compensation_checkpoint(
          *structural_duplicate_scalar_checkpoint_);
      structural_duplicate_scalar_checkpoint_.reset();
      structural_duplicate_selection_before_.reset();
      clear_runtime_state(context);
      return {
          .ok = false,
          .changed = true,
          .preview_driven = was_preview_driven,
          .message = transaction_commit.ok
                         ? "Structural drag scalar compensation boundary "
                           "could not be acknowledged."
                         : "Structural drag scalar compensation boundary "
                           "could not be acknowledged; applied duplicate "
                           "state could not be retained in history: " +
                               transaction_commit.message,
      };
    }

    const CommandResult transaction_commit =
        scene_duplicate_transaction_->commit();
    scene_duplicate_transaction_.reset();
    const bool scalar_boundary_valid =
        context.scene.begin_scalar_compensation_restore(
            *structural_duplicate_scalar_checkpoint_);
    const bool scalar_scope_released =
        context.scene.discard_scalar_compensation_checkpoint(
            *structural_duplicate_scalar_checkpoint_);
    structural_duplicate_scalar_checkpoint_.reset();
    structural_duplicate_selection_before_.reset();
    if (!scalar_boundary_valid || !scalar_scope_released) {
      clear_runtime_state(context);
      return {
          .ok = false,
          .changed = true,
          .preview_driven = was_preview_driven,
          .message = !scalar_boundary_valid
                         ? "Scene changed while the structural drag duplicate "
                           "was committed; scalar rollback was rejected."
                         : "Structural drag scalar compensation scope could "
                           "not be released.",
      };
    }
    if (!transaction_commit.ok) {
      clear_runtime_state(context);
      return {
          .ok = false,
          // The editor transaction has already detached its commands. Until
          // an explicit rollback can prove otherwise, the centrally created
          // hierarchy remains observable and must keep the project dirty.
          .changed = true,
          .preview_driven = was_preview_driven,
          .message = transaction_commit.message.empty()
                         ? "Could not record linked instance drag history."
                         : transaction_commit.message,
      };
    }
    changed = changed || transaction_commit.changed;
  }

  // Record only after every portable, scene, and enclosing duplicate
  // transaction has committed. Component transforms are preview-driven;
  // direct SDK modeling-object transforms are scene-driven. Other scene-only
  // targets, such as lights, do not have a portable replay contract.
  if ((was_preview_driven || structural_duplicate_active_ ||
       repeatable_scene_modeling_transform) &&
      changed && repeat_drag.active) {
    context.port.record_portable_transform(
        repeat_drag, controller_.duplicate_selected());
  }

  ModelingTransformDragPreview preview =
      transform_drag_preview(context, changed);
  preview.preview_driven = was_preview_driven;
  clear_runtime_state(context);
  return preview;
}

ModelingCommandResult ModelingTransformDragService::cancel_transform_drag(
    ModelingTransformDragServiceContext context) {
  QDR_PROFILE_FUNCTION();
  std::string message;
  const auto append_failure = [&message](std::string_view prefix,
                                         const std::string &detail = {}) {
    if (!message.empty()) {
      message += "; ";
    }
    message.append(prefix);
    if (!detail.empty()) {
      message += detail;
    }
  };

  const bool structural_scope_required =
      scene_duplicate_transaction_.has_value() || structural_duplicate_active_ ||
      structural_duplicate_scalar_checkpoint_.has_value();
  bool scalar_inverse_started = false;
  if (structural_scope_required) {
    if (!structural_duplicate_scalar_checkpoint_.has_value() ||
        !structural_duplicate_scalar_checkpoint_->valid() ||
        !structural_duplicate_selection_before_.has_value()) {
      append_failure(
          "structural drag scalar compensation state is unavailable");
    } else if (!context.scene.acknowledge_scalar_compensation_checkpoint(
                   *structural_duplicate_scalar_checkpoint_)) {
      append_failure(
          "structural drag scalar compensation boundary could not be "
          "acknowledged");
    } else if (!context.scene.begin_scalar_compensation_restore(
                   *structural_duplicate_scalar_checkpoint_)) {
      append_failure(
          "Scene changed outside the structural drag compensation boundary; "
          "rollback was rejected");
    } else {
      scalar_inverse_started = true;
    }

    if (!scalar_inverse_started) {
      if (structural_duplicate_scalar_checkpoint_.has_value()) {
        if (!context.scene.discard_scalar_compensation_checkpoint(
                *structural_duplicate_scalar_checkpoint_)) {
          append_failure(
              "structural drag scalar compensation scope could not be "
              "released");
        }
      }
      structural_duplicate_scalar_checkpoint_.reset();
      structural_duplicate_selection_before_.reset();

      if (context.port.portable_modeling_preview_active()) {
        const ModelingCommandResult preserved =
            context.port.commit_portable_translate_preview();
        if (!preserved.ok) {
          append_failure("SDK preview preservation failed: ",
                         preserved.message);
        }
      }
      if (scene_transform_drag_service_.preview_active()) {
        const DocumentCommandResult preserved =
            scene_transform_drag_service_.accept();
        if (!preserved.ok) {
          append_failure("scene preview preservation failed: ",
                         preserved.message);
        }
      }
      if (scene_duplicate_transaction_.has_value()) {
        const CommandResult preserved =
            scene_duplicate_transaction_->active()
                ? scene_duplicate_transaction_->commit()
                : command_unchanged_result();
        scene_duplicate_transaction_.reset();
        if (!preserved.ok) {
          append_failure("structural transaction preservation failed: ",
                         preserved.message);
        }
      }
      clear_runtime_state(context);
      return {
          .ok = false,
          .changed = true,
          .message = std::move(message),
      };
    }
  }

  const bool was_scene_preview_active =
      scene_transform_drag_service_.preview_active();
  DocumentCommandResult scene_cancel = document_command_unchanged_result();
  if (was_scene_preview_active) {
    scene_cancel = scene_transform_drag_service_.cancel();
    if (scene_cancel.changed) {
      context.port.invalidate_selection_overlay_caches();
    }
  }

  ModelingCommandResult preview_cancel{
      .ok = true,
      .changed = false,
  };
  if (context.port.portable_modeling_preview_active()) {
    preview_cancel = context.port.cancel_portable_translate_preview();
    if (preview_cancel.changed) {
      context.port.invalidate_selection_overlay_caches();
    }
  }

  CommandResult transaction_cancel = command_unchanged_result();
  if (scene_duplicate_transaction_.has_value()) {
    if (scene_duplicate_transaction_->active()) {
      transaction_cancel = scene_duplicate_transaction_->cancel();
    }
    scene_duplicate_transaction_.reset();
    if (transaction_cancel.changed) {
      context.port.invalidate_selection_overlay_caches();
    }
  }

  if (!scene_cancel.ok) {
    append_failure("scene preview cancellation failed: ",
                   scene_cancel.message.empty() ? "unknown failure"
                                                : scene_cancel.message);
  }
  if (!preview_cancel.ok) {
    append_failure("SDK preview cancellation failed: ",
                   preview_cancel.message.empty() ? "unknown failure"
                                                  : preview_cancel.message);
  }
  if (!transaction_cancel.ok) {
    append_failure("structural transaction cancellation failed: ",
                   transaction_cancel.message.empty()
                       ? "unknown failure"
                       : transaction_cancel.message);
  }

  bool selection_restored = true;
  bool scalar_restored = true;
  if (scalar_inverse_started) {
    selection_restored = structural_duplicate_selection_before_.has_value() &&
                         same_scene_selection(
                             *structural_duplicate_selection_before_,
                             capture_scene_selection(context.scene));
    if (!selection_restored) {
      append_failure(
          "structural drag rollback did not restore the exact scene selection");
    }

    if (scene_cancel.ok && preview_cancel.ok && transaction_cancel.ok &&
        selection_restored) {
      scalar_restored =
          context.scene.restore_scalar_compensation_checkpoint(
              *structural_duplicate_scalar_checkpoint_);
      if (!scalar_restored) {
        append_failure("structural drag scalar compensation failed");
      }
    } else {
      scalar_restored = false;
      (void)context.scene.discard_scalar_compensation_checkpoint(
          *structural_duplicate_scalar_checkpoint_);
    }
    structural_duplicate_scalar_checkpoint_.reset();
    structural_duplicate_selection_before_.reset();
  }

  clear_runtime_state(context);
  return {
      .ok = scene_cancel.ok && preview_cancel.ok && transaction_cancel.ok &&
            selection_restored && scalar_restored,
      .changed = scene_cancel.changed || preview_cancel.changed ||
                 transaction_cancel.changed ||
                 !scene_cancel.ok || !preview_cancel.ok ||
                 !transaction_cancel.ok || !selection_restored ||
                 !scalar_restored,
      .message = std::move(message),
  };
}

bool ModelingTransformDragService::preview_driven() const {
  return controller_.preview_driven();
}

bool ModelingTransformDragService::duplicate_selected() const {
  return controller_.duplicate_selected();
}

bool ModelingTransformDragService::scene_preview_active() const {
  return scene_transform_drag_service_.preview_active();
}

void ModelingTransformDragService::clear_runtime_state(
    ModelingTransformDragServiceContext context) {
  if (structural_duplicate_scalar_checkpoint_.has_value()) {
    (void)context.scene.discard_scalar_compensation_checkpoint(
        *structural_duplicate_scalar_checkpoint_);
    structural_duplicate_scalar_checkpoint_.reset();
  }
  structural_duplicate_selection_before_.reset();
  context.runtime.clear_transform_drag_state();
  context.runtime.clear_gizmo_hover();
  controller_.clear();
  scene_transform_drag_service_.reset();
  structural_duplicate_active_ = false;
}

ModelingTransformDragPreview
ModelingTransformDragService::transform_drag_preview(
    const ModelingTransformDragServiceContext &context, bool changed) const {
  ModelingTransformDragPreview preview;
  preview.ok = true;
  const quader_editor_core::TransformDragState drag =
      context.runtime.transform_drag_state();
  preview.active = drag.active;
  preview.changed = changed;
  preview.preview_driven = controller_.preview_driven();
  preview.tool = from_runtime(drag.tool);
  preview.operation = from_runtime(drag.operation);
  preview.axis = from_runtime(drag.axis);
  preview.pivot = from_runtime(drag.pivot);
  preview.move_delta = from_runtime(drag.move.preview_delta);
  preview.rotate_angle_degrees = drag.rotate.preview_angle_degrees;
  preview.scale = from_runtime(drag.scale.preview_scale);
  preview.content_revision = context.port.modeling_content_revision();
  preview.selection_revision = context.port.modeling_selection_revision();
  preview.scene_transform = drag.scene_transform_preview_eligible;
  quader_poly::Transform3 scene_transform;
  if (scene_preview_transform_for_drag(drag, scene_transform)) {
    preview.has_scene_preview_transform = true;
    preview.scene_preview_transform = from_runtime(scene_transform);
  }
  return preview;
}

bool ModelingTransformDragService::scene_preview_transform_for_drag(
    const quader_editor_core::TransformDragState &drag,
    quader_poly::Transform3 &out_transform) const {
  out_transform = {};
  if (!drag.active || !drag.scene_transform_preview_eligible ||
      drag.operation != quader_editor_core::TransformDragOperation::Transform) {
    return false;
  }
  if (drag.space == quader_editor_core::TransformSpace::Local &&
      (drag.tool == quader_editor_core::ViewportInteractionTool::Rotate ||
       drag.tool == quader_editor_core::ViewportInteractionTool::Scale)) {
    // A local rotate/scale preview is applied independently in every target's
    // authored frame, so no single world affine represents the whole batch.
    return false;
  }
  if (drag.tool == quader_editor_core::ViewportInteractionTool::Move) {
    out_transform.origin = drag.move.preview_delta;
    return true;
  }
  if (drag.tool == quader_editor_core::ViewportInteractionTool::Rotate &&
      drag.rotate.active) {
    quader_editor_core::RotateToolState rotate = drag.rotate;
    const quader_editor_core::RotatePreviewResult preview =
        quader_editor_core::preview_rotate_from_angle_degrees(
            rotate, drag.rotate.preview_angle_degrees, 0.0);
    if (!preview.ok) {
      return false;
    }
    out_transform = preview.pivoted_transform;
    return true;
  }
  if (drag.tool == quader_editor_core::ViewportInteractionTool::Scale &&
      drag.scale.active) {
    quader_editor_core::ScaleToolState scale = drag.scale;
    const quader_editor_core::ScalePreviewResult preview =
        quader_editor_core::preview_scale_from_scale(scale,
                                                     drag.scale.preview_scale);
    if (!preview.ok) {
      return false;
    }
    out_transform = preview.pivoted_transform;
    return true;
  }
  return false;
}

ModelingTransformDragPreview
ModelingTransformDragService::apply_transform_drag_preview(
    ModelingTransformDragServiceContext context,
    const ModelingTransformDragUpdateRequest &request) {
  QDR_PROFILE_FUNCTION();
  const auto fail_after_cancel =
      [this, &context](std::string message)
      -> ModelingTransformDragPreview {
    const bool was_preview_driven = controller_.preview_driven();
    const ModelingCommandResult canceled = cancel_transform_drag(context);
    if (!canceled.ok) {
      message += "; rollback failed: ";
      message += canceled.message.empty() ? "unknown transform rollback failure"
                                          : canceled.message;
    }
    return {
        .ok = false,
        .active = false,
        .changed = !canceled.ok,
        .preview_driven = was_preview_driven,
        .message = std::move(message),
    };
  };
  const quader_editor_core::TransformDragState drag_before_update =
      context.runtime.transform_drag_state();
  quader_editor_core::TransformDragPreviewInput input;
  input.camera = modeling_runtime_camera(request.camera);
  input.screen_position = to_runtime(request.screen_position);
  input.ray = to_runtime(request.ray);
  input.grid_size = request.grid_size;
  input.grid_enabled = request.grid_snap_enabled;
  input.rotation_snap_degrees = request.rotation_snap_degrees;
  input.precision = request.precision;
  const bool portable_modeling_preview =
      context.port.portable_modeling_preview_active();
  if (request.vertex_snap_radius > 0.0 &&
      transform_drag_can_use_vertex_snap(drag_before_update) &&
      transform_drag_has_stationary_vertex_snap_target(context.session)) {
    QDR_PROFILE_SCOPE("Modeling transform drag build snap caches");
    input.vertex_snap_enabled = true;
    input.vertex_snap_radius = request.vertex_snap_radius;
    input.snap_object_bounds = context.port.cached_pick_object_bounds();
    input.snap_face_geometry =
        context.port.cached_local_pick_face_geometry();
    input.snap_object_transforms =
        context.port.cached_local_picking_transforms();
    input.snap_objects = &context.port.cached_local_objects_for_picking();
  }

  const quader_editor_core::CommandResult result = [&]() {
    QDR_PROFILE_SCOPE("Modeling transform drag apply runtime preview");
    return context.runtime.apply_transform_drag_preview(
        context.session.objects(), context.session.selection_mode(), input);
  }();
  if (!result.ok) {
    return fail_after_cancel(
        result.message.empty() ? "Could not update transform drag preview."
                               : result.message);
  }

  bool changed = result.changed;
  if (portable_modeling_preview && controller_.preview_driven() &&
      result.changed) {
    const ModelingCommandResult portable_result = [&]() {
      QDR_PROFILE_SCOPE("Modeling transform drag update portable preview");
      return context.port.update_portable_transform_preview(
          context.runtime.transform_drag_state(), input.grid_size);
    }();
    if (!portable_result.ok) {
      return fail_after_cancel(
          portable_result.message.empty()
              ? "Could not update portable transform preview."
              : portable_result.message);
    }
    changed = changed || portable_result.changed;
  }

  if (scene_transform_drag_service_.preview_active()) {
    QDR_PROFILE_SCOPE("Modeling transform drag apply scene preview");
    const quader_editor_core::TransformDragState scene_drag =
        context.runtime.transform_drag_state();
    const std::optional<DocumentCommandResult> scene_preview = [&]()
        -> std::optional<DocumentCommandResult> {
      if (scene_drag.space == quader_editor_core::TransformSpace::Local &&
          scene_drag.tool ==
              quader_editor_core::ViewportInteractionTool::Rotate) {
        const double angle_radians =
            scene_drag.rotate.preview_angle_degrees * std::acos(-1.0) / 180.0;
        return scene_transform_drag_service_.preview_local_rotation_delta(
            context.scene,
            quader_editor_core::rotation_axis_vector(scene_drag.axis) *
                static_cast<float>(angle_radians));
      }
      if (scene_drag.space == quader_editor_core::TransformSpace::Local &&
          scene_drag.tool == quader_editor_core::ViewportInteractionTool::Scale) {
        return scene_transform_drag_service_.preview_local_scale_delta(
            context.scene, scene_drag.scale.preview_scale);
      }

      quader_poly::Transform3 scene_transform;
      if (!scene_preview_transform_for_drag(scene_drag, scene_transform)) {
        return std::nullopt;
      }
      return scene_drag.tool == quader_editor_core::ViewportInteractionTool::Move
                 ? scene_transform_drag_service_.preview_world_translation_delta(
                       context.scene, scene_transform.origin)
                 : scene_transform_drag_service_.preview_world_transform_delta(
                       context.scene,
                       matrix_from_runtime_transform(scene_transform),
                       scene_drag.tool == quader_editor_core::ViewportInteractionTool::Scale
                           ? SceneTextureTransformKind::Scale
                           : SceneTextureTransformKind::Rotation);
    }();
    if (scene_preview.has_value()) {
      if (!scene_preview->ok) {
        return fail_after_cancel(
            scene_preview->message.empty()
                ? "Could not update scene transform preview."
                : scene_preview->message);
      }
      changed = changed || scene_preview->changed;
    }
  }

  if (controller_.preview_driven() && changed &&
      !context.port.transient_transform_preview_active()) {
    {
      QDR_PROFILE_SCOPE(
          "Modeling transform drag invalidate overlay caches after apply");
      context.port.invalidate_selection_overlay_caches();
    }
    {
      QDR_PROFILE_SCOPE(
          "Modeling transform drag sync scene selection after apply");
      context.port.sync_scene_selection_from_modeling();
    }
  }
  return transform_drag_preview(context, changed);
}

} // namespace quader
