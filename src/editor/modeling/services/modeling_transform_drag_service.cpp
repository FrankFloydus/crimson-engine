////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_transform_drag_service.hpp"

#include "core/i_undoable_command.hpp"
#include "diagnostics/profile.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/preview/transform_preview_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "math/glm_bridge.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <editor/interactions/move_tool.hpp>
#include <editor/interactions/rotate_tool.hpp>
#include <editor/interactions/scale_tool.hpp>
#include <editor/interactions/tool_transform_math.hpp>
#include <editor/interactions/transform_drag.hpp>

namespace quader {
namespace {

quader::QVec3 to_runtime(QVec3 value) { return {value.x, value.y, value.z}; }

quader::QVec2 to_runtime(QVec2 value) { return {value.x, value.y}; }

quader_poly::Ray to_runtime(const ModelingRay &ray) {
  return {
      .origin = to_runtime(ray.origin),
      .direction = to_runtime(normalize(ray.direction)),
  };
}

quader_editor_core::ViewportCameraState
to_runtime(const ModelingViewportCamera &value) {
  quader_editor_core::ViewportCameraState camera;
  camera.projection = value.orthographic
                          ? quader_editor_core::CameraProjection::Orthographic
                          : quader_editor_core::CameraProjection::Perspective;
  camera.transform = {
      .x_axis = to_runtime(value.x_axis),
      .y_axis = to_runtime(value.y_axis),
      .z_axis = to_runtime(value.z_axis),
      .origin = to_runtime(value.origin),
  };
  camera.viewport_size = {
      std::max(value.viewport_size.x, 1.0f),
      std::max(value.viewport_size.y, 1.0f),
  };
  camera.orthographic_size = std::max(value.orthographic_size, 0.0001);
  camera.fov_degrees = value.fov_degrees;
  return camera;
}

QMat4 matrix_from_runtime_transform(const quader_poly::Transform3 &transform) {
  glm::mat4 matrix{1.0f};
  matrix[0] = glm::vec4{to_glm(transform.x_axis), 0.0f};
  matrix[1] = glm::vec4{to_glm(transform.y_axis), 0.0f};
  matrix[2] = glm::vec4{to_glm(transform.z_axis), 0.0f};
  matrix[3] = glm::vec4{to_glm(transform.origin), 1.0f};
  return from_glm(matrix);
}

quader_poly::Transform3 runtime_transform_from_matrix(const QMat4 &matrix) {
  const glm::mat4 value = to_glm(matrix);
  return {
      .x_axis = from_glm(glm::vec3{value[0]}),
      .y_axis = from_glm(glm::vec3{value[1]}),
      .z_axis = from_glm(glm::vec3{value[2]}),
      .origin = from_glm(glm::vec3{value[3]}),
  };
}

QMat4 world_affine_from_transform(const quader_poly::Transform3 &transform,
                                  QVec3 pivot) {
  return matrix_from_runtime_transform(
      quader_editor_core::pivoted_transform(transform, to_runtime(pivot)));
}

QMat4 world_translation_affine(QVec3 delta) {
  return from_glm(glm::translate(glm::mat4{1.0f}, to_glm(delta)));
}

std::vector<quader_editor_core::ObjectLocalTransform>
local_transforms_for_selected_objects(
    const ModelingTransformDragServiceContext &context,
    const QMat4 &world_affine) {
  std::vector<quader_editor_core::ObjectLocalTransform> transforms;
  const glm::mat4 world_delta = to_glm(world_affine);
  for (const quader_editor_core::PolygonObject &object :
       context.session.objects()) {
    if (!object.selected) {
      continue;
    }
    const QMat4 world_from_object =
        context.port.modeling_object_world_transform(object.id)
            .value_or(QMat4{});
    const glm::mat4 object_world = to_glm(world_from_object);
    const glm::mat4 local_delta =
        glm::inverse(object_world) * world_delta * object_world;
    transforms.push_back({
        .object_id = object.id,
        .transform = runtime_transform_from_matrix(from_glm(local_delta)),
    });
  }
  return transforms;
}

std::vector<quader_editor_core::ObjectLocalOffset>
local_offsets_for_selected_objects(
    const ModelingTransformDragServiceContext &context, QVec3 world_delta) {
  std::vector<quader_editor_core::ObjectLocalOffset> offsets;
  const std::vector<quader_editor_core::ObjectLocalTransform> transforms =
      local_transforms_for_selected_objects(context,
                                            world_translation_affine(world_delta));
  offsets.reserve(transforms.size());
  for (const quader_editor_core::ObjectLocalTransform &transform :
       transforms) {
    offsets.push_back({
        .object_id = transform.object_id,
        .offset = transform.transform.origin,
    });
  }
  return offsets;
}

bool transform_drag_uses_component_modeling_preview(
    const ModelingTransformDragServiceContext &context,
    const TransformDragController &controller) {
  return controller.preview_driven() &&
         context.session.selection_mode() !=
             quader_editor_core::SelectionMode::Object;
}

quader_editor_core::CommandResult apply_component_modeling_preview_in_local_space(
    ModelingTransformDragServiceContext context,
    const quader_editor_core::TransformDragState &drag,
    const quader_editor_core::TransformDragPreviewInput &input) {
  if (drag.tool == quader_editor_core::ViewportInteractionTool::Move) {
    const std::vector<quader_editor_core::ObjectLocalOffset> offsets =
        local_offsets_for_selected_objects(context, drag.move.preview_delta);
    if (drag.operation ==
        quader_editor_core::TransformDragOperation::Extrude) {
      return context.session.update_transform_extrude_preview(
          offsets, static_cast<float>(std::max(input.grid_size, 0.0001)));
    }
    return context.session.update_transform_translate_preview(offsets, false);
  }

  if (drag.tool == quader_editor_core::ViewportInteractionTool::Rotate) {
    const quader_poly::Transform3 world_transform =
        quader_editor_core::rotation_transform(
            drag.axis,
            drag.rotate.preview_angle_degrees * std::acos(-1.0) / 180.0);
    const std::vector<quader_editor_core::ObjectLocalTransform> transforms =
        local_transforms_for_selected_objects(
            context, world_affine_from_transform(world_transform,
                                                 drag.pivot));
    if (drag.operation ==
        quader_editor_core::TransformDragOperation::Extrude) {
      return context.session.update_transform_extrude_preview(transforms);
    }
    return context.session.update_transform_matrix_preview(transforms, false,
                                                           false);
  }

  if (drag.tool == quader_editor_core::ViewportInteractionTool::Scale) {
    const quader_poly::Transform3 world_transform =
        quader_editor_core::scale_transform(drag.scale.preview_scale);
    const std::vector<quader_editor_core::ObjectLocalTransform> transforms =
        local_transforms_for_selected_objects(
            context, world_affine_from_transform(world_transform,
                                                 drag.pivot));
    if (drag.operation == quader_editor_core::TransformDragOperation::Inset) {
      return context.session.update_transform_inset_preview(transforms);
    }
    return context.session.update_transform_matrix_preview(transforms, false,
                                                           false);
  }

  return {
      .ok = true,
      .changed = false,
  };
}

CommandResult
modeling_service_command_result_from(ModelingCommandResult result) {
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = std::move(result.message),
  };
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
    return modeling_service_command_result_from(restore_(before_));
  }

  CommandResult redo() override {
    return modeling_service_command_result_from(restore_(after_));
  }

private:
  Restore restore_;
  std::string label_;
  ModelingSessionMemento before_;
  ModelingSessionMemento after_;
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
      TransformPreviewService::has_transform_preview(context.session)) {
    QDR_PROFILE_SCOPE("Modeling transform drag cancel active preview");
    (void)cancel_transform_drag(context);
  }
  if ((!context.session.has_selection() &&
       !context.port.has_scene_transform_selection()) ||
      request.axis == ModelingTransformAxis::None) {
    return transform_drag_preview(context, false);
  }

  const bool runtime_has_selection = context.session.has_selection();
  const bool scene_transform_selection =
      !runtime_has_selection && context.port.has_scene_transform_selection();
  const quader_editor_core::SelectionMode drag_selection_mode =
      scene_transform_selection ? quader_editor_core::SelectionMode::Object
                                : context.session.selection_mode();
  const quader_editor_core::ViewportInteractionTool tool =
      to_runtime(request.tool);
  const quader_editor_core::TransformDragOperation operation =
      quader_editor_core::sanitize_transform_drag_operation(
          to_runtime(request.operation), tool, drag_selection_mode);
  const quader_editor_core::TransformAxis axis = to_runtime(request.axis);
  const quader::QVec3 pivot = to_runtime(request.pivot);
  const TransformDragPreviewRouting preview_routing =
      TransformDragController::preview_routing(
          request.duplicate_selected,
          drag_selection_mode == quader_editor_core::SelectionMode::Object,
          operation == quader_editor_core::TransformDragOperation::Transform);

  {
    QDR_PROFILE_SCOPE("Modeling transform drag begin runtime state");
    context.runtime.begin_transform_drag_state(tool, operation, axis, pivot);
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
    const std::vector<ModelingObjectSceneIdentity> duplicate_scene_identities =
        request.duplicate_selected
            ? context.port.capture_selected_duplicate_scene_identities()
            : std::vector<ModelingObjectSceneIdentity>{};
    const quader_editor_core::CommandResult begin_preview = [&]() {
      QDR_PROFILE_SCOPE("Modeling transform drag begin transform preview");
      return context.session.begin_transform_preview(request.duplicate_selected);
    }();
    if (!begin_preview.ok) {
      clear_runtime_state(context);
      return {
          .ok = false,
          .message = begin_preview.message.empty()
                         ? "Could not start transform preview."
                         : begin_preview.message,
      };
    }
    {
      QDR_PROFILE_SCOPE("Modeling transform drag invalidate overlay caches");
      context.port.invalidate_selection_overlay_caches();
    }
    if (begin_preview.changed) {
      {
        QDR_PROFILE_SCOPE(
            "Modeling transform drag sync scene objects after begin");
        context.port.sync_scene_objects_from_modeling(true);
      }
      {
        QDR_PROFILE_SCOPE(
            "Modeling transform drag sync scene selection after begin");
        context.port.sync_scene_selection_from_modeling();
      }
      if (request.duplicate_selected) {
        context.port.apply_scene_identities_to_selected_duplicates(
            duplicate_scene_identities);
      }
    }
  }

  if (preview_routing.scene_transform_preview_eligible && !([&]() {
        QDR_PROFILE_SCOPE(
            "Modeling transform drag begin scene transform preview");
        return scene_transform_drag_service_.begin(
            context.scene, request.pivot, true, "Transform Selection");
      }())) {
    (void)cancel_transform_drag(context);
    return {
        .ok = false,
        .message = "Could not start scene transform preview.",
    };
  }

  const quader_editor_core::ViewportCameraState camera =
      to_runtime(request.camera);
  const quader::QVec2 screen_position = to_runtime(request.screen_position);
  const quader::QVec2 screen_pivot = to_runtime(request.screen_pivot);
  const quader::QVec2 screen_tip = to_runtime(request.screen_tip);

  if (tool == quader_editor_core::ViewportInteractionTool::Move) {
    quader_editor_core::MoveBeginInput begin;
    begin.axis = axis;
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
      (void)cancel_transform_drag(context);
      return {.ok = false,
              .message = begin_result.message.empty()
                             ? "Move drag failed."
                             : begin_result.message};
    }
    context.runtime.set_transform_drag_move_state(begin_result.state);
  } else if (tool == quader_editor_core::ViewportInteractionTool::Rotate) {
    quader_editor_core::RotateBeginInput begin;
    begin.axis = axis;
    begin.object_ids = context.session.selected_object_ids();
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
      (void)cancel_transform_drag(context);
      return {.ok = false,
              .message = begin_result.message.empty()
                             ? "Rotate drag failed."
                             : begin_result.message};
    }
    context.runtime.set_transform_drag_rotate_state(begin_result.state);
  } else if (tool == quader_editor_core::ViewportInteractionTool::Scale) {
    quader_editor_core::ScaleBeginInput begin;
    begin.axis = axis;
    begin.object_ids = context.session.selected_object_ids();
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
      (void)cancel_transform_drag(context);
      return {.ok = false,
              .message = begin_result.message.empty()
                             ? "Scale drag failed."
                             : begin_result.message};
    }
    context.runtime.set_transform_drag_scale_state(begin_result.state);
  } else {
    clear_runtime_state(context);
    return {.ok = false,
            .message = "Transform drag only supports move, rotate, or scale."};
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
    if (TransformPreviewService::has_transform_preview(context.session)) {
      (void)cancel_transform_drag(context);
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
  const bool finish_preview_transaction =
      was_preview_driven || controller_.duplicate_selected();
  const quader_editor_core::TransformDragState repeat_drag =
      context.runtime.transform_drag_state();
  ModelingSessionMemento preview_before =
      finish_preview_transaction && was_preview_driven && history != nullptr
          ? context.port.capture_active_preview_history_memento()
          : ModelingSessionMemento();
  bool changed = false;

  if (finish_preview_transaction) {
    const quader_editor_core::PreviewTransactionFinishResult finish = [&]() {
      QDR_PROFILE_SCOPE("Modeling transform drag commit preview transaction");
      return context.session.commit_preview_transaction(
          quader_editor_core::PreviewTransactionKind::Transform);
    }();
    if (finish.before.active) {
      context.port.invalidate_selection_overlay_caches();
    }
    {
      QDR_PROFILE_SCOPE(
          "Modeling transform drag sync scene objects after commit");
      context.port.sync_scene_objects_from_modeling(finish.before.active);
    }
    {
      QDR_PROFILE_SCOPE(
          "Modeling transform drag sync scene selection after commit");
      context.port.sync_scene_selection_from_modeling();
    }
    const bool ok =
        !finish.before.active ||
        finish.before.kind ==
            quader_editor_core::PreviewTransactionKind::Transform;
    if (!ok) {
      clear_runtime_state(context);
      return {
          .ok = false,
          .preview_driven = was_preview_driven,
          .message = "Active preview transaction was not a transform preview.",
      };
    }
    changed = finish.before.changed;
    if (was_preview_driven && changed && repeat_drag.active) {
      context.history_bridge.record_transform(
          context.session, repeat_drag, controller_.duplicate_selected());
    }
    if (was_preview_driven && changed && history != nullptr &&
        preview_before.valid()) {
      const ModelingSessionMemento preview_after =
          context.port.capture_history_memento();
      ModelingTransformDragPort *port = &context.port;
      const CommandResult recorded = history->record_executed(
          std::make_unique<RecordedModelingPreviewMementoCommand>(
              [port](const ModelingSessionMemento &memento) {
                return port->restore_history_memento(memento);
              },
              transform_drag_history_label(repeat_drag.tool),
              std::move(preview_before), preview_after));
      if (!recorded.ok) {
        clear_runtime_state(context);
        return {
            .ok = false,
            .preview_driven = was_preview_driven,
            .message = recorded.message.empty()
                           ? "Could not record transform drag history."
                           : recorded.message,
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
      clear_runtime_state(context);
      return {
          .ok = false,
          .preview_driven = was_preview_driven,
          .message = scene_commit.message.empty()
                         ? "Could not commit scene transform preview."
                         : scene_commit.message,
      };
    }
    changed = changed || scene_commit.changed;
    if (scene_commit.changed) {
      context.port.invalidate_selection_overlay_caches();
    }
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
  const bool active_before =
      context.runtime.transform_drag_active() ||
      TransformPreviewService::has_transform_preview(context.session);
  const bool was_scene_preview_active =
      scene_transform_drag_service_.preview_active();
  if (was_scene_preview_active) {
    const DocumentCommandResult scene_cancel =
        scene_transform_drag_service_.cancel();
    if (!scene_cancel.ok) {
      clear_runtime_state(context);
      return {
          .ok = false,
          .changed = false,
          .message = scene_cancel.message,
      };
    }
    if (scene_cancel.changed) {
      context.port.invalidate_selection_overlay_caches();
    }
  }
  const quader_editor_core::PreviewTransactionFinishResult finish =
      context.runtime.cancel_transform_preview_state(context.session);
  if (finish.before.active) {
    context.port.invalidate_selection_overlay_caches();
  }
  context.port.sync_scene_objects_from_modeling(finish.before.active);
  context.port.sync_scene_selection_from_modeling();
  clear_runtime_state(context);
  return {
      .ok = true,
      .changed = (active_before && finish.restored) || was_scene_preview_active,
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
  context.runtime.clear_transform_drag_state();
  context.runtime.clear_gizmo_hover();
  controller_.clear();
  scene_transform_drag_service_.reset();
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
  preview.content_revision = context.session.state().content_revision;
  preview.selection_revision = context.session.state().selection_revision;
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
  const quader_editor_core::TransformDragState drag_before_update =
      context.runtime.transform_drag_state();
  quader_editor_core::TransformDragPreviewInput input;
  input.camera = to_runtime(request.camera);
  input.screen_position = to_runtime(request.screen_position);
  input.ray = to_runtime(request.ray);
  input.grid_size = request.grid_size;
  input.grid_enabled = request.grid_snap_enabled;
  input.precision = request.precision;
  const bool component_modeling_preview =
      transform_drag_uses_component_modeling_preview(context, controller_);
  input.defer_session_update = component_modeling_preview;
  if (request.vertex_snap_radius > 0.0 &&
      transform_drag_can_use_vertex_snap(drag_before_update)) {
    QDR_PROFILE_SCOPE("Modeling transform drag build snap caches");
    input.vertex_snap_enabled = true;
    input.vertex_snap_radius = request.vertex_snap_radius;
    input.snap_object_bounds = context.port.cached_pick_object_bounds();
    input.snap_face_geometry = context.port.cached_pick_face_geometry();
  }

  const quader_editor_core::CommandResult result = [&]() {
    QDR_PROFILE_SCOPE("Modeling transform drag apply runtime preview");
    return context.runtime.apply_transform_drag_preview(context.session, input);
  }();
  if (!result.ok) {
    const bool was_preview_driven = controller_.preview_driven();
    (void)cancel_transform_drag(context);
    return {
        .ok = false,
        .preview_driven = was_preview_driven,
        .message = result.message.empty()
                       ? "Could not update transform drag preview."
                       : result.message,
    };
  }

  bool changed = result.changed;
  if (component_modeling_preview && result.changed) {
    const quader_editor_core::CommandResult local_result = [&]() {
      QDR_PROFILE_SCOPE(
          "Modeling transform drag apply local-space component preview");
      return apply_component_modeling_preview_in_local_space(
          context, context.runtime.transform_drag_state(), input);
    }();
    if (!local_result.ok) {
      const bool was_preview_driven = controller_.preview_driven();
      (void)cancel_transform_drag(context);
      return {
          .ok = false,
          .preview_driven = was_preview_driven,
          .message =
              local_result.message.empty()
                  ? "Could not update local-space transform drag preview."
                  : local_result.message,
      };
    }
    changed = changed || local_result.changed;
  }

  if (scene_transform_drag_service_.preview_active()) {
    QDR_PROFILE_SCOPE("Modeling transform drag apply scene preview");
    const quader_editor_core::TransformDragState scene_drag =
        context.runtime.transform_drag_state();
    quader_poly::Transform3 scene_transform;
    if (scene_preview_transform_for_drag(scene_drag, scene_transform)) {
      const DocumentCommandResult scene_preview =
          scene_drag.tool == quader_editor_core::ViewportInteractionTool::Move
              ? scene_transform_drag_service_.preview_world_translation_delta(
                    context.scene, scene_transform.origin)
              : scene_transform_drag_service_.preview_world_transform_delta(
                    context.scene,
                    matrix_from_runtime_transform(scene_transform));
      if (!scene_preview.ok) {
        const bool was_preview_driven = controller_.preview_driven();
        (void)cancel_transform_drag(context);
        return {
            .ok = false,
            .preview_driven = was_preview_driven,
            .message = scene_preview.message.empty()
                           ? "Could not update scene transform preview."
                           : scene_preview.message,
        };
      }
      changed = changed || scene_preview.changed;
    }
  }

  if (controller_.preview_driven() && changed) {
    {
      QDR_PROFILE_SCOPE(
          "Modeling transform drag invalidate overlay caches after apply");
      context.port.invalidate_selection_overlay_caches();
    }
    {
      QDR_PROFILE_SCOPE("Modeling transform drag sync scene objects after apply");
      context.port.sync_scene_objects_from_modeling(true);
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
