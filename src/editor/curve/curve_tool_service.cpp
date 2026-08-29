////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/curve/curve_tool_service.hpp"

#include "assets/asset_registry.hpp"
#include "editor/history/curve_history_commands.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/interactions/selection_edit.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

QVec3 native_point(modeling::Vec3 point) {
  return {point.x, point.y, point.z};
}

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

std::uint64_t curve_point_key(modeling::CurvePointId id) {
  return (static_cast<std::uint64_t>(id.index) << 32u) |
         static_cast<std::uint64_t>(id.generation);
}

const modeling::CurveControlPoint *find_point(
    const modeling::CurveDocument &document, modeling::CurvePointId id) {
  const auto points = document.points();
  const auto found = std::find_if(points.begin(), points.end(),
                                  [id](const auto &point) {
                                    return point.id == id;
                                  });
  return found == points.end() ? nullptr : &*found;
}

} // namespace

CurveToolService::CurveToolService(SceneDocument &scene, AssetRegistry &assets,
                                 EditorHistoryService &history,
                                 ModelingSessionService &modeling)
    : scene_(scene), assets_(assets), history_(history), modeling_(modeling) {}

bool CurveToolService::can_apply() const {
  return active_ && drawing_ && draft_.point_count() >= 2u && !dragging();
}

bool CurveToolService::closed() const {
  if (!drawing_) {
    const auto &preview = preview_state();
    if (preview.document.has_value()) {
      return preview.document->closed();
    }
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

  const bool source_editable =
      !drawing_ && editable_source(entity_) &&
      scene_.uuid(entity_) == source_uuid_;
  const bool controls_active = (active_ && drawing_) || source_editable;
  CurveToolPreviewState preview{
      .active = controls_active,
      .drawing = active_ && drawing_,
      .dragging = dragging(),
      .entity = entity_,
      .modeling_object_id = modeling_object_id_,
      .control_selection = control_selection_,
      .hover_world = hover_world_,
      .revision = ++preview_revision_,
  };
  if (active_ && drawing_) {
    preview.document = draft_;
  } else if (source_editable) {
    preview.document = modeling_.curve_document(modeling_object_id_);
    preview.world_transform = scene_.world_transform_matrix(entity_);
  }
  cached_preview_ = std::move(preview);
  cached_tool_revision_ = revision_;
  cached_scene_revision_ = scene_revision;
  cached_modeling_revision_ = modeling_revision;
  cached_asset_revision_ = asset_revision;
  return cached_preview_;
}

bool CurveToolService::editable_source(EntityId entity) const {
  const VisibilityComponent *visibility = scene_.visibility(entity);
  return scene_.curve(entity) != nullptr &&
         assets_.curve(scene_.curve(entity)->curve) != nullptr &&
         scene_.modeling_object(entity) != nullptr &&
         scene_.is_effectively_visible(entity) &&
         !scene_.is_effectively_locked(entity) &&
         !scene_.is_instance_definition_member(entity) &&
         !scene_.is_instance_definition_entity(entity) &&
         (visibility == nullptr || visibility->selectable) &&
         try_scene_matrix_inverse(scene_.world_transform_matrix(entity))
             .has_value();
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
  if (active_) {
    return command_unchanged_result();
  }
  active_ = true;
  drawing_ = true;
  draft_ = {};
  if (const auto entity = selected_curve()) {
    attach_source(*entity);
  }
  observe_owner_revisions();
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::refresh() {
  const bool lost_preview = drag_ && !drawing_ &&
                            !modeling_.curve_document_preview_active();
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
    const bool source_valid = editable_source(entity_) &&
                              scene_.uuid(entity_) == source_uuid_ &&
                              scene_.modeling_object(entity_)->object_id ==
                                  modeling_object_id_ &&
                              modeling_.curve_document(modeling_object_id_,
                                                       false).has_value();
    // Our own publications observe all stamps immediately. Reaching this path
    // during a drag therefore means another owner changed the scene, assets, or
    // portable history. Cancel before an old control snapshot can overwrite it.
    if (drag_) {
      const CommandResult canceled = cancel_drag();
      if (!canceled.ok) {
        return canceled;
      }
    }
    if (!source_valid || selection_changed) {
      detach_source();
      if (const auto entity = selected_curve()) {
        attach_source(*entity);
      }
    } else if (!control_selection_.empty()) {
      // A same-entity document replacement can retire generational point IDs
      // without changing scene selection or source identity.
      const auto current =
          modeling_.curve_document(modeling_object_id_, false);
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
  if (!can_apply()) {
    return command_invalid_result("Place at least two points and finish the current drag.");
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

CommandResult CurveToolService::new_curve() {
  if (!active_) {
    return command_invalid_result("The Curve tool is not active.");
  }
  if (dragging() || (drawing_ && draft_.point_count() != 0u)) {
    return command_invalid_result("Finish or cancel the current curve first.");
  }
  if (drawing_) {
    return command_unchanged_result();
  }
  drawing_ = true;
  entity_ = kInvalidEntityId;
  modeling_object_id_ = -1;
  source_uuid_.clear();
  draft_ = {};
  (void)set_control_selection({});
  hover_world_.reset();
  observe_owner_revisions();
  invalidate(true);
  return command_changed_result();
}

CommandResult CurveToolService::set_closed(bool closed_value) {
  if (!active_ || dragging()) {
    return command_invalid_result("Finish the active curve drag before changing closure.");
  }
  (void)refresh();
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
  const CommandResult result = history_.execute(
      std::make_unique<ReplaceCurveHistoryCommand>(modeling_,
                                                  modeling_object_id_,
                                                  std::move(*document)));
  if (result.ok && result.changed) {
    preferred_closed_ = closed_value;
    (void)set_control_selection(std::move(next_selection));
    observe_owner_revisions();
    invalidate(true);
  }
  return result;
}

CommandResult CurveToolService::toggle_point_smoothness(modeling::CurvePointId point) {
  if (!controls_editable() || dragging()) {
    return command_invalid_result("Finish the active curve drag before converting a point.");
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok) {
    return refreshed;
  }
  auto candidate = drawing_ ? std::optional(draft_)
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
  if (drawing_) {
    draft_ = std::move(*candidate);
  } else {
    const CommandResult result = history_.execute(
        std::make_unique<ReplaceCurveHistoryCommand>(modeling_,
                                                    modeling_object_id_,
                                                    std::move(*candidate)));
    if (!result.ok || !result.changed) {
      return result;
    }
    observe_owner_revisions();
  }
  (void)set_control_selection(std::move(next_selection));
  invalidate();
  return command_changed_result();
}

CommandResult CurveToolService::set_hover(std::optional<QVec3> position) {
  if (position && !finite_point(*position)) {
    return command_invalid_result("Curve hover position must be finite.");
  }
  if (!active_ || !drawing_ || dragging()) {
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

bool CurveToolService::edit_control_selection(
    std::span<const CurveControlTarget> hits,
    quader_editor_core::SelectionEditOperation operation,
    const CurveControlSelection *baseline,
    std::optional<CurveControlTarget> direct_hit) {
  const CommandResult refreshed = refresh();
  if (!refreshed.ok || !controls_editable() || dragging()) {
    return false;
  }
  const auto &preview = preview_state();
  if (!preview.document) {
    return false;
  }

  const CurveControlSelection valid_baseline = valid_control_selection(
      baseline != nullptr ? *baseline : control_selection_, *preview.document);
  CurveControlSelection hit_selection;
  hit_selection.controls.assign(hits.begin(), hits.end());
  hit_selection =
      valid_control_selection(hit_selection, *preview.document);
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
  return selection_changed || hover_changed;
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
  for (std::size_t point_index = 0; point_index < points.size(); ++point_index) {
    const auto &point = points[point_index];
    for (const CurveControlKind kind : {
             CurveControlKind::Anchor, CurveControlKind::IncomingHandle,
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
  if (!active_ || !drawing_ || dragging() || !finite_point(world_position)) {
    return command_invalid_result("Cannot place a curve point in the current state.");
  }
  modeling::CurveDocument candidate = draft_;
  const auto appended = candidate.append_point(
      {.position = portable_point(world_position)});
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
      .selection = CurveControlSelection{
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
    const auto begun = modeling_.begin_curve_document_preview(modeling_object_id_);
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
    const QVec3 local_origin = transform_point(
        drag_->inverse_world_transform, drag_->origin_world);
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
          replacement.incoming_handle = portable_point(
              native_point(point.incoming_handle) + local_delta);
        }
        if (outgoing_selected) {
          replacement.outgoing_handle = portable_point(
              native_point(point.outgoing_handle) + local_delta);
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
    const auto updated = modeling_.update_curve_document_preview(std::move(candidate));
    if (!updated.ok || !updated.changed) {
      return modeling_memento_command_result(updated);
    }
  }
  observe_owner_revisions();
  invalidate();
  return command_changed_result();
}

CommandResult CurveToolService::commit_drag() {
  if (!drag_) {
    return command_unchanged_result();
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok || !drag_) {
    return refreshed;
  }
  const bool appended_point = drag_->appended_point;
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
