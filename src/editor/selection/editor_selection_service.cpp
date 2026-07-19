////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/selection/editor_selection_service.hpp"

#include "editor/selection/scene_selection_summary.hpp"

#include "diagnostics/profile.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <array>
#include <optional>
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

ModelingCommandResult
modeling_command_result_from_document(DocumentCommandResult result) {
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = std::move(result.message),
  };
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
  const std::uint64_t scene_revision = scene_.revision();
  const std::uint64_t modeling_content_revision =
      modeling_.content_revision();
  const std::uint64_t modeling_selection_revision =
      modeling_.selection_revision();
  const bool preview_active = modeling_.can_cancel_active_preview_for_undo();
  if (summary_cache_valid_ && !preview_active &&
      !summary_cache_preview_active_ &&
      summary_cache_scene_revision_ == scene_revision &&
      summary_cache_modeling_content_revision_ == modeling_content_revision &&
      summary_cache_modeling_selection_revision_ ==
          modeling_selection_revision) {
    return summary_cache_resolved_;
  }

  const ModelingSelectionSummary current = modeling_.selection_summary();
  if (summary_cache_valid_ &&
      summary_cache_scene_revision_ == scene_revision &&
      same_selection_summary(summary_cache_modeling_, current)) {
    return summary_cache_resolved_;
  }

  const std::vector<EntityId> selected = scene_.selected_entities();
  ModelingSelectionSummary resolved = current;
  if (!selected.empty() && has_scene_only_selection(scene_, selected)) {
    resolved = make_scene_selection_summary(
        scene_,
        current.mode == ModelingSelectionMode::Group
            ? ModelingSelectionMode::Group
            : ModelingSelectionMode::Object,
        current.content_revision);
  } else if (current.has_selection &&
             current.active_object_id != kInvalidModelingObjectId) {
    const std::optional<EntityId> active_entity =
        modeling_.entity_for_modeling_object(current.active_object_id);
    if (active_entity.has_value() &&
        scene_.is_effectively_locked(*active_entity)) {
      resolved = make_scene_selection_summary(scene_, current.mode,
                                              current.content_revision);
    }
  }
  summary_cache_scene_revision_ = scene_revision;
  summary_cache_preview_active_ = preview_active;
  summary_cache_modeling_content_revision_ = modeling_content_revision;
  summary_cache_modeling_selection_revision_ = modeling_selection_revision;
  summary_cache_modeling_ = current;
  summary_cache_resolved_ = resolved;
  summary_cache_valid_ = true;
  return resolved;
}

ModelingHoverSummary EditorSelectionService::hover_summary() const {
  return modeling_.selection_hover_summary();
}

ModelingSelectionMode EditorSelectionService::mode() const {
  return modeling_.selection_mode();
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

DocumentCommandResult EditorSelectionService::apply_scene_selection_state(
    SceneSelectionState selection) {
  if (scene_selection_history_suppression_provider_ &&
      scene_selection_history_suppression_provider_()) {
    return apply_scene_selection(scene_, selection,
                                 SceneSelectionApplicationMode::RuntimeSync)
               ? document_command_changed_result()
               : document_command_unchanged_result();
  }
  return document_commands_.select_entities(
      std::move(selection.selected), selection.active,
      SceneSelectionApplicationMode::UndoableCommand);
}

DocumentCommandResult
EditorSelectionService::set_scene_selection(std::span<const EntityId> entities,
                                            EntityId active) {
  QDR_PROFILE_FUNCTION();
  const ModelingSelectionSummary before = modeling_.selection_summary();
  SceneSelectionState selection;
  if (scene_target_resolver_ || scene_scope_filter_) {
    std::vector<EntityId> resolved;
    resolved.reserve(entities.size());
    EntityId resolved_active = kInvalidEntityId;
    for (const EntityId entity : entities) {
      const EntityId target = scene_target_resolver_
                                  ? scene_target_resolver_(entity)
                                  : entity;
      if (target == kInvalidEntityId ||
          (scene_scope_filter_ && !scene_scope_filter_(target)) ||
          std::find(resolved.begin(), resolved.end(), target) !=
              resolved.end()) {
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
      apply_scene_selection_state(std::move(selection));
  if (result.ok && modeling_.selection_mode() != ModelingSelectionMode::Group) {
    modeling_.project_object_selection_from_scene(true);
  }
  return merge_modeling_selection_change(result, before,
                                         modeling_.selection_summary());
}

DocumentCommandResult EditorSelectionService::set_inspection_scene_selection(
    std::span<const EntityId> entities, EntityId active) {
  QDR_PROFILE_FUNCTION();
  const ModelingSelectionSummary before = modeling_.selection_summary();
  SceneSelectionState selection = make_scene_selection(scene_, entities, active);
  DocumentCommandResult result =
      apply_scene_selection_state(std::move(selection));
  if (result.ok && modeling_.selection_mode() != ModelingSelectionMode::Group) {
    modeling_.project_object_selection_from_scene(true);
  }
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
  const std::optional<EntityId> entity =
      modeling_.entity_for_modeling_object(request.modeling_object_id);
  if (!entity.has_value() || !scene_target_selectable(*entity)) {
    return {.ok = false,
            .changed = false,
            .message = "The component owner is not visible in the current "
                       "viewport scope."};
  }
  return modeling_.set_component_selection(request);
}

ModelingCommandResult
EditorSelectionService::set_mode(ModelingSelectionMode mode) {
  return modeling_.set_selection_mode(mode);
}

bool EditorSelectionService::scene_target_selectable(EntityId entity) const {
  if (entity == kInvalidEntityId || !scene_.exists(entity)) {
    return false;
  }
  return scene_scope_filter_ ? scene_scope_filter_(entity)
                             : scene_entity_selectable_for_user_selection(
                                   scene_, entity);
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
  const ModelingSelectionMode selection_mode = modeling_.selection_mode();
  if (!selection_mode_uses_scene_document(selection_mode)) {
    QDR_PROFILE_SCOPE("Selection select all runtime components");
    return modeling_.select_all();
  }

  std::vector<EntityId> selected;
  {
    QDR_PROFILE_SCOPE("Selection collect scene select all entities");
    selected = scene_bulk_candidates(selection_mode);
  }
  const EntityId active = selected.empty() ? kInvalidEntityId : selected.back();
  return modeling_command_result_from_document(
      set_scene_selection(selected, active));
}

ModelingCommandResult EditorSelectionService::invert() {
  const ModelingSelectionMode selection_mode = modeling_.selection_mode();
  if (!selection_mode_uses_scene_document(selection_mode)) {
    return modeling_.invert_selection();
  }

  const std::vector<EntityId> current = scene_.selected_entities();
  std::vector<EntityId> selected;
  for (const EntityId entity : scene_bulk_candidates(selection_mode)) {
    if (std::find(current.begin(), current.end(), entity) == current.end()) {
      selected.push_back(entity);
    }
  }
  const EntityId active = selected.empty() ? kInvalidEntityId : selected.back();
  return modeling_command_result_from_document(
      set_scene_selection(selected, active));
}

ModelingCommandResult EditorSelectionService::clear() {
  const ModelingSelectionMode selection_mode = modeling_.selection_mode();
  if (!selection_mode_uses_scene_document(selection_mode)) {
    return modeling_.clear_selection();
  }

  return modeling_command_result_from_document(
      set_scene_selection(std::span<const EntityId>{}, kInvalidEntityId));
}

ModelingCommandResult EditorSelectionService::apply_component_selection_command(
    ModelingComponentSelectionCommand command,
    std::span<const ModelingSelectionMode> allowed_modes) {
  const ModelingSelectionMode current_mode = modeling_.selection_mode();
  if (std::find(allowed_modes.begin(), allowed_modes.end(), current_mode) ==
      allowed_modes.end()) {
    return {
        .ok = false,
        .changed = false,
        .message = "The selection command is unavailable in the current mode.",
    };
  }
  return modeling_.apply_component_selection_command(command);
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
  if (modeling_.selection_mode() != ModelingSelectionMode::Object) {
    return {
        .ok = false,
        .changed = false,
        .message = "Select N-Gons is available only in Mesh mode.",
    };
  }

  std::vector<int> targets;
  std::optional<int> active_target;
  for (const EntityId entity : scene_.selected_entities()) {
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
  return modeling_.select_mesh_ngons(targets);
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
  baseline.mode = modeling_.selection_mode();
  baseline.scene_selection = scene_.selected_entities();
  baseline.active_entity = scene_.primary_selection();
  if (!selection_mode_uses_scene_document(baseline.mode)) {
    baseline.modeling_selection = modeling_.capture_box_selection_baseline();
  }
  return baseline;
}

ModelingSelectionSummary
EditorSelectionService::pick_select(const ModelingPickSelectRequest &request) {
  const ModelingSelectionMode selection_mode = modeling_.selection_mode();
  if (selection_mode_uses_scene_document(selection_mode)) {
    const std::optional<EntityId> hit_mesh =
        modeling_.pick_object_entity(request);
    const EntityId hit_entity = hit_mesh.value_or(kInvalidEntityId);
    return scene_pick_selection(selection_mode == ModelingSelectionMode::Group
                                    ? nearest_group_ancestor(hit_entity)
                                    : hit_entity,
                                selection_mode, request);
  }

  return modeling_.pick_select(request);
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
  const ModelingSelectionMode selection_mode = modeling_.selection_mode();
  if (selection_mode_uses_scene_document(selection_mode)) {
    const std::vector<EntityId> mesh_entities = [&]() {
      QDR_PROFILE_SCOPE("Box selection collect scene object hits");
      return modeling_.box_pick_object_entities(request);
    }();
    std::vector<EntityId> entities;
    {
      QDR_PROFILE_SCOPE("Box selection map group or object targets");
      for (const EntityId mesh_entity : mesh_entities) {
        const EntityId entity = selection_mode == ModelingSelectionMode::Group
                                    ? nearest_group_ancestor(mesh_entity)
                                    : mesh_entity;
        if (entity != kInvalidEntityId &&
            std::find(entities.begin(), entities.end(), entity) ==
                entities.end()) {
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
  return modeling_.update_selection_hover(request);
}

ModelingCommandResult
EditorSelectionService::set_hover(const ModelingDirectHoverRequest &request) {
  return modeling_.set_selection_hover(request);
}

ModelingHoverSummary EditorSelectionService::preview_hover(
    const ModelingHoverRequest &request) const {
  return modeling_.preview_selection_hover(request);
}

ModelingCommandResult EditorSelectionService::clear_hover() {
  return modeling_.clear_selection_hover();
}

EditorSelectionTarget EditorSelectionService::target_from_summary(
    const ModelingSelectionSummary &summary) const {
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

ModelingSelectionSummary EditorSelectionService::scene_pick_selection(
    EntityId target_entity, ModelingSelectionMode mode,
    const ModelingPickSelectRequest &request) {
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
  const std::vector<EntityId> previous_selected = scene_.selected_entities();
  const EntityId previous_active = scene_.primary_selection();
  SceneSelectionState resolved{
      .selected = previous_selected,
      .active = previous_active,
  };

  if (target_entity == kInvalidEntityId) {
    if (edit == SceneSelectionEdit::Replace &&
        !request.preserve_selection_on_miss) {
      resolved.selected.clear();
      resolved.active = kInvalidEntityId;
    }
  } else if (!preserve_selected_hit) {
    const std::array<EntityId, 1> edited_entities{{target_entity}};
    resolved = resolve_scene_selection_edit(previous_selected, previous_active,
                                            edited_entities, edit);
  }

  if (resolved.selected != previous_selected ||
      resolved.active != previous_active) {
    (void)set_scene_selection(
        std::span<const EntityId>(resolved.selected.data(),
                                  resolved.selected.size()),
        resolved.active);
  }

  const ModelingSelectionSummary current = summary();
  return current.mode == mode ? current : scene_summary_for(mode);
}

ModelingSelectionSummary EditorSelectionService::scene_box_selection(
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
  for (const EntityId entity : target_entities) {
    if (scene_target_selectable(entity) &&
        std::find(selectable.begin(), selectable.end(), entity) ==
            selectable.end()) {
      selectable.push_back(entity);
    }
  }
  const SceneSelectionState resolved =
      resolve_scene_selection_edit(previous_selected, previous_active,
                                   selectable, to_scene_selection_edit(edit));
  if (resolved.selected != scene_.selected_entities() ||
      resolved.active != scene_.primary_selection()) {
    QDR_PROFILE_SCOPE("Box selection commit scene selection");
    (void)set_scene_selection(
        std::span<const EntityId>(resolved.selected.data(),
                                  resolved.selected.size()),
        resolved.active);
  }

  const ModelingSelectionSummary current = summary();
  return current.mode == mode ? current : scene_summary_for(mode);
}

EntityId EditorSelectionService::nearest_group_ancestor(EntityId entity) const {
  EntityId parent = scene_.parent(entity);
  while (parent != kInvalidEntityId) {
    if (!scene_.is_scene_root(parent) &&
        scene_.kind(parent) == EntityKind::Empty) {
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
