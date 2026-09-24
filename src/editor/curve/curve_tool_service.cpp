////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/curve/curve_tool_service.hpp"
#include "core/release_features.hpp"

#include "assets/asset_registry.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/history/curve_history_commands.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/commands/modeling_selection_memento_command.hpp"
#include "editor/interactions/selection_edit.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <quader/modeling/curves/curve_shapes.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

QVec3 native_point(modeling::Vec3 point) { return {point.x, point.y, point.z}; }

modeling::Vec3 portable_point(QVec3 point) {
  return {point.x, point.y, point.z};
}

bool finite_point(QVec3 point) {
  return std::isfinite(point.x) && std::isfinite(point.y) &&
         std::isfinite(point.z);
}

bool same_point(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

constexpr float kPresetFrameEpsilon = 0.000001f;

bool same_frame(const CurvePresetPlacementFrame &left,
                const CurvePresetPlacementFrame &right) {
  return same_point(left.center, right.center) &&
         same_point(left.axis_u, right.axis_u) &&
         same_point(left.axis_v, right.axis_v);
}

std::optional<CurvePresetPlacementFrame>
normalized_preset_frame(CurvePresetPlacementFrame frame) {
  if (!finite_point(frame.center) || !finite_point(frame.axis_u) ||
      !finite_point(frame.axis_v)) {
    return std::nullopt;
  }
  const float u_length = length(frame.axis_u);
  if (!std::isfinite(u_length) || u_length <= kPresetFrameEpsilon) {
    return std::nullopt;
  }
  frame.axis_u = frame.axis_u / u_length;
  frame.axis_v = frame.axis_v - frame.axis_u * dot(frame.axis_v, frame.axis_u);
  const float v_length = length(frame.axis_v);
  if (!std::isfinite(v_length) || v_length <= kPresetFrameEpsilon) {
    return std::nullopt;
  }
  frame.axis_v = frame.axis_v / v_length;
  const QVec3 normal = cross(frame.axis_u, frame.axis_v);
  if (!finite_point(normal) || length(normal) <= kPresetFrameEpsilon) {
    return std::nullopt;
  }
  return frame;
}

QMat4 preset_frame_matrix(const CurvePresetPlacementFrame &frame) {
  const QVec3 normal = normalize(cross(frame.axis_u, frame.axis_v));
  QMat4 result;
  result.values[0] = frame.axis_u.x;
  result.values[1] = frame.axis_u.y;
  result.values[2] = frame.axis_u.z;
  result.values[4] = frame.axis_v.x;
  result.values[5] = frame.axis_v.y;
  result.values[6] = frame.axis_v.z;
  result.values[8] = normal.x;
  result.values[9] = normal.y;
  result.values[10] = normal.z;
  result.values[12] = frame.center.x;
  result.values[13] = frame.center.y;
  result.values[14] = frame.center.z;
  return result;
}

modeling::Transform3
preset_frame_transform(const CurvePresetPlacementFrame &frame) {
  const QVec3 normal = normalize(cross(frame.axis_u, frame.axis_v));
  return {
      .x_axis = portable_point(frame.axis_u),
      .y_axis = portable_point(frame.axis_v),
      .z_axis = portable_point(normal),
      .origin = portable_point(frame.center),
  };
}

std::optional<modeling::CurveDocument>
make_preset_document(CurveCreationKind kind, double circle_radius,
                     double oval_radius, double oval_length,
                     std::string &message) {
  modeling::Result<modeling::CurveDocument> created;
  switch (kind) {
  case CurveCreationKind::Circle:
    created = modeling::make_xy_circle_curve(circle_radius);
    break;
  case CurveCreationKind::Oval:
    created = modeling::make_xy_stadium_curve(oval_radius, oval_length);
    break;
  case CurveCreationKind::Draw:
    message = "Draw curves do not have preset geometry.";
    return std::nullopt;
  default:
    message = "Unknown Curve creation type.";
    return std::nullopt;
  }
  if (!created.ok()) {
    message = created.error_message();
    return std::nullopt;
  }
  return std::move(created).value();
}

std::optional<modeling::CurveDocument>
transform_preset_document(const modeling::CurveDocument &canonical,
                          const CurvePresetPlacementFrame &frame,
                          std::string &message) {
  modeling::CurveDocument placed = canonical;
  const auto transformed = placed.transform(preset_frame_transform(frame));
  if (!transformed.ok()) {
    message = transformed.error_message();
    return std::nullopt;
  }
  return placed;
}

std::uint64_t curve_point_key(modeling::CurvePointId id) {
  return (static_cast<std::uint64_t>(id.index) << 32u) |
         static_cast<std::uint64_t>(id.generation);
}

const modeling::CurveControlPoint *
find_point(const modeling::CurveDocument &document, modeling::CurvePointId id) {
  const auto points = document.points();
  const auto found =
      std::find_if(points.begin(), points.end(),
                   [id](const auto &point) { return point.id == id; });
  return found == points.end() ? nullptr : &*found;
}

std::vector<CurveControlTarget>
visible_curve_controls(const modeling::CurveDocument &document, bool drawing) {
  std::vector<CurveControlTarget> controls;
  controls.reserve(document.point_count() * 3u);
  for (std::size_t point_index = 0; point_index < document.point_count();
       ++point_index) {
    const modeling::CurvePointId point = document.points()[point_index].id;
    for (const CurveControlKind kind :
         {CurveControlKind::Anchor, CurveControlKind::IncomingHandle,
          CurveControlKind::OutgoingHandle}) {
      if (curve_control_is_visible(document, drawing, point_index, kind)) {
        controls.push_back({.point = point, .kind = kind});
      }
    }
  }
  return controls;
}

EditorSubSelectionElementRole sub_selection_role(CurveControlKind kind) {
  switch (kind) {
  case CurveControlKind::Anchor:
    return EditorSubSelectionElementRole::Primary;
  case CurveControlKind::IncomingHandle:
    return EditorSubSelectionElementRole::SecondaryA;
  case CurveControlKind::OutgoingHandle:
    return EditorSubSelectionElementRole::SecondaryB;
  }
  return EditorSubSelectionElementRole::Primary;
}

CurveControlKind curve_control_kind(EditorSubSelectionElementRole role) {
  switch (role) {
  case EditorSubSelectionElementRole::Primary:
    return CurveControlKind::Anchor;
  case EditorSubSelectionElementRole::SecondaryA:
    return CurveControlKind::IncomingHandle;
  case EditorSubSelectionElementRole::SecondaryB:
    return CurveControlKind::OutgoingHandle;
  }
  return CurveControlKind::Anchor;
}

CurveControlTarget curve_control_target(EditorSubSelectionTarget target) {
  return {
      .point = {.index = target.index, .generation = target.generation},
      .kind = curve_control_kind(target.role),
  };
}

EditorSubSelectionTarget sub_selection_target(CurveControlTarget target) {
  return {
      .index = target.point.index,
      .generation = target.point.generation,
      .role = sub_selection_role(target.kind),
  };
}

quader_editor_core::SelectionEditOperation
selection_edit_operation(EditorSubSelectionEdit edit) {
  switch (edit) {
  case EditorSubSelectionEdit::Replace:
    return quader_editor_core::SelectionEditOperation::Replace;
  case EditorSubSelectionEdit::Add:
    return quader_editor_core::SelectionEditOperation::Add;
  case EditorSubSelectionEdit::Remove:
    return quader_editor_core::SelectionEditOperation::Remove;
  case EditorSubSelectionEdit::Toggle:
    return quader_editor_core::SelectionEditOperation::Toggle;
  }
  return quader_editor_core::SelectionEditOperation::Replace;
}

ModelingCommandResult modeling_result(CommandResult result) {
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = std::move(result.message),
  };
}

} // namespace

CurveToolService::CurveToolService(SceneDocument &scene, AssetRegistry &assets,
                                   EditorHistoryService &history,
                                   ModelingSessionService &modeling,
                                   EditorSelectionService &selection,
                                   DocumentCommandService &document_commands,
                                   CurveSessionDefaults *session_defaults)
    : scene_(scene), assets_(assets), history_(history), modeling_(modeling),
      selection_(selection), document_commands_(document_commands),
      session_defaults_(session_defaults != nullptr ? session_defaults
                                                   : &local_session_defaults_) {
  restore_session_defaults();
}

void CurveToolService::restore_session_defaults() {
  creation_kind_ = session_defaults_->creation_kind;
  preferred_closed_ = session_defaults_->closed;
  circle_radius_ = session_defaults_->circle_radius;
  oval_radius_ = session_defaults_->oval_radius;
  oval_length_ = session_defaults_->oval_length;
  preset_document_cache_.reset();
}

bool CurveToolService::can_activate() const {
  return release_has_feature(ReleaseFeature::Curves);
}

bool CurveToolService::controls_editable() const {
  return release_has_feature(ReleaseFeature::Curves) &&
         ((active_ && drawing_) || (!drawing_ && entity_ != kInvalidEntityId));
}

bool CurveToolService::can_apply() const {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return false;
  }
  return active_ && drawing_ && draft_.point_count() >= 2u && !dragging();
}

bool CurveToolService::can_change_creation_kind() const {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return false;
  }
  return active_ && drawing_ && !dragging() &&
         (draft_.point_count() == 0u || procedural_draft_);
}

bool CurveToolService::can_reset_settings() const {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return false;
  }
  return active_ && drawing_ && !dragging();
}

bool CurveToolService::has_editable_existing_curve() const {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return false;
  }
  return editable_existing_curve_component() != nullptr;
}

bool CurveToolService::can_apply_solidify() const {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return false;
  }
  const CurveComponent *component = editable_existing_curve_component();
  return component != nullptr && component->solidify && !dragging();
}

bool CurveToolService::solidify_enabled() const {
  const CurveComponent *component = editable_existing_curve_component();
  return component != nullptr && component->solidify;
}

std::uint32_t CurveToolService::solidify_resolution() const {
  const CurveComponent *component = editable_existing_curve_component();
  return component != nullptr
             ? static_cast<std::uint32_t>(component->solidify_resolution)
             : static_cast<std::uint32_t>(kCurveSolidifyDefaultResolution);
}

double CurveToolService::solidify_radius() const {
  const CurveComponent *component = editable_existing_curve_component();
  if (component == nullptr) {
    return kCurveSolidifyDefaultRadius;
  }
  const float stored_default =
      static_cast<float>(kCurveSolidifyDefaultRadius);
  return component->solidify_radius == stored_default
             ? kCurveSolidifyDefaultRadius
             : static_cast<double>(component->solidify_radius);
}

bool CurveToolService::closed() const {
  if (!drawing_) {
    const auto &preview = preview_state();
    if (preview.document.has_value()) {
      return preview.document->closed();
    }
  }
  if (creation_kind_ != CurveCreationKind::Draw) {
    return true;
  }
  return preferred_closed_;
}

std::size_t CurveToolService::point_count() const {
  const auto &preview = preview_state();
  return preview.document ? preview.document->point_count() : 0u;
}

bool CurveToolService::owns_editing_entity(EntityId entity) const {
  return !drawing_ && dragging() && entity_ == entity;
}

std::uint64_t CurveToolService::tool_settings_revision() const {
  return settings_revision_;
}

const CurveToolPreviewState &CurveToolService::preview_state() const {
  const std::uint64_t scene_revision = scene_.revision();
  const std::uint64_t modeling_revision = modeling_.content_revision();
  const std::uint64_t asset_revision = assets_.revision();
  if (cached_tool_revision_ == revision_ &&
      cached_scene_revision_ == scene_revision &&
      cached_modeling_revision_ == modeling_revision &&
      cached_asset_revision_ == asset_revision) {
    return cached_preview_;
  }

  rebuild_preview_cache(++preview_revision_);
  return cached_preview_;
}

std::optional<modeling::CurveDocument>
CurveToolService::make_current_preset_document(std::string &message) const {
  return make_preset_document(creation_kind_, circle_radius_, oval_radius_,
                              oval_length_, message);
}

std::optional<modeling::CurveDocument>
CurveToolService::place_current_preset(const CurvePresetPlacementFrame &frame,
                                       std::string &message) const {
  std::optional<modeling::CurveDocument> canonical = preset_document_cache_;
  if (!canonical.has_value()) {
    canonical = make_current_preset_document(message);
  }
  if (!canonical.has_value()) {
    return std::nullopt;
  }
  return transform_preset_document(*canonical, frame, message);
}

bool CurveToolService::convert_procedural_draft_to_draw() {
  if (!drawing_ || !procedural_draft_) {
    return false;
  }
  procedural_draft_ = false;
  procedural_frame_.reset();
  preset_hover_frame_.reset();
  creation_kind_ = CurveCreationKind::Draw;
  session_defaults_->creation_kind = creation_kind_;
  session_defaults_->closed = preferred_closed_;
  return true;
}

void CurveToolService::clear_preset_placement_state() {
  procedural_draft_ = false;
  procedural_frame_.reset();
  preset_hover_frame_.reset();
}

void CurveToolService::rebuild_preview_cache(
    std::uint64_t presentation_revision) const {
  const bool source_editable = has_editable_existing_curve();
  const bool controls_active = (active_ && drawing_) || source_editable;
  CurveToolPreviewState preview{
      .active = controls_active,
      .drawing = active_ && drawing_,
      .dragging = dragging(),
      .entity = entity_,
      .modeling_object_id = modeling_object_id_,
      .control_selection = control_selection_,
      .hover_world = preset_hover_frame_.has_value()
                         ? std::optional<QVec3>(preset_hover_frame_->center)
                         : hover_world_,
      .revision = presentation_revision,
  };
  if (active_ && drawing_) {
    preview.document = draft_;
  } else if (source_editable) {
    preview.document = modeling_.curve_document(modeling_object_id_);
    preview.world_transform = scene_.world_transform_matrix(entity_);
  }
  if (active_ && drawing_ && !dragging() && draft_.point_count() == 0u &&
      creation_kind_ != CurveCreationKind::Draw &&
      preset_hover_frame_.has_value() && preset_document_cache_.has_value()) {
    preview.preset_hover_document = preset_document_cache_;
    preview.preset_hover_transform = preset_frame_matrix(*preset_hover_frame_);
  }
  cached_preview_ = std::move(preview);
  cached_tool_revision_ = revision_;
  cached_scene_revision_ = scene_.revision();
  cached_modeling_revision_ = modeling_.content_revision();
  cached_asset_revision_ = assets_.revision();
}

bool CurveToolService::editable_source(EntityId entity) const {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return false;
  }
  const CurveComponent *curve = scene_.curve(entity);
  const VisibilityComponent *visibility = scene_.visibility(entity);
  return curve != nullptr && curve_component_is_valid(*curve) &&
         assets_.curve(curve->curve) != nullptr &&
         scene_.modeling_object(entity) != nullptr &&
         scene_.is_effectively_visible(entity) &&
         !scene_.is_effectively_locked(entity) &&
         !scene_.is_instance_definition_member(entity) &&
         !scene_.is_instance_definition_entity(entity) &&
         (visibility == nullptr || visibility->selectable) &&
         try_scene_matrix_inverse(scene_.world_transform_matrix(entity))
             .has_value();
}

const CurveComponent *
CurveToolService::editable_existing_curve_component() const {
  if (drawing_ || entity_ == kInvalidEntityId) {
    return nullptr;
  }
  const EntityUuidComponent *identity = scene_.entity_uuid(entity_);
  if (identity == nullptr || identity->uuid != source_uuid_) {
    return nullptr;
  }
  const CurveComponent *curve = scene_.curve(entity_);
  const ModelingObjectComponent *modeling_object =
      scene_.modeling_object(entity_);
  if (curve == nullptr || !curve_component_is_valid(*curve) ||
      assets_.curve(curve->curve) == nullptr || modeling_object == nullptr ||
      modeling_object->object_id != modeling_object_id_ ||
      modeling_.modeling_object_for_entity(entity_).value_or(-1) !=
          modeling_object_id_) {
    return nullptr;
  }
  return curve;
}

CommandResult CurveToolService::commit_existing_curve_component(
    CurveComponent after, UndoRedoExecuteOptions options) {
  const CommandResult result = document_commands_.set_curve_component(
      entity_, std::move(after), std::move(options));
  if (result.ok && result.changed) {
    observe_owner_revisions();
    invalidate(true);
  }
  return result;
}

std::optional<EntityId> CurveToolService::selected_curve() const {
  if (modeling_.selection_mode() != ModelingSelectionMode::Object) {
    return std::nullopt;
  }
  const auto selected = scene_.selected_entities();
  if (selected.size() != 1u || !editable_source(selected.front())) {
    return std::nullopt;
  }
  return selected.front();
}

CurveControlSelection CurveToolService::valid_control_selection(
    const CurveControlSelection &selection,
    const modeling::CurveDocument &document) const {
  if (selection.empty()) {
    return {};
  }
  CurveControlSelection valid;
  valid.controls.reserve(selection.controls.size());
  std::unordered_map<std::uint64_t, std::size_t> point_indices;
  point_indices.reserve(document.point_count());
  const auto points = document.points();
  for (std::size_t index = 0; index < points.size(); ++index) {
    point_indices.emplace(curve_point_key(points[index].id), index);
  }
  for (const CurveControlTarget target : selection.controls) {
    const auto point_index = point_indices.find(curve_point_key(target.point));
    if (point_index != point_indices.end() &&
        points[point_index->second].id == target.point &&
        curve_control_is_visible(document, drawing_, point_index->second,
                                 target.kind)) {
      valid.controls.push_back(target);
    }
  }
  std::sort(valid.controls.begin(), valid.controls.end());
  valid.controls.erase(
      std::unique(valid.controls.begin(), valid.controls.end()),
      valid.controls.end());
  if (selection.active &&
      std::binary_search(valid.controls.begin(), valid.controls.end(),
                         *selection.active)) {
    valid.active = selection.active;
  } else if (!valid.controls.empty()) {
    valid.active = valid.controls.back();
  }
  return valid;
}

bool CurveToolService::set_control_selection(CurveControlSelection selection) {
  std::sort(selection.controls.begin(), selection.controls.end());
  selection.controls.erase(
      std::unique(selection.controls.begin(), selection.controls.end()),
      selection.controls.end());
  if (selection.active &&
      !std::binary_search(selection.controls.begin(), selection.controls.end(),
                          *selection.active)) {
    selection.active.reset();
  }
  if (!selection.active && !selection.controls.empty()) {
    selection.active = selection.controls.back();
  }
  if (selection == control_selection_) {
    return false;
  }
  control_selection_ = std::move(selection);
  ++control_selection_revision_;
  cached_controls_world_pivot_.reset();
  return true;
}

void CurveToolService::attach_source(EntityId entity) {
  drawing_ = false;
  clear_preset_placement_state();
  entity_ = entity;
  source_uuid_ = scene_.uuid(entity);
  modeling_object_id_ =
      modeling_.modeling_object_for_entity(entity).value_or(-1);
  draft_ = {};
  (void)set_control_selection({});
  hover_world_.reset();
  if (const auto document = modeling_.curve_document(modeling_object_id_)) {
    preferred_closed_ = document->closed();
  }
}

void CurveToolService::detach_source() {
  drawing_ = true;
  clear_preset_placement_state();
  restore_session_defaults();
  entity_ = kInvalidEntityId;
  modeling_object_id_ = -1;
  source_uuid_.clear();
  draft_ = {};
  (void)set_control_selection({});
  hover_world_.reset();
}

void CurveToolService::invalidate(bool settings_changed) {
  ++revision_;
  if (settings_changed) {
    ++settings_revision_;
  }
}

void CurveToolService::observe_owner_revisions() {
  observed_scene_revision_ = scene_.revision();
  observed_modeling_revision_ = modeling_.content_revision();
  observed_asset_revision_ = assets_.revision();
  observed_selection_revision_ = scene_.selection_revision();
  observed_modeling_selection_revision_ = modeling_.selection_revision();
}

CommandResult CurveToolService::activate() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (active_) {
    return command_unchanged_result();
  }
  active_ = true;
  drawing_ = true;
  draft_ = {};
  clear_preset_placement_state();
  restore_session_defaults();
  if (const auto entity = selected_curve()) {
    attach_source(*entity);
  }
  observe_owner_revisions();
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::refresh() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_unchanged_result();
  }
  const bool lost_preview =
      drag_ && !drawing_ && !modeling_.curve_document_preview_active();
  if (!lost_preview && observed_scene_revision_ == scene_.revision() &&
      observed_modeling_revision_ == modeling_.content_revision() &&
      observed_asset_revision_ == assets_.revision() &&
      observed_selection_revision_ == scene_.selection_revision() &&
      observed_modeling_selection_revision_ == modeling_.selection_revision()) {
    return command_unchanged_result();
  }
  const bool selection_changed =
      observed_selection_revision_ != scene_.selection_revision() ||
      observed_modeling_selection_revision_ != modeling_.selection_revision();
  if (!drawing_) {
    const bool source_valid =
        editable_source(entity_) && scene_.uuid(entity_) == source_uuid_ &&
        scene_.modeling_object(entity_)->object_id == modeling_object_id_ &&
        modeling_.curve_document(modeling_object_id_, false).has_value();
    const std::vector<EntityId> selected = scene_.selected_entities();
    const bool source_still_selected =
        modeling_.selection_mode() == ModelingSelectionMode::Object &&
        std::find(selected.begin(), selected.end(), entity_) != selected.end();
    // Our own publications observe all stamps immediately. Reaching this path
    // during a drag therefore means another owner changed the scene, assets, or
    // portable history. Cancel before an old control snapshot can overwrite it.
    if (drag_) {
      const CommandResult canceled = cancel_drag();
      if (!canceled.ok) {
        return canceled;
      }
    }
    if (!source_valid || (selection_changed && !source_still_selected)) {
      detach_source();
      if (const auto entity = selected_curve()) {
        attach_source(*entity);
      }
    } else if (!control_selection_.empty()) {
      // A same-entity document replacement can retire generational point IDs
      // without changing scene selection or source identity.
      const auto current = modeling_.curve_document(modeling_object_id_, false);
      if (!current) {
        (void)set_control_selection({});
      } else {
        (void)set_control_selection(
            valid_control_selection(control_selection_, *current));
      }
    }
  } else if ((selection_changed || !active_) && draft_.point_count() == 0u &&
             !drag_) {
    if (const auto entity = selected_curve()) {
      attach_source(*entity);
    }
  }
  observe_owner_revisions();
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::cancel() {
  if (!active_) {
    return command_unchanged_result();
  }
  const CommandResult canceled = cancel_drag();
  if (!canceled.ok) {
    return canceled;
  }
  active_ = false;
  detach_source();
  if (const auto entity = selected_curve()) {
    attach_source(*entity);
  }
  observe_owner_revisions();
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::apply() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!can_apply()) {
    return command_invalid_result(
        "Place at least two points and finish the current drag.");
  }
  auto command = std::make_unique<CreateCurveHistoryCommand>(modeling_, draft_);
  const CreateCurveHistoryCommand *created = command.get();
  const CommandResult result = history_.execute(std::move(command));
  if (!result.ok || !result.changed) {
    return result;
  }
  attach_source(created->created().object.entity);
  observe_owner_revisions();
  invalidate(true);
  return result;
}

CommandResult CurveToolService::begin_new_curve() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!can_begin_new_curve()) {
    return command_invalid_result("Finish or cancel the current curve first.");
  }

  const bool changed = !active_ || !drawing_ || entity_ != kInvalidEntityId ||
                       modeling_object_id_ != -1 || !source_uuid_.empty();
  if (!changed) {
    // Treat the current selection stamp as observed so refresh() does not
    // attach an intentionally bypassed selected Curve on the next frame.
    observe_owner_revisions();
    return command_unchanged_result();
  }
  active_ = true;
  detach_source();
  observe_owner_revisions();
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::new_curve() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!active_) {
    return command_invalid_result("The Curve tool is not active.");
  }
  if (dragging() || (drawing_ && draft_.point_count() != 0u)) {
    return command_invalid_result("Finish or cancel the current curve first.");
  }
  if (drawing_) {
    return command_unchanged_result();
  }
  detach_source();
  observe_owner_revisions();
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::set_closed(bool closed_value) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (dragging()) {
    return command_invalid_result(
        "Finish the active curve drag before changing closure.");
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return refreshed;
  }
  if (!active_ && !has_editable_existing_curve()) {
    return command_invalid_result("Select one editable Curve first.");
  }
  if (drawing_ && creation_kind_ != CurveCreationKind::Draw) {
    return command_invalid_result("Circle and Oval presets are always closed.");
  }
  if (closed() == closed_value) {
    return command_unchanged_result();
  }
  if (drawing_) {
    if (draft_.point_count() >= 2u) {
      const auto changed = draft_.set_closed(closed_value);
      if (!changed.ok()) {
        return command_invalid_result(changed.error_message());
      }
      (void)set_control_selection(
          valid_control_selection(control_selection_, draft_));
    }
    preferred_closed_ = closed_value;
    session_defaults_->creation_kind = creation_kind_;
    session_defaults_->closed = closed_value;
    invalidate(true);
    return command_changed_result();
  }
  auto document = modeling_.curve_document(modeling_object_id_, false);
  if (!document) {
    return command_invalid_result("The selected curve is no longer available.");
  }
  const auto changed = document->set_closed(closed_value);
  if (!changed.ok()) {
    return command_invalid_result(changed.error_message());
  }
  CurveControlSelection next_selection =
      valid_control_selection(control_selection_, *document);
  const CommandResult result =
      history_.execute(std::make_unique<ReplaceCurveHistoryCommand>(
          modeling_, modeling_object_id_, std::move(*document)));
  if (result.ok && result.changed) {
    preferred_closed_ = closed_value;
    session_defaults_->closed = closed_value;
    (void)set_control_selection(std::move(next_selection));
    observe_owner_revisions();
    invalidate(true);
  }
  return result;
}

CommandResult CurveToolService::set_solidify(bool enabled) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return refreshed;
  }
  const CurveComponent *current = editable_existing_curve_component();
  if (current == nullptr) {
    return command_invalid_result("Select one editable existing Curve first.");
  }
  if (current->solidify == enabled) {
    return command_unchanged_result();
  }

  CurveComponent updated = *current;
  updated.solidify = enabled;
  return commit_existing_curve_component(std::move(updated));
}

CommandResult
CurveToolService::set_solidify_resolution(
    std::uint32_t resolution, UndoRedoExecuteOptions options) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (resolution <
          static_cast<std::uint32_t>(kCurveSolidifyMinimumResolution) ||
      resolution >
          static_cast<std::uint32_t>(kCurveSolidifyMaximumResolution)) {
    return command_invalid_result("Invalid Curve solidify resolution.");
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return refreshed;
  }
  const CurveComponent *current = editable_existing_curve_component();
  if (current == nullptr) {
    return command_invalid_result("Select one editable existing Curve first.");
  }
  if (current->solidify_resolution == static_cast<int>(resolution)) {
    return command_unchanged_result();
  }

  CurveComponent updated = *current;
  updated.solidify_resolution = static_cast<int>(resolution);
  return commit_existing_curve_component(std::move(updated),
                                         std::move(options));
}

CommandResult CurveToolService::set_solidify_radius(
    double radius, UndoRedoExecuteOptions options) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!std::isfinite(radius) || radius < kCurveSolidifyMinimumRadius ||
      radius > kCurveSolidifyMaximumRadius) {
    return command_invalid_result("Invalid Curve solidify radius.");
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return refreshed;
  }
  const CurveComponent *current = editable_existing_curve_component();
  if (current == nullptr) {
    return command_invalid_result("Select one editable existing Curve first.");
  }
  const float native_radius = static_cast<float>(radius);
  if (current->solidify_radius == native_radius) {
    return command_unchanged_result();
  }

  CurveComponent updated = *current;
  updated.solidify_radius = native_radius;
  return commit_existing_curve_component(std::move(updated),
                                         std::move(options));
}

CommandResult CurveToolService::apply_solidify() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return refreshed;
  }
  if (dragging()) {
    return command_invalid_result(
        "Finish the active Curve drag before applying Solidify.");
  }
  const CurveComponent *current = editable_existing_curve_component();
  if (current == nullptr) {
    return command_invalid_result("Select one editable existing Curve first.");
  }
  if (!current->solidify) {
    return command_invalid_result("Enable Curve Solidify before applying it.");
  }

  auto bake = std::make_unique<ApplyCurveSolidifyHistoryCommand>(
      modeling_, modeling_object_id_);
  CommandResult result = history_.execute(
      std::make_unique<ModelingSelectionMementoCommand>(
          modeling_, &selection_, std::move(bake)));
  if (!result.ok || !result.changed) {
    return result;
  }

  active_ = false;
  detach_source();
  observe_owner_revisions();
  invalidate(true);
  return result;
}

CommandResult CurveToolService::set_creation_kind(CurveCreationKind kind) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!can_change_creation_kind()) {
    return command_invalid_result("Curve type can only change before drawing "
                                  "or while a preset remains procedural.");
  }
  if (kind != CurveCreationKind::Draw && kind != CurveCreationKind::Circle &&
      kind != CurveCreationKind::Oval) {
    return command_invalid_result("Unknown Curve creation type.");
  }
  if (creation_kind_ == kind) {
    return command_unchanged_result();
  }

  if (kind == CurveCreationKind::Draw) {
    creation_kind_ = kind;
    session_defaults_->creation_kind = kind;
    session_defaults_->closed = preferred_closed_;
    procedural_draft_ = false;
    procedural_frame_.reset();
    preset_hover_frame_.reset();
    hover_world_.reset();
    invalidate(true);
    return command_changed_result();
  }

  std::string message;
  std::optional<modeling::CurveDocument> canonical = make_preset_document(
      kind, circle_radius_, oval_radius_, oval_length_, message);
  if (!canonical.has_value()) {
    return command_invalid_result(std::move(message));
  }

  std::optional<modeling::CurveDocument> placed;
  if (procedural_draft_) {
    if (!procedural_frame_.has_value()) {
      return command_invalid_result(
          "The procedural Curve placement is unavailable.");
    }
    placed = transform_preset_document(*canonical, *procedural_frame_, message);
    if (!placed.has_value()) {
      return command_invalid_result(std::move(message));
    }
  }

  const std::size_t old_point_count = draft_.point_count();
  creation_kind_ = kind;
  session_defaults_->creation_kind = kind;
  if (kind == CurveCreationKind::Circle) {
    session_defaults_->circle_radius = circle_radius_;
  } else {
    session_defaults_->oval_radius = oval_radius_;
    session_defaults_->oval_length = oval_length_;
  }
  preset_document_cache_ = std::move(*canonical);
  hover_world_.reset();
  if (placed.has_value()) {
    draft_ = std::move(*placed);
    preferred_closed_ = true;
    if (old_point_count != draft_.point_count()) {
      (void)set_control_selection({});
    } else {
      (void)set_control_selection(
          valid_control_selection(control_selection_, draft_));
    }
  }
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::set_preset_radius(double radius) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!active_ || !drawing_ || dragging() ||
      creation_kind_ == CurveCreationKind::Draw) {
    return command_invalid_result(
        "Curve preset radius is unavailable in the current state.");
  }
  if (!std::isfinite(radius) || radius < kCurvePresetMinimumRadius ||
      radius > kCurvePresetMaximumRadius) {
    return command_invalid_result("Invalid Curve preset radius.");
  }
  if (creation_kind_ == CurveCreationKind::Oval &&
      radius * 2.0 > oval_length_) {
    return command_invalid_result(
        "Oval radius cannot exceed half of its total length.");
  }
  const double current = preset_radius();
  if (current == radius) {
    return command_unchanged_result();
  }

  const double next_circle_radius =
      creation_kind_ == CurveCreationKind::Circle ? radius : circle_radius_;
  const double next_oval_radius =
      creation_kind_ == CurveCreationKind::Oval ? radius : oval_radius_;
  std::string message;
  std::optional<modeling::CurveDocument> canonical =
      make_preset_document(creation_kind_, next_circle_radius, next_oval_radius,
                           oval_length_, message);
  if (!canonical.has_value()) {
    return command_invalid_result(std::move(message));
  }
  std::optional<modeling::CurveDocument> placed;
  if (procedural_draft_) {
    if (!procedural_frame_.has_value()) {
      return command_invalid_result(
          "The procedural Curve placement is unavailable.");
    }
    placed = transform_preset_document(*canonical, *procedural_frame_, message);
    if (!placed.has_value()) {
      return command_invalid_result(std::move(message));
    }
  }

  const std::size_t old_point_count = draft_.point_count();
  if (creation_kind_ == CurveCreationKind::Circle) {
    circle_radius_ = radius;
    session_defaults_->circle_radius = radius;
  } else {
    oval_radius_ = radius;
    session_defaults_->oval_radius = radius;
    session_defaults_->oval_length = oval_length_;
  }
  session_defaults_->creation_kind = creation_kind_;
  preset_document_cache_ = std::move(*canonical);
  if (placed.has_value()) {
    draft_ = std::move(*placed);
    if (old_point_count != draft_.point_count()) {
      (void)set_control_selection({});
    } else {
      (void)set_control_selection(
          valid_control_selection(control_selection_, draft_));
    }
  }
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::set_oval_length(double length_value) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!active_ || !drawing_ || dragging() ||
      creation_kind_ != CurveCreationKind::Oval) {
    return command_invalid_result(
        "Oval length is unavailable in the current state.");
  }
  if (!std::isfinite(length_value) || length_value < oval_radius_ * 2.0 ||
      length_value > kCurvePresetMaximumLength) {
    return command_invalid_result("Invalid Oval total length.");
  }
  if (oval_length_ == length_value) {
    return command_unchanged_result();
  }

  std::string message;
  std::optional<modeling::CurveDocument> canonical = make_preset_document(
      creation_kind_, circle_radius_, oval_radius_, length_value, message);
  if (!canonical.has_value()) {
    return command_invalid_result(std::move(message));
  }
  std::optional<modeling::CurveDocument> placed;
  if (procedural_draft_) {
    if (!procedural_frame_.has_value()) {
      return command_invalid_result(
          "The procedural Curve placement is unavailable.");
    }
    placed = transform_preset_document(*canonical, *procedural_frame_, message);
    if (!placed.has_value()) {
      return command_invalid_result(std::move(message));
    }
  }

  const std::size_t old_point_count = draft_.point_count();
  oval_length_ = length_value;
  session_defaults_->creation_kind = creation_kind_;
  session_defaults_->oval_radius = oval_radius_;
  session_defaults_->oval_length = length_value;
  preset_document_cache_ = std::move(*canonical);
  if (placed.has_value()) {
    draft_ = std::move(*placed);
    if (old_point_count != draft_.point_count()) {
      (void)set_control_selection({});
    } else {
      (void)set_control_selection(
          valid_control_selection(control_selection_, draft_));
    }
  }
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::reset_settings() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!can_reset_settings()) {
    return command_invalid_result(
        "Curve settings cannot reset in the current state.");
  }
  if (creation_kind_ == CurveCreationKind::Draw) {
    return set_closed(false);
  }
  if (creation_kind_ == CurveCreationKind::Circle) {
    return set_preset_radius(kCurvePresetDefaultRadius);
  }

  if (oval_radius_ == kCurvePresetDefaultRadius &&
      oval_length_ == kCurveOvalDefaultLength) {
    return command_unchanged_result();
  }
  std::string message;
  std::optional<modeling::CurveDocument> canonical = make_preset_document(
      CurveCreationKind::Oval, circle_radius_, kCurvePresetDefaultRadius,
      kCurveOvalDefaultLength, message);
  if (!canonical.has_value()) {
    return command_invalid_result(std::move(message));
  }
  std::optional<modeling::CurveDocument> placed;
  if (procedural_draft_) {
    if (!procedural_frame_.has_value()) {
      return command_invalid_result(
          "The procedural Curve placement is unavailable.");
    }
    placed = transform_preset_document(*canonical, *procedural_frame_, message);
    if (!placed.has_value()) {
      return command_invalid_result(std::move(message));
    }
  }

  const std::size_t old_point_count = draft_.point_count();
  oval_radius_ = kCurvePresetDefaultRadius;
  oval_length_ = kCurveOvalDefaultLength;
  session_defaults_->creation_kind = creation_kind_;
  session_defaults_->oval_radius = oval_radius_;
  session_defaults_->oval_length = oval_length_;
  preset_document_cache_ = std::move(*canonical);
  if (placed.has_value()) {
    draft_ = std::move(*placed);
    if (old_point_count != draft_.point_count()) {
      (void)set_control_selection({});
    } else {
      (void)set_control_selection(
          valid_control_selection(control_selection_, draft_));
    }
  }
  invalidate(true);
  return command_changed_result();
}

CommandResult
CurveToolService::toggle_point_smoothness(modeling::CurvePointId point) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!controls_editable() || dragging()) {
    return command_invalid_result(
        "Finish the active curve drag before converting a point.");
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return refreshed;
  }
  auto candidate = drawing_
                       ? std::optional(draft_)
                       : modeling_.curve_document(modeling_object_id_, false);
  if (!candidate) {
    return command_invalid_result("The selected curve is no longer available.");
  }
  const auto edited = candidate->toggle_point_smoothness(point);
  if (!edited.ok()) {
    return command_invalid_result(edited.error_message());
  }
  if (!edited.value().changed) {
    return command_unchanged_result();
  }
  const CurveControlTarget anchor{.point = point,
                                  .kind = CurveControlKind::Anchor};
  CurveControlSelection current =
      valid_control_selection(control_selection_, *candidate);
  auto selection_edit = quader_editor_core::resolve_selection_edit_operation(
      std::move(current.controls), std::vector<CurveControlTarget>{anchor},
      current.active, quader_editor_core::SelectionEditOperation::Add,
      std::optional<CurveControlTarget>(anchor));
  CurveControlSelection next_selection{
      .controls = std::move(selection_edit.selected),
      .active = selection_edit.active,
  };
  const bool converted_procedural = drawing_ && procedural_draft_;
  if (drawing_) {
    draft_ = std::move(*candidate);
    (void)convert_procedural_draft_to_draw();
  } else {
    const CommandResult result =
        history_.execute(std::make_unique<ReplaceCurveHistoryCommand>(
            modeling_, modeling_object_id_, std::move(*candidate)));
    if (!result.ok || !result.changed) {
      return result;
    }
    observe_owner_revisions();
  }
  (void)set_control_selection(std::move(next_selection));
  invalidate(converted_procedural);
  return command_changed_result();
}

CommandResult CurveToolService::set_hover(std::optional<QVec3> position) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (position && !finite_point(*position)) {
    return command_invalid_result("Curve hover position must be finite.");
  }
  if (!active_ || !drawing_ || dragging() ||
      creation_kind_ != CurveCreationKind::Draw) {
    position.reset();
  }
  if (position.has_value() == hover_world_.has_value() &&
      (!position || same_point(*position, *hover_world_))) {
    return command_unchanged_result();
  }
  hover_world_ = position;
  invalidate();
  return command_changed_result();
}

CommandResult CurveToolService::set_preset_hover(
    std::optional<CurvePresetPlacementFrame> frame) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  std::optional<CurvePresetPlacementFrame> normalized;
  if (frame.has_value()) {
    normalized = normalized_preset_frame(*frame);
    if (!normalized.has_value()) {
      return command_invalid_result(
          "Curve preset placement frame must be finite and nondegenerate.");
    }
  }

  const bool accepts_hover = active_ && drawing_ && !dragging() &&
                             draft_.point_count() == 0u &&
                             creation_kind_ != CurveCreationKind::Draw;
  if (!accepts_hover) {
    normalized.reset();
  }
  if (normalized.has_value() && !preset_document_cache_.has_value()) {
    std::string message;
    std::optional<modeling::CurveDocument> canonical =
        make_current_preset_document(message);
    if (!canonical.has_value()) {
      return command_invalid_result(std::move(message));
    }
    preset_document_cache_ = std::move(*canonical);
  }
  if (normalized.has_value() == preset_hover_frame_.has_value() &&
      (!normalized || same_frame(*normalized, *preset_hover_frame_))) {
    return command_unchanged_result();
  }
  preset_hover_frame_ = normalized;
  invalidate();
  return command_changed_result();
}

CommandResult CurveToolService::place_preset(CurvePresetPlacementFrame frame) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!active_ || !drawing_ || dragging() ||
      creation_kind_ == CurveCreationKind::Draw) {
    return command_invalid_result(
        "A Curve preset cannot be placed in the current state.");
  }
  if (draft_.point_count() != 0u) {
    return command_unchanged_result();
  }
  const std::optional<CurvePresetPlacementFrame> normalized =
      normalized_preset_frame(frame);
  if (!normalized.has_value()) {
    return command_invalid_result(
        "Curve preset placement frame must be finite and nondegenerate.");
  }
  std::string message;
  if (!preset_document_cache_.has_value()) {
    std::optional<modeling::CurveDocument> canonical =
        make_current_preset_document(message);
    if (!canonical.has_value()) {
      return command_invalid_result(std::move(message));
    }
    preset_document_cache_ = std::move(*canonical);
  }
  std::optional<modeling::CurveDocument> placed =
      place_current_preset(*normalized, message);
  if (!placed.has_value()) {
    return command_invalid_result(std::move(message));
  }

  draft_ = std::move(*placed);
  preferred_closed_ = true;
  procedural_draft_ = true;
  procedural_frame_ = normalized;
  preset_hover_frame_.reset();
  hover_world_.reset();
  (void)set_control_selection({});
  invalidate(true);
  return command_changed_result();
}

bool CurveToolService::edit_control_selection(
    std::span<const CurveControlTarget> hits,
    quader_editor_core::SelectionEditOperation operation,
    const CurveControlSelection *baseline,
    std::optional<CurveControlTarget> direct_hit) {
  const ModelingCommandResult result =
      edit_control_selection_checked(hits, operation, baseline, direct_hit);
  return result.ok && result.changed;
}

ModelingCommandResult CurveToolService::edit_control_selection_checked(
    std::span<const CurveControlTarget> hits,
    quader_editor_core::SelectionEditOperation operation,
    const CurveControlSelection *baseline,
    std::optional<CurveControlTarget> direct_hit) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return {.ok = false,
            .changed = false,
            .message = release_feature_unavailable_message()};
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return modeling_result(refreshed);
  }
  if (dragging()) {
    return {.ok = false,
            .changed = refreshed.changed,
            .message = "Finish the active Curve drag before editing controls."};
  }
  if (!controls_editable()) {
    return {.ok = false,
            .changed = refreshed.changed,
            .message = "Curve controls are not editable."};
  }
  const auto &preview = preview_state();
  if (!preview.document) {
    return {.ok = false,
            .changed = refreshed.changed,
            .message = "The Curve control owner is unavailable."};
  }

  const CurveControlSelection valid_baseline = valid_control_selection(
      baseline != nullptr ? *baseline : control_selection_, *preview.document);
  CurveControlSelection hit_selection;
  hit_selection.controls.assign(hits.begin(), hits.end());
  hit_selection = valid_control_selection(hit_selection, *preview.document);
  if (direct_hit && !hit_selection.contains(*direct_hit)) {
    direct_hit.reset();
  }
  auto edited = quader_editor_core::resolve_selection_edit_operation(
      valid_baseline.controls, std::move(hit_selection.controls),
      valid_baseline.active, operation, direct_hit);
  CurveControlSelection next{
      .controls = std::move(edited.selected),
      .active = edited.active,
  };
  const bool selection_changed = set_control_selection(std::move(next));
  const bool hover_changed = hover_world_.has_value();
  hover_world_.reset();
  if (selection_changed || hover_changed) {
    invalidate();
  }
  return {.ok = true,
          .changed = refreshed.changed || selection_changed || hover_changed};
}

EditorSubSelectionSnapshot CurveToolService::sub_selection_snapshot() const {
  EntityId owner = kInvalidEntityId;
  int owner_object_id = kInvalidModelingObjectId;
  if (active_ && drawing_) {
    // An uncommitted Curve draft has no scene identity, but its controls still
    // participate in the same editor selection commands.
  } else {
    // Keep summary polling allocation-free. refresh() owns the expensive
    // selection/source validation and pins these stamps; a changed owner stamp
    // makes this snapshot empty until that synchronization runs.
    if (drawing_ || entity_ == kInvalidEntityId ||
        observed_scene_revision_ != scene_.revision() ||
        observed_modeling_revision_ != modeling_.content_revision() ||
        observed_asset_revision_ != assets_.revision() ||
        observed_selection_revision_ != scene_.selection_revision() ||
        observed_modeling_selection_revision_ !=
            modeling_.selection_revision() ||
        modeling_.selection_mode() != ModelingSelectionMode::Object) {
      return {};
    }
    const SelectionComponent *selection = scene_.selection(entity_);
    const ModelingObjectComponent *object = scene_.modeling_object(entity_);
    if (selection == nullptr || !selection->selected || object == nullptr ||
        object->object_id != modeling_object_id_) {
      return {};
    }
    owner = entity_;
    owner_object_id = modeling_object_id_;
  }

  EditorSubSelectionSnapshot snapshot{
      .kind = EditorSubSelectionKind::CurveControl,
      .participates_in_bulk_selection = !dragging(),
      .entity = owner,
      .modeling_object_id = owner_object_id,
      .selected_count =
          static_cast<std::uint32_t>(control_selection_.controls.size()),
      .revision = control_selection_revision_,
  };
  if (control_selection_.active.has_value()) {
    snapshot.active = EditorSubSelectionTarget{
        .index = control_selection_.active->point.index,
        .generation = control_selection_.active->point.generation,
        .role = sub_selection_role(control_selection_.active->kind),
    };
  }
  return snapshot;
}

EditorSubSelectionBaseline
CurveToolService::capture_sub_selection_baseline() const {
  const EditorSubSelectionSnapshot snapshot = sub_selection_snapshot();
  if (snapshot.kind == EditorSubSelectionKind::None) {
    return {};
  }
  const std::uint64_t presentation_revision = preview_state().revision;
  EditorSubSelectionBaseline baseline{
      .kind = snapshot.kind,
      .entity = snapshot.entity,
      .modeling_object_id = snapshot.modeling_object_id,
      .active = snapshot.active,
      .hover = sub_element_hover_,
      .revision = snapshot.revision,
      .owner_revision = revision_,
      .presentation_revision = presentation_revision,
  };
  baseline.selected.reserve(control_selection_.controls.size());
  for (const CurveControlTarget control : control_selection_.controls) {
    baseline.selected.push_back(sub_selection_target(control));
  }
  return baseline;
}

void CurveToolService::synchronize_sub_selection_scope() {
  (void)refresh();
  if (sub_selection_snapshot().kind == EditorSubSelectionKind::None) {
    sub_element_hover_.reset();
  }
}

ModelingCommandResult CurveToolService::edit_sub_elements(
    std::span<const EditorSubSelectionTarget> hits, EditorSubSelectionEdit edit,
    const EditorSubSelectionBaseline *baseline,
    std::optional<EditorSubSelectionTarget> direct_hit) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return {.ok = false,
            .changed = false,
            .message = release_feature_unavailable_message()};
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return modeling_result(refreshed);
  }
  if (dragging()) {
    return {.ok = false,
            .changed = refreshed.changed,
            .message = "Finish the active Curve drag before editing controls."};
  }
  if (!controls_editable()) {
    return {.ok = false,
            .changed = refreshed.changed,
            .message = "Curve controls are not editable."};
  }
  const EditorSubSelectionSnapshot snapshot = sub_selection_snapshot();
  if (snapshot.kind != EditorSubSelectionKind::CurveControl) {
    return {.ok = false,
            .changed = refreshed.changed,
            .message = "The Curve control owner is stale."};
  }
  const auto &preview = preview_state();
  if (!preview.document) {
    return {.ok = false,
            .changed = refreshed.changed,
            .message = "The Curve control owner is unavailable."};
  }

  std::vector<CurveControlTarget> curve_hits;
  curve_hits.reserve(hits.size());
  for (const EditorSubSelectionTarget target : hits) {
    curve_hits.push_back(curve_control_target(target));
  }
  std::vector<CurveControlTarget> canonical_hits = curve_hits;
  std::sort(canonical_hits.begin(), canonical_hits.end());
  canonical_hits.erase(
      std::unique(canonical_hits.begin(), canonical_hits.end()),
      canonical_hits.end());
  CurveControlSelection requested_hits{.controls = canonical_hits};
  if (direct_hit.has_value()) {
    requested_hits.active = curve_control_target(*direct_hit);
  }
  const CurveControlSelection valid_hits =
      valid_control_selection(requested_hits, *preview.document);
  if (valid_hits.controls != canonical_hits ||
      (requested_hits.active.has_value() &&
       valid_hits.active != requested_hits.active)) {
    return {.ok = false,
            .changed = refreshed.changed,
            .message = "Curve control selection target is stale."};
  }

  CurveControlSelection curve_baseline;
  const CurveControlSelection *curve_baseline_pointer = nullptr;
  if (baseline != nullptr) {
    if (baseline->kind != snapshot.kind ||
        baseline->entity != snapshot.entity ||
        baseline->modeling_object_id != snapshot.modeling_object_id) {
      return {.ok = false,
              .changed = refreshed.changed,
              .message = "Curve control selection baseline owner is stale."};
    }
    curve_baseline.controls.reserve(baseline->selected.size());
    for (const EditorSubSelectionTarget target : baseline->selected) {
      curve_baseline.controls.push_back(curve_control_target(target));
    }
    if (baseline->active.has_value()) {
      curve_baseline.active = curve_control_target(*baseline->active);
    }
    std::sort(curve_baseline.controls.begin(), curve_baseline.controls.end());
    curve_baseline.controls.erase(std::unique(curve_baseline.controls.begin(),
                                              curve_baseline.controls.end()),
                                  curve_baseline.controls.end());
    const CurveControlSelection valid_baseline =
        valid_control_selection(curve_baseline, *preview.document);
    if (valid_baseline != curve_baseline) {
      return {.ok = false,
              .changed = refreshed.changed,
              .message = "Curve control selection baseline is stale."};
    }
    curve_baseline_pointer = &curve_baseline;
  }

  ModelingCommandResult result = edit_control_selection_checked(
      curve_hits, selection_edit_operation(edit), curve_baseline_pointer,
      direct_hit.has_value()
          ? std::optional<CurveControlTarget>(curve_control_target(*direct_hit))
          : std::nullopt);
  if (!result.ok) {
    result.changed = result.changed || refreshed.changed;
    return result;
  }
  const bool hover_changed = sub_element_hover_.has_value();
  sub_element_hover_.reset();
  result.changed = result.changed || hover_changed || refreshed.changed;
  return result;
}

ModelingCommandResult CurveToolService::restore_sub_selection_baseline(
    const EditorSubSelectionBaseline &baseline) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return {.ok = false,
            .changed = false,
            .message = release_feature_unavailable_message()};
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return {.ok = false,
            .changed = true,
            .message =
                refreshed.message.empty()
                    ? "Curve selection rollback could not refresh its owner."
                    : refreshed.message};
  }
  const EditorSubSelectionSnapshot snapshot = sub_selection_snapshot();
  if (baseline.kind != EditorSubSelectionKind::CurveControl ||
      snapshot.kind != baseline.kind || snapshot.entity != baseline.entity ||
      snapshot.modeling_object_id != baseline.modeling_object_id) {
    return {.ok = false,
            .changed = true,
            .message = "Curve selection rollback owner is no longer current."};
  }
  const std::optional<modeling::CurveDocument> document =
      drawing_ ? std::optional<modeling::CurveDocument>{draft_}
               : modeling_.curve_document(modeling_object_id_, false);
  if (!document.has_value()) {
    return {.ok = false,
            .changed = true,
            .message = "Curve selection rollback document is unavailable."};
  }

  CurveControlSelection restored;
  restored.controls.reserve(baseline.selected.size());
  for (const EditorSubSelectionTarget target : baseline.selected) {
    restored.controls.push_back(curve_control_target(target));
  }
  if (baseline.active.has_value()) {
    restored.active = curve_control_target(*baseline.active);
  }
  const CurveControlSelection validated =
      valid_control_selection(restored, *document);
  if (validated != restored) {
    return {.ok = false,
            .changed = true,
            .message = "Curve selection rollback contains stale controls."};
  }

  if (baseline.hover.has_value()) {
    CurveControlSelection hovered{
        .controls = {curve_control_target(*baseline.hover)},
        .active = curve_control_target(*baseline.hover),
    };
    if (valid_control_selection(hovered, *document) != hovered) {
      return {.ok = false,
              .changed = true,
              .message = "Curve selection rollback hover is stale."};
    }
  }

  const bool changed = control_selection_ != restored ||
                       control_selection_revision_ != baseline.revision ||
                       sub_element_hover_ != baseline.hover ||
                       revision_ != baseline.owner_revision ||
                       preview_revision_ != baseline.presentation_revision;
  control_selection_ = std::move(restored);
  control_selection_revision_ = baseline.revision;
  sub_element_hover_ = baseline.hover;
  revision_ = baseline.owner_revision;
  preview_revision_ = baseline.presentation_revision;
  cached_control_preview_revision_ = std::numeric_limits<std::uint64_t>::max();
  cached_control_selection_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  cached_controls_world_pivot_.reset();
  rebuild_preview_cache(baseline.presentation_revision);
  return {.ok = true, .changed = changed};
}

ModelingCommandResult CurveToolService::preview_sub_element_hover(
    std::optional<EditorSubSelectionTarget> target) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return {.ok = false,
            .changed = false,
            .message = release_feature_unavailable_message()};
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return modeling_result(refreshed);
  }
  if (target.has_value()) {
    if (dragging()) {
      return {.ok = false,
              .changed = refreshed.changed,
              .message =
                  "Finish the active Curve drag before previewing controls."};
    }
    if (!controls_editable() ||
        sub_selection_snapshot().kind != EditorSubSelectionKind::CurveControl) {
      return {.ok = false,
              .changed = refreshed.changed,
              .message = "The Curve control hover owner is stale."};
    }
    const auto &preview = preview_state();
    if (!preview.document) {
      return {.ok = false,
              .changed = refreshed.changed,
              .message = "The Curve control hover owner is unavailable."};
    }
    CurveControlSelection candidate{
        .controls = {curve_control_target(*target)},
        .active = curve_control_target(*target),
    };
    if (valid_control_selection(candidate, *preview.document) != candidate) {
      return {.ok = false,
              .changed = refreshed.changed,
              .message = "Curve control hover target is stale."};
    }
  }
  if (sub_element_hover_ == target) {
    return {.ok = true, .changed = refreshed.changed};
  }
  sub_element_hover_ = target;
  return {.ok = true, .changed = true};
}

ModelingCommandResult CurveToolService::select_all_sub_elements() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return {.ok = false,
            .changed = false,
            .message = release_feature_unavailable_message()};
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return modeling_result(refreshed);
  }
  const EditorSubSelectionSnapshot snapshot = sub_selection_snapshot();
  const auto &preview = preview_state();
  if (!snapshot.participates_in_bulk_selection || !preview.document) {
    return {.ok = false,
            .changed = false,
            .message = "Curve controls are not editable."};
  }
  std::vector<CurveControlTarget> controls =
      visible_curve_controls(*preview.document, preview.drawing);
  const std::optional<CurveControlTarget> active =
      controls.empty() ? std::nullopt
                       : std::optional<CurveControlTarget>(controls.back());
  const bool changed = set_control_selection(
      CurveControlSelection{.controls = std::move(controls), .active = active});
  if (changed) {
    invalidate();
  }
  return {.ok = true, .changed = changed};
}

ModelingCommandResult CurveToolService::invert_sub_elements() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return {.ok = false,
            .changed = false,
            .message = release_feature_unavailable_message()};
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return modeling_result(refreshed);
  }
  const auto &preview = preview_state();
  if (!sub_selection_snapshot().participates_in_bulk_selection ||
      !preview.document) {
    return {.ok = false,
            .changed = false,
            .message = "Curve controls are not editable."};
  }
  const std::vector<CurveControlTarget> controls =
      visible_curve_controls(*preview.document, preview.drawing);
  const bool changed = edit_control_selection(
      controls, quader_editor_core::SelectionEditOperation::Toggle);
  return {.ok = true, .changed = changed};
}

ModelingCommandResult CurveToolService::clear_sub_elements() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return {.ok = false,
            .changed = false,
            .message = release_feature_unavailable_message()};
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return modeling_result(refreshed);
  }
  if (sub_selection_snapshot().kind == EditorSubSelectionKind::None) {
    return {.ok = false,
            .changed = false,
            .message = "Curve controls are not editable."};
  }
  const bool changed = set_control_selection({});
  if (changed) {
    hover_world_.reset();
    invalidate();
  }
  return {.ok = true, .changed = changed};
}

ModelingCommandResult CurveToolService::delete_selected_sub_elements() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return {.ok = false,
            .changed = false,
            .message = release_feature_unavailable_message()};
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return modeling_result(refreshed);
  }
  if (dragging()) {
    return {.ok = false,
            .changed = false,
            .message =
                "Finish the active Curve drag before deleting controls."};
  }
  const auto &preview = preview_state();
  if (!preview.document) {
    return {.ok = false,
            .changed = false,
            .message = "The Curve control owner is unavailable."};
  }
  const CurveControlSelection selected =
      valid_control_selection(control_selection_, *preview.document);
  if (selected.empty()) {
    return {.ok = false,
            .changed = false,
            .message = "No Curve controls are selected."};
  }

  modeling::CurveDocument candidate = *preview.document;
  std::vector<modeling::CurvePointEdit> handle_edits;
  std::vector<modeling::CurvePointId> removed_anchors;
  handle_edits.reserve(selected.controls.size());
  removed_anchors.reserve(selected.controls.size());
  for (const modeling::CurveControlPoint &point : candidate.points()) {
    const CurveControlTarget anchor{.point = point.id,
                                    .kind = CurveControlKind::Anchor};
    if (selected.contains(anchor)) {
      removed_anchors.push_back(point.id);
      continue;
    }
    const bool incoming = selected.contains(
        {.point = point.id, .kind = CurveControlKind::IncomingHandle});
    const bool outgoing = selected.contains(
        {.point = point.id, .kind = CurveControlKind::OutgoingHandle});
    if (!incoming && !outgoing) {
      continue;
    }
    modeling::CurvePointSpec replacement{
        .position = point.position,
        .incoming_handle = incoming ? modeling::Vec3{} : point.incoming_handle,
        .outgoing_handle = outgoing ? modeling::Vec3{} : point.outgoing_handle,
    };
    handle_edits.push_back({.id = point.id, .point = replacement});
  }
  if (!handle_edits.empty()) {
    const auto edited = candidate.set_points(handle_edits);
    if (!edited.ok()) {
      return {.ok = false, .changed = false, .message = edited.error_message()};
    }
  }
  for (const modeling::CurvePointId point : removed_anchors) {
    const auto removed = candidate.remove_point(point);
    if (!removed.ok()) {
      return {
          .ok = false, .changed = false, .message = removed.error_message()};
    }
  }

  CurveControlSelection next_selection =
      valid_control_selection(selected, candidate);
  const bool next_closed = candidate.closed();
  if (drawing_) {
    draft_ = std::move(candidate);
    (void)convert_procedural_draft_to_draw();
  } else {
    const CommandResult result =
        history_.execute(std::make_unique<ReplaceCurveHistoryCommand>(
            modeling_, modeling_object_id_, std::move(candidate)));
    if (!result.ok || !result.changed) {
      return modeling_result(result);
    }
    preferred_closed_ = next_closed;
    observe_owner_revisions();
  }
  (void)set_control_selection(std::move(next_selection));
  hover_world_.reset();
  invalidate(true);
  return {.ok = true, .changed = true};
}

std::optional<QVec3> CurveToolService::selected_controls_world_pivot() const {
  if (!controls_editable() || control_selection_.empty()) {
    return std::nullopt;
  }
  const auto &preview = preview_state();
  if (cached_control_preview_revision_ == preview.revision &&
      cached_control_selection_revision_ == control_selection_revision_) {
    return cached_controls_world_pivot_;
  }
  cached_control_preview_revision_ = preview.revision;
  cached_control_selection_revision_ = control_selection_revision_;
  cached_controls_world_pivot_.reset();
  if (!preview.document) {
    return std::nullopt;
  }

  QVec3 minimum{};
  QVec3 maximum{};
  bool any = false;
  const auto points = preview.document->points();
  for (std::size_t point_index = 0; point_index < points.size();
       ++point_index) {
    const auto &point = points[point_index];
    for (const CurveControlKind kind :
         {CurveControlKind::Anchor, CurveControlKind::IncomingHandle,
          CurveControlKind::OutgoingHandle}) {
      const CurveControlTarget target{.point = point.id, .kind = kind};
      if (!control_selection_.contains(target) ||
          !curve_control_is_visible(preview, point_index, kind)) {
        continue;
      }
      const QVec3 world = transform_point(
          preview.world_transform, curve_control_local_position(point, kind));
      if (!any) {
        minimum = world;
        maximum = world;
        any = true;
      } else {
        minimum = {std::min(minimum.x, world.x), std::min(minimum.y, world.y),
                   std::min(minimum.z, world.z)};
        maximum = {std::max(maximum.x, world.x), std::max(maximum.y, world.y),
                   std::max(maximum.z, world.z)};
      }
    }
  }
  if (any) {
    cached_controls_world_pivot_ = (minimum + maximum) * 0.5f;
  }
  return cached_controls_world_pivot_;
}

CommandResult CurveToolService::begin_point(QVec3 world_position) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!active_ || !drawing_ || dragging() ||
      creation_kind_ != CurveCreationKind::Draw ||
      !finite_point(world_position)) {
    return command_invalid_result(
        "Cannot place a curve point in the current state.");
  }
  modeling::CurveDocument candidate = draft_;
  const auto appended =
      candidate.append_point({.position = portable_point(world_position)});
  if (!appended.ok()) {
    return command_invalid_result(appended.error_message());
  }
  if (candidate.point_count() >= 2u &&
      candidate.closed() != preferred_closed_) {
    const auto closure = candidate.set_closed(preferred_closed_);
    if (!closure.ok()) {
      return command_invalid_result(closure.error_message());
    }
  }
  const CurveControlTarget target{
      .point = candidate.points().back().id,
      .kind = CurveControlKind::OutgoingHandle,
  };
  drag_ = Drag{
      .before = draft_,
      .selection =
          CurveControlSelection{
              .controls = {target},
              .active = target,
          },
      .selection_before = control_selection_,
      .origin_world = world_position,
      .construction_point = target.point,
      .appended_point = true,
  };
  draft_ = std::move(candidate);
  (void)set_control_selection(drag_->selection);
  hover_world_.reset();
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::begin_selected_control_drag() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return refreshed;
  }
  if (!controls_editable() || dragging()) {
    return command_invalid_result("The Curve tool cannot start another drag.");
  }
  const auto &preview = preview_state();
  if (!preview.document) {
    return command_invalid_result("The curve is no longer available.");
  }
  const modeling::CurveDocument before = *preview.document;
  const auto inverse_world = try_scene_matrix_inverse(preview.world_transform);
  const CurveControlSelection valid =
      valid_control_selection(control_selection_, *preview.document);
  if (valid.empty() || !inverse_world) {
    return command_invalid_result("The Curve control selection is invalid.");
  }
  if (valid != control_selection_) {
    (void)set_control_selection(valid);
    invalidate();
  }
  const auto origin_world = selected_controls_world_pivot();
  if (!origin_world) {
    return command_invalid_result("The Curve control selection has no pivot.");
  }
  Drag drag{
      .before = before,
      .selection = control_selection_,
      .selection_before = control_selection_,
      .inverse_world_transform = *inverse_world,
      .origin_world = *origin_world,
  };
  if (!drawing_) {
    const auto begun =
        modeling_.begin_curve_document_preview(modeling_object_id_);
    if (!begun.ok) {
      return modeling_memento_command_result(begun);
    }
  }
  drag_ = std::move(drag);
  hover_world_.reset();
  observe_owner_revisions();
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::update_drag(QVec3 world_position) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!drag_ || !finite_point(world_position)) {
    return command_invalid_result("A finite curve drag position is required.");
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok || !drag_) {
    return refreshed;
  }
  modeling::CurveDocument candidate =
      drag_->appended_point ? draft_ : drag_->before;
  if (drag_->appended_point) {
    if (!drag_->construction_point) {
      return command_invalid_result("The construction point is unavailable.");
    }
    const auto *point = find_point(candidate, *drag_->construction_point);
    if (point == nullptr) {
      return command_invalid_result("The curve point is no longer available.");
    }
    modeling::CurvePointSpec replacement{
        .position = point->position,
        .incoming_handle = point->incoming_handle,
        .outgoing_handle = point->outgoing_handle,
    };
    const QVec3 local =
        transform_point(drag_->inverse_world_transform, world_position);
    const QVec3 offset = local - native_point(point->position);
    replacement.incoming_handle = portable_point(offset * -1.0f);
    replacement.outgoing_handle = portable_point(offset);
    const auto changed =
        candidate.set_point(*drag_->construction_point, replacement);
    if (!changed.ok()) {
      return command_invalid_result(changed.error_message());
    }
  } else {
    const QVec3 local_origin =
        transform_point(drag_->inverse_world_transform, drag_->origin_world);
    const QVec3 local_position =
        transform_point(drag_->inverse_world_transform, world_position);
    const QVec3 local_delta = local_position - local_origin;
    std::vector<modeling::CurvePointEdit> edits;
    edits.reserve(drag_->selection.controls.size());
    for (const modeling::CurveControlPoint &point : drag_->before.points()) {
      const CurveControlTarget anchor{.point = point.id,
                                      .kind = CurveControlKind::Anchor};
      const CurveControlTarget incoming{
          .point = point.id,
          .kind = CurveControlKind::IncomingHandle,
      };
      const CurveControlTarget outgoing{
          .point = point.id,
          .kind = CurveControlKind::OutgoingHandle,
      };
      const bool anchor_selected = drag_->selection.contains(anchor);
      const bool incoming_selected = drag_->selection.contains(incoming);
      const bool outgoing_selected = drag_->selection.contains(outgoing);
      if (!anchor_selected && !incoming_selected && !outgoing_selected) {
        continue;
      }
      modeling::CurvePointSpec replacement{
          .position = point.position,
          .incoming_handle = point.incoming_handle,
          .outgoing_handle = point.outgoing_handle,
      };
      if (anchor_selected) {
        replacement.position =
            portable_point(native_point(point.position) + local_delta);
      } else {
        if (incoming_selected) {
          replacement.incoming_handle =
              portable_point(native_point(point.incoming_handle) + local_delta);
        }
        if (outgoing_selected) {
          replacement.outgoing_handle =
              portable_point(native_point(point.outgoing_handle) + local_delta);
        }
      }
      edits.push_back({.id = point.id, .point = replacement});
    }
    const auto changed = candidate.set_points(edits);
    if (!changed.ok()) {
      return command_invalid_result(changed.error_message());
    }
  }
  if (drawing_) {
    if (draft_.has_same_geometry(candidate)) {
      return command_unchanged_result();
    }
    draft_ = std::move(candidate);
  } else {
    const auto updated =
        modeling_.update_curve_document_preview(std::move(candidate));
    if (!updated.ok || !updated.changed) {
      return modeling_memento_command_result(updated);
    }
  }
  drag_->geometry_changed = true;
  observe_owner_revisions();
  invalidate();
  return command_changed_result();
}

CommandResult CurveToolService::commit_drag() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!drag_) {
    return command_unchanged_result();
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok || !drag_) {
    return refreshed;
  }
  const bool appended_point = drag_->appended_point;
  const bool geometry_changed = drag_->geometry_changed;
  const std::optional<modeling::CurvePointId> dragged_point =
      drag_->construction_point;
  if (!drawing_) {
    const CommandResult result = history_.execute(
        std::make_unique<CommitCurvePreviewHistoryCommand>(modeling_));
    if (!result.ok) {
      return result;
    }
  }
  drag_.reset();
  if (appended_point && dragged_point) {
    const CurveControlTarget anchor{
        .point = *dragged_point,
        .kind = CurveControlKind::Anchor,
    };
    (void)set_control_selection(
        CurveControlSelection{.controls = {anchor}, .active = anchor});
  }
  if (!appended_point && geometry_changed) {
    (void)convert_procedural_draft_to_draw();
  }
  observe_owner_revisions();
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::cancel_drag() {
  if (!drag_) {
    return command_unchanged_result();
  }
  const bool appended_point = drag_->appended_point;
  const CurveControlSelection selection_before = drag_->selection_before;
  if (drawing_) {
    draft_ = drag_->before;
  } else if (modeling_.curve_document_preview_active()) {
    const auto canceled = modeling_.cancel_curve_document_preview();
    if (!canceled.ok) {
      return modeling_memento_command_result(canceled);
    }
  }
  drag_.reset();
  if (appended_point) {
    // Canceling construction removes its control and restores the press-time
    // selection from the still-authored draft.
    (void)set_control_selection(
        valid_control_selection(selection_before, draft_));
  }
  observe_owner_revisions();
  invalidate(true);
  return command_changed_result();
}

std::optional<QVec3> CurveToolService::drag_origin_world() const {
  return drag_ ? std::optional<QVec3>(drag_->origin_world) : std::nullopt;
}

} // namespace quader
