////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "core/release_features.hpp"

#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "math/types.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <utility>

namespace quader {

std::string_view primitive_creation_default_name(PrimitiveCreationKind kind) {
  switch (kind) {
  case PrimitiveCreationKind::Box:
    return "Box";
  case PrimitiveCreationKind::Cylinder:
    return "Cylinder";
  case PrimitiveCreationKind::Sphere:
    return "Sphere";
  case PrimitiveCreationKind::Cone:
    return "Cone";
  case PrimitiveCreationKind::Torus:
    return "Torus";
  }
  return "Mesh";
}

namespace {

constexpr float kPrimitivePlacementEpsilon = 0.00001f;

bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

float &cube_axis_component(QVec3 &value, PrimitiveCubeAxis axis) {
  switch (axis) {
  case PrimitiveCubeAxis::X:
    return value.x;
  case PrimitiveCubeAxis::Y:
    return value.y;
  case PrimitiveCubeAxis::Z:
    return value.z;
  }
  return value.x;
}

bool same_bounds(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

void fit_unit_document_to_bounds(quader_poly::Document &document,
                                 QVec3 minimum, QVec3 maximum) {
  for (quader_poly::Vertex &vertex : document.vertices) {
    vertex.position = {
        std::lerp(minimum.x, maximum.x, (vertex.position.x + 1.0f) * 0.5f),
        std::lerp(minimum.y, maximum.y, (vertex.position.y + 1.0f) * 0.5f),
        std::lerp(minimum.z, maximum.z, (vertex.position.z + 1.0f) * 0.5f),
    };
  }
}

quader_poly::Document cube_document_with_bounds(
    QVec3 minimum, QVec3 maximum,
    const BoxPrimitiveCreationSettings &settings) {
  // Unit topology bypasses the generic generator's dimension clamp, allowing
  // the creation minimum without moving the entity or its texture frame.
  quader_poly::Document document = quader_poly::make_cube({
      .width = 2.0f,
      .height = 2.0f,
      .depth = 2.0f,
      .width_segments = settings.subdivisions_x,
      .height_segments = settings.subdivisions_y,
      .depth_segments = settings.subdivisions_z,
      .flip_faces = settings.flip_faces,
  });
  fit_unit_document_to_bounds(document, minimum, maximum);
  return document;
}

quader_poly::Document cylinder_document_with_bounds(
    QVec3 minimum, QVec3 maximum,
    const CylinderPrimitiveCreationSettings &settings) {
  quader_poly::Document document = quader_poly::make_cylinder({
      .width = 2.0f,
      .height = 2.0f,
      .depth = 2.0f,
      .side_count = settings.side_count,
      .capped = settings.caps,
      .flip_faces = settings.flip_faces,
  });
  const QVec3 center{std::midpoint(minimum.x, maximum.x),
                     std::midpoint(minimum.y, maximum.y),
                     std::midpoint(minimum.z, maximum.z)};
  const QVec3 radius = (maximum - minimum) * 0.5f;
  for (auto &vertex : document.vertices) {
    vertex.position = {center.x + vertex.position.x * radius.x,
                       vertex.position.y < 0.0f ? minimum.y : maximum.y,
                       center.z + vertex.position.z * radius.z};
  }
  return document;
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
  const QVec3 rejected_y =
      y_edge - frame.x_axis * dot(y_edge, frame.x_axis);
  if (length_squared(rejected_y) <=
      kPrimitivePlacementEpsilon * kPrimitivePlacementEpsilon) {
    return std::nullopt;
  }
  frame.y_axis = normalize(rejected_y);
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

quader_poly::Document build_primitive_document(
    PrimitiveCreationKind kind, const PrimitivePlacementFrame &frame,
    const BoxPrimitiveCreationSettings &box,
    const CylinderPrimitiveCreationSettings &cylinder,
    const ConePrimitiveCreationSettings &cone,
    const SpherePrimitiveCreationSettings &sphere,
    const TorusPrimitiveCreationSettings &torus) {
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
  case PrimitiveCreationKind::Cone:
    document = quader_poly::make_cone({
        .width = frame.width,
        .height = frame.height,
        .depth = frame.depth,
        .side_count = cone.side_count,
        .capped = cone.caps,
        .flip_faces = cone.flip_faces,
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
        .type = sphere.type == PrimitiveSphereType::Quad
                    ? quader_poly::SphereType::Quad : quader_poly::SphereType::Uv,
        .subdivision = sphere.subdivision,
    });
    break;
  case PrimitiveCreationKind::Torus:
    document = quader_poly::make_torus({
        .width = frame.width,
        .height = frame.height,
        .depth = frame.depth,
        .major_segments = torus.major_segments,
        .minor_segments = torus.minor_segments,
        .tube_radius_ratio = static_cast<float>(torus.tube_radius_ratio),
        .flip_faces = torus.flip_faces,
    });
    break;
  }
  return document;
}

/// Fits Cone/Sphere/Torus topology to the authored local volume without moving its entity.
quader_poly::Document curved_document_with_bounds(
    PrimitiveCreationKind kind, QVec3 minimum, QVec3 maximum,
    const BoxPrimitiveCreationSettings &box,
    const CylinderPrimitiveCreationSettings &cylinder,
    const ConePrimitiveCreationSettings &cone,
    const SpherePrimitiveCreationSettings &sphere,
    const TorusPrimitiveCreationSettings &torus) {
  const PrimitivePlacementFrame unit{.width = 2.0f, .height = 2.0f, .depth = 2.0f};
  auto document = build_primitive_document(kind, unit, box, cylinder, cone, sphere, torus);
  // Keep the generator's hemisphere and torus parametrizations, mapping their
  // nominal volume rather than the tessellated mesh's sampled extrema.
  fit_unit_document_to_bounds(document, minimum, maximum);
  return document;
}

ModelingTransform3
placement_transform(const PrimitivePlacementFrame &frame) {
  return {
      .x_axis = frame.x_axis,
      .y_axis = frame.y_axis,
      .z_axis = frame.z_axis,
      .origin = frame.center,
  };
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

bool primitive_creation_available_in_release(PrimitiveCreationKind kind) noexcept {
  switch (kind) {
  case PrimitiveCreationKind::Cone:
    return release_has_feature(ReleaseFeature::ConePrimitive);
  case PrimitiveCreationKind::Torus:
    return release_has_feature(ReleaseFeature::TorusPrimitive);
  case PrimitiveCreationKind::Box:
  case PrimitiveCreationKind::Cylinder:
  case PrimitiveCreationKind::Sphere:
    return true;
  }
  return false;
}

bool primitive_sphere_type_available_in_release(PrimitiveSphereType type) noexcept {
  return type == PrimitiveSphereType::Uv ||
         (type == PrimitiveSphereType::Quad &&
          release_has_feature(ReleaseFeature::QuadSpherePrimitive));
}

bool primitive_setting_available_in_release(std::string_view name,
                                            double value) noexcept {
  if (name.starts_with("primitive.cone.")) {
    return release_has_feature(ReleaseFeature::ConePrimitive);
  }
  if (name.starts_with("primitive.torus.")) {
    return release_has_feature(ReleaseFeature::TorusPrimitive);
  }
  if (name == kSphereSubdivisionSetting ||
      (name == kSphereTypeSetting && value == 1.0)) {
    return release_has_feature(ReleaseFeature::QuadSpherePrimitive);
  }
  if (name == kPrimitiveTypeSetting) {
    if (value == static_cast<double>(PrimitiveCreationKind::Cone)) {
      return primitive_creation_available_in_release(PrimitiveCreationKind::Cone);
    }
    if (value == static_cast<double>(PrimitiveCreationKind::Torus)) {
      return primitive_creation_available_in_release(PrimitiveCreationKind::Torus);
    }
  }
  return true;
}

PrimitiveCreationToolService::PrimitiveCreationToolService(
    EditorHistoryService &history, ModelingSessionService &modeling,
    ModelingOperationPreviewCheckpointHooks checkpoint_hooks,
    PrimitiveCreationSessionDefaults *session_defaults)
    : history_(history), modeling_(modeling),
      checkpoint_hooks_(std::move(checkpoint_hooks)),
      session_defaults_(session_defaults) {
  if (session_defaults_) {
    accepted_settings_ = *session_defaults_;
  }
  load_settings(accepted_settings_);
}

bool PrimitiveCreationToolService::can_apply() const {
  return session_.has_value() && session_->accepted_shapes > 0U;
}

bool PrimitiveCreationToolService::can_undo() const {
  return session_.has_value() &&
         ((history_.max_steps() > 0U && session_->history_cursor > 0U) ||
          (live_primitive_.has_value() && live_primitive_->resize.has_value()) ||
          modeling_.box_drag_state() != ModelingBoxDragState::Idle);
}

bool PrimitiveCreationToolService::can_redo() const {
  return session_.has_value() &&
         history_.max_steps() > 0U &&
         session_->history_cursor < session_->shapes.size() &&
         (!live_primitive_.has_value() || !live_primitive_->resize.has_value()) &&
         modeling_.box_drag_state() == ModelingBoxDragState::Idle;
}

void PrimitiveCreationToolService::apply_history_limit() {
  if (!session_.has_value() ||
      session_->shapes.size() <= history_.max_steps()) {
    return;
  }
  const std::size_t excess = session_->shapes.size() - history_.max_steps();
  const std::size_t undo_removed =
      std::min(excess, session_->history_cursor);
  session_->shapes.erase(
      session_->shapes.begin(),
      session_->shapes.begin() + static_cast<std::ptrdiff_t>(undo_removed));
  session_->history_cursor -= undo_removed;
  // Preserve every applied object's identity, including shapes whose undo was
  // evicted. Only the distant redo tail may lose identities along with history.
  session_->shapes.resize(history_.max_steps());
  session_->created_object_ids.resize(
      session_->accepted_shapes +
      session_->shapes.size() - session_->history_cursor);
  ++tool_settings_revision_;
}

std::size_t PrimitiveCreationToolService::shape_count() const {
  return session_.has_value() ? session_->accepted_shapes : 0U;
}

bool PrimitiveCreationToolService::owns_created_object(
    int modeling_object_id) const {
  if (!session_.has_value()) {
    return false;
  }
  const auto end = session_->created_object_ids.begin() +
                   session_->accepted_shapes;
  return std::find(session_->created_object_ids.begin(), end,
                   modeling_object_id) != end;
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
  const auto kind = !session_ && session_defaults_
                        ? session_defaults_->preferred_kind : preferred_kind_;
  return activate(primitive_creation_available_in_release(kind)
                      ? kind : PrimitiveCreationKind::Box);
}

CommandResult
PrimitiveCreationToolService::activate(PrimitiveCreationKind kind) {
  if (!primitive_creation_available_in_release(kind)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
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
  if (session_defaults_) {
    accepted_settings_ = *session_defaults_;
  }
  load_settings(accepted_settings_);
  preferred_kind_ = kind;
  remember_settings();
  ++tool_settings_revision_;
  return {
      .ok = true,
      .changed = true,
  };
}

CommandResult
PrimitiveCreationToolService::set_kind(PrimitiveCreationKind kind) {
  if (!primitive_creation_available_in_release(kind)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  apply_history_limit();
  if (!session_.has_value()) {
    return command_invalid_result("No primitive tool is active.");
  }
  if (modeling_.box_preview(false).drag_state !=
      ModelingBoxDragState::Idle) {
    return command_invalid_result(
        "Primitive type can only change before drawing a footprint.");
  }

  const CommandResult frozen = freeze_live_primitive_if_selection_changed();
  if (!frozen.ok) {
    return frozen;
  }
  if (live_primitive_.has_value() && live_primitive_->resize.has_value()) {
    return command_invalid_result("Finish resizing before changing primitive type.");
  }
  preferred_kind_ = kind;
  if (session_->kind == kind) {
    return frozen;
  }
  session_->kind = kind;
  ++tool_settings_revision_;
  return update_live_primitive();
}

CommandResult PrimitiveCreationToolService::accept_preview(
    const ModelingBoxPreview &preview, std::string name,
    MaterialReference material) {
  return accept_volume(preview, std::move(name), material, false);
}

CommandResult PrimitiveCreationToolService::accept_footprint(
    const ModelingBoxPreview &preview, std::string name,
    MaterialReference material) {
  return accept_volume(preview, std::move(name), material, true);
}

CommandResult PrimitiveCreationToolService::accept_volume(
    const ModelingBoxPreview &preview, std::string name,
    MaterialReference material, bool use_footprint_proportions) {
  if (session_ &&
      (!primitive_creation_available_in_release(session_->kind) ||
       (session_->kind == PrimitiveCreationKind::Sphere &&
        !primitive_sphere_type_available_in_release(sphere_settings_.type)))) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  apply_history_limit();
  if (!session_.has_value()) {
    return command_invalid_result("No primitive tool is active.");
  }
  std::optional<PrimitivePlacementFrame> frame =
      placement_frame(preview);
  if (!frame.has_value()) {
    return rollback_and_exit(
        "Primitive footprint does not define a valid volume.", true);
  }

  // Beginning another accepted shape is itself a selection transition: the
  // runtime will select the new object and deselect the prior live primitive.
  const ModelingSessionMemento before = modeling_.capture_history_memento();
  if (!before.valid()) {
    return rollback_and_exit(
        "Primitive tool could not capture the shape's history state.", true);
  }
  if (session_->history_cursor > 0U) {
    // Seal the preceding shape after its final live settings edits.
    session_->shapes[session_->history_cursor - 1U].after = before;
  }
  live_primitive_.reset();

  QVec3 local_min{-frame->width * 0.5f, -frame->height * 0.5f,
                  -frame->depth * 0.5f};
  QVec3 local_max = local_min * -1.0f;
  if (session_->kind != PrimitiveCreationKind::Box) {
    // Capture the generator's dimension clamp and complete radial volume,
    // including extrema omitted by odd tessellation counts.
    const auto volume = quader_poly::make_cube({
        .width = frame->width, .height = frame->height, .depth = frame->depth});
    const QVec3 corner = volume.vertices.front().position;
    local_max = {std::abs(corner.x), std::abs(corner.y), std::abs(corner.z)};
    local_min = local_max * -1.0f;
  }
  if (use_footprint_proportions &&
      (session_->kind == PrimitiveCreationKind::Sphere ||
       session_->kind == PrimitiveCreationKind::Torus)) {
    // The shared footprint tool supplies a grid-step height. At creation only,
    // use the smaller drawn diameter so rectangular footprints keep both X/Z
    // spans and get a natural height along their narrower radial section.
    // Torus's ratio is tube/outer radius, so tube diameter = outer diameter *
    // ratio. A hemisphere's base-to-pole height is one radius.
    const double diameter = std::min(local_max.x - local_min.x,
                                     local_max.z - local_min.z);
    const double ratio = session_->kind == PrimitiveCreationKind::Torus
                             ? torus_settings_.tube_radius_ratio
                             : (sphere_settings_.hemisphere ? 0.5 : 1.0);
    float height = static_cast<float>(std::max(kPrimitiveMinimumExtent,
                                               diameter * ratio));
    if (static_cast<double>(height) < kPrimitiveMinimumExtent) {
      height = std::nextafter(height, std::numeric_limits<float>::infinity());
    }
    const QVec3 bottom = (preview.corners[0] + preview.corners[1] +
                          preview.corners[2] + preview.corners[3]) * 0.25f;
    frame->height = height;
    frame->center = bottom + frame->y_axis * (height * 0.5f);
    local_min.y = -height * 0.5f;
    local_max.y = height * 0.5f;
  }
  quader_poly::Document document =
      session_->kind == PrimitiveCreationKind::Box ||
              session_->kind == PrimitiveCreationKind::Cylinder
          ? build_primitive_document(session_->kind, *frame, box_settings_,
                                     cylinder_settings_, cone_settings_,
                                     sphere_settings_, torus_settings_)
          : curved_document_with_bounds(session_->kind, local_min, local_max,
                                        box_settings_, cylinder_settings_,
                                        cone_settings_, sphere_settings_, torus_settings_);
  if (document.vertices.empty() || document.faces.empty()) {
    return rollback_and_exit("Primitive geometry could not be generated.", true);
  }
  if (name.empty()) {
    name = std::string(primitive_creation_default_name(session_->kind));
  }
  if (session_->kind == PrimitiveCreationKind::Box) {
    local_min = document.vertices.front().position;
    local_max = local_min;
    for (const quader_poly::Vertex &vertex : document.vertices) {
      local_min = {std::min(local_min.x, vertex.position.x),
                   std::min(local_min.y, vertex.position.y),
                   std::min(local_min.z, vertex.position.z)};
      local_max = {std::max(local_max.x, vertex.position.x),
                   std::max(local_max.y, vertex.position.y),
                   std::max(local_max.z, vertex.position.z)};
    }
  }

  if (checkpoint_hooks_.observe_before_projection) {
    checkpoint_hooks_.observe_before_projection();
  }
  const ModelingCreateBoxResult created = modeling_.create_mesh({
      .document = std::move(document),
      .name = std::move(name),
      .material_slot = 0,
      .material = material,
      .initial_scene_transform = placement_transform(*frame),
      .world_texture_projection = true,
  });
  if (!created.ok || !created.changed) {
    return rollback_and_exit(
        created.message.empty() ? "Primitive shape could not be created."
                                : created.message,
        true);
  }
  session_->shapes.resize(session_->history_cursor);
  session_->created_object_ids.resize(session_->accepted_shapes);
  if (history_.max_steps() > 0U) {
    session_->shapes.push_back({.before = before});
    ++session_->history_cursor;
  }
  ++session_->accepted_shapes;
  ++tool_settings_revision_;
  session_->created_object_ids.push_back(created.object.modeling_object_id);
  const ModelingSelectionSummary selection = modeling_.selection_summary();
  live_primitive_ = LivePrimitive{
      .placement = preview,
      .modeling_object_id = created.object.modeling_object_id,
      .selection_revision = selection.selection_revision,
      .local_min = local_min,
      .local_max = local_max,
  };
  if (checkpoint_hooks_.record_after_projection) {
    checkpoint_hooks_.record_after_projection();
  }

  const CommandResult reset = reset_footprint();
  if (!reset.ok) {
    return rollback_and_exit(
        reset.message.empty()
            ? "Primitive shape was created, but the next footprint could not start."
            : reset.message,
        true);
  }
  if (session_->history_cursor > 0U) {
    session_->shapes.back().after = modeling_.capture_history_memento();
    if (!session_->shapes.back().after.valid()) {
      return rollback_and_exit(
          "Primitive tool could not capture the created shape.", true);
    }
  }
  apply_history_limit();
  modeling_.release_redundant_sdk_history();
  return command_changed_result();
}

CommandResult PrimitiveCreationToolService::apply() {
  apply_history_limit();
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

  if (session_->history_cursor > 0U) {
    session_->shapes[session_->history_cursor - 1U].after =
        modeling_.capture_history_memento();
  }
  std::vector<std::unique_ptr<RecordedModelingMementoCommand>> commands;
  commands.reserve(session_->history_cursor);
  for (std::size_t index = 0; index < session_->history_cursor; ++index) {
    const ShapeHistory &shape = session_->shapes[index];
    // Each snapshot already contains exactly this step's objects. Rebase its
    // host projection so unrelated edits made during creation stay before the
    // primitive steps in document history.
    const ModelingSessionMemento before =
        modeling_.capture_history_memento_without_objects(shape.before, {});
    const ModelingSessionMemento after =
        modeling_.capture_history_memento_without_objects(shape.after, {});
    if (!before.valid() || !after.valid()) {
      return rollback_and_exit(
          "Primitive tool could not capture its applied state.", true);
    }
    commands.push_back(std::make_unique<RecordedModelingMementoCommand>(
        modeling_, "Create Primitive", before, after));
  }
  // Prepare every memento before publishing any history entry. Recording a
  // non-null, already-executed command performs no further document mutation.
  if (session_->accepted_shapes > session_->history_cursor) {
    // Evicted shapes become the new permanent baseline. Older native mementos
    // predate those shapes and cannot safely remain reachable through Undo.
    history_.clear();
  }
  for (auto &command : commands) {
    const CommandResult recorded = history_.record_executed(std::move(command));
    if (!recorded.ok) {
      return rollback_and_exit(
          recorded.message.empty()
              ? "Primitive tool could not record its history transaction."
              : recorded.message,
          true);
    }
  }

  finish_session();
  finish_checkpoint(false);
  return command_changed_result();
}

CommandResult PrimitiveCreationToolService::undo() {
  apply_history_limit();
  if (!can_undo()) {
    return command_invalid_result("No primitive to undo.");
  }
  if (live_primitive_.has_value() && live_primitive_->resize.has_value()) {
    return cancel_resize();
  }
  if (modeling_.box_drag_state() != ModelingBoxDragState::Idle) {
    live_primitive_.reset();
    const CommandResult reset = reset_footprint();
    if (reset.ok) {
      ++tool_settings_revision_;
    }
    return reset;
  }

  ShapeHistory &shape = session_->shapes[session_->history_cursor - 1U];
  const ModelingSessionMemento after = modeling_.capture_history_memento();
  if (!after.valid()) {
    return command_invalid_result("Primitive redo state could not be captured.");
  }
  const CommandResult restored = restore_shape_history(shape.before);
  if (!restored.ok) {
    return restored;
  }
  shape.after = after;
  --session_->accepted_shapes;
  --session_->history_cursor;
  return restored;
}

CommandResult PrimitiveCreationToolService::redo() {
  apply_history_limit();
  if (!can_redo()) {
    return command_invalid_result("No primitive to redo.");
  }
  const CommandResult restored = restore_shape_history(
      session_->shapes[session_->history_cursor].after);
  if (restored.ok) {
    ++session_->accepted_shapes;
    ++session_->history_cursor;
  }
  return restored;
}

CommandResult PrimitiveCreationToolService::reset_footprint() {
  const ModelingCommandResult reset = modeling_.cancel_box();
  if (!reset.ok) {
    return modeling_memento_command_result(reset);
  }
  const ModelingCommandResult activated = modeling_.activate_box_tool();
  return {
      .ok = activated.ok,
      .changed = reset.changed || activated.changed,
      .message = activated.message,
  };
}

CommandResult PrimitiveCreationToolService::restore_shape_history(
    const ModelingSessionMemento &memento) {
  const ModelingSessionMemento rebased =
      modeling_.capture_history_memento_without_objects(memento, {});
  if (!rebased.valid()) {
    return command_invalid_result("Primitive history state could not be captured.");
  }
  if (checkpoint_hooks_.observe_before_projection) {
    checkpoint_hooks_.observe_before_projection();
  }
  const ModelingCommandResult restored =
      modeling_.restore_history_memento(rebased);
  if (checkpoint_hooks_.record_after_projection) {
    checkpoint_hooks_.record_after_projection();
  }
  if (!restored.ok) {
    return modeling_memento_command_result(restored);
  }
  // A history selection transition freezes all previously accepted primitives.
  live_primitive_.reset();
  ++tool_settings_revision_;
  return modeling_memento_command_result(restored);
}

CommandResult PrimitiveCreationToolService::cancel() {
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  return rollback_and_exit({}, false);
}

CommandResult PrimitiveCreationToolService::set_setting(std::string_view name,
                                                        double value) {
  if (!primitive_setting_available_in_release(name, value)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  apply_history_limit();
  if (!session_.has_value()) {
    return command_invalid_result("No primitive tool is active.");
  }
  const bool drawing =
      modeling_.box_drag_state() != ModelingBoxDragState::Idle;
  const bool pending_toggle =
      name == kPrimitiveFlipFacesSetting ||
      (name == kCylinderCapsSetting && session_->kind == PrimitiveCreationKind::Cylinder) ||
      (name == kConeCapsSetting && session_->kind == PrimitiveCreationKind::Cone) ||
      (name == kSphereHemisphereSetting && session_->kind == PrimitiveCreationKind::Sphere);
  if (drawing && !pending_toggle) {
    return command_invalid_result(
        "Primitive settings can only change before drawing a footprint.");
  }

  if (!std::isfinite(value)) {
    return command_invalid_result("Primitive setting must be finite.");
  }
  // Hidden topology controls can still have an in-flight edit callback. Reject
  // those edits, and all invalid Sphere input, before changing gesture ownership.
  if (session_->kind == PrimitiveCreationKind::Sphere) {
    int count = 0;
    const bool uv = sphere_settings_.type == PrimitiveSphereType::Uv;
    const bool valid = name == kPrimitiveFlipFacesSetting ||
        name == kSphereHemisphereSetting ||
        (name == kSphereTypeSetting && integer_setting_value(value, 0, 1, count)) ||
        (name == kSphereSubdivisionSetting && !uv && integer_setting_value(
            value, kSphereMinimumSubdivision, kSphereMaximumSubdivision, count)) ||
        (name == kSphereRadialSegmentsSetting && uv && integer_setting_value(
            value, kSphereMinimumRadialSegments, kSphereMaximumRadialSegments, count)) ||
        (name == kSphereRingsSetting && uv && integer_setting_value(
            value, kSphereMinimumRings, kSphereMaximumRings, count));
    if (!valid) {
      return command_invalid_result("Invalid or inactive Sphere primitive setting.");
    }
  }
  // Invalid input must not cancel a pending resize as a side effect. Validate
  // shape-specific settings before retiring an interrupted gesture.
  if (session_->kind == PrimitiveCreationKind::Box &&
      live_primitive_.has_value() && live_primitive_->resize.has_value() &&
      name != kPrimitiveFlipFacesSetting) {
    if (name != kBoxSubdivisionsXSetting && name != kBoxSubdivisionsYSetting &&
        name != kBoxSubdivisionsZSetting) {
      return command_invalid_result("Unknown Box primitive setting.");
    }
    int count = 0;
    if (!integer_setting_value(value, kPrimitiveMinimumSubdivisionCount,
                               kPrimitiveMaximumSubdivisionCount, count)) {
      return command_invalid_result("Invalid Box subdivision count.");
    }
  }
  if (session_->kind == PrimitiveCreationKind::Cylinder &&
      live_primitive_.has_value() && live_primitive_->resize.has_value()) {
    int count = 0;
    if (name != kCylinderSideCountSetting && name != kCylinderCapsSetting &&
        name != kPrimitiveFlipFacesSetting) {
      return command_invalid_result("Unknown Cylinder primitive setting.");
    }
    if (name == kCylinderSideCountSetting &&
        !integer_setting_value(value, kCylinderMinimumSideCount,
                               kCylinderMaximumSideCount, count)) {
      return command_invalid_result("Invalid Cylinder side count.");
    }
  }
  if (live_primitive_ && live_primitive_->resize) {
    int count = 0;
    bool valid = true;
    switch (session_->kind) {
    case PrimitiveCreationKind::Cone:
      valid = name == kPrimitiveFlipFacesSetting || name == kConeCapsSetting ||
          (name == kConeSideCountSetting && integer_setting_value(
              value, kConeMinimumSideCount, kConeMaximumSideCount, count));
      break;
    case PrimitiveCreationKind::Torus:
      valid = name == kPrimitiveFlipFacesSetting ||
          (name == kTorusMajorSegmentsSetting && integer_setting_value(
              value, kTorusMinimumMajorSegments, kTorusMaximumMajorSegments, count)) ||
          (name == kTorusMinorSegmentsSetting && integer_setting_value(
              value, kTorusMinimumMinorSegments, kTorusMaximumMinorSegments, count)) ||
          (name == kTorusTubeRadiusRatioSetting && value >= kTorusMinimumTubeRadiusRatio &&
              value <= kTorusMaximumTubeRadiusRatio);
      break;
    case PrimitiveCreationKind::Box:
    case PrimitiveCreationKind::Cylinder:
    case PrimitiveCreationKind::Sphere:
      break;
    }
    if (!valid) {
      return command_invalid_result("Invalid primitive setting during resize.");
    }
  }
  const CommandResult frozen = freeze_live_primitive_if_selection_changed();
  if (!frozen.ok) {
    // Cancellation failure may have rolled back and destroyed the session.
    return frozen;
  }
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
        return frozen;
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
      return frozen;
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
        return frozen;
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
      return frozen;
    }
    *setting = toggle;
    ++tool_settings_revision_;
    return update_live_primitive();
  }
  case PrimitiveCreationKind::Cone: {
    if (name == kConeSideCountSetting) {
      int count = 0;
      if (!integer_setting_value(value, kConeMinimumSideCount,
                                 kConeMaximumSideCount, count)) {
        return command_invalid_result("Invalid Cone side count.");
      }
      if (cone_settings_.side_count == count) {
        return frozen;
      }
      cone_settings_.side_count = count;
      ++tool_settings_revision_;
      return update_live_primitive();
    }
    bool *setting = nullptr;
    if (name == kConeCapsSetting) {
      setting = &cone_settings_.caps;
    } else if (name == kPrimitiveFlipFacesSetting) {
      setting = &cone_settings_.flip_faces;
    } else {
      return command_invalid_result("Unknown Cone primitive setting.");
    }
    if (*setting == toggle) {
      return frozen;
    }
    *setting = toggle;
    ++tool_settings_revision_;
    return update_live_primitive();
  }
  case PrimitiveCreationKind::Sphere: {
    int count = 0;
    if (name == kSphereTypeSetting) {
      const auto type = static_cast<PrimitiveSphereType>(static_cast<int>(value));
      if (sphere_settings_.type == type) {
        return frozen;
      }
      // The accepted bounds survive the topology switch, but its previous
      // gesture must no longer accept drag updates against the old baseline.
      const CommandResult ended = end_resize();
      if (!ended.ok) {
        return ended;
      }
      sphere_settings_.type = type;
      ++tool_settings_revision_;
      return update_live_primitive();
    }
    if (name == kSphereSubdivisionSetting) {
      count = static_cast<int>(value);
      if (sphere_settings_.subdivision == count) {
        return frozen;
      }
      sphere_settings_.subdivision = count;
      ++tool_settings_revision_;
      return update_live_primitive();
    }
    if (name == kSphereRadialSegmentsSetting) {
      if (!integer_setting_value(value, kSphereMinimumRadialSegments,
                                 kSphereMaximumRadialSegments, count)) {
        return command_invalid_result("Invalid Sphere radial segment count.");
      }
      if (sphere_settings_.radial_segments == count) {
        return frozen;
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
        return frozen;
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
      return frozen;
    }
    *setting = toggle;
    ++tool_settings_revision_;
    return update_live_primitive();
  }
  case PrimitiveCreationKind::Torus: {
    int count = 0;
    if (name == kTorusMajorSegmentsSetting) {
      if (!integer_setting_value(value, kTorusMinimumMajorSegments,
                                 kTorusMaximumMajorSegments, count)) {
        return command_invalid_result("Invalid Torus major segment count.");
      }
      if (torus_settings_.major_segments == count) {
        return frozen;
      }
      torus_settings_.major_segments = count;
      ++tool_settings_revision_;
      return update_live_primitive();
    }
    if (name == kTorusMinorSegmentsSetting) {
      if (!integer_setting_value(value, kTorusMinimumMinorSegments,
                                 kTorusMaximumMinorSegments, count)) {
        return command_invalid_result("Invalid Torus minor segment count.");
      }
      if (torus_settings_.minor_segments == count) {
        return frozen;
      }
      torus_settings_.minor_segments = count;
      ++tool_settings_revision_;
      return update_live_primitive();
    }
    if (name == kTorusTubeRadiusRatioSetting) {
      if (!std::isfinite(value) || value < kTorusMinimumTubeRadiusRatio ||
          value > kTorusMaximumTubeRadiusRatio) {
        return command_invalid_result("Invalid Torus tube radius ratio.");
      }
      if (torus_settings_.tube_radius_ratio == value) {
        return frozen;
      }
      torus_settings_.tube_radius_ratio = value;
      ++tool_settings_revision_;
      return update_live_primitive();
    }
    if (name != kPrimitiveFlipFacesSetting) {
      return command_invalid_result("Unknown Torus primitive setting.");
    }
    if (torus_settings_.flip_faces == toggle) {
      return frozen;
    }
    torus_settings_.flip_faces = toggle;
    ++tool_settings_revision_;
    return update_live_primitive();
  }
  }
  return command_invalid_result("Unknown primitive setting.");
}

CommandResult PrimitiveCreationToolService::reset_settings() {
  apply_history_limit();
  if (!session_.has_value()) {
    return command_invalid_result("No primitive tool is active.");
  }
  if (!can_reset_settings()) {
    return command_invalid_result(
        "Primitive settings can only change before drawing a footprint.");
  }

  const CommandResult frozen = freeze_live_primitive_if_selection_changed();
  if (!frozen.ok) {
    return frozen;
  }
  bool changed = false;
  switch (session_->kind) {
  case PrimitiveCreationKind::Box: {
    const BoxPrimitiveCreationSettings defaults;
    changed = box_settings_.subdivisions_x != defaults.subdivisions_x ||
              box_settings_.subdivisions_y != defaults.subdivisions_y ||
              box_settings_.subdivisions_z != defaults.subdivisions_z ||
              box_settings_.flip_faces != defaults.flip_faces;
    box_settings_ = defaults;
    break;
  }
  case PrimitiveCreationKind::Cylinder: {
    const CylinderPrimitiveCreationSettings defaults;
    changed = cylinder_settings_.side_count != defaults.side_count ||
              cylinder_settings_.caps != defaults.caps ||
              cylinder_settings_.flip_faces != defaults.flip_faces;
    cylinder_settings_ = defaults;
    break;
  }
  case PrimitiveCreationKind::Cone: {
    const ConePrimitiveCreationSettings defaults;
    changed = cone_settings_.side_count != defaults.side_count ||
              cone_settings_.caps != defaults.caps ||
              cone_settings_.flip_faces != defaults.flip_faces;
    cone_settings_ = defaults;
    break;
  }
  case PrimitiveCreationKind::Sphere: {
    const SpherePrimitiveCreationSettings defaults;
    changed =
        sphere_settings_.radial_segments != defaults.radial_segments ||
        sphere_settings_.rings != defaults.rings ||
        sphere_settings_.hemisphere != defaults.hemisphere ||
        sphere_settings_.flip_faces != defaults.flip_faces ||
        sphere_settings_.type != defaults.type ||
        sphere_settings_.subdivision != defaults.subdivision;
    if (sphere_settings_.type != defaults.type) {
      const CommandResult ended = end_resize();
      if (!ended.ok) {
        return ended;
      }
    }
    sphere_settings_ = defaults;
    break;
  }
  case PrimitiveCreationKind::Torus: {
    const TorusPrimitiveCreationSettings defaults;
    changed = torus_settings_.major_segments != defaults.major_segments ||
              torus_settings_.minor_segments != defaults.minor_segments ||
              torus_settings_.tube_radius_ratio !=
                  defaults.tube_radius_ratio ||
              torus_settings_.flip_faces != defaults.flip_faces;
    torus_settings_ = defaults;
    break;
  }
  }
  if (!changed) {
    return frozen;
  }
  ++tool_settings_revision_;
  return update_live_primitive();
}

bool PrimitiveCreationToolService::can_reset_settings() const {
  return session_.has_value() &&
         modeling_.box_drag_state() == ModelingBoxDragState::Idle;
}

std::optional<PrimitiveCreationState>
PrimitiveCreationToolService::live_primitive_creation() const {
  if (!session_.has_value() ||
      modeling_.box_drag_state() != ModelingBoxDragState::Idle ||
      !live_primitive_is_selected() ||
      !modeling_.entity_for_modeling_object(live_primitive_->modeling_object_id)
           .has_value()) {
    return std::nullopt;
  }
  return PrimitiveCreationState{
      .modeling_object_id = live_primitive_->modeling_object_id,
      .kind = session_->kind,
      .local_min = live_primitive_->local_min,
      .local_max = live_primitive_->local_max,
      .subdivisions = {box_settings_.subdivisions_x, box_settings_.subdivisions_y,
                      box_settings_.subdivisions_z},
      .side_count = session_->kind == PrimitiveCreationKind::Cone
                        ? cone_settings_.side_count : cylinder_settings_.side_count,
      .caps = session_->kind == PrimitiveCreationKind::Cone
                  ? cone_settings_.caps : cylinder_settings_.caps,
      .radial_segments = sphere_settings_.radial_segments,
      .rings = sphere_settings_.rings,
      .hemisphere = sphere_settings_.hemisphere,
      .major_segments = torus_settings_.major_segments,
      .minor_segments = torus_settings_.minor_segments,
      .tube_radius_ratio = torus_settings_.tube_radius_ratio,
      .flip_faces = session_->kind == PrimitiveCreationKind::Box ? box_settings_.flip_faces
          : session_->kind == PrimitiveCreationKind::Cylinder ? cylinder_settings_.flip_faces
          : session_->kind == PrimitiveCreationKind::Cone ? cone_settings_.flip_faces
          : session_->kind == PrimitiveCreationKind::Sphere ? sphere_settings_.flip_faces
          : torus_settings_.flip_faces,
      .resizing = live_primitive_->resize.has_value(),
      .revision = primitive_revision_ + tool_settings_revision_,
      .sphere_type = sphere_settings_.type,
      .sphere_subdivision = sphere_settings_.subdivision,
  };
}

std::optional<PrimitiveCubeState>
PrimitiveCreationToolService::live_cube() const {
  const auto primitive = live_primitive_creation();
  if (!primitive || primitive->kind != PrimitiveCreationKind::Box) {
    return std::nullopt;
  }
  return PrimitiveCubeState{
      .modeling_object_id = primitive->modeling_object_id,
      .local_min = primitive->local_min,
      .local_max = primitive->local_max,
      .subdivisions = primitive->subdivisions,
      .flip_faces = primitive->flip_faces,
      .resizing = primitive->resizing,
      .revision = primitive->revision,
  };
}

CommandResult PrimitiveCreationToolService::begin_cube_resize(
    PrimitiveCubeAxis axis, bool positive) {
  if (axis != PrimitiveCubeAxis::X && axis != PrimitiveCubeAxis::Y &&
      axis != PrimitiveCubeAxis::Z) {
    return command_invalid_result("Invalid cube resize axis.");
  }
  if (!live_cube()) {
    return command_invalid_result("No selected live cube is available to resize.");
  }
  return begin_resize(static_cast<PrimitiveResizeControl>(
      static_cast<int>(axis) * 2 + (positive ? 0 : 1)));
}

CommandResult PrimitiveCreationToolService::update_cube_resize(double displacement) {
  if (!session_ || session_->kind != PrimitiveCreationKind::Box) {
    return command_invalid_result("No selected cube resize is active.");
  }
  return update_resize(displacement);
}

CommandResult PrimitiveCreationToolService::end_cube_resize() {
  return session_ && session_->kind != PrimitiveCreationKind::Box
             ? command_unchanged_result() : end_resize();
}

CommandResult PrimitiveCreationToolService::cancel_cube_resize() {
  return session_ && session_->kind != PrimitiveCreationKind::Box
             ? command_unchanged_result() : cancel_resize();
}

CommandResult PrimitiveCreationToolService::begin_resize(
    PrimitiveResizeControl control, QVec3 radial_axis) {
  if (control < PrimitiveResizeControl::PositiveX ||
      control > PrimitiveResizeControl::InnerRadius) {
    return command_invalid_result("Invalid primitive resize control.");
  }
  const auto primitive = live_primitive_creation();
  if (!primitive) {
    return command_invalid_result("No selected live primitive is available to resize.");
  }
  if (live_primitive_->resize) {
    return command_invalid_result("A primitive resize is already active.");
  }
  const bool radial = control >= PrimitiveResizeControl::Radius;
  const bool height = control == PrimitiveResizeControl::PositiveY ||
                      control == PrimitiveResizeControl::NegativeY;
  bool allowed = false;
  switch (primitive->kind) {
  case PrimitiveCreationKind::Box:
    allowed = !radial;
    break;
  case PrimitiveCreationKind::Cylinder:
  case PrimitiveCreationKind::Cone:
  case PrimitiveCreationKind::Sphere:
    allowed = height || control == PrimitiveResizeControl::Radius;
    break;
  case PrimitiveCreationKind::Torus:
    allowed = control == PrimitiveResizeControl::PositiveY ||
              control == PrimitiveResizeControl::OuterRadius ||
              control == PrimitiveResizeControl::InnerRadius;
    break;
  }
  if (!allowed) {
    return command_invalid_result("Resize control does not belong to this primitive.");
  }
  LivePrimitive::ResizeBaseline baseline{
      .control = control,
      .local_min = live_primitive_->local_min,
      .local_max = live_primitive_->local_max,
      .tube_radius_ratio = torus_settings_.tube_radius_ratio,
  };
  if (radial) {
    if (!finite_vector(radial_axis)) {
      return command_invalid_result("Primitive radial axis must be finite.");
    }
    const double magnitude = std::hypot(static_cast<double>(radial_axis.x),
                                        static_cast<double>(radial_axis.z));
    if (magnitude == 0.0) {
      return command_invalid_result("Primitive radial axis must have an XZ direction.");
    }
    const double x = radial_axis.x / magnitude;
    const double z = radial_axis.z / magnitude;
    baseline.radial_axis = {static_cast<float>(x), 0.0f, static_cast<float>(z)};
    const double rx = (static_cast<double>(baseline.local_max.x) - baseline.local_min.x) * 0.5;
    const double rz = (static_cast<double>(baseline.local_max.z) - baseline.local_min.z) * 0.5;
    baseline.radial_distance = 1.0 / std::hypot(x / rx, z / rz);
    if (!std::isfinite(baseline.radial_distance) || baseline.radial_distance <= 0.0) {
      return command_invalid_result("Primitive has an invalid radial extent.");
    }
  }
  live_primitive_->resize = baseline;
  ++primitive_revision_;
  return command_changed_result();
}

CommandResult PrimitiveCreationToolService::update_resize(double displacement) {
  if (!std::isfinite(displacement)) {
    return command_invalid_result("Primitive resize displacement must be finite.");
  }
  if (!live_primitive_creation() || !live_primitive_->resize) {
    return command_invalid_result("No selected primitive resize is active.");
  }
  const LivePrimitive::ResizeBaseline baseline = *live_primitive_->resize;
  QVec3 minimum = baseline.local_min;
  QVec3 maximum = baseline.local_max;
  double next_ratio = torus_settings_.tube_radius_ratio;
  if (baseline.control >= PrimitiveResizeControl::Radius) {
    double minimum_scale = 0.0;
    // Include representable endpoints in the minimum, including ellipses whose
    // centers were offset by a previous Box face edit before morphing.
    for (const auto axis : {PrimitiveCubeAxis::X, PrimitiveCubeAxis::Z}) {
      const double lo = cube_axis_component(minimum, axis);
      const double hi = cube_axis_component(maximum, axis);
      const double center = (lo + hi) * 0.5;
      const double radius = (hi - lo) * 0.5;
      const double low_limit = center - kPrimitiveMinimumExtent * 0.5;
      const double high_limit = center + kPrimitiveMinimumExtent * 0.5;
      float low = static_cast<float>(low_limit);
      float high = static_cast<float>(high_limit);
      if (low > low_limit) {
        low = std::nextafter(low, -std::numeric_limits<float>::infinity());
      }
      if (high < high_limit) {
        high = std::nextafter(high, std::numeric_limits<float>::infinity());
      }
      minimum_scale = std::max(minimum_scale,
          std::max(center - low, high - center) / radius);
    }
    double scale = 1.0;
    if (baseline.control == PrimitiveResizeControl::Radius) {
      scale = std::max(minimum_scale,
          (baseline.radial_distance + displacement) / baseline.radial_distance);
    } else {
      const double outer = baseline.radial_distance;
      const double inner = outer * (1.0 - 2.0 * baseline.tube_radius_ratio);
      if (baseline.control == PrimitiveResizeControl::OuterRadius) {
        const double smallest = inner / (1.0 - 2.0 * kTorusMinimumTubeRadiusRatio);
        const double largest = inner / (1.0 - 2.0 * kTorusMaximumTubeRadiusRatio);
        const double minimum_outer = std::max(smallest, outer * minimum_scale);
        if (minimum_outer > largest) {
          return command_invalid_result("Torus radius cannot satisfy its minimum extent.");
        }
        const double resized = std::clamp(outer + displacement, minimum_outer, largest);
        scale = resized / outer;
        next_ratio = resized == smallest ? kTorusMinimumTubeRadiusRatio
            : resized == largest ? kTorusMaximumTubeRadiusRatio
            : std::clamp(0.5 * (1.0 - inner / resized),
                         kTorusMinimumTubeRadiusRatio, kTorusMaximumTubeRadiusRatio);
      } else {
        const double smallest = outer * (1.0 - 2.0 * kTorusMaximumTubeRadiusRatio);
        const double largest = outer * (1.0 - 2.0 * kTorusMinimumTubeRadiusRatio);
        const double resized = std::clamp(inner + displacement, smallest, largest);
        next_ratio = resized == smallest ? kTorusMaximumTubeRadiusRatio
            : resized == largest ? kTorusMinimumTubeRadiusRatio
            : std::clamp(0.5 * (1.0 - resized / outer),
                         kTorusMinimumTubeRadiusRatio, kTorusMaximumTubeRadiusRatio);
      }
      // Avoid numerical drift from reconstructing the ratio at the press point.
      if (displacement == 0.0) {
        scale = 1.0;
        next_ratio = baseline.tube_radius_ratio;
      }
    }
    for (const auto axis : {PrimitiveCubeAxis::X, PrimitiveCubeAxis::Z}) {
      float &lo = cube_axis_component(minimum, axis);
      float &hi = cube_axis_component(maximum, axis);
      const double center = (static_cast<double>(lo) + hi) * 0.5;
      const double radius = (static_cast<double>(hi) - lo) * 0.5 * scale;
      if (!std::isfinite(radius) || radius * 2.0 > std::numeric_limits<float>::max() ||
          std::abs(center) + radius > std::numeric_limits<float>::max()) {
        return command_invalid_result("Primitive resize exceeds the local coordinate range.");
      }
      lo = static_cast<float>(center - radius);
      hi = static_cast<float>(center + radius);
    }
  } else {
    const int control = static_cast<int>(baseline.control);
    const auto axis = static_cast<PrimitiveCubeAxis>(control / 2);
    const bool positive = control % 2 == 0;
    float &moving = cube_axis_component(positive ? maximum : minimum, axis);
    const float fixed = cube_axis_component(positive ? minimum : maximum, axis);
    const double requested = static_cast<double>(moving) + displacement;
    const double limit = static_cast<double>(fixed) +
                         (positive ? kPrimitiveMinimumExtent : -kPrimitiveMinimumExtent);
    const double clamped = positive ? std::max(requested, limit) : std::min(requested, limit);
    if (!std::isfinite(clamped) || std::abs(clamped) > std::numeric_limits<float>::max()) {
      return command_invalid_result("Primitive resize exceeds the local coordinate range.");
    }
    moving = static_cast<float>(clamped);
    // Round away from the fixed face if conversion undershoots the minimum.
    if (positive ? static_cast<double>(moving) < limit : static_cast<double>(moving) > limit) {
      moving = std::nextafter(moving, positive ? std::numeric_limits<float>::infinity()
                                             : -std::numeric_limits<float>::infinity());
    }
    if (!std::isfinite(moving) ||
        std::abs(static_cast<double>(moving) - fixed) > std::numeric_limits<float>::max()) {
      return command_invalid_result("Primitive resize exceeds the local coordinate range.");
    }
  }
  const bool ratio_changed = session_->kind == PrimitiveCreationKind::Torus &&
                             next_ratio != torus_settings_.tube_radius_ratio;
  if (same_bounds(minimum, live_primitive_->local_min) &&
      same_bounds(maximum, live_primitive_->local_max) && !ratio_changed) {
    return command_unchanged_result();
  }
  live_primitive_->local_min = minimum;
  live_primitive_->local_max = maximum;
  if (ratio_changed) {
    torus_settings_.tube_radius_ratio = next_ratio;
    ++tool_settings_revision_;
  }
  const CommandResult result = update_live_primitive();
  if (result.ok) {
    ++primitive_revision_;
  }
  return result;
}

CommandResult PrimitiveCreationToolService::end_resize() {
  if (!live_primitive_ || !live_primitive_->resize) {
    return command_unchanged_result();
  }
  if (!live_primitive_creation()) {
    return cancel_resize();
  }
  live_primitive_->resize.reset();
  ++primitive_revision_;
  return command_changed_result();
}

CommandResult PrimitiveCreationToolService::cancel_resize() {
  if (!live_primitive_ || !live_primitive_->resize) {
    return command_unchanged_result();
  }
  const bool selected = live_primitive_is_selected();
  const LivePrimitive::ResizeBaseline baseline = *live_primitive_->resize;
  const bool restore_bounds =
      !same_bounds(baseline.local_min, live_primitive_->local_min) ||
      !same_bounds(baseline.local_max, live_primitive_->local_max);
  const bool restore_ratio = session_->kind == PrimitiveCreationKind::Torus &&
                             torus_settings_.tube_radius_ratio != baseline.tube_radius_ratio;
  if (restore_bounds || restore_ratio) {
    live_primitive_->local_min = baseline.local_min;
    live_primitive_->local_max = baseline.local_max;
    if (restore_ratio) {
      torus_settings_.tube_radius_ratio = baseline.tube_radius_ratio;
      ++tool_settings_revision_;
    }
    // Restore bounds and torus ratio, retaining other settings and winding.
    const CommandResult restored = update_live_primitive(true);
    if (!restored.ok) {
      return restored;
    }
  }
  live_primitive_->resize.reset();
  if (!selected) {
    live_primitive_.reset();
  }
  ++primitive_revision_;
  return command_changed_result();
}

CommandResult PrimitiveCreationToolService::flip_faces() {
  if (!session_) {
    return command_invalid_result("No primitive creation tool is active.");
  }
  bool flipped = false;
  switch (session_->kind) {
  case PrimitiveCreationKind::Box: flipped = box_settings_.flip_faces; break;
  case PrimitiveCreationKind::Cylinder: flipped = cylinder_settings_.flip_faces; break;
  case PrimitiveCreationKind::Cone: flipped = cone_settings_.flip_faces; break;
  case PrimitiveCreationKind::Sphere: flipped = sphere_settings_.flip_faces; break;
  case PrimitiveCreationKind::Torus: flipped = torus_settings_.flip_faces; break;
  }
  return set_setting(kPrimitiveFlipFacesSetting, flipped ? 0.0 : 1.0);
}

CommandResult PrimitiveCreationToolService::toggle_caps() {
  if (!session_ || (session_->kind != PrimitiveCreationKind::Cylinder &&
                    session_->kind != PrimitiveCreationKind::Cone)) {
    return command_invalid_result("Toggle creation caps requires Cylinder or Cone creation.");
  }
  return session_->kind == PrimitiveCreationKind::Cone
      ? set_setting(kConeCapsSetting, cone_settings_.caps ? 0.0 : 1.0)
      : set_setting(kCylinderCapsSetting, cylinder_settings_.caps ? 0.0 : 1.0);
}

CommandResult PrimitiveCreationToolService::toggle_hemisphere() {
  if (!session_ || session_->kind != PrimitiveCreationKind::Sphere) {
    return command_invalid_result("Toggle hemisphere requires Sphere creation.");
  }
  return set_setting(kSphereHemisphereSetting, sphere_settings_.hemisphere ? 0.0 : 1.0);
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

CommandResult
PrimitiveCreationToolService::freeze_live_primitive_if_selection_changed() {
  if (!live_primitive_.has_value() ||
      (live_primitive_is_selected() &&
       modeling_.box_drag_state() == ModelingBoxDragState::Idle)) {
    return command_unchanged_result();
  }
  // Selection loss (including a new footprint) can precede viewport gesture
  // retirement. Restore the resize using the previous authored settings before
  // applying any incoming setting to the pending primitive. Never discard its
  // baseline first, and propagate rollback failures before callers use session_.
  CommandResult result = command_unchanged_result();
  if (live_primitive_->resize.has_value()) {
    result = cancel_resize();
    if (!result.ok) {
      return result;
    }
  }
  live_primitive_.reset();
  return result;
}

CommandResult PrimitiveCreationToolService::update_live_primitive(
    bool preserve_selection) {
  if (!session_.has_value()) {
    return command_invalid_result("No primitive tool is active.");
  }
  if (!live_primitive_.has_value()) {
    remember_settings();
    return command_changed_result();
  }

  const std::optional<PrimitivePlacementFrame> frame =
      placement_frame(live_primitive_->placement);
  if (!frame.has_value()) {
    return rollback_and_exit(
        "Live primitive no longer has a valid placement volume.", true);
  }
  quader_poly::Document document =
      session_->kind == PrimitiveCreationKind::Box
          ? cube_document_with_bounds(live_primitive_->local_min,
                                      live_primitive_->local_max, box_settings_)
          : session_->kind == PrimitiveCreationKind::Cylinder
              ? cylinder_document_with_bounds(live_primitive_->local_min,
                                               live_primitive_->local_max,
                                               cylinder_settings_)
              : curved_document_with_bounds(session_->kind, live_primitive_->local_min,
                                             live_primitive_->local_max, box_settings_,
                                             cylinder_settings_, cone_settings_,
                                             sphere_settings_, torus_settings_);
  if (document.vertices.empty() || document.faces.empty()) {
    return rollback_and_exit("Live primitive geometry could not be generated.",
                             true);
  }

  if (checkpoint_hooks_.observe_before_projection) {
    checkpoint_hooks_.observe_before_projection();
  }
  const ModelingCommandResult updated = modeling_.replace_mesh_document(
      live_primitive_->modeling_object_id, std::move(document), preserve_selection);
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
  modeling_.release_redundant_sdk_history();
  remember_settings();
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
  finish_session();
  finish_checkpoint(ok);
  return {
      .ok = ok && !report_failure,
      // A failed shape that rolls back cleanly has no observable mutation.
      .changed = report_failure && ok ? false : changed,
      .message = std::move(message),
  };
}

void PrimitiveCreationToolService::finish_session() {
  live_primitive_.reset();
  session_.reset();
  // A rejected live edit may have modified the working settings before its
  // publication failed. Retain the last accepted options, never that candidate.
  load_settings(accepted_settings_);
  ++tool_settings_revision_;
}

void PrimitiveCreationToolService::load_settings(
    const PrimitiveCreationSessionDefaults &settings) {
  preferred_kind_ = primitive_creation_available_in_release(settings.preferred_kind)
                        ? settings.preferred_kind : PrimitiveCreationKind::Box;
  box_settings_ = settings.box;
  cylinder_settings_ = settings.cylinder;
  cone_settings_ = settings.cone;
  sphere_settings_ = settings.sphere;
  if (!primitive_sphere_type_available_in_release(sphere_settings_.type)) {
    sphere_settings_.type = PrimitiveSphereType::Uv;
  }
  torus_settings_ = settings.torus;
}

void PrimitiveCreationToolService::remember_settings() {
  accepted_settings_ = {
      .preferred_kind = preferred_kind_,
      .box = box_settings_,
      .cylinder = cylinder_settings_,
      .cone = cone_settings_,
      .sphere = sphere_settings_,
      .torus = torus_settings_,
  };
  if (!session_defaults_) {
    return;
  }
  session_defaults_->preferred_kind = preferred_kind_;
  // Other scenes may have accepted new options for other shapes since this
  // session began. Publishing the edited shape must not replace those options.
  switch (preferred_kind_) {
  case PrimitiveCreationKind::Box:
    session_defaults_->box = box_settings_;
    break;
  case PrimitiveCreationKind::Cylinder:
    session_defaults_->cylinder = cylinder_settings_;
    break;
  case PrimitiveCreationKind::Cone:
    session_defaults_->cone = cone_settings_;
    break;
  case PrimitiveCreationKind::Sphere:
    session_defaults_->sphere = sphere_settings_;
    break;
  case PrimitiveCreationKind::Torus:
    session_defaults_->torus = torus_settings_;
    break;
  }
}

void PrimitiveCreationToolService::finish_checkpoint(
    bool restored_by_cancel) {
  if (checkpoint_hooks_.finish) {
    checkpoint_hooks_.finish(restored_by_cancel);
  }
}

} // namespace quader
