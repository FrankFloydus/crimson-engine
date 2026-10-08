////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/selection/editor_selection_service.hpp"
#include "editor/modes/editor_mode_service.hpp"

#include "editor/selection/scene_selection_summary.hpp"

#include "core/release_features.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

bool has_scene_only_selection(const SceneDocument &scene,
                              const std::vector<EntityId> &selected) {
  return std::any_of(selected.begin(), selected.end(), [&](EntityId entity) {
    return scene.modeling_object(entity) == nullptr;
  });
}

bool same_selection_summary(const ModelingSelectionSummary &left,
                            const ModelingSelectionSummary &right) {
  return left.mode == right.mode &&
         left.has_selection == right.has_selection &&
         left.active_kind == right.active_kind &&
         left.active_object_id == right.active_object_id &&
         left.active_vertex_id == right.active_vertex_id &&
         left.active_edge_a == right.active_edge_a &&
         left.active_edge_b == right.active_edge_b &&
         left.active_face_id == right.active_face_id &&
         left.selected_object_count == right.selected_object_count &&
         left.selected_vertex_count == right.selected_vertex_count &&
         left.selected_edge_count == right.selected_edge_count &&
         left.selected_face_count == right.selected_face_count &&
         left.sub_selection_kind == right.sub_selection_kind &&
         left.selected_sub_element_count ==
             right.selected_sub_element_count &&
         left.sub_selection_revision == right.sub_selection_revision &&
         left.content_revision == right.content_revision &&
         left.selection_revision == right.selection_revision;
}

DocumentCommandResult
merge_modeling_selection_change(DocumentCommandResult result,
                                const ModelingSelectionSummary &before,
                                const ModelingSelectionSummary &after) {
  if (result.ok && (before.content_revision != after.content_revision ||
                    before.selection_revision != after.selection_revision)) {
    result.changed = true;
  }
  return result;
}

template <typename Result>
Result include_settlement_change(Result result,
                                 const ModelingCommandResult &settled) {
  result.changed = result.changed || settled.changed;
  return result;
}

ModelingCommandResult
modeling_command_result_from_document(DocumentCommandResult result) {
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = std::move(result.message),
  };
}

ModelingSubSelectionKind
modeling_sub_selection_kind(EditorSubSelectionKind kind) {
  switch (kind) {
  case EditorSubSelectionKind::None:
    return ModelingSubSelectionKind::None;
  case EditorSubSelectionKind::CurveControl:
    return ModelingSubSelectionKind::CurveControl;
  }
  return ModelingSubSelectionKind::None;
}

void project_sub_selection(ModelingSelectionSummary &summary,
                           const EditorSubSelectionSnapshot &sub_selection) {
  summary.sub_selection_kind = modeling_sub_selection_kind(sub_selection.kind);
  summary.selected_sub_element_count = sub_selection.selected_count;
  summary.sub_selection_revision = sub_selection.revision;
  if (sub_selection.selected_count == 0u) {
    return;
  }
  summary.has_selection = true;
}

SceneSelectionBulkMode scene_selection_bulk_mode(ModelingSelectionMode mode) {
  return mode == ModelingSelectionMode::Group ? SceneSelectionBulkMode::Group
                                              : SceneSelectionBulkMode::Object;
}

SceneSelectionEdit to_scene_selection_edit(ModelingSelectionEdit edit) {
  switch (edit) {
  case ModelingSelectionEdit::Add:
    return SceneSelectionEdit::Add;
  case ModelingSelectionEdit::Remove:
    return SceneSelectionEdit::Remove;
  case ModelingSelectionEdit::Toggle:
    return SceneSelectionEdit::Toggle;
  case ModelingSelectionEdit::Replace:
    return SceneSelectionEdit::Replace;
  }
  return SceneSelectionEdit::Replace;
}

} // namespace

EditorSelectionService::EditorSelectionService(
    SceneDocument &scene, DocumentCommandService &document_commands,
    ModelingSessionService &modeling)
    : scene_(scene), document_commands_(document_commands),
      modeling_(modeling) {}

EditorSelectionState EditorSelectionService::state() const {
  const ModelingSelectionSummary current_summary = summary();
  return {
      .mode = current_summary.mode,
      .summary = current_summary,
      .target = target_from_summary(current_summary),
      .scene_selection = scene_.selected_entities(),
      .active_entity = scene_.primary_selection(),
  };
}

EditorSelectionTarget EditorSelectionService::target() const {
  return target_from_summary(summary());
}

ModelingSelectionSummary EditorSelectionService::summary() const {
  const EditorSubSelectionSnapshot sub_selection =
      current_sub_selection_snapshot();
  const std::uint64_t scene_revision = scene_.revision();
  const std::uint64_t modeling_content_revision =
      modeling_.content_revision();
  const std::uint64_t modeling_selection_revision =
      modeling_.selection_revision();
  const int modeling_selection_mode =
      static_cast<int>(modeling_.selection_mode());
  const bool preview_active = modeling_.can_cancel_active_preview_for_undo();
  if (summary_cache_valid_ && !preview_active &&
      !summary_cache_preview_active_ &&
      summary_cache_scene_revision_ == scene_revision &&
      summary_cache_modeling_content_revision_ == modeling_content_revision &&
      summary_cache_modeling_selection_revision_ ==
          modeling_selection_revision &&
      summary_cache_modeling_selection_mode_ == modeling_selection_mode &&
      summary_cache_sub_selection_ == sub_selection) {
    return summary_cache_resolved_;
  }

  const ModelingSelectionSummary current = modeling_.selection_summary();
  if (summary_cache_valid_ &&
      summary_cache_scene_revision_ == scene_revision &&
      same_selection_summary(summary_cache_modeling_, current) &&
      summary_cache_sub_selection_ == sub_selection) {
    return summary_cache_resolved_;
  }

  const std::vector<EntityId> selected = scene_.selected_entities();
  ModelingSelectionSummary resolved = current;
  const bool object_mode = editor_mode_service_ == nullptr ||
                           editor_mode_service_->mode() == EditorMode::Object;
  if (object_mode && !selected.empty() && has_scene_only_selection(scene_, selected)) {
    resolved = make_scene_selection_summary(
        scene_,
        current.mode == ModelingSelectionMode::Group
            ? ModelingSelectionMode::Group
            : ModelingSelectionMode::Object,
        current.content_revision);
  } else if (object_mode && current.has_selection &&
             current.active_object_id != kInvalidModelingObjectId) {
    const std::optional<EntityId> active_entity =
        modeling_.entity_for_modeling_object(current.active_object_id);
    if (active_entity.has_value() &&
        scene_.is_effectively_locked(*active_entity)) {
      resolved = make_scene_selection_summary(scene_, current.mode,
                                              current.content_revision);
    }
  }
  project_sub_selection(resolved, sub_selection);
  summary_cache_scene_revision_ = scene_revision;
  summary_cache_preview_active_ = preview_active;
  summary_cache_modeling_content_revision_ = modeling_content_revision;
  summary_cache_modeling_selection_revision_ = modeling_selection_revision;
  summary_cache_modeling_selection_mode_ = modeling_selection_mode;
  summary_cache_modeling_ = current;
  summary_cache_resolved_ = resolved;
  summary_cache_sub_selection_ = sub_selection;
  summary_cache_valid_ = true;
  return resolved;
}

EditorSubSelectionEdit to_sub_selection_edit(ModelingSelectionEdit edit) {
  switch (edit) {
  case ModelingSelectionEdit::Add:
    return EditorSubSelectionEdit::Add;
  case ModelingSelectionEdit::Remove:
    return EditorSubSelectionEdit::Remove;
  case ModelingSelectionEdit::Toggle:
    return EditorSubSelectionEdit::Toggle;
  case ModelingSelectionEdit::Replace:
    return EditorSubSelectionEdit::Replace;
  }
  return EditorSubSelectionEdit::Replace;
}

ModelingSelectionEdit click_selection_edit(
    const ModelingPickSelectRequest &request) {
  if (request.control_pressed || request.command_pressed) {
    return ModelingSelectionEdit::Remove;
  }
  if (request.click_count == 0) {
    return ModelingSelectionEdit::Add;
  }
  return request.shift_pressed ? ModelingSelectionEdit::Add
                               : ModelingSelectionEdit::Replace;
}

ResolvedSelectionHit resolved_modeling_hit(ModelingSelectionHit hit) {
  const ResolvedSelectionDomain domain =
      hit.kind == ModelingActiveSelectionKind::Object
          ? ResolvedSelectionDomain::SceneEntity
          : ResolvedSelectionDomain::ModelingComponent;
  std::uint64_t component_key = static_cast<std::uint64_t>(hit.kind);
  component_key = component_key * 0x9e3779b97f4a7c15ULL +
                  static_cast<std::uint32_t>(hit.vertex_id);
  component_key = component_key * 0xbf58476d1ce4e5b9ULL +
                  static_cast<std::uint32_t>(hit.edge_a);
  component_key = component_key * 0x94d049bb133111ebULL +
                  static_cast<std::uint32_t>(hit.edge_b);
  component_key = component_key * 0xd6e8feb86659fd93ULL +
                  static_cast<std::uint32_t>(hit.face_id);
  return {
      .target = {
          .domain = domain,
          .entity = hit.entity,
          .modeling_object_id = hit.modeling_object_id,
          .component_kind = hit.kind,
          .vertex = hit.vertex_id,
          .edge_a = hit.edge_a,
          .edge_b = hit.edge_b,
          .face = hit.face_id,
          .source_occurrence = hit.source_occurrence,
      },
      .ray_distance = hit.ray_distance,
      .pick_distance_pixels = hit.pick_distance,
      .world_position = hit.world_position,
      .visibility_witnesses = std::move(hit.visibility_witnesses),
      .rectangle_target_only = hit.rectangle_target_only,
      .stable_key = {
          static_cast<std::uint64_t>(domain), hit.entity,
          static_cast<std::uint64_t>(
              static_cast<std::uint32_t>(hit.modeling_object_id)),
          component_key,
      },
  };
}

std::optional<ResolvedSelectionTarget> resolved_modeling_hover_target(
    const ModelingHoverTarget &hover,
    const ModelingSessionService &modeling) {
  if (hover.object_id == kInvalidModelingObjectId ||
      hover.kind == ModelingActiveSelectionKind::None ||
      hover.kind == ModelingActiveSelectionKind::Light) {
    return std::nullopt;
  }

  const EntityId entity =
      modeling.entity_for_modeling_object(hover.object_id)
          .value_or(kInvalidEntityId);
  if (hover.kind == ModelingActiveSelectionKind::Object) {
    return ResolvedSelectionTarget{
        .domain = ResolvedSelectionDomain::SceneEntity,
        .entity = entity,
        .modeling_object_id = hover.object_id,
    };
  }
  return ResolvedSelectionTarget{
      .domain = ResolvedSelectionDomain::ModelingComponent,
      .entity = entity,
      .modeling_object_id = hover.object_id,
      .component_kind = hover.kind,
      .vertex = hover.vertex_id,
      .edge_a = hover.edge_a,
      .edge_b = hover.edge_b,
      .face = hover.face_id,
  };
}

void append_component_target(ModelingComponentSelectionRequest &request,
                             const ResolvedSelectionTarget &target) {
  switch (target.component_kind) {
  case ModelingActiveSelectionKind::Vertex:
    request.vertices.push_back(target.vertex);
    request.active_kind = ModelingActiveSelectionKind::Vertex;
    request.active_vertex = target.vertex;
    break;
  case ModelingActiveSelectionKind::Edge:
    request.edges.emplace_back(target.edge_a, target.edge_b);
    request.active_kind = ModelingActiveSelectionKind::Edge;
    request.active_edge = {target.edge_a, target.edge_b};
    break;
  case ModelingActiveSelectionKind::Face:
    request.faces.push_back(target.face);
    request.active_kind = ModelingActiveSelectionKind::Face;
    request.active_face = target.face;
    break;
  case ModelingActiveSelectionKind::None:
  case ModelingActiveSelectionKind::Object:
  case ModelingActiveSelectionKind::Light:
    break;
  }
}

ModelingHoverSummary EditorSelectionService::hover_summary() const {
  return modeling_.selection_hover_summary();
}

ModelingSelectionMode EditorSelectionService::mode() const {
  return modeling_.selection_mode();
}

void EditorSelectionService::set_editor_mode_service(EditorModeService *service) {
  editor_mode_service_ = service;
  summary_cache_valid_ = false;
}

void EditorSelectionService::set_scene_selection_scope_policy(
    SceneSelectionTargetResolver resolver, SceneSelectionScopeFilter filter) {
  scene_target_resolver_ = std::move(resolver);
  scene_scope_filter_ = std::move(filter);
}

void EditorSelectionService::set_scene_selection_history_suppression_provider(
    SceneSelectionHistorySuppressionProvider provider) {
  scene_selection_history_suppression_provider_ = std::move(provider);
}

void EditorSelectionService::set_selection_edit_policy(
    SelectionEditCallback preflight, SelectionEditCallback edited,
    SelectionEditBeginCallback begin,
    SelectionSettlementPolicy requires_settlement) {
  selection_edit_preflight_ = std::move(preflight);
  selection_edited_ = std::move(edited);
  selection_edit_begin_ = std::move(begin);
  selection_settlement_policy_ = std::move(requires_settlement);
}

ModelingCommandResult EditorSelectionService::selection_edit_preflight() const {
  return selection_edit_preflight_ ? selection_edit_preflight_()
                                   : ModelingCommandResult{.ok = true};
}

ModelingCommandResult EditorSelectionService::begin_selection_edit(bool changing_mode) {
  const auto preflight = selection_edit_preflight();
  if (!preflight.ok) return preflight;
  return selection_edit_begin_ && requires_selection_settlement(changing_mode)
      ? selection_edit_begin_(changing_mode)
      : ModelingCommandResult{.ok = true};
}

bool EditorSelectionService::requires_selection_settlement(bool changing_mode) const {
  return selection_settlement_policy_
      ? selection_settlement_policy_(changing_mode)
      : modeling_.can_cancel_active_preview_for_undo() ||
            modeling_.pending_operation_active();
}

ModelingCommandResult EditorSelectionService::finish_selection_edit(
    ModelingCommandResult result) {
  if (result.ok && selection_edited_) {
    ModelingCommandResult notified = selection_edited_();
    result.changed = result.changed || notified.changed;
    if (!notified.ok) {
      result.ok = false;
      result.message = std::move(notified.message);
    }
  }
  return result;
}

ModelingCommandResult EditorSelectionService::complete_selection_edit() {
  return finish_selection_edit({.ok = true});
}

void EditorSelectionService::set_sub_selection_provider(
    EditorSubSelectionProvider *provider) {
  if (sub_selection_provider_ == provider) {
    return;
  }
  sub_selection_provider_ = provider;
  if (sub_selection_provider_ != nullptr) {
    sub_selection_provider_->synchronize_sub_selection_scope();
  }
  summary_cache_valid_ = false;
  summary_cache_sub_selection_ = {};
}

EditorSubSelectionSnapshot
EditorSelectionService::current_sub_selection_snapshot() const {
  if (sub_selection_provider_ == nullptr) {
    return {};
  }
  EditorSubSelectionSnapshot snapshot =
      sub_selection_provider_->sub_selection_snapshot();
  if (snapshot.kind == EditorSubSelectionKind::None) {
    return {};
  }
  if (snapshot.selected_count == 0u) {
    snapshot.active.reset();
  }
  return snapshot;
}

bool EditorSelectionService::has_sub_selection() const {
  return current_sub_selection_snapshot().selected_count > 0u;
}

std::optional<EditorSubSelectionBaseline>
EditorSelectionService::capture_registered_sub_selection_baseline() const {
  if (sub_selection_provider_ == nullptr) {
    return std::nullopt;
  }
  EditorSubSelectionBaseline baseline =
      sub_selection_provider_->capture_sub_selection_baseline();
  return baseline.valid()
             ? std::optional<EditorSubSelectionBaseline>(std::move(baseline))
             : std::nullopt;
}

ModelingCommandResult
EditorSelectionService::restore_registered_sub_selection_baseline(
    const std::optional<EditorSubSelectionBaseline> &baseline) {
  summary_cache_valid_ = false;
  if (!baseline.has_value()) {
    if (sub_selection_provider_ == nullptr) {
      return {.ok = true, .changed = false};
    }
    const EditorSubSelectionSnapshot before =
        current_sub_selection_snapshot();
    sub_selection_provider_->synchronize_sub_selection_scope();
    const EditorSubSelectionSnapshot after =
        current_sub_selection_snapshot();
    summary_cache_valid_ = false;
    return {.ok = true, .changed = before != after};
  }
  if (sub_selection_provider_ == nullptr) {
    return {.ok = false,
            .changed = true,
            .message = "Sub-selection rollback owner is unavailable."};
  }
  ModelingCommandResult restored =
      sub_selection_provider_->restore_sub_selection_baseline(*baseline);
  summary_cache_valid_ = false;
  return restored;
}

std::optional<ModelingCommandResult>
EditorSelectionService::try_delete_sub_selection() {
  if (sub_selection_provider_ == nullptr) {
    return std::nullopt;
  }
  sub_selection_provider_->synchronize_sub_selection_scope();
  if (current_sub_selection_snapshot().selected_count == 0u) {
    return std::nullopt;
  }
  return sub_selection_provider_->delete_selected_sub_elements();
}

DocumentCommandResult EditorSelectionService::apply_scene_selection_state(
    SceneSelectionState selection, bool preserve_selection_mode,
    std::uint64_t gesture_merge_key,
    std::optional<std::uint64_t> expected_history_revision) {
  const auto preflight = selection_edit_preflight();
  if (!preflight.ok) {
    return {.ok = false, .message = preflight.message};
  }
  if (same_scene_selection(capture_scene_selection(scene_), selection)) {
    if (is_component_selection_mode(modeling_.selection_mode())) {
      const auto settled = begin_selection_edit(
          !preserve_selection_mode && is_component_selection_mode(modeling_.selection_mode()));
      if (!settled.ok) {
        return {.ok = false, .changed = settled.changed,
                .message = settled.message};
      }
      return include_settlement_change(
          publish_scene_selection(preserve_selection_mode), settled);
    }
    return document_command_unchanged_result();
  }
  const auto settled = begin_selection_edit(
          !preserve_selection_mode && is_component_selection_mode(modeling_.selection_mode()));
  if (!settled.ok) {
    return {.ok = false, .changed = settled.changed,
            .message = settled.message};
  }
  const auto publish = [this, preserve_selection_mode]() {
    return publish_scene_selection(preserve_selection_mode);
  };
  if (scene_selection_history_suppression_provider_ &&
      scene_selection_history_suppression_provider_()) {
    return include_settlement_change(
        apply_runtime_scene_selection_state(std::move(selection),
                                             preserve_selection_mode), settled);
  }
  return include_settlement_change(document_commands_.select_entities(
      std::move(selection.selected), selection.active,
      SceneSelectionApplicationMode::UndoableCommand, publish,
      {.merge_key = gesture_merge_key == 0
                        ? std::string{}
                        : "selection-click/" + std::to_string(gesture_merge_key)},
      expected_history_revision), settled);
}

std::uint64_t EditorSelectionService::selection_history_revision() const {
  return document_commands_.history_revision();
}

DocumentCommandResult
EditorSelectionService::apply_runtime_scene_selection_state(
    SceneSelectionState selection, bool preserve_selection_mode) {
  const SceneSelectionState before = capture_scene_selection(scene_);
  if (!same_scene_selection(before, selection) &&
      document_commands_.entity_property_edit_active()) {
    const auto accepted = document_commands_.accept_active_property_edit();
    if (!accepted.ok) return accepted;
  }
  const SceneModelingProjectionCheckpoint checkpoint =
      capture_scene_selection_checkpoint(scene_, before, selection);
  const bool changed =
      apply_scene_selection(scene_, selection,
                            SceneSelectionApplicationMode::RuntimeSync);
  DocumentCommandResult published =
      publish_scene_selection(preserve_selection_mode);
  if (!published.ok) {
    summary_cache_valid_ = false;
    if (!restore_scene_selection_checkpoint(scene_, checkpoint)) {
      published.changed = true;
      if (!published.message.empty()) {
        published.message += " ";
      }
      published.message += "Scene selection rollback failed.";
    }
    return published;
  }
  return changed ? document_command_changed_result()
                 : document_command_unchanged_result();
}

DocumentCommandResult EditorSelectionService::publish_scene_selection(
    bool preserve_selection_mode) {
  ModelingCommandResult result{.ok = true};
  if (preserve_selection_mode ||
      modeling_.selection_mode() == ModelingSelectionMode::Group) {
    result =
        modeling_.clear_runtime_object_selection_for_scene_owned_selection();
  } else {
    result = modeling_.project_object_selection_from_scene(true);
  }
  if (!result.ok) {
    summary_cache_valid_ = false;
    return {.ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message)};
  }
  if (sub_selection_provider_ != nullptr) {
    sub_selection_provider_->synchronize_sub_selection_scope();
  }
  if (editor_mode_service_ != nullptr) {
    editor_mode_service_->synchronize_object_active_entity();
  }
  summary_cache_valid_ = false;
  return {.ok = result.ok,
          .changed = result.changed,
          .message = std::move(result.message)};
}

DocumentCommandResult
EditorSelectionService::set_scene_selection(std::span<const EntityId> entities,
                                            EntityId active,
                                            std::uint64_t gesture_merge_key,
                                            std::optional<std::uint64_t>
                                                expected_history_revision) {
  if (editor_mode_service_ != nullptr &&
      editor_mode_service_->mode() == EditorMode::Edit) {
    return {.ok = false,
            .message = "Exit Edit Mode before changing the selected objects."};
  }
  QDR_PROFILE_FUNCTION();
  const ModelingSelectionSummary before = modeling_.selection_summary();
  SceneSelectionState selection;
  if (scene_target_resolver_ || scene_scope_filter_) {
    std::vector<EntityId> resolved;
    resolved.reserve(entities.size());
    std::unordered_set<EntityId> membership;
    membership.reserve(entities.size());
    EntityId resolved_active = kInvalidEntityId;
    for (const EntityId entity : entities) {
      const EntityId target = scene_target_resolver_
                                  ? scene_target_resolver_(entity)
                                  : entity;
      if (target == kInvalidEntityId ||
          (scene_scope_filter_ && !scene_scope_filter_(target)) ||
          !membership.insert(target).second) {
        continue;
      }
      resolved.push_back(target);
      if (entity == active || target == active) {
        resolved_active = target;
      }
    }
    selection = make_scene_selection(scene_, resolved, resolved_active);
  } else {
    selection = make_user_scene_selection(scene_, entities, active);
  }
  DocumentCommandResult result =
      apply_scene_selection_state(std::move(selection), false, gesture_merge_key,
                                  expected_history_revision);
  return merge_modeling_selection_change(result, before,
                                         modeling_.selection_summary());
}

DocumentCommandResult EditorSelectionService::set_inspection_scene_selection(
    std::span<const EntityId> entities, EntityId active) {
  if (editor_mode_service_ != nullptr &&
      editor_mode_service_->mode() == EditorMode::Edit) {
    return {.ok = false,
            .message = "Exit Edit Mode before changing the selected objects."};
  }
  QDR_PROFILE_FUNCTION();
  const ModelingSelectionSummary before = modeling_.selection_summary();
  SceneSelectionState selection = make_scene_selection(scene_, entities, active);
  DocumentCommandResult result =
      apply_scene_selection_state(std::move(selection));
  return merge_modeling_selection_change(result, before,
                                         modeling_.selection_summary());
}

DocumentCommandResult EditorSelectionService::synchronize_scene_selection(
    std::span<const EntityId> entities, EntityId active) {
  QDR_PROFILE_FUNCTION();
  const ModelingSelectionSummary before = modeling_.selection_summary();
  SceneSelectionState selection = make_scene_selection(scene_, entities, active);
  DocumentCommandResult result =
      apply_runtime_scene_selection_state(std::move(selection));
  return merge_modeling_selection_change(result, before,
                                         modeling_.selection_summary());
}

DocumentCommandResult
EditorSelectionService::edit_scene_selection(EntityId entity,
                                             EditorSelectionEdit edit) {
  const std::array<EntityId, 1> entities{{entity}};
  return edit_scene_selection(entities, edit);
}

DocumentCommandResult
EditorSelectionService::edit_scene_selection(std::span<const EntityId> entities,
                                             EditorSelectionEdit edit) {
  const std::vector<EntityId> current = scene_.selected_entities();
  const SceneSelectionState resolved = resolve_scene_selection_edit(
      current, scene_.primary_selection(), entities, edit);
  return set_scene_selection(resolved.selected, resolved.active);
}

ModelingCommandResult
EditorSelectionService::set_object_selection(std::span<const EntityId> entities,
                                             EntityId active) {
  const DocumentCommandResult result = set_scene_selection(entities, active);
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = result.message,
  };
}

ModelingCommandResult EditorSelectionService::set_component_selection(
    const ModelingComponentSelectionRequest &request) {
  const auto preflight = selection_edit_preflight();
  if (!preflight.ok) {
    return preflight;
  }
  const std::optional<EntityId> entity =
      modeling_.entity_for_modeling_object(request.modeling_object_id);
  if (!entity.has_value() || !component_owner_selectable(*entity)) {
    return {.ok = false,
            .changed = false,
            .message = "The component owner is not visible in the current "
                       "viewport scope."};
  }
  const bool changing_kind = request.mode != modeling_.selection_mode();
  if (changing_kind || requires_selection_settlement()) {
    const auto validated = modeling_.validate_component_selection(request);
    if (!validated.ok) return validated;
  }
  ModelingCommandResult settled{.ok = true};
  if (changing_kind && editor_mode_service_ != nullptr) {
    const CommandResult kind_changed = editor_mode_service_->set_component_kind(request.mode);
    settled = {.ok = kind_changed.ok, .changed = kind_changed.changed,
               .message = kind_changed.message};
  } else {
    settled = begin_selection_edit(changing_kind);
  }
  if (!settled.ok) return settled;
  ModelingCommandResult result = modeling_.set_component_selection(request);
  if (result.ok && sub_selection_provider_ != nullptr) {
    sub_selection_provider_->synchronize_sub_selection_scope();
  }
  return include_settlement_change(finish_selection_edit(std::move(result)), settled);
}

ModelingCommandResult
EditorSelectionService::set_mode(ModelingSelectionMode mode) {
  if (editor_mode_service_ != nullptr) {
    if (!editor_mode_service_->permits_selection_mode(mode)) {
      return {.ok = false,
              .message = "Selection kind is unavailable in the current editor mode."};
    }
    if (is_component_selection_mode(mode)) {
      const CommandResult result = editor_mode_service_->set_component_kind(mode);
      return {.ok = result.ok, .changed = result.changed, .message = result.message};
    }
  }
  const auto preflight = selection_edit_preflight();
  if (!preflight.ok) {
    return preflight;
  }
  const ModelingSelectionMode previous_mode = modeling_.selection_mode();
  if (mode != ModelingSelectionMode::Object &&
      mode != ModelingSelectionMode::Group &&
      mode != ModelingSelectionMode::Vertex &&
      mode != ModelingSelectionMode::Edge &&
      mode != ModelingSelectionMode::Face) {
    return {.ok = false, .message = "Selection mode is invalid."};
  }
  ModelingCommandResult settled{.ok = true};
  if (previous_mode != mode) {
    settled = begin_selection_edit(true);
    if (!settled.ok) return settled;
  }
  ModelingCommandResult result = modeling_.set_selection_mode(mode);
  if (result.ok && previous_mode != mode) {
    resolved_depth_cycle_active_ = false;
    resolved_depth_cycle_signature_.clear();
    resolved_depth_cycle_index_ = 0u;
  }
  if (result.ok && sub_selection_provider_ != nullptr) {
    sub_selection_provider_->synchronize_sub_selection_scope();
  }
  return include_settlement_change(std::move(result), settled);
}

bool EditorSelectionService::scene_target_selectable(EntityId entity) const {
  if (entity == kInvalidEntityId || !scene_.exists(entity)) {
    return false;
  }
  return scene_scope_filter_ ? scene_scope_filter_(entity)
                             : scene_entity_selectable_for_user_selection(
                                   scene_, entity);
}

bool EditorSelectionService::component_owner_selectable(EntityId entity) const {
  if (!modeling_.component_target_is_editable(entity)) {
    return false;
  }
  const ModelingInstanceEditScope scope = modeling_.instance_edit_scope();
  return scope.active() || scene_target_selectable(entity);
}

std::vector<EntityId>
EditorSelectionService::scene_bulk_candidates(ModelingSelectionMode mode) const {
  std::vector<EntityId> candidates = user_selectable_scene_entities_for_mode(
      scene_, scene_selection_bulk_mode(mode));
  if (!scene_scope_filter_) {
    return candidates;
  }
  std::erase_if(candidates, [this](EntityId entity) {
    return !scene_scope_filter_(entity);
  });
  return candidates;
}

ModelingCommandResult EditorSelectionService::select_all() {
  QDR_PROFILE_FUNCTION();
  const auto preflight = selection_edit_preflight();
  if (!preflight.ok) {
    return preflight;
  }
  if (sub_selection_provider_ != nullptr) {
    sub_selection_provider_->synchronize_sub_selection_scope();
  }
  const EditorSubSelectionSnapshot sub_selection =
      current_sub_selection_snapshot();
  if (sub_selection_provider_ != nullptr &&
      sub_selection.participates_in_bulk_selection) {
    return sub_selection_provider_->select_all_sub_elements();
  }
  const auto settled = begin_selection_edit();
  if (!settled.ok) return settled;
  const ModelingSelectionMode selection_mode = modeling_.selection_mode();
  if (!selection_mode_uses_scene_document(selection_mode)) {
    QDR_PROFILE_SCOPE("Selection select all runtime components");
    return include_settlement_change(finish_selection_edit(modeling_.select_all()), settled);
  }

  std::vector<EntityId> selected;
  {
    QDR_PROFILE_SCOPE("Selection collect scene select all entities");
    selected = scene_bulk_candidates(selection_mode);
  }
  const EntityId active = selected.empty() ? kInvalidEntityId : selected.back();
  return include_settlement_change(modeling_command_result_from_document(
      set_scene_selection(selected, active)), settled);
}

ModelingCommandResult EditorSelectionService::invert() {
  const auto preflight = selection_edit_preflight();
  if (!preflight.ok) {
    return preflight;
  }
  if (sub_selection_provider_ != nullptr) {
    sub_selection_provider_->synchronize_sub_selection_scope();
  }
  const EditorSubSelectionSnapshot sub_selection =
      current_sub_selection_snapshot();
  if (sub_selection_provider_ != nullptr &&
      sub_selection.participates_in_bulk_selection) {
    return sub_selection_provider_->invert_sub_elements();
  }
  const auto settled = begin_selection_edit();
  if (!settled.ok) return settled;
  const ModelingSelectionMode selection_mode = modeling_.selection_mode();
  if (!selection_mode_uses_scene_document(selection_mode)) {
    return include_settlement_change(finish_selection_edit(modeling_.invert_selection()), settled);
  }

  const std::vector<EntityId> current = scene_.selected_entities();
  const EntityId current_active = scene_.primary_selection();
  const std::unordered_set<EntityId> current_membership(
      current.begin(), current.end());
  std::vector<EntityId> candidates = scene_bulk_candidates(selection_mode);
  std::erase_if(candidates, [this](EntityId entity) {
    return !scene_.is_effectively_visible(entity);
  });
  const std::unordered_set<EntityId> candidate_membership(
      candidates.begin(), candidates.end());

  std::vector<EntityId> selected;
  selected.reserve(current.size() + candidates.size());
  for (const EntityId entity : current) {
    if (!candidate_membership.contains(entity)) {
      selected.push_back(entity);
    }
  }
  for (const EntityId entity : candidates) {
    if (!current_membership.contains(entity)) {
      selected.push_back(entity);
    }
  }

  const EntityId active =
      std::find(selected.begin(), selected.end(), current_active) !=
              selected.end()
          ? current_active
          : (selected.empty() ? kInvalidEntityId : selected.back());
  // The XOR is already policy-resolved. Reapplying user pickability here would
  // discard hidden, locked, or out-of-scope members that must stay selected.
  return include_settlement_change(modeling_command_result_from_document(
      set_inspection_scene_selection(selected, active)), settled);
}

ModelingCommandResult EditorSelectionService::clear() {
  const auto preflight = selection_edit_preflight();
  if (!preflight.ok) {
    return preflight;
  }
  if (sub_selection_provider_ != nullptr) {
    sub_selection_provider_->synchronize_sub_selection_scope();
  }
  const auto sub_selection = current_sub_selection_snapshot();
  if (sub_selection_provider_ != nullptr &&
      (has_sub_selection() ||
       (sub_selection.participates_in_bulk_selection &&
        sub_selection.entity == kInvalidEntityId))) {
    return sub_selection_provider_->clear_sub_elements();
  }
  const auto settled = begin_selection_edit();
  if (!settled.ok) return settled;
  const ModelingSelectionMode selection_mode = modeling_.selection_mode();
  if (!selection_mode_uses_scene_document(selection_mode)) {
    ModelingCommandResult result = modeling_.clear_selection();
    if (!result.ok || result.changed) {
      return include_settlement_change(std::move(result), settled);
    }
    const std::vector<EntityId> selected = scene_.selected_entities();
    if (selected.empty() ||
        std::ranges::any_of(selected, [&](EntityId entity) {
          return scene_.modeling_object(entity) != nullptr;
        })) {
      return include_settlement_change(std::move(result), settled);
    }
    // Lights and other scene-only selections can remain in component mode.
    // With no selected mesh, the modeling owner has nothing to clear.
    return include_settlement_change(modeling_command_result_from_document(
        apply_scene_selection_state(
            make_scene_selection(scene_, {}, kInvalidEntityId), true)), settled);
  }

  return include_settlement_change(modeling_command_result_from_document(
      set_scene_selection(std::span<const EntityId>{}, kInvalidEntityId)), settled);
}

ModelingCommandResult EditorSelectionService::apply_component_selection_command(
    ModelingComponentSelectionCommand command,
    std::span<const ModelingSelectionMode> allowed_modes) {
  if (command == ModelingComponentSelectionCommand::SelectFacesWithSameMaterial &&
      !release_has_feature(ReleaseFeature::Materials)) {
    return {.ok = false, .changed = false,
            .message = release_feature_unavailable_message()};
  }
  const auto preflight = selection_edit_preflight();
  if (!preflight.ok) {
    return preflight;
  }
  const ModelingSelectionMode current_mode = modeling_.selection_mode();
  if (std::find(allowed_modes.begin(), allowed_modes.end(), current_mode) ==
      allowed_modes.end()) {
    return {
        .ok = false,
        .changed = false,
        .message = "The selection command is unavailable in the current mode.",
    };
  }
  const auto settled = begin_selection_edit();
  if (!settled.ok) return settled;
  return include_settlement_change(finish_selection_edit(
      modeling_.apply_component_selection_command(command)), settled);
}

ModelingCommandResult EditorSelectionService::select_edge_direction() {
  constexpr std::array kModes{ModelingSelectionMode::Edge};
  return apply_component_selection_command(
      ModelingComponentSelectionCommand::SelectEdgeDirection, kModes);
}

ModelingCommandResult EditorSelectionService::select_edge_loops() {
  constexpr std::array kModes{ModelingSelectionMode::Edge};
  return apply_component_selection_command(
      ModelingComponentSelectionCommand::SelectEdgeLoops, kModes);
}

ModelingCommandResult EditorSelectionService::select_edge_ring() {
  constexpr std::array kModes{ModelingSelectionMode::Edge};
  return apply_component_selection_command(
      ModelingComponentSelectionCommand::SelectEdgeRing, kModes);
}

ModelingCommandResult
EditorSelectionService::select_faces_with_same_material() {
  constexpr std::array kModes{ModelingSelectionMode::Face};
  return apply_component_selection_command(
      ModelingComponentSelectionCommand::SelectFacesWithSameMaterial, kModes);
}

ModelingCommandResult EditorSelectionService::select_coplanar_faces() {
  constexpr std::array kModes{ModelingSelectionMode::Face};
  return apply_component_selection_command(
      ModelingComponentSelectionCommand::SelectCoplanarFaces, kModes);
}

ModelingCommandResult EditorSelectionService::select_face_normal() {
  constexpr std::array kModes{ModelingSelectionMode::Face};
  return apply_component_selection_command(
      ModelingComponentSelectionCommand::SelectFaceNormal, kModes);
}

ModelingCommandResult EditorSelectionService::select_mesh_ngons() {
  const auto preflight = selection_edit_preflight();
  if (!preflight.ok) {
    return preflight;
  }
  if (editor_mode_service_ == nullptr ||
      editor_mode_service_->mode() != EditorMode::Edit) {
    return {
        .ok = false,
        .changed = false,
        .message = "Select N-Gons requires Edit Mode.",
    };
  }

  std::vector<int> targets;
  std::optional<int> active_target;
  for (const auto &participant : editor_mode_service_->snapshot().participants) {
    const EntityId entity = participant.entity;
    if (!component_owner_selectable(entity)) continue;
    const std::optional<int> modeling_object =
        modeling_.modeling_object_for_entity(entity);
    if (!modeling_object.has_value()) {
      continue;
    }
    targets.push_back(*modeling_object);
    if (entity == scene_.primary_selection()) {
      active_target = *modeling_object;
    }
  }
  if (active_target.has_value()) {
    const auto active =
        std::find(targets.begin(), targets.end(), *active_target);
    if (active != targets.end()) {
      std::rotate(active, std::next(active), targets.end());
    }
  }
  if (targets.empty()) {
    return {
        .ok = true,
        .changed = false,
        .message = "No directly editable mesh is selected.",
    };
  }
  const auto settled = begin_selection_edit();
  if (!settled.ok) return settled;
  return include_settlement_change(
      finish_selection_edit(modeling_.select_mesh_ngons(targets)), settled);
}

ModelingCommandResult EditorSelectionService::select_similar_meshes() {
  if (modeling_.selection_mode() != ModelingSelectionMode::Object) {
    return {
        .ok = false,
        .changed = false,
        .message = "Select Similar Meshes is available only in Mesh mode.",
    };
  }

  const std::vector<EntityId> current = scene_.selected_entities();
  const std::vector<EntityId> candidates =
      scene_bulk_candidates(ModelingSelectionMode::Object);
  const std::vector<EntityId> matches =
      modeling_.matching_geometry_entities(candidates, current);
  std::vector<EntityId> selected = current;
  selected.reserve(current.size() + matches.size());
  for (const EntityId entity : matches) {
    if (std::find(selected.begin(), selected.end(), entity) == selected.end()) {
      selected.push_back(entity);
    }
  }
  const EntityId current_active = scene_.primary_selection();
  const EntityId active =
      std::find(selected.begin(), selected.end(), current_active) !=
              selected.end()
          ? current_active
          : (selected.empty() ? kInvalidEntityId : selected.back());
  return modeling_command_result_from_document(
      set_scene_selection(selected, active));
}

ModelingCommandResult EditorSelectionService::grow() {
  constexpr std::array kModes{ModelingSelectionMode::Vertex,
                              ModelingSelectionMode::Edge,
                              ModelingSelectionMode::Face};
  return apply_component_selection_command(
      ModelingComponentSelectionCommand::Grow, kModes);
}

ModelingCommandResult EditorSelectionService::shrink() {
  constexpr std::array kModes{ModelingSelectionMode::Vertex,
                              ModelingSelectionMode::Edge,
                              ModelingSelectionMode::Face};
  return apply_component_selection_command(
      ModelingComponentSelectionCommand::Shrink, kModes);
}

EditorSelectionBoxBaseline
EditorSelectionService::capture_box_selection_baseline() const {
  QDR_PROFILE_SCOPE("Box selection capture editor baseline");
  EditorSelectionBoxBaseline baseline;
  baseline.editor_mode_generation = editor_mode_service_ == nullptr
                                        ? 0 : editor_mode_service_->generation();
  baseline.mode = modeling_.selection_mode();
  baseline.scene_selection = scene_.selected_entities();
  baseline.active_entity = scene_.primary_selection();
  if (!selection_mode_uses_scene_document(baseline.mode)) {
    baseline.modeling_selection = modeling_.capture_box_selection_baseline();
  }
  if (sub_selection_provider_ != nullptr) {
    EditorSubSelectionBaseline sub_selection =
        sub_selection_provider_->capture_sub_selection_baseline();
    if (sub_selection.valid()) {
      baseline.sub_selection = std::move(sub_selection);
    }
  }
  return baseline;
}

std::optional<ResolvedSelectionHit>
EditorSelectionService::query_modeling_hit(
    const ModelingPickSelectRequest &request) const {
  const std::optional<ModelingSelectionHit> hit =
      modeling_.query_selection_hit(request);
  return hit.has_value()
             ? std::optional<ResolvedSelectionHit>(
                   resolved_modeling_hit(*hit))
             : std::nullopt;
}

std::vector<ResolvedSelectionHit>
EditorSelectionService::query_modeling_depth_hits(
    const ModelingPickSelectRequest &request) const {
  const std::vector<ModelingSelectionHit> hits =
      modeling_.query_selection_depth_hits(request);
  std::vector<ResolvedSelectionHit> resolved;
  resolved.reserve(hits.size());
  for (const ModelingSelectionHit &hit : hits) {
    resolved.push_back(resolved_modeling_hit(hit));
  }
  return resolved;
}

std::vector<ResolvedSelectionHit>
EditorSelectionService::query_modeling_marquee_hits(
    const ModelingBoxSelectRequest &request) const {
  std::vector<ModelingSelectionHit> modeling_hits =
      modeling_.query_selection_marquee_hits(request);
  std::vector<ResolvedSelectionHit> hits;
  hits.reserve(modeling_hits.size());
  for (ModelingSelectionHit &hit : modeling_hits) {
    hits.push_back(resolved_modeling_hit(std::move(hit)));
  }
  return hits;
}

std::vector<ResolvedSelectionHit> EditorSelectionService::query_modeling_lasso_hits(
    const ModelingLassoSelectRequest &request,
    std::vector<EntityId> *eligible_entities) const {
  auto modeling_hits = modeling_.query_selection_lasso_hits(request, eligible_entities);
  std::vector<ResolvedSelectionHit> result;
  result.reserve(modeling_hits.size());
  for (auto &hit : modeling_hits) {
    result.push_back(resolved_modeling_hit(std::move(hit)));
  }
  return result;
}

std::vector<ResolvedSelectionTarget> EditorSelectionService::filter_lasso_group_targets(
    std::span<const ResolvedSelectionTarget> targets,
    std::span<const EntityId> eligible_entities, bool partial_intersection) const {
  if (modeling_.selection_mode() != ModelingSelectionMode::Group) {
    return {targets.begin(), targets.end()};
  }
  std::vector<ResolvedSelectionTarget> result;
  std::unordered_set<EntityId> matched_entities;
  for (const auto &target : targets) {
    if (target.domain == ResolvedSelectionDomain::SceneEntity) {
      matched_entities.insert(target.entity);
    }
  }
  std::unordered_set<EntityId> visited;
  for (const auto &target : targets) {
    const EntityId group = nearest_group_ancestor(target.entity);
    if (target.domain != ResolvedSelectionDomain::SceneEntity ||
        group == kInvalidEntityId || !visited.insert(group).second) {
      continue;
    }
    if (!partial_intersection) {
      const bool unmatched_descendant = std::any_of(
          eligible_entities.begin(), eligible_entities.end(), [&](EntityId entity) {
            if (matched_entities.contains(entity)) {
              return false;
            }
            for (EntityId parent = entity; parent != kInvalidEntityId;
                 parent = scene_.parent(parent)) {
              if (parent == group) {
                return true;
              }
            }
            return false;
          });
      if (unmatched_descendant) {
        continue;
      }
    }
    result.push_back({.domain = ResolvedSelectionDomain::SceneEntity, .entity = group});
  }
  return result;
}

std::optional<ResolvedSelectionTarget>
EditorSelectionService::resolve_depth_arbitrated_target(
    std::span<const ResolvedSelectionTarget> ordered_depth_targets,
    std::optional<ResolvedSelectionTarget> ordinary_target,
    const ModelingPickSelectRequest &request) {
  const ModelingSelectionMode selection_mode = modeling_.selection_mode();
  if (!request.depth_cycle_requested(selection_mode)) {
    if (selection_mode != ModelingSelectionMode::Object ||
        request.click_count == 0 || request.alt_pressed) {
      resolved_depth_cycle_active_ = false;
      resolved_depth_cycle_signature_.clear();
    }
    return ordinary_target;
  }

  const auto same_identity = [](const ResolvedSelectionTarget &left,
                                const ResolvedSelectionTarget &right) {
    return left.domain == right.domain && left.entity == right.entity &&
           left.modeling_object_id == right.modeling_object_id &&
           left.component_kind == right.component_kind &&
           left.vertex == right.vertex && left.edge_a == right.edge_a &&
           left.edge_b == right.edge_b && left.face == right.face &&
           left.sub_selection_kind == right.sub_selection_kind &&
           left.sub_element == right.sub_element;
  };
  std::vector<ResolvedSelectionTarget> targets;
  targets.reserve(ordered_depth_targets.size());
  for (ResolvedSelectionTarget target : ordered_depth_targets) {
    if (target.domain == ResolvedSelectionDomain::SceneEntity &&
        selection_mode == ModelingSelectionMode::Group) {
      target.entity = nearest_group_ancestor(target.entity);
      target.modeling_object_id = kInvalidModelingObjectId;
    }
    if ((target.domain == ResolvedSelectionDomain::SceneEntity &&
         target.entity == kInvalidEntityId) ||
        std::ranges::any_of(targets, [&](const ResolvedSelectionTarget &seen) {
          return same_identity(seen, target);
        })) {
      continue;
    }
    targets.push_back(std::move(target));
  }
  if (targets.empty()) {
    resolved_depth_cycle_active_ = false;
    resolved_depth_cycle_signature_.clear();
    return std::nullopt;
  }

  if (ordinary_target.has_value() &&
      ordinary_target->domain == ResolvedSelectionDomain::SceneEntity &&
      selection_mode == ModelingSelectionMode::Group) {
    ordinary_target->entity = nearest_group_ancestor(ordinary_target->entity);
    ordinary_target->modeling_object_id = kInvalidModelingObjectId;
  }

  std::string signature;
  signature.reserve(96u + targets.size() * 80u);
  const auto append = [&signature](const auto &value) {
    signature.append(reinterpret_cast<const char *>(&value), sizeof(value));
  };
  append(selection_mode);
  append(editor_mode_service_ != nullptr ? editor_mode_service_->generation()
                                         : std::uint64_t{0});
  append(request.control_pressed);
  append(request.command_pressed);
  append(request.shift_pressed);
  append(request.ray.direction.x);
  append(request.ray.direction.y);
  append(request.ray.direction.z);
  append(request.vertex_radius);
  append(request.edge_radius);
  append(request.select_backfaces);
  const double origin_tolerance =
      std::isfinite(request.cycle_origin_tolerance) &&
              request.cycle_origin_tolerance > 0.0
          ? request.cycle_origin_tolerance
          : 0.0;
  append(origin_tolerance);
  for (const ResolvedSelectionTarget &target : targets) {
    append(target.domain);
    append(target.entity);
    append(target.modeling_object_id);
    append(target.component_kind);
    append(target.vertex);
    append(target.edge_a);
    append(target.edge_b);
    append(target.face);
    append(target.sub_selection_kind);
    const bool has_sub_element = target.sub_element.has_value();
    append(has_sub_element);
    if (has_sub_element) {
      append(target.sub_element->index);
      append(target.sub_element->generation);
      append(target.sub_element->role);
    }
  }

  const double dx = static_cast<double>(request.ray.origin.x) -
                    resolved_depth_cycle_origin_.x;
  const double dy = static_cast<double>(request.ray.origin.y) -
                    resolved_depth_cycle_origin_.y;
  const double dz = static_cast<double>(request.ray.origin.z) -
                    resolved_depth_cycle_origin_.z;
  const double origin_distance_squared = dx * dx + dy * dy + dz * dz;
  const bool same_context =
      resolved_depth_cycle_active_ &&
      resolved_depth_cycle_signature_ == signature &&
      std::isfinite(origin_distance_squared) &&
      origin_distance_squared <= origin_tolerance * origin_tolerance;
  if (!same_context) {
    resolved_depth_cycle_active_ = true;
    resolved_depth_cycle_signature_ = std::move(signature);
    resolved_depth_cycle_origin_ = request.ray.origin;
    const auto ordinary =
        ordinary_target.has_value()
            ? std::ranges::find_if(
                  targets, [&](const ResolvedSelectionTarget &target) {
                    return same_identity(target, *ordinary_target);
                  })
            : targets.end();
    resolved_depth_cycle_index_ =
        ordinary != targets.end()
            ? static_cast<std::size_t>(ordinary - targets.begin())
            : targets.size() - 1u;
  }
  resolved_depth_cycle_index_ =
      (resolved_depth_cycle_index_ + 1u) % targets.size();
  return targets[resolved_depth_cycle_index_];
}

ModelingCommandResult EditorSelectionService::restore_sub_selection_after_failure(
    const EditorSubSelectionBaseline &baseline,
    std::string_view failure_message) {
  summary_cache_valid_ = false;
  if (sub_selection_provider_ == nullptr) {
    return {.ok = false,
            .changed = true,
            .message = std::string(failure_message) +
                       " Provider rollback is unavailable."};
  }
  const ModelingCommandResult restored =
      sub_selection_provider_->restore_sub_selection_baseline(baseline);
  summary_cache_valid_ = false;
  if (!restored.ok) {
    return {.ok = false,
            .changed = true,
            .message = std::string(failure_message) +
                       " Provider rollback failed: " + restored.message};
  }
  return {.ok = false,
          .changed = false,
          .message = std::string(failure_message)};
}

ResolvedSelectionApplyResult EditorSelectionService::apply_resolved_click(
    const ResolvedSelectionClickRequest &request) {
  const auto preflight = selection_edit_preflight();
  if (!preflight.ok) {
    return {.command = preflight, .selection = summary()};
  }
  const ModelingSelectionMode selection_mode = modeling_.selection_mode();
  const ModelingEdgeSelectionTraversal edge_traversal =
      request.policy.edge_traversal_requested(selection_mode);
  const ModelingComponentSelectionTraversal component_traversal =
      request.policy.component_traversal_requested(selection_mode);
  const bool traversal_requested =
      edge_traversal != ModelingEdgeSelectionTraversal::None ||
      component_traversal != ModelingComponentSelectionTraversal::None;
  if (request.baseline.has_value() &&
      (request.baseline->mode != selection_mode ||
       (editor_mode_service_ != nullptr &&
        request.baseline->editor_mode_generation != editor_mode_service_->generation()))) {
    return {
        .command = {.ok = false,
                    .message = "Resolved click selection baseline is stale."},
        .selection = summary(),
    };
  }
  const EditorSelectionBoxBaseline *baseline =
      request.baseline.has_value() ? &*request.baseline : nullptr;

  const auto apply_sub_selection =
      [&](EditorSubSelectionKind kind,
          const ResolvedSelectionTarget *target)
      -> ResolvedSelectionApplyResult {
    if (sub_selection_provider_ == nullptr ||
        kind == EditorSubSelectionKind::None ||
        (target != nullptr && !target->sub_element.has_value())) {
      return {
          .command = {.ok = false,
                      .message = "Sub-element selection provider is unavailable."},
          .selection = summary(),
      };
    }
    sub_selection_provider_->synchronize_sub_selection_scope();
    const EditorSubSelectionSnapshot provider =
        sub_selection_provider_->sub_selection_snapshot();
    if (provider.kind != kind ||
        (target != nullptr &&
         (provider.entity != target->entity ||
          (target->modeling_object_id != kInvalidModelingObjectId &&
           provider.modeling_object_id != target->modeling_object_id)))) {
      return {
          .command = {.ok = false,
                      .message = "Sub-element selection owner is stale."},
          .selection = summary(),
      };
    }
    const EditorSubSelectionBaseline rollback =
        sub_selection_provider_->capture_sub_selection_baseline();
    if (!rollback.valid()) {
      return {
          .command = {.ok = false,
                      .message = "Sub-element selection owner is stale."},
          .selection = summary(),
      };
    }
    const EditorSubSelectionBaseline *sub_baseline =
        baseline != nullptr && baseline->sub_selection.has_value()
            ? &*baseline->sub_selection
            : nullptr;
    if (sub_baseline != nullptr &&
        (sub_baseline->kind != provider.kind ||
         sub_baseline->entity != provider.entity ||
         sub_baseline->modeling_object_id != provider.modeling_object_id)) {
      return {
          .command = {.ok = false,
                      .message = "Sub-element selection baseline is stale."},
          .selection = summary(),
      };
    }
    const std::array<EditorSubSelectionTarget, 1> target_hit =
        target != nullptr
            ? std::array<EditorSubSelectionTarget, 1>{*target->sub_element}
            : std::array<EditorSubSelectionTarget, 1>{};
    const std::span<const EditorSubSelectionTarget> hits =
        target != nullptr
            ? std::span<const EditorSubSelectionTarget>{target_hit}
            : std::span<const EditorSubSelectionTarget>{};
    ModelingCommandResult command = sub_selection_provider_->edit_sub_elements(
        hits, to_sub_selection_edit(click_selection_edit(request.policy)),
        sub_baseline,
        target != nullptr ? target->sub_element : std::nullopt);
    summary_cache_valid_ = false;
    if (!command.ok) {
      command = restore_sub_selection_after_failure(
          rollback, command.message.empty()
                        ? "Sub-element selection edit failed."
                        : command.message);
    }
    return {.command = std::move(command), .selection = summary()};
  };

  if (!request.winner.has_value()) {
    if (request.miss_sub_selection_kind != EditorSubSelectionKind::None) {
      if (request.policy.preserve_selection_on_miss) {
        return {.command = {.ok = true}, .selection = summary()};
      }
      return apply_sub_selection(request.miss_sub_selection_kind, nullptr);
    }
    if (selection_mode_uses_scene_document(selection_mode)) {
      return scene_pick_selection_result(kInvalidEntityId, selection_mode,
                                         request.policy, baseline,
                                         request.gesture_merge_key,
                                         request.expected_history_revision);
    }
    // A traversal miss must not clear existing selection or settle a draft.
    if (traversal_requested) {
      return {.command = {.ok = true}, .selection = summary()};
    }
    ModelingCommandResult command{.ok = true};
    if (!request.policy.preserve_selection_on_miss &&
        click_selection_edit(request.policy) ==
            ModelingSelectionEdit::Replace &&
        (!modeling_.operation_preview_active() ||
         requires_selection_settlement())) {
      const auto settled = begin_selection_edit();
      if (!settled.ok) {
        return {.command = settled, .selection = summary()};
      }
      command = modeling_.clear_selection();
      command.changed = command.changed || settled.changed;
    }
    return {.command = std::move(command), .selection = summary()};
  }

  const ResolvedSelectionTarget &winner = *request.winner;
  const bool scene_domain =
      selection_mode_uses_scene_document(selection_mode);
  if ((winner.domain == ResolvedSelectionDomain::SceneEntity &&
       !scene_domain) ||
      (winner.domain == ResolvedSelectionDomain::ModelingComponent &&
       scene_domain) ||
      (winner.domain == ResolvedSelectionDomain::RegisteredSubElement &&
       !scene_domain)) {
    return {
        .command = {
            .ok = false,
            .message =
                "Resolved selection target does not match the current selection domain."},
        .selection = summary(),
    };
  }
  if (winner.domain == ResolvedSelectionDomain::SceneEntity) {
    const EntityId resolved_entity =
        scene_target_resolver_ ? scene_target_resolver_(winner.entity)
                               : winner.entity;
    if (resolved_entity == kInvalidEntityId &&
        winner.source_occurrence != kInvalidEntityId) {
      // A surface can remain render-visible while being outside an active
      // instance edit root. Treat that hit-derived scope rejection exactly as
      // a spatial miss; an explicit target has no source occurrence and keeps
      // the checked invalid-target failure below.
      return scene_pick_selection_result(kInvalidEntityId, selection_mode,
                                         request.policy, baseline,
                                         request.gesture_merge_key,
                                         request.expected_history_revision);
    }
    const bool selectable = scene_target_selectable(resolved_entity);
    // Canonical definition members inherit a hidden storage root. During an
    // active instance edit, the scope policy admits them as edit-root targets
    // while their visible placement comes from the active occurrence.
    const bool visible_in_current_scope =
        scene_.is_effectively_visible(resolved_entity) ||
        (scene_scope_filter_ && selectable &&
         scene_.is_instance_definition_member(resolved_entity));
    if (!selectable || !visible_in_current_scope) {
      return {
          .command = {
              .ok = false,
              .message = "Resolved scene selection target is not selectable."},
          .selection = summary(),
      };
    }
    const EntityId target = selection_mode == ModelingSelectionMode::Group
                                ? nearest_group_ancestor(resolved_entity)
                                : resolved_entity;
    return scene_pick_selection_result(target, selection_mode, request.policy,
                                       baseline, request.gesture_merge_key,
                                       request.expected_history_revision);
  }

  if (winner.domain == ResolvedSelectionDomain::RegisteredSubElement) {
    return apply_sub_selection(winner.sub_selection_kind, &winner);
  }

  if (winner.modeling_object_id == kInvalidModelingObjectId ||
       ((selection_mode == ModelingSelectionMode::Vertex &&
         winner.component_kind != ModelingActiveSelectionKind::Vertex) ||
        (selection_mode == ModelingSelectionMode::Edge &&
         winner.component_kind != ModelingActiveSelectionKind::Edge) ||
        (selection_mode == ModelingSelectionMode::Face &&
         winner.component_kind != ModelingActiveSelectionKind::Face))) {
    return {
        .command = {.ok = false,
                    .message = "Resolved component selection target is invalid."},
        .selection = summary(),
    };
  }
  const auto component_entity =
      modeling_.entity_for_modeling_object(winner.modeling_object_id);
  if (!component_entity.has_value() ||
      !component_owner_selectable(*component_entity)) {
    return {.command = {.ok = false,
                        .message = "Component selection is outside the edit set."},
            .selection = summary()};
  }

  ModelingResolvedComponentSelectionRequest component{
      .modeling_object_id = winner.modeling_object_id,
      .mode = selection_mode,
      .kind = winner.component_kind,
      .vertex = winner.vertex,
      .edge = {winner.edge_a, winner.edge_b},
      .face = winner.face,
      .edit = traversal_requested
                  ? (request.policy.shift_pressed
                         ? ModelingSelectionEdit::Toggle
                         : ModelingSelectionEdit::Replace)
                  : click_selection_edit(request.policy),
      .click_count = request.policy.click_count,
      .edge_traversal = edge_traversal,
      .component_traversal = component_traversal,
      .preserve_selection_on_selected_hit =
          request.policy.preserve_selection_on_selected_hit,
      .preserve_existing_component_selection_on_hit =
          request.policy.preserve_existing_component_selection_on_hit,
  };
  std::optional<ModelingComponentTraversalSelectionPreparation>
      traversal_preparation;
  const std::uint64_t content_revision = modeling_.content_revision();
  const std::uint64_t selection_revision = modeling_.selection_revision();
  const ModelingSelectionBoxBaseline *component_baseline =
      baseline != nullptr && baseline->modeling_selection.has_value()
          ? &*baseline->modeling_selection : nullptr;
  if (traversal_requested || component.click_count > 1) {
    traversal_preparation = traversal_requested
        ? modeling_.prepare_component_traversal_selection(component, component_baseline)
        : modeling_.prepare_component_double_click_selection(component, component_baseline);
    if (!traversal_preparation->command.ok ||
        !traversal_preparation->command.changed) {
      return {.command = traversal_preparation->command, .selection = summary()};
    }
  } else if (requires_selection_settlement()) {
    ModelingComponentSelectionRequest validation{
        .modeling_object_id = winner.modeling_object_id,
        .mode = selection_mode,
    };
    if (selection_mode == ModelingSelectionMode::Vertex) {
      validation.vertices.push_back(winner.vertex);
    } else if (selection_mode == ModelingSelectionMode::Edge) {
      validation.edges.emplace_back(winner.edge_a, winner.edge_b);
    } else {
      validation.faces.push_back(winner.face);
    }
    const auto validated = modeling_.validate_component_selection(validation);
    if (!validated.ok) {
      return {.command = validated, .selection = summary()};
    }
  }
  const auto settled = begin_selection_edit();
  if (!settled.ok) return {.command = settled, .selection = summary()};
  ModelingCommandResult command =
      traversal_preparation.has_value() &&
              modeling_.content_revision() == content_revision &&
              modeling_.selection_revision() == selection_revision
          ? modeling_.set_component_selections(traversal_preparation->replacements)
          : modeling_.apply_resolved_component_selection(component, component_baseline);
  command = include_settlement_change(
      request.defer_selection_completion ? std::move(command)
                                          : finish_selection_edit(std::move(command)),
      settled);
  return {.command = std::move(command), .selection = summary()};
}

ResolvedSelectionApplyResult EditorSelectionService::apply_resolved_marquee(
    const ResolvedSelectionMarqueeRequest &request) {
  if (editor_mode_service_ != nullptr &&
      request.baseline.editor_mode_generation != editor_mode_service_->generation()) {
    return {.command = {.ok = false,
                        .message = "Resolved marquee edit context is stale."},
            .selection = summary()};
  }

  const auto preflight = selection_edit_preflight();
  if (!preflight.ok) {
    return {.command = preflight, .selection = summary()};
  }
  const ModelingSelectionMode current_mode = modeling_.selection_mode();
  if (request.mode != current_mode || request.baseline.mode != current_mode) {
    return {
        .command = {.ok = false,
                    .message = "Resolved marquee selection baseline is stale."},
        .selection = summary(),
    };
  }

  std::vector<EntityId> scene_targets;
  std::vector<EditorSubSelectionTarget> sub_targets;
  std::vector<ModelingComponentSelectionRequest> component_requests;
  std::unordered_map<int, std::size_t> component_request_by_object;
  std::unordered_set<EntityId> scene_membership;
  bool has_scene_targets = false;
  bool has_component_targets = false;
  bool has_sub_targets = false;
  bool invalid_component_target = false;
  bool invalid_sub_target = false;
  std::optional<EditorSubSelectionKind> sub_target_kind;
  std::optional<EntityId> sub_target_entity;
  std::optional<int> sub_target_modeling_object;
  for (const ResolvedSelectionTarget &target : request.targets) {
    if (target.domain == ResolvedSelectionDomain::SceneEntity) {
      has_scene_targets = true;
      if (!selection_mode_uses_scene_document(current_mode)) {
        continue;
      }
      const EntityId promoted = current_mode == ModelingSelectionMode::Group
                                    ? nearest_group_ancestor(target.entity)
                                    : target.entity;
      if (promoted != kInvalidEntityId &&
          scene_membership.insert(promoted).second) {
        scene_targets.push_back(promoted);
      }
      continue;
    }
    if (target.domain == ResolvedSelectionDomain::RegisteredSubElement) {
      has_sub_targets = true;
      if (!target.sub_element.has_value() ||
          target.sub_selection_kind == EditorSubSelectionKind::None) {
        invalid_sub_target = true;
        continue;
      }
      if (!sub_target_kind.has_value()) {
        sub_target_kind = target.sub_selection_kind;
        sub_target_entity = target.entity;
        if (target.modeling_object_id != kInvalidModelingObjectId) {
          sub_target_modeling_object = target.modeling_object_id;
        }
      } else if (*sub_target_kind != target.sub_selection_kind ||
                 *sub_target_entity != target.entity ||
                 (target.modeling_object_id != kInvalidModelingObjectId &&
                  sub_target_modeling_object.has_value() &&
                  *sub_target_modeling_object != target.modeling_object_id)) {
        invalid_sub_target = true;
      } else if (target.modeling_object_id != kInvalidModelingObjectId &&
                 !sub_target_modeling_object.has_value()) {
        sub_target_modeling_object = target.modeling_object_id;
      }
      sub_targets.push_back(*target.sub_element);
      continue;
    }
    if (target.domain != ResolvedSelectionDomain::ModelingComponent) {
      continue;
    }
    has_component_targets = true;
    const bool kind_matches_mode =
        (current_mode == ModelingSelectionMode::Vertex &&
         target.component_kind == ModelingActiveSelectionKind::Vertex) ||
        (current_mode == ModelingSelectionMode::Edge &&
         target.component_kind == ModelingActiveSelectionKind::Edge) ||
        (current_mode == ModelingSelectionMode::Face &&
         target.component_kind == ModelingActiveSelectionKind::Face);
    if (target.modeling_object_id == kInvalidModelingObjectId ||
        !kind_matches_mode ||
        !component_owner_selectable(modeling_.entity_for_modeling_object(
            target.modeling_object_id).value_or(kInvalidEntityId))) {
      invalid_component_target = true;
      continue;
    }
    auto [found, inserted] = component_request_by_object.emplace(
        target.modeling_object_id, component_requests.size());
    if (inserted) {
      component_requests.push_back({
          .modeling_object_id = target.modeling_object_id,
          .mode = current_mode,
          .edit = request.edit,
      });
    }
    append_component_target(component_requests[found->second], target);
  }

  const bool scene_domain = selection_mode_uses_scene_document(current_mode);
  if (invalid_component_target) {
    return {
        .command = {.ok = false,
                    .message = "Resolved marquee contains an invalid component target."},
        .selection = summary(),
    };
  }
  if (invalid_sub_target) {
    return {
        .command = {.ok = false,
                    .message = "Resolved marquee contains an invalid or mixed sub-element owner."},
        .selection = summary(),
    };
  }
  if (scene_domain && has_component_targets) {
    return {
        .command = {.ok = false,
                    .message = "Resolved marquee mixes scene and component selection domains."},
        .selection = summary(),
    };
  }
  if (!scene_domain &&
      (has_scene_targets || has_sub_targets ||
       request.baseline.sub_selection.has_value())) {
    return {
        .command = {.ok = false,
                    .message = "Resolved marquee mixes component and registered sub-element domains."},
        .selection = summary(),
    };
  }
  if (!scene_domain &&
      (!request.baseline.modeling_selection.has_value() ||
       !request.baseline.modeling_selection->valid())) {
    return {
        .command = {.ok = false,
                    .message = "Resolved component marquee baseline is stale."},
        .selection = summary(),
    };
  }

  std::optional<EditorSubSelectionBaseline> provider_rollback;
  ModelingCommandResult provider_result{.ok = true};
  const bool provider_edit_requested =
      scene_domain && (!sub_targets.empty() ||
                       request.baseline.sub_selection.has_value() ||
                       current_sub_selection_snapshot().selected_count > 0u);
  if (provider_edit_requested) {
    if (sub_selection_provider_ == nullptr) {
      return {
          .command = {.ok = false,
                      .message = "Sub-element selection provider is unavailable."},
          .selection = summary(),
      };
    }
    sub_selection_provider_->synchronize_sub_selection_scope();
    const EditorSubSelectionSnapshot provider =
        current_sub_selection_snapshot();
    provider_rollback =
        sub_selection_provider_->capture_sub_selection_baseline();
    const bool target_scope_matches =
        !sub_target_kind.has_value() ||
        (provider.kind == *sub_target_kind &&
         provider.entity == *sub_target_entity &&
         (!sub_target_modeling_object.has_value() ||
          provider.modeling_object_id == *sub_target_modeling_object));
    const bool baseline_scope_matches =
        !request.baseline.sub_selection.has_value() ||
        (provider.kind == request.baseline.sub_selection->kind &&
         provider.entity == request.baseline.sub_selection->entity &&
         provider.modeling_object_id ==
             request.baseline.sub_selection->modeling_object_id);
    if (!provider_rollback->valid() || !target_scope_matches ||
        !baseline_scope_matches) {
      return {
          .command = {.ok = false,
                      .message = "Sub-element selection owner is stale."},
          .selection = summary(),
      };
    }
    const EditorSubSelectionBaseline *sub_baseline =
        request.baseline.sub_selection.has_value()
            ? &*request.baseline.sub_selection
            : nullptr;
    provider_result = sub_selection_provider_->edit_sub_elements(
        sub_targets, to_sub_selection_edit(request.edit), sub_baseline,
        std::nullopt);
    summary_cache_valid_ = false;
    if (!provider_result.ok) {
      ModelingCommandResult failure = restore_sub_selection_after_failure(
          *provider_rollback,
          provider_result.message.empty()
              ? "Sub-element marquee selection edit failed."
              : provider_result.message);
      return {.command = std::move(failure), .selection = summary()};
    }
  }

  ResolvedSelectionApplyResult base_result{
      .command = {.ok = true},
      .selection = summary(),
  };
  if (scene_domain) {
    if (!sub_targets.empty() && request.edit == ModelingSelectionEdit::Replace &&
        request.baseline.sub_selection.has_value()) {
      const EntityId owner = request.baseline.sub_selection->entity;
      if (owner != kInvalidEntityId && scene_membership.insert(owner).second) {
        scene_targets.push_back(owner);
      }
    }
    base_result = scene_box_selection_result(
        scene_targets, current_mode, request.edit, &request.baseline);
  } else {
    if (requires_selection_settlement()) {
      for (const auto &component : component_requests) {
        const auto validated = modeling_.validate_component_selection(component);
        if (!validated.ok) {
          return {.command = validated, .selection = summary()};
        }
      }
    }
    const auto settled = begin_selection_edit();
    if (!settled.ok) return {.command = settled, .selection = summary()};
    base_result.command = modeling_.apply_resolved_component_selections(
        component_requests, request.edit,
        *request.baseline.modeling_selection);
    base_result.command.changed = base_result.command.changed || settled.changed;
    base_result.selection = summary();
  }

  if (!base_result.command.ok && provider_rollback.has_value()) {
    ModelingCommandResult failure = restore_sub_selection_after_failure(
        *provider_rollback,
        base_result.command.message.empty()
            ? "Canonical marquee selection edit failed."
            : base_result.command.message);
    if (base_result.command.changed) {
      failure.changed = true;
      failure.message +=
          " The base selection owner reported a partial failed mutation.";
    }
    return {.command = std::move(failure), .selection = summary()};
  }
  base_result.command.changed =
      base_result.command.changed || provider_result.changed;
  base_result.command = finish_selection_edit(std::move(base_result.command));
  summary_cache_valid_ = false;
  base_result.selection = summary();
  return base_result;
}

ModelingCommandResult EditorSelectionService::preview_resolved_hover(
    std::optional<ResolvedSelectionTarget> target) {
  return publish_resolved_hover(std::move(target), false);
}

ModelingCommandResult EditorSelectionService::preview_spatial_hover(
    std::optional<ResolvedSelectionTarget> target,
    bool selected_modifier_pressed) {
  return publish_resolved_hover(std::move(target), false,
                                selected_modifier_pressed);
}

ModelingCommandResult EditorSelectionService::publish_resolved_hover(
    std::optional<ResolvedSelectionTarget> target,
    bool explicit_modeling_element_request,
    bool selected_modifier_pressed, bool face_plane_hover,
    const ModelingHoverSummary *modeling_preview) {
  if (target.has_value() &&
      target->domain == ResolvedSelectionDomain::SceneEntity &&
      modeling_.selection_mode() == ModelingSelectionMode::Group &&
      !explicit_modeling_element_request) {
    const EntityId group = nearest_group_ancestor(target->entity);
    if (group == kInvalidEntityId) {
      target.reset();
    } else {
      target->entity = group;
      target->modeling_object_id = kInvalidModelingObjectId;
      target->component_kind = ModelingActiveSelectionKind::None;
    }
  }

  if (target.has_value() &&
      target->domain == ResolvedSelectionDomain::ModelingComponent) {
    const ModelingSelectionMode mode = modeling_.selection_mode();
    const bool kind_matches_mode =
        (mode == ModelingSelectionMode::Vertex &&
         target->component_kind == ModelingActiveSelectionKind::Vertex) ||
        (mode == ModelingSelectionMode::Edge &&
         target->component_kind == ModelingActiveSelectionKind::Edge) ||
        (mode == ModelingSelectionMode::Face &&
         target->component_kind == ModelingActiveSelectionKind::Face);
    if ((!kind_matches_mode && !explicit_modeling_element_request) ||
        target->modeling_object_id == kInvalidModelingObjectId) {
      return {.ok = false,
              .changed = false,
              .message = "Resolved component hover target is invalid."};
    }
  }

  if (target.has_value() &&
      target->domain == ResolvedSelectionDomain::ModelingComponent &&
      !component_owner_selectable(modeling_.entity_for_modeling_object(
          target->modeling_object_id).value_or(kInvalidEntityId))) {
    return {.ok = false, .message = "Component hover is outside the edit set."};
  }

  const std::optional<ResolvedSelectionTarget> resolved_before =
      resolved_hover_target_;
  const ModelingHoverSummary modeling_before = modeling_.selection_hover_summary();
  std::optional<EditorSubSelectionBaseline> provider_before;
  EditorSubSelectionSnapshot provider_snapshot;
  if (sub_selection_provider_ != nullptr) {
    sub_selection_provider_->synchronize_sub_selection_scope();
    provider_snapshot = current_sub_selection_snapshot();
    if (provider_snapshot.kind != EditorSubSelectionKind::None) {
      EditorSubSelectionBaseline baseline =
          sub_selection_provider_->capture_sub_selection_baseline();
      if (baseline.valid()) {
        provider_before = std::move(baseline);
      }
    }
  }

  if (target.has_value() &&
      target->domain == ResolvedSelectionDomain::RegisteredSubElement) {
    if (sub_selection_provider_ == nullptr || !target->sub_element.has_value() ||
        target->sub_selection_kind == EditorSubSelectionKind::None ||
        !provider_before.has_value() ||
        provider_snapshot.kind != target->sub_selection_kind ||
        provider_snapshot.entity != target->entity ||
        (target->modeling_object_id != kInvalidModelingObjectId &&
         provider_snapshot.modeling_object_id != target->modeling_object_id)) {
      return {.ok = false,
              .changed = false,
              .message = "Sub-element hover owner is stale."};
    }
  }

  const auto restore_modeling_hover = [&]() {
    const ModelingHoverTarget &before = modeling_before.target;
    if (before.kind == ModelingActiveSelectionKind::None &&
        modeling_before.candidate_object_id <= 0) {
      return modeling_.clear_selection_hover();
    }
    return modeling_.set_selection_hover({
        .modeling_object_id = before.object_id,
        .candidate_object_id = modeling_before.candidate_object_id,
        .kind = before.kind,
        .vertex_id = before.vertex_id,
        .edge = {before.edge_a, before.edge_b},
        .face_id = before.face_id,
        .selected_modifier_pressed = modeling_before.selected_modifier_pressed,
        .face_plane_hover = modeling_before.face_plane_hover,
    });
  };
  const auto rollback_failure = [&](std::string message,
                                    bool provider_may_have_changed,
                                    bool modeling_may_have_changed) {
    bool rollback_ok = true;
    std::string rollback_message;
    if (provider_may_have_changed) {
      if (!provider_before.has_value() || sub_selection_provider_ == nullptr) {
        rollback_ok = false;
        rollback_message = " Provider hover rollback is unavailable.";
      } else {
        const ModelingCommandResult restored =
            sub_selection_provider_->restore_sub_selection_baseline(
                *provider_before);
        if (!restored.ok) {
          rollback_ok = false;
          rollback_message = " Provider hover rollback failed: " +
                             restored.message;
        }
      }
    }
    if (modeling_may_have_changed) {
      const ModelingCommandResult restored = restore_modeling_hover();
      if (!restored.ok) {
        rollback_ok = false;
        rollback_message += " Modeling hover rollback failed: " +
                            restored.message;
      }
    }
    resolved_hover_target_ = resolved_before;
    return ModelingCommandResult{
        .ok = false,
        .changed = !rollback_ok,
        .message = std::move(message) + rollback_message,
    };
  };

  ModelingCommandResult provider{.ok = true};
  if (sub_selection_provider_ != nullptr && provider_before.has_value()) {
    provider = sub_selection_provider_->preview_sub_element_hover(
        target.has_value() &&
                target->domain ==
                    ResolvedSelectionDomain::RegisteredSubElement
            ? target->sub_element
            : std::nullopt);
    if (!provider.ok) {
      return rollback_failure(
          provider.message.empty() ? "Sub-element hover edit failed."
                                   : provider.message,
          provider.changed, false);
    }
  }

  ModelingCommandResult modeling{.ok = true};
  const bool modeling_hover_target =
      target.has_value() &&
      (target->domain == ResolvedSelectionDomain::ModelingComponent ||
       (target->domain == ResolvedSelectionDomain::SceneEntity &&
        target->modeling_object_id != kInvalidModelingObjectId));
  if (modeling_preview != nullptr) {
    // Publish the query result through the same selection authority.
    const ModelingHoverTarget &preview_target = modeling_preview->target;
    modeling = modeling_.set_selection_hover({
        .modeling_object_id = preview_target.object_id,
        .candidate_object_id = modeling_preview->candidate_object_id,
        .kind = preview_target.kind,
        .vertex_id = preview_target.vertex_id,
        .edge = {preview_target.edge_a, preview_target.edge_b},
        .face_id = preview_target.face_id,
        .selected_modifier_pressed =
            modeling_preview->selected_modifier_pressed,
        .face_plane_hover = modeling_preview->face_plane_hover,
    });
  } else if (modeling_hover_target) {
    modeling = modeling_.set_selection_hover({
        .modeling_object_id = target->modeling_object_id,
        .kind = target->domain == ResolvedSelectionDomain::SceneEntity
                    ? ModelingActiveSelectionKind::Object
                    : target->component_kind,
        .vertex_id = target->vertex,
        .edge = {target->edge_a, target->edge_b},
        .face_id = target->face,
        .selected_modifier_pressed = selected_modifier_pressed,
        .face_plane_hover = face_plane_hover,
    });
  } else {
    modeling = modeling_.clear_selection_hover();
  }
  if (!modeling.ok) {
    return rollback_failure(
        modeling.message.empty() ? "Modeling hover edit failed."
                                 : modeling.message,
        provider.changed, modeling.changed);
  }

  const bool changed = resolved_before != target || provider.changed ||
                       modeling.changed;
  resolved_hover_target_ = std::move(target);
  return {.ok = true, .changed = changed};
}

DocumentCommandResult EditorSelectionService::pick_scene_entity(
    EntityId entity, const ModelingPickSelectRequest &request) {
  const ModelingSelectionMode selection_mode = modeling_.selection_mode();
  if (!selection_mode_uses_scene_document(selection_mode)) {
    return document_command_invalid_request_result(
        "Scene entity picking requires Mesh or Group selection mode.");
  }
  if (!scene_target_selectable(entity) ||
      !scene_.is_effectively_visible(entity)) {
    return document_command_invalid_request_result(
        "The picked entity is not visible and editable in the current scene scope.");
  }
  const EntityId target = selection_mode == ModelingSelectionMode::Group
                              ? nearest_group_ancestor(entity)
                              : entity;
  ResolvedSelectionApplyResult applied = scene_pick_selection_result(
      target, selection_mode, request);
  return {.ok = applied.command.ok,
          .changed = applied.command.changed,
          .message = std::move(applied.command.message)};
}

EntityId
EditorSelectionService::group_promoted_scene_target(EntityId entity) const {
  if (scene_target_resolver_) {
    entity = scene_target_resolver_(entity);
  }
  if (entity == kInvalidEntityId || !scene_.exists(entity) ||
      !scene_target_selectable(entity) ||
      !scene_.is_effectively_visible(entity) ||
      scene_.is_effectively_locked(entity)) {
    return kInvalidEntityId;
  }
  const EntityId group = nearest_group_ancestor(entity);
  return group != kInvalidEntityId ? group : entity;
}

ModelingSelectionSummary
EditorSelectionService::box_select(const ModelingBoxSelectRequest &request) {
  return box_select(request, nullptr);
}

ModelingSelectionSummary
EditorSelectionService::box_select(const ModelingBoxSelectRequest &request,
                                   const EditorSelectionBoxBaseline &baseline) {
  return box_select(request, &baseline);
}

ModelingSelectionSummary
EditorSelectionService::box_select(const ModelingBoxSelectRequest &request,
                                   const EditorSelectionBoxBaseline *baseline) {
  QDR_PROFILE_SCOPE("Editor box selection route");
  if (!selection_edit_preflight().ok) {
    return summary();
  }
  const ModelingSelectionMode selection_mode = modeling_.selection_mode();
  if (selection_mode_uses_scene_document(selection_mode)) {
    const std::vector<EntityId> mesh_entities = [&]() {
      QDR_PROFILE_SCOPE("Box selection collect scene object hits");
      return modeling_.box_pick_object_entities(request);
    }();
    std::vector<EntityId> entities;
    {
      QDR_PROFILE_SCOPE("Box selection map group or object targets");
      std::unordered_set<EntityId> membership;
      membership.reserve(mesh_entities.size());
      for (const EntityId mesh_entity : mesh_entities) {
        const EntityId entity = selection_mode == ModelingSelectionMode::Group
                                    ? nearest_group_ancestor(mesh_entity)
                                    : mesh_entity;
        if (entity != kInvalidEntityId &&
            membership.insert(entity).second) {
          entities.push_back(entity);
        }
      }
    }
    {
      QDR_PROFILE_SCOPE("Box selection publish scene selection");
      return scene_box_selection(entities, selection_mode, request.edit,
                                 baseline);
    }
  }

  {
    QDR_PROFILE_SCOPE("Box selection publish modeling selection");
    if (baseline != nullptr && baseline->mode != selection_mode) {
      return summary();
    }
    if (!begin_selection_edit().ok) return summary();
    if (baseline != nullptr && baseline->modeling_selection.has_value() &&
        baseline->modeling_selection->valid() &&
        baseline->mode == modeling_.selection_mode()) {
      return modeling_.box_select(request, *baseline->modeling_selection);
    }
    return modeling_.box_select(request);
  }
}

ModelingCommandResult
EditorSelectionService::update_hover(const ModelingHoverRequest &request) {
  const ModelingHoverSummary preview =
      modeling_.preview_selection_hover(request);
  return publish_resolved_hover(
      resolved_modeling_hover_target(preview.target, modeling_), true,
      request.selected_modifier_pressed, request.face_plane_hover,
      &preview);
}

ModelingCommandResult
EditorSelectionService::set_hover(const ModelingDirectHoverRequest &request) {
  return publish_resolved_hover(
      resolved_modeling_hover_target(
          ModelingHoverTarget{
              .kind = request.kind,
              .object_id = request.modeling_object_id,
              .vertex_id = request.vertex_id,
              .edge_a = request.edge.first,
              .edge_b = request.edge.second,
              .face_id = request.face_id,
          },
          modeling_),
      true, request.selected_modifier_pressed, request.face_plane_hover);
}

ModelingHoverSummary EditorSelectionService::preview_hover(
    const ModelingHoverRequest &request) const {
  return modeling_.preview_selection_hover(request);
}

ModelingCommandResult EditorSelectionService::clear_hover() {
  return preview_resolved_hover(std::nullopt);
}

EditorSelectionTarget EditorSelectionService::target_from_summary(
    const ModelingSelectionSummary &summary) const {
  if (summary.selected_sub_element_count > 0u) {
    const EditorSubSelectionSnapshot sub_selection =
        current_sub_selection_snapshot();
    if (sub_selection.active.has_value()) {
      return {
          .kind = EditorSelectionTargetKind::SubElement,
          .entity = sub_selection.entity,
          .modeling_object_id = sub_selection.modeling_object_id,
          .sub_selection_kind = sub_selection.kind,
          .sub_element = sub_selection.active,
      };
    }
  }
  auto active_scene_target = [&]() {
    const EntityId active = scene_.primary_selection();
    if (active == kInvalidEntityId || !scene_.exists(active)) {
      return EditorSelectionTarget{};
    }
    return EditorSelectionTarget{
        .kind = scene_.kind(active) == EntityKind::Empty
                    ? EditorSelectionTargetKind::Group
                    : EditorSelectionTargetKind::Object,
        .entity = active,
        .modeling_object_id = scene_.modeling_object(active) != nullptr
                                  ? scene_.modeling_object(active)->object_id
                                  : kInvalidModelingObjectId,
    };
  };

  if (!summary.has_selection) {
    return {};
  }

  if (summary.mode == ModelingSelectionMode::Group) {
    EditorSelectionTarget target = active_scene_target();
    target.kind = target.entity == kInvalidEntityId
                      ? EditorSelectionTargetKind::None
                      : EditorSelectionTargetKind::Group;
    return target;
  }

  const auto active_entity =
      summary.active_object_id > 0
          ? modeling_.entity_for_modeling_object(summary.active_object_id)
          : std::optional<EntityId>{};
  const EntityId entity = active_entity.value_or(scene_.primary_selection());

  switch (summary.active_kind) {
  case ModelingActiveSelectionKind::Object:
    return entity == kInvalidEntityId
               ? active_scene_target()
               : EditorSelectionTarget{
                     .kind = EditorSelectionTargetKind::Object,
                     .entity = entity,
                     .modeling_object_id = summary.active_object_id,
                 };
  case ModelingActiveSelectionKind::Vertex:
    return {
        .kind = EditorSelectionTargetKind::Vertex,
        .entity = entity,
        .modeling_object_id = summary.active_object_id,
        .vertex = summary.active_vertex_id,
    };
  case ModelingActiveSelectionKind::Edge:
    return {
        .kind = EditorSelectionTargetKind::Edge,
        .entity = entity,
        .modeling_object_id = summary.active_object_id,
        .edge_a = summary.active_edge_a,
        .edge_b = summary.active_edge_b,
    };
  case ModelingActiveSelectionKind::Face:
    return {
        .kind = EditorSelectionTargetKind::Face,
        .entity = entity,
        .modeling_object_id = summary.active_object_id,
        .face = summary.active_face_id,
    };
  case ModelingActiveSelectionKind::Light:
  case ModelingActiveSelectionKind::None:
    break;
  }

  return active_scene_target();
}

ResolvedSelectionApplyResult
EditorSelectionService::scene_pick_selection_result(
    EntityId target_entity, ModelingSelectionMode mode,
    const ModelingPickSelectRequest &request,
    const EditorSelectionBoxBaseline *baseline,
    std::uint64_t gesture_merge_key,
    std::optional<std::uint64_t> expected_history_revision) {
  if (!scene_target_selectable(target_entity)) {
    target_entity = kInvalidEntityId;
  }
  const bool remove_selection =
      request.control_pressed || request.command_pressed;
  const bool add_selection = !remove_selection && request.shift_pressed;
  const SceneSelectionEdit edit =
      remove_selection ? SceneSelectionEdit::Remove
                       : (add_selection ? SceneSelectionEdit::Add
                                        : SceneSelectionEdit::Replace);
  const bool target_selected = target_entity != kInvalidEntityId &&
                               scene_.selection(target_entity) != nullptr &&
                               scene_.selection(target_entity)->selected;
  const bool preserve_selected_hit =
      request.preserve_selection_on_selected_hit && target_selected &&
      !add_selection && !remove_selection;
  const std::vector<EntityId> previous_selected =
      baseline != nullptr ? baseline->scene_selection : scene_.selected_entities();
  const EntityId previous_active =
      baseline != nullptr ? baseline->active_entity : scene_.primary_selection();
  SceneSelectionState resolved{
      .selected = previous_selected,
      .active = previous_active,
  };
  ModelingCommandResult command{.ok = true};

  if (target_entity == kInvalidEntityId) {
    if (edit == SceneSelectionEdit::Replace &&
        !request.depth_cycle_requested(mode) &&
        !request.preserve_selection_on_miss) {
      resolved.selected.clear();
      resolved.active = kInvalidEntityId;
    }
  } else if (!preserve_selected_hit) {
    const std::array<EntityId, 1> edited_entities{{target_entity}};
    resolved = resolve_scene_selection_edit(previous_selected, previous_active,
                                            edited_entities, edit);
  }

  if (resolved.selected != scene_.selected_entities() ||
      resolved.active != scene_.primary_selection()) {
    command = modeling_command_result_from_document(set_scene_selection(
        std::span<const EntityId>(resolved.selected.data(),
                                  resolved.selected.size()),
        resolved.active, gesture_merge_key, expected_history_revision));
  }

  const ModelingSelectionSummary current = summary();
  return {
      .command = std::move(command),
      .selection = current.mode == mode ? current : scene_summary_for(mode),
  };
}

ModelingSelectionSummary EditorSelectionService::scene_pick_selection(
    EntityId target_entity, ModelingSelectionMode mode,
    const ModelingPickSelectRequest &request,
    const EditorSelectionBoxBaseline *baseline) {
  return scene_pick_selection_result(target_entity, mode, request, baseline)
      .selection;
}

ResolvedSelectionApplyResult
EditorSelectionService::scene_box_selection_result(
    std::span<const EntityId> target_entities, ModelingSelectionMode mode,
    ModelingSelectionEdit edit, const EditorSelectionBoxBaseline *baseline) {
  QDR_PROFILE_SCOPE("Box selection apply scene state");
  const std::vector<EntityId> previous_selected =
      (baseline != nullptr && baseline->mode == mode)
          ? baseline->scene_selection
          : scene_.selected_entities();
  const EntityId previous_active =
      (baseline != nullptr && baseline->mode == mode)
          ? baseline->active_entity
          : scene_.primary_selection();
  std::vector<EntityId> selectable;
  selectable.reserve(target_entities.size());
  std::unordered_set<EntityId> membership;
  membership.reserve(target_entities.size());
  for (const EntityId entity : target_entities) {
    if (scene_target_selectable(entity) &&
        membership.insert(entity).second) {
      selectable.push_back(entity);
    }
  }
  const SceneSelectionState resolved =
      resolve_scene_selection_edit(previous_selected, previous_active,
                                   selectable, to_scene_selection_edit(edit));
  ModelingCommandResult command{.ok = true};
  if (resolved.selected != scene_.selected_entities() ||
      resolved.active != scene_.primary_selection()) {
    QDR_PROFILE_SCOPE("Box selection commit scene selection");
    command = modeling_command_result_from_document(set_scene_selection(
        std::span<const EntityId>(resolved.selected.data(),
                                  resolved.selected.size()),
        resolved.active));
  }

  const ModelingSelectionSummary current = summary();
  return {
      .command = std::move(command),
      .selection = current.mode == mode ? current : scene_summary_for(mode),
  };
}

ModelingSelectionSummary EditorSelectionService::scene_box_selection(
    std::span<const EntityId> target_entities, ModelingSelectionMode mode,
    ModelingSelectionEdit edit, const EditorSelectionBoxBaseline *baseline) {
  return scene_box_selection_result(target_entities, mode, edit, baseline)
      .selection;
}

EntityId EditorSelectionService::nearest_group_ancestor(EntityId entity) const {
  if (entity != kInvalidEntityId && scene_.exists(entity) &&
      scene_.kind(entity) == EntityKind::Empty) {
    return entity;
  }
  EntityId parent = scene_.parent(entity);
  while (parent != kInvalidEntityId) {
    if (scene_.kind(parent) == EntityKind::Empty) {
      return parent;
    }
    parent = scene_.parent(parent);
  }
  return kInvalidEntityId;
}

ModelingSelectionSummary
EditorSelectionService::scene_summary_for(ModelingSelectionMode mode) const {
  return make_scene_selection_summary(
      scene_, mode, modeling_.selection_summary().content_revision);
}

} // namespace quader
