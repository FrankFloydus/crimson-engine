////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/primitive_creation_tool_service.hpp"

#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "math/types.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

namespace quader {
namespace {

constexpr float kPrimitivePlacementEpsilon = 0.00001f;

bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

std::string_view primitive_default_name(PrimitiveCreationKind kind) {
  switch (kind) {
  case PrimitiveCreationKind::Box:
    return "Box";
  case PrimitiveCreationKind::Cylinder:
    return "Cylinder";
  case PrimitiveCreationKind::Sphere:
    return "Sphere";
  }
  return "Mesh";
}

bool integer_setting_value(double value, int minimum, int maximum,
                           int &result) {
  if (!std::isfinite(value) || std::round(value) != value ||
      value < static_cast<double>(minimum) ||
      value > static_cast<double>(maximum)) {
    return false;
  }
  result = static_cast<int>(value);
  return true;
}

/// Describes the oriented volume inferred from the active Box preview.
struct PrimitivePlacementFrame {
  QVec3 center = {};
  QVec3 x_axis = {1.0f, 0.0f, 0.0f};
  QVec3 y_axis = {0.0f, 1.0f, 0.0f};
  QVec3 z_axis = {0.0f, 0.0f, 1.0f};
  float width = 0.0f;
  float height = 0.0f;
  float depth = 0.0f;
};

std::optional<PrimitivePlacementFrame>
placement_frame(const ModelingBoxPreview &preview) {
  if (!preview.ok || preview.drag_state != ModelingBoxDragState::Height ||
      preview.corner_count != 8U) {
    return std::nullopt;
  }
  for (const QVec3 corner : preview.corners) {
    if (!finite_vector(corner)) {
      return std::nullopt;
    }
  }

  const QVec3 x_edge = preview.corners[3] - preview.corners[0];
  const QVec3 footprint_edge = preview.corners[1] - preview.corners[0];
  const QVec3 y_edge = preview.corners[4] - preview.corners[0];
  PrimitivePlacementFrame frame;
  frame.width = length(x_edge);
  frame.height = length(y_edge);
  frame.depth = length(footprint_edge);
  if (frame.width <= kPrimitivePlacementEpsilon ||
      frame.height <= kPrimitivePlacementEpsilon ||
      frame.depth <= kPrimitivePlacementEpsilon) {
    return std::nullopt;
  }

  frame.x_axis = normalize(x_edge);
  frame.y_axis = normalize(y_edge);
  // The footprint can be dragged in either direction. Rebuilding Z from X/Y
  // keeps the placement transform right-handed, so authored winding changes
  // only when the explicit Flip Faces setting requests it.
  frame.z_axis = normalize(cross(frame.x_axis, frame.y_axis));
  if (length_squared(frame.z_axis) <=
      kPrimitivePlacementEpsilon * kPrimitivePlacementEpsilon) {
    return std::nullopt;
  }
  for (const QVec3 corner : preview.corners) {
    frame.center += corner;
  }
  frame.center = frame.center / static_cast<float>(preview.corners.size());
  return frame;
}

void transform_document(quader_poly::Document &document,
                        const PrimitivePlacementFrame &frame) {
  for (quader_poly::Vertex &vertex : document.vertices) {
    const QVec3 local = vertex.position;
    vertex.position = frame.center + frame.x_axis * local.x +
                      frame.y_axis * local.y + frame.z_axis * local.z;
  }
}

quader_poly::Document build_primitive_document(
    PrimitiveCreationKind kind, const PrimitivePlacementFrame &frame,
    const BoxPrimitiveCreationSettings &box,
    const CylinderPrimitiveCreationSettings &cylinder,
    const SpherePrimitiveCreationSettings &sphere) {
  quader_poly::Document document;
  switch (kind) {
  case PrimitiveCreationKind::Box:
    document = quader_poly::make_cube({
        .width = frame.width,
        .height = frame.height,
        .depth = frame.depth,
        .width_segments = box.subdivisions_x,
        .height_segments = box.subdivisions_y,
        .depth_segments = box.subdivisions_z,
        .flip_faces = box.flip_faces,
    });
    break;
  case PrimitiveCreationKind::Cylinder:
    document = quader_poly::make_cylinder({
        .width = frame.width,
        .height = frame.height,
        .depth = frame.depth,
        .side_count = cylinder.side_count,
        .capped = cylinder.caps,
        .flip_faces = cylinder.flip_faces,
    });
    break;
  case PrimitiveCreationKind::Sphere:
    document = quader_poly::make_sphere({
        .width = frame.width,
        .height = frame.height,
        .depth = frame.depth,
        .radial_segments = sphere.radial_segments,
        .rings = sphere.rings,
        .hemisphere = sphere.hemisphere,
        .flip_faces = sphere.flip_faces,
    });
    break;
  }
  transform_document(document, frame);
  return document;
}

std::string combined_failure(std::string message, std::string_view suffix) {
  if (suffix.empty()) {
    return message;
  }
  if (!message.empty()) {
    message.append("; ");
  }
  message.append(suffix);
  return message;
}

} // namespace

PrimitiveCreationToolService::PrimitiveCreationToolService(
    EditorHistoryService &history, ModelingSessionService &modeling,
    ModelingOperationPreviewCheckpointHooks checkpoint_hooks)
    : history_(history), modeling_(modeling),
      checkpoint_hooks_(std::move(checkpoint_hooks)) {}

bool PrimitiveCreationToolService::can_apply() const {
  return session_.has_value() && session_->accepted_shapes > 0U;
}

std::size_t PrimitiveCreationToolService::shape_count() const {
  return session_.has_value() ? session_->accepted_shapes : 0U;
}

bool PrimitiveCreationToolService::owns_created_object(
    int modeling_object_id) const {
  return session_.has_value() &&
         std::find(session_->created_object_ids.begin(),
                   session_->created_object_ids.end(),
                   modeling_object_id) != session_->created_object_ids.end();
}

std::optional<PrimitiveCreationKind>
PrimitiveCreationToolService::active_kind() const {
  return session_.has_value()
             ? std::optional<PrimitiveCreationKind>(session_->kind)
             : std::nullopt;
}

std::string_view PrimitiveCreationToolService::active_tool_id() const {
  return session_.has_value() ? kCreatePrimitiveToolId : std::string_view{};
}

CommandResult
PrimitiveCreationToolService::activate() {
  return activate(preferred_kind_);
}

CommandResult
PrimitiveCreationToolService::activate(PrimitiveCreationKind kind) {
  if (session_.has_value()) {
    return set_kind(kind);
  }

  const ModelingSessionMemento before = modeling_.capture_history_memento();
  if (!before.valid()) {
    return command_invalid_result(
        "Primitive tool could not capture its rollback state.");
  }
  if (checkpoint_hooks_.begin) {
    checkpoint_hooks_.begin();
  }
  const ModelingCommandResult activated = modeling_.activate_box_tool();
  if (!activated.ok) {
    finish_checkpoint(true);
    return {
        .ok = false,
        .changed = activated.changed,
        .message = activated.message.empty()
                       ? "Primitive footprint tool could not activate."
                       : activated.message,
    };
  }
  session_ = Session{
      .kind = kind,
      .before = before,
  };
  preferred_kind_ = kind;
  ++tool_settings_revision_;
  return {
      .ok = true,
      .changed = true,
  };
}

CommandResult
PrimitiveCreationToolService::set_kind(PrimitiveCreationKind kind) {
  if (!session_.has_value()) {
    return command_invalid_result("No primitive tool is active.");
  }
  if (modeling_.box_preview(false).drag_state !=
      ModelingBoxDragState::Idle) {
    return command_invalid_result(
        "Primitive type can only change before drawing a footprint.");
  }

  freeze_live_primitive_if_selection_changed();
  preferred_kind_ = kind;
  if (session_->kind == kind) {
    return command_unchanged_result();
  }
  session_->kind = kind;
  ++tool_settings_revision_;
  return update_live_primitive();
}

CommandResult PrimitiveCreationToolService::accept_preview(
    const ModelingBoxPreview &preview, std::string name,
    MaterialReference material) {
  if (!session_.has_value()) {
    return command_invalid_result("No primitive tool is active.");
  }
  const std::optional<PrimitivePlacementFrame> frame =
      placement_frame(preview);
  if (!frame.has_value()) {
    return rollback_and_exit(
        "Primitive footprint does not define a valid volume.", true);
  }

  // Beginning another accepted shape is itself a selection transition: the
  // runtime will select the new object and deselect the prior live primitive.
  live_primitive_.reset();

  quader_poly::Document document = build_primitive_document(
      session_->kind, *frame, box_settings_, cylinder_settings_,
      sphere_settings_);
  if (document.vertices.empty() || document.faces.empty()) {
    return rollback_and_exit("Primitive geometry could not be generated.",
                             true);
  }
  if (name.empty()) {
    name = std::string(primitive_default_name(session_->kind));
  }

  if (checkpoint_hooks_.observe_before_projection) {
    checkpoint_hooks_.observe_before_projection();
  }
  const ModelingCreateBoxResult created = modeling_.create_mesh({
      .document = std::move(document),
      .name = std::move(name),
      .material_slot = 0,
      .material = material,
  });
  if (!created.ok || !created.changed) {
    return rollback_and_exit(
        created.message.empty() ? "Primitive shape could not be created."
                                : created.message,
        true);
  }
  ++session_->accepted_shapes;
  ++tool_settings_revision_;
  session_->created_object_ids.push_back(created.object.modeling_object_id);
  const ModelingSelectionSummary selection = modeling_.selection_summary();
  live_primitive_ = LivePrimitive{
      .placement = preview,
      .modeling_object_id = created.object.modeling_object_id,
      .selection_revision = selection.selection_revision,
  };
  if (checkpoint_hooks_.record_after_projection) {
    checkpoint_hooks_.record_after_projection();
  }

  // Clear only the footprint brush. The native primitive session remains
  // active, and the same Box runtime is immediately readied for the next
  // footprint.
  const ModelingCommandResult reset = modeling_.cancel_box();
  if (!reset.ok) {
    return rollback_and_exit(
        reset.message.empty()
            ? "Primitive shape was created, but its footprint could not reset."
            : reset.message,
        true);
  }
  const ModelingCommandResult reactivated = modeling_.activate_box_tool();
  if (!reactivated.ok) {
    return rollback_and_exit(
        reactivated.message.empty()
            ? "Primitive shape was created, but the next footprint could not start."
            : reactivated.message,
        true);
  }
  return command_changed_result();
}

CommandResult PrimitiveCreationToolService::apply() {
  if (!session_.has_value()) {
    return command_invalid_result("No primitive tool is active.");
  }
  if (session_->accepted_shapes == 0U) {
    return command_invalid_result(
        "Draw at least one primitive before applying the tool.");
  }

  const ModelingCommandResult reset = modeling_.cancel_box();
  if (!reset.ok) {
    return rollback_and_exit(
        reset.message.empty() ? "Primitive tool could not finalize."
                              : reset.message,
        true);
  }

  const ModelingSessionMemento before =
      modeling_.capture_history_memento_without_objects(
          session_->before, session_->created_object_ids);
  const ModelingSessionMemento after = modeling_.capture_history_memento();
  if (!before.valid() || !after.valid()) {
    return rollback_and_exit(
        "Primitive tool could not capture its applied state.", true);
  }
  const CommandResult recorded = history_.record_executed(
      std::make_unique<RecordedModelingMementoCommand>(
          modeling_, "Create Primitives", before, after));
  if (!recorded.ok) {
    return rollback_and_exit(
        recorded.message.empty()
            ? "Primitive tool could not record its history transaction."
            : recorded.message,
        true);
  }

  live_primitive_.reset();
  session_.reset();
  ++tool_settings_revision_;
  finish_checkpoint(false);
  return {
      .ok = true,
      .changed = recorded.changed || reset.changed,
      .message = recorded.message,
  };
}

CommandResult PrimitiveCreationToolService::cancel() {
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  return rollback_and_exit({}, false);
}

CommandResult PrimitiveCreationToolService::set_setting(std::string_view name,
                                                        double value) {
  if (!session_.has_value()) {
    return command_invalid_result("No primitive tool is active.");
  }
  if (modeling_.box_preview(false).drag_state !=
      ModelingBoxDragState::Idle) {
    return command_invalid_result(
        "Primitive settings can only change before drawing a footprint.");
  }

  if (!std::isfinite(value)) {
    return command_invalid_result("Primitive setting must be finite.");
  }
  freeze_live_primitive_if_selection_changed();
  const bool toggle = value != 0.0;
  switch (session_->kind) {
  case PrimitiveCreationKind::Box: {
    int count = 0;
    int *setting = nullptr;
    if (name == kBoxSubdivisionsXSetting) {
      setting = &box_settings_.subdivisions_x;
    } else if (name == kBoxSubdivisionsYSetting) {
      setting = &box_settings_.subdivisions_y;
    } else if (name == kBoxSubdivisionsZSetting) {
      setting = &box_settings_.subdivisions_z;
    } else if (name == kPrimitiveFlipFacesSetting) {
      if (box_settings_.flip_faces == toggle) {
        return command_unchanged_result();
      }
      box_settings_.flip_faces = toggle;
      ++tool_settings_revision_;
      return update_live_primitive();
    } else {
      return command_invalid_result("Unknown Box primitive setting.");
    }
    if (!integer_setting_value(value, kPrimitiveMinimumSubdivisionCount,
                               kPrimitiveMaximumSubdivisionCount, count)) {
      return command_invalid_result("Invalid Box subdivision count.");
    }
    if (*setting == count) {
      return command_unchanged_result();
    }
    *setting = count;
    ++tool_settings_revision_;
    return update_live_primitive();
  }
  case PrimitiveCreationKind::Cylinder: {
    if (name == kCylinderSideCountSetting) {
      int count = 0;
      if (!integer_setting_value(value, kCylinderMinimumSideCount,
                                 kCylinderMaximumSideCount, count)) {
        return command_invalid_result("Invalid Cylinder side count.");
      }
      if (cylinder_settings_.side_count == count) {
        return command_unchanged_result();
      }
      cylinder_settings_.side_count = count;
      ++tool_settings_revision_;
      return update_live_primitive();
    }
    bool *setting = nullptr;
    if (name == kCylinderCapsSetting) {
      setting = &cylinder_settings_.caps;
    } else if (name == kPrimitiveFlipFacesSetting) {
      setting = &cylinder_settings_.flip_faces;
    } else {
      return command_invalid_result("Unknown Cylinder primitive setting.");
    }
    if (*setting == toggle) {
      return command_unchanged_result();
    }
    *setting = toggle;
    ++tool_settings_revision_;
    return update_live_primitive();
  }
  case PrimitiveCreationKind::Sphere: {
    int count = 0;
    if (name == kSphereRadialSegmentsSetting) {
      if (!integer_setting_value(value, kSphereMinimumRadialSegments,
                                 kSphereMaximumRadialSegments, count)) {
        return command_invalid_result("Invalid Sphere radial segment count.");
      }
      if (sphere_settings_.radial_segments == count) {
        return command_unchanged_result();
      }
      sphere_settings_.radial_segments = count;
      ++tool_settings_revision_;
      return update_live_primitive();
    }
    if (name == kSphereRingsSetting) {
      if (!integer_setting_value(value, kSphereMinimumRings,
                                 kSphereMaximumRings, count)) {
        return command_invalid_result("Invalid Sphere ring count.");
      }
      if (sphere_settings_.rings == count) {
        return command_unchanged_result();
      }
      sphere_settings_.rings = count;
      ++tool_settings_revision_;
      return update_live_primitive();
    }
    bool *setting = nullptr;
    if (name == kSphereHemisphereSetting) {
      setting = &sphere_settings_.hemisphere;
    } else if (name == kPrimitiveFlipFacesSetting) {
      setting = &sphere_settings_.flip_faces;
    } else {
      return command_invalid_result("Unknown Sphere primitive setting.");
    }
    if (*setting == toggle) {
      return command_unchanged_result();
    }
    *setting = toggle;
    ++tool_settings_revision_;
    return update_live_primitive();
  }
  }
  return command_invalid_result("Unknown primitive setting.");
}

bool PrimitiveCreationToolService::live_primitive_is_selected() const {
  if (!live_primitive_.has_value()) {
    return false;
  }
  const ModelingSelectionSummary selection = modeling_.selection_summary();
  return selection.mode == ModelingSelectionMode::Object &&
         selection.has_selection &&
         selection.active_kind == ModelingActiveSelectionKind::Object &&
         selection.active_object_id == live_primitive_->modeling_object_id &&
         selection.selected_object_count == 1U &&
         selection.selection_revision == live_primitive_->selection_revision;
}

void
PrimitiveCreationToolService::freeze_live_primitive_if_selection_changed() {
  if (live_primitive_.has_value() && !live_primitive_is_selected()) {
    live_primitive_.reset();
  }
}

CommandResult PrimitiveCreationToolService::update_live_primitive() {
  if (!session_.has_value()) {
    return command_invalid_result("No primitive tool is active.");
  }
  if (!live_primitive_.has_value()) {
    return command_changed_result();
  }

  const std::optional<PrimitivePlacementFrame> frame =
      placement_frame(live_primitive_->placement);
  if (!frame.has_value()) {
    return rollback_and_exit(
        "Live primitive no longer has a valid placement volume.", true);
  }
  quader_poly::Document document = build_primitive_document(
      session_->kind, *frame, box_settings_, cylinder_settings_,
      sphere_settings_);
  if (document.vertices.empty() || document.faces.empty()) {
    return rollback_and_exit("Live primitive geometry could not be generated.",
                             true);
  }

  if (checkpoint_hooks_.observe_before_projection) {
    checkpoint_hooks_.observe_before_projection();
  }
  const ModelingCommandResult updated = modeling_.replace_mesh_document(
      live_primitive_->modeling_object_id, std::move(document));
  if (!updated.ok || !updated.changed) {
    return rollback_and_exit(
        updated.message.empty() ? "Live primitive could not be updated."
                                : updated.message,
        true);
  }
  if (checkpoint_hooks_.record_after_projection) {
    checkpoint_hooks_.record_after_projection();
  }
  live_primitive_->selection_revision =
      modeling_.selection_summary().selection_revision;
  return command_changed_result();
}

CommandResult PrimitiveCreationToolService::rollback_and_exit(
    std::string message, bool report_failure) {
  if (!session_.has_value()) {
    return report_failure ? command_invalid_result(std::move(message))
                          : command_unchanged_result();
  }

  bool changed = false;
  bool ok = true;
  if (checkpoint_hooks_.observe_before_projection) {
    checkpoint_hooks_.observe_before_projection();
  }
  const ModelingSessionMemento rollback =
      modeling_.capture_history_memento_without_objects(
          session_->before, session_->created_object_ids);
  const ModelingCommandResult restored =
      rollback.valid()
          ? modeling_.restore_history_memento(rollback)
          : ModelingCommandResult{
                .ok = false,
                .changed = false,
                .message = "Primitive rollback state could not be captured.",
            };
  changed = changed || restored.changed;
  ok = ok && restored.ok;
  if (!restored.ok) {
    message = combined_failure(
        std::move(message),
        restored.message.empty() ? "Primitive rollback failed."
                                 : restored.message);
  }

  const ModelingCommandResult reset = modeling_.cancel_box();
  changed = changed || reset.changed;
  ok = ok && reset.ok;
  if (!reset.ok) {
    message = combined_failure(
        std::move(message),
        reset.message.empty() ? "Primitive footprint reset failed."
                              : reset.message);
  }
  if (checkpoint_hooks_.record_after_projection) {
    checkpoint_hooks_.record_after_projection();
  }
  live_primitive_.reset();
  session_.reset();
  ++tool_settings_revision_;
  finish_checkpoint(ok);
  return {
      .ok = ok && !report_failure,
      // A failed shape that rolls back cleanly has no observable mutation.
      .changed = report_failure && ok ? false : changed,
      .message = std::move(message),
  };
}

void PrimitiveCreationToolService::finish_checkpoint(
    bool restored_by_cancel) {
  if (checkpoint_hooks_.finish) {
    checkpoint_hooks_.finish(restored_by_cancel);
  }
}

} // namespace quader
