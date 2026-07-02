////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/selection/editor_selection_service.hpp"

#include "diagnostics/profile.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
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

bool scene_entity_selectable_for_user_selection(const SceneDocument &scene,
                                                EntityId entity) {
  if (entity == kInvalidEntityId || !scene.exists(entity)) {
    return false;
  }
  if (scene.is_scene_root(entity)) {
    return true;
  }
  if (scene.is_effectively_locked(entity)) {
    return false;
  }
  const VisibilityComponent *visibility = scene.visibility(entity);
  return visibility == nullptr || visibility->selectable;
}

std::vector<EntityId>
user_selectable_scene_entities(const SceneDocument &scene,
                               std::span<const EntityId> entities) {
  std::vector<EntityId> result;
  result.reserve(entities.size());
  for (const EntityId entity : entities) {
    if (!scene_entity_selectable_for_user_selection(scene, entity) ||
        std::find(result.begin(), result.end(), entity) != result.end()) {
      continue;
    }
    result.push_back(entity);
  }
  return result;
}

EntityId user_selectable_active_entity(const SceneDocument &scene,
                                       const std::vector<EntityId> &entities,
                                       EntityId active) {
  if (active != kInvalidEntityId &&
      scene_entity_selectable_for_user_selection(scene, active) &&
      std::find(entities.begin(), entities.end(), active) != entities.end()) {
    return active;
  }
  return entities.empty() ? kInvalidEntityId : entities.back();
}

std::vector<EntityId>
user_selectable_scene_entities_for_mode(const SceneDocument &scene,
                                        ModelingSelectionMode mode) {
  std::vector<EntityId> result;
  for (const EntityId entity : scene.entity_tree_order()) {
    if (scene.is_scene_root(entity) ||
        !scene_entity_selectable_for_user_selection(scene, entity)) {
      continue;
    }
    if (mode == ModelingSelectionMode::Group &&
        scene.kind(entity) != EntityKind::Empty) {
      continue;
    }
    result.push_back(entity);
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

/**
 * Stores normalized scene selection contents after applying an add/remove/toggle/replace edit.
 */
struct ResolvedSceneSelectionEdit {
    std::vector<EntityId> selected;
    EntityId active = kInvalidEntityId;
};

ResolvedSceneSelectionEdit
resolve_scene_selection_edit(std::span<const EntityId> current_selection,
                             std::span<const EntityId> edited_entities,
                             EditorSelectionEdit edit) {
  ResolvedSceneSelectionEdit result;
  if (edit == EditorSelectionEdit::Replace) {
    result.selected.reserve(edited_entities.size());
    for (const EntityId entity : edited_entities) {
      if (entity == kInvalidEntityId ||
          std::find(result.selected.begin(), result.selected.end(), entity) !=
              result.selected.end()) {
        continue;
      }
      result.selected.push_back(entity);
      result.active = entity;
    }
    return result;
  }

  result.selected.assign(current_selection.begin(), current_selection.end());
  result.active =
      result.selected.empty() ? kInvalidEntityId : result.selected.back();
  for (const EntityId entity : edited_entities) {
    const auto found =
        std::find(result.selected.begin(), result.selected.end(), entity);
    const bool already_selected = found != result.selected.end();
    if (edit == EditorSelectionEdit::Add ||
        (edit == EditorSelectionEdit::Toggle && !already_selected)) {
      if (entity != kInvalidEntityId && !already_selected) {
        result.selected.push_back(entity);
        result.active = entity;
      }
    } else if ((edit == EditorSelectionEdit::Remove ||
                edit == EditorSelectionEdit::Toggle) &&
               already_selected) {
      result.selected.erase(found);
      result.active =
          result.selected.empty() ? kInvalidEntityId : result.selected.back();
    }
  }
  return result;
}

EditorSelectionEdit to_editor_selection_edit(ModelingSelectionEdit edit) {
  switch (edit) {
  case ModelingSelectionEdit::Add:
    return EditorSelectionEdit::Add;
  case ModelingSelectionEdit::Remove:
    return EditorSelectionEdit::Remove;
  case ModelingSelectionEdit::Toggle:
    return EditorSelectionEdit::Toggle;
  case ModelingSelectionEdit::Replace:
    return EditorSelectionEdit::Replace;
  }
  return EditorSelectionEdit::Replace;
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
  const ModelingSelectionSummary current = modeling_.selection_summary();
  const std::vector<EntityId> selected = scene_.selected_entities();
  if (!selected.empty() && has_scene_only_selection(scene_, selected)) {
    return scene_summary_for(current.mode == ModelingSelectionMode::Group
                                 ? ModelingSelectionMode::Group
                                 : ModelingSelectionMode::Object);
  }
  if (current.has_selection &&
      current.active_object_id != kInvalidModelingObjectId) {
    const std::optional<EntityId> active_entity =
        modeling_.entity_for_modeling_object(current.active_object_id);
    if (active_entity.has_value() &&
        scene_.is_effectively_locked(*active_entity)) {
      return scene_summary_for(current.mode);
    }
  }
    return current;
}

ModelingHoverSummary EditorSelectionService::hover_summary() const {
  return modeling_.selection_hover_summary();
}

ModelingSelectionMode EditorSelectionService::mode() const {
  return modeling_.selection_mode();
}

DocumentCommandResult
EditorSelectionService::set_scene_selection(std::span<const EntityId> entities,
                                            EntityId active) {
  QDR_PROFILE_FUNCTION();
  const ModelingSelectionSummary before = modeling_.selection_summary();
  std::vector<EntityId> selectable =
      user_selectable_scene_entities(scene_, entities);
  const EntityId selectable_active =
      user_selectable_active_entity(scene_, selectable, active);
  DocumentCommandResult result = document_commands_.select_entities(
      std::move(selectable), selectable_active);
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
  const ResolvedSceneSelectionEdit resolved =
      resolve_scene_selection_edit(current, entities, edit);
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
  return modeling_.set_component_selection(request);
}

ModelingCommandResult
EditorSelectionService::set_mode(ModelingSelectionMode mode) {
  return modeling_.set_selection_mode(mode);
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
    selected = user_selectable_scene_entities_for_mode(scene_, selection_mode);
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
  for (const EntityId entity :
       user_selectable_scene_entities_for_mode(scene_, selection_mode)) {
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

EditorSelectionBoxBaseline
EditorSelectionService::capture_box_selection_baseline() const {
  EditorSelectionBoxBaseline baseline;
  baseline.mode = modeling_.selection_mode();
  baseline.scene_selection = scene_.selected_entities();
  baseline.active_entity = scene_.primary_selection();
  if (baseline.mode != ModelingSelectionMode::Group) {
    baseline.modeling_selection = modeling_.capture_box_selection_baseline();
  }
  return baseline;
}

ModelingSelectionSummary
EditorSelectionService::pick_select(const ModelingPickSelectRequest &request) {
  if (modeling_.selection_mode() == ModelingSelectionMode::Group) {
    const std::optional<EntityId> hit_mesh =
        modeling_.pick_object_entity(request);
    return scene_pick_selection(
        nearest_group_ancestor(hit_mesh.value_or(kInvalidEntityId)),
        ModelingSelectionMode::Group, request);
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
  if (modeling_.selection_mode() == ModelingSelectionMode::Group) {
    std::vector<EntityId> groups;
    for (const EntityId mesh_entity :
         modeling_.box_pick_object_entities(request)) {
      const EntityId group = nearest_group_ancestor(mesh_entity);
      if (group != kInvalidEntityId &&
          std::find(groups.begin(), groups.end(), group) == groups.end()) {
        groups.push_back(group);
      }
    }
    return scene_box_selection(groups, ModelingSelectionMode::Group,
                               request.edit, baseline);
  }

  if (baseline != nullptr && baseline->modeling_selection.has_value() &&
      baseline->modeling_selection->valid() &&
      baseline->mode == modeling_.selection_mode()) {
    return modeling_.box_select(request, *baseline->modeling_selection);
  }
  return modeling_.box_select(request);
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
  if (!scene_entity_selectable_for_user_selection(scene_, target_entity)) {
    target_entity = kInvalidEntityId;
  }
  const bool target_selected = target_entity != kInvalidEntityId &&
                               scene_.selection(target_entity) != nullptr &&
                               scene_.selection(target_entity)->selected;
  const bool preserve_selected_hit =
      request.preserve_selection_on_selected_hit && target_selected;
  const std::vector<EntityId> previous_selected = scene_.selected_entities();
  std::vector<EntityId> selected = previous_selected;

  const bool additive_toggle =
      request.control_pressed || request.command_pressed;
  if (!additive_toggle && !request.shift_pressed && !preserve_selected_hit &&
      (!request.preserve_selection_on_miss ||
       target_entity != kInvalidEntityId)) {
    selected.clear();
  }
  if (target_entity != kInvalidEntityId && !preserve_selected_hit) {
    const auto found =
        std::find(selected.begin(), selected.end(), target_entity);
    const bool already_selected = found != selected.end();
    if (additive_toggle && already_selected) {
      selected.erase(found);
    } else if (!already_selected) {
      selected.push_back(target_entity);
    }
  }

  if (selected != previous_selected) {
    const EntityId active =
        selected.empty() ? kInvalidEntityId
                         : (target_entity != kInvalidEntityId &&
                                    std::find(selected.begin(), selected.end(),
                                              target_entity) != selected.end()
                                ? target_entity
                                : selected.back());
    (void)set_scene_selection(
        std::span<const EntityId>(selected.data(), selected.size()), active);
  }

  const ModelingSelectionSummary current = modeling_.selection_summary();
  return current.mode == mode ? current : scene_summary_for(mode);
}

ModelingSelectionSummary EditorSelectionService::scene_box_selection(
    std::span<const EntityId> target_entities, ModelingSelectionMode mode,
    ModelingSelectionEdit edit, const EditorSelectionBoxBaseline *baseline) {
  const std::vector<EntityId> previous_selected =
      (baseline != nullptr && baseline->mode == mode)
          ? baseline->scene_selection
          : scene_.selected_entities();
  const ResolvedSceneSelectionEdit resolved = resolve_scene_selection_edit(
      previous_selected, target_entities, to_editor_selection_edit(edit));
  if (resolved.selected != previous_selected) {
    (void)set_scene_selection(
        std::span<const EntityId>(resolved.selected.data(),
                                  resolved.selected.size()),
        resolved.active);
  }

  const ModelingSelectionSummary current = modeling_.selection_summary();
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
  const std::vector<EntityId> selected = scene_.selected_entities();
  const EntityId active = scene_.primary_selection();
  const bool active_is_light =
      active != kInvalidEntityId && scene_.light(active) != nullptr;
  return {
      .mode = mode,
      .has_selection = !selected.empty(),
      .active_kind =
          selected.empty()
              ? ModelingActiveSelectionKind::None
              : (active_is_light ? ModelingActiveSelectionKind::Light
                                 : ModelingActiveSelectionKind::Object),
      .selected_object_count = static_cast<std::uint32_t>(selected.size()),
      .content_revision = modeling_.selection_summary().content_revision,
      .selection_revision = scene_.revision(),
  };
}

} // namespace quader
