////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modes/editor_mode_service.hpp"

#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/viewport_local_visibility_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"

#include <algorithm>
#include <utility>

namespace quader {
namespace {

bool is_component_kind(ModelingSelectionMode kind) noexcept {
  return kind == ModelingSelectionMode::Vertex ||
         kind == ModelingSelectionMode::Edge ||
         kind == ModelingSelectionMode::Face;
}

void increment_nonzero(std::uint64_t &value) noexcept {
  if (++value == 0u) {
    ++value;
  }
}

void append_unique(std::vector<EntityId> &entities, EntityId entity) {
  if (entity != kInvalidEntityId &&
      std::find(entities.begin(), entities.end(), entity) == entities.end()) {
    entities.push_back(entity);
  }
}

} // namespace

bool EditorModeSnapshot::contains(EntityId entity) const noexcept {
  return std::any_of(participants.begin(), participants.end(),
                     [entity](const EditorModeParticipant &participant) {
                       return participant.entity == entity;
                     });
}

EditorModeService::EditorModeService(SceneSession &session) : session_(session) {
  snapshot_.object_results = session_.scene().selected_entities();
  assign_active_entity(session_.scene().primary_selection());
}

bool EditorModeService::permits_selection_mode(
    ModelingSelectionMode kind) const noexcept {
  return mode() == EditorMode::Edit
             ? is_component_kind(kind)
             : kind == ModelingSelectionMode::Object ||
                   kind == ModelingSelectionMode::Group;
}

EditorModeParticipant EditorModeService::participant_for(EntityId entity) const {
  return {.entity = entity,
          .uuid = session_.scene().uuid(entity),
          .modeling_object_id = session_.modeling()
                                    .modeling_object_for_entity(entity)
                                    .value_or(kInvalidModelingObjectId)};
}

std::vector<EditorModeParticipant> EditorModeService::entry_participants() const {
  const SceneDocument &scene = session_.scene();
  const ModelingInstanceEditScope instance_scope =
      session_.modeling().instance_edit_scope();
  std::vector<EditorModeParticipant> participants;
  std::unordered_set<EntityId> visited;
  std::unordered_set<EntityId> expanded;
  std::vector<std::pair<EntityId, bool>> pending;
  for (const EntityId entity : scene.selected_entities()) {
    pending.emplace_back(entity, scene.kind(entity) == EntityKind::Empty);
  }
  if (pending.empty() && !snapshot_.active_uuid.empty()) {
    const EntityId active = scene.entity_for_uuid(snapshot_.active_uuid);
    if (active != kInvalidEntityId && scene.mesh(active) != nullptr) {
      pending.emplace_back(active, false);
    }
  }
  for (std::size_t index = 0u; index < pending.size(); ++index) {
    const auto [entity, expand_children] = pending[index];
    if (!scene.exists(entity)) {
      continue;
    }
    const bool active_definition_member =
        instance_scope.active() && entity != instance_scope.definition &&
        scene.containing_instance_definition(entity) == instance_scope.definition;
    if (active_definition_member) {
      // Definition storage is deliberately hidden. Validate the member's
      // authored hierarchy and the displayed occurrence independently, as
      // component picking and explicit material-target validation do.
      if (!scene_hierarchy_segment_is_selectable(scene, entity,
                                                  instance_scope.definition) ||
          !scene.is_effectively_visible(instance_scope.occurrence) ||
          scene.is_effectively_locked(instance_scope.occurrence) ||
          !scene_hierarchy_segment_is_selectable_ignoring_visibility(
              scene, instance_scope.occurrence)) {
        continue;
      }
    } else if (instance_scope.active() ||
               !scene.is_effectively_visible(entity) ||
               scene.is_effectively_locked(entity)) {
      continue;
    }
    // Only selected groups expand membership. Their complete hierarchy is
    // visited, even if a mesh also appeared as an explicit selected root.
    if (expand_children && expanded.insert(entity).second) {
      for (const EntityId child : scene.children(entity)) {
        pending.emplace_back(child, true);
      }
    }
    if (!visited.insert(entity).second) {
      continue;
    }
    const VisibilityComponent *visibility = scene.visibility(entity);
    if (visibility != nullptr && !visibility->selectable) {
      continue;
    }
    if (scene.mesh(entity) != nullptr &&
        (active_definition_member ||
         session_.instances().is_selectable_in_current_scope(entity)) &&
        session_.viewport_local_visibility().allows_selection_entity(entity)) {
      EditorModeParticipant participant = participant_for(entity);
      if (participant.modeling_object_id != kInvalidModelingObjectId &&
          !participant.uuid.empty()) {
        participants.push_back(std::move(participant));
      }
    }
  }
  return participants;
}

bool EditorModeService::can_enter_edit() const {
  return mode() == EditorMode::Edit || !entry_participants().empty();
}

bool EditorModeService::can_set_mode(EditorMode requested) const {
  switch (requested) {
  case EditorMode::Object:
    return true;
  case EditorMode::Edit:
    return can_enter_edit();
  }
  return false;
}

void EditorModeService::advance_revision(bool new_generation) noexcept {
  increment_nonzero(snapshot_.revision);
  if (new_generation) {
    increment_nonzero(snapshot_.generation);
  }
}

void EditorModeService::set_transition_settlement(
    std::function<CommandResult()> settlement) {
  transition_settlement_ = std::move(settlement);
}

CommandResult EditorModeService::set_mode(EditorMode requested) {
  if (requested != EditorMode::Object && requested != EditorMode::Edit) {
    return command_invalid_result("Unknown editor mode.");
  }
  if (requested == mode()) {
    return command_unchanged_result();
  }
  std::vector<EditorModeParticipant> participants;
  if (requested == EditorMode::Edit) {
    participants = entry_participants();
    if (participants.empty()) {
      return command_unchanged_result();
    }
  }

  if (transition_settlement_) {
    const CommandResult settled = transition_settlement_();
    if (!settled.ok) {
      return settled;
    }
  }
  const CommandResult settled = session_.native_tools().settle_transient_state();
  if (!settled.ok) {
    return settled;
  }
  const CommandResult retired =
      session_.native_tools().retire_incompatible_tools(requested);
  if (!retired.ok) {
    return retired;
  }

  EditorModeSnapshot previous = snapshot_;
  std::vector<EntityId> first_admission;
  if (requested == EditorMode::Edit) {
    snapshot_.participants = std::move(participants);
    snapshot_.object_results.clear();
    const EntityId active = session_.scene().primary_selection();
    assign_active_entity(snapshot_.contains(active)
                             ? active
                             : snapshot_.participants.front().entity);
    for (const EditorModeParticipant &participant : snapshot_.participants) {
      if (!admitted_uuids_.contains(participant.uuid)) {
        first_admission.push_back(participant.entity);
      }
    }
  } else {
    std::vector<EntityId> selected;
    for (const EditorModeParticipant &participant : snapshot_.participants) {
      const EntityId live = session_.scene().entity_for_uuid(participant.uuid);
      if (live != kInvalidEntityId) {
        append_unique(selected, live);
      }
    }
    for (const EntityId entity : snapshot_.object_results) {
      if (session_.scene().exists(entity)) {
        append_unique(selected, entity);
      }
    }
    snapshot_.object_results = std::move(selected);
    if (std::find(snapshot_.object_results.begin(), snapshot_.object_results.end(),
                  snapshot_.active_entity) == snapshot_.object_results.end()) {
      assign_active_entity(snapshot_.object_results.empty()
                               ? kInvalidEntityId
                               : snapshot_.object_results.front());
    }
    snapshot_.participants.clear();
  }
  snapshot_.mode = requested;
  advance_revision(true);
  const ModelingCommandResult applied =
      session_.modeling().apply_editor_mode_context(snapshot_, first_admission);
  if (!applied.ok) {
    restore_exact_state(std::move(previous));
    return {.ok = false, .message = applied.message};
  }
  for (const EditorModeParticipant &participant : snapshot_.participants) {
    admitted_uuids_.insert(participant.uuid);
  }
  (void)session_.selection().clear_hover();
  normalize_tools();
  return command_changed_result();
}

CommandResult EditorModeService::toggle() {
  return set_mode(mode() == EditorMode::Object ? EditorMode::Edit
                                              : EditorMode::Object);
}

CommandResult
EditorModeService::set_component_kind(ModelingSelectionMode kind) {
  if (mode() != EditorMode::Edit || !is_component_kind(kind)) {
    return command_invalid_result("Component selection requires Edit Mode.");
  }
  if (snapshot_.component_kind == kind &&
      session_.modeling().selection_mode() == kind) {
    return command_unchanged_result();
  }
  const CommandResult settled = session_.native_tools().settle_transient_state();
  if (!settled.ok) {
    return settled;
  }
  const ModelingCommandResult changed = session_.modeling().set_selection_mode(kind);
  if (!changed.ok) {
    return {.ok = false, .message = changed.message};
  }
  record_component_kind(kind);
  const CommandResult retired = session_.native_tools().retire_incompatible_tools(mode());
  if (!retired.ok) {
    return {.ok = false, .changed = true, .message = retired.message};
  }
  (void)session_.selection().clear_hover();
  normalize_tools();
  return command_changed_result();
}

void EditorModeService::normalize_tools() {
  const auto compatible = [this](const ToolDescriptor *descriptor) {
    return descriptor != nullptr &&
           editor_mode_supported(descriptor->supported_editor_modes, mode()) &&
           (mode() != EditorMode::Edit ||
            !descriptor->required_component_kind.has_value() ||
            *descriptor->required_component_kind == snapshot_.component_kind);
  };
  ViewportWorkspaceService &workspace = session_.viewport_workspace();
  if (!compatible(find_viewport_tool_descriptor(
          session_.modeling().active_viewport_tool()))) {
    (void)session_.modeling().activate_viewport_tool(ModelingViewportTool::Select);
  }
  for (std::size_t index = 0u; index < kViewportPaneCount; ++index) {
    const auto pane = static_cast<ViewportPaneId>(index);
    if (!compatible(find_viewport_tool_descriptor(workspace.selected_tool(pane)))) {
      (void)workspace.record_selected_tool(pane, ViewportToolId::Select, false);
    }
  }
  const auto &origin = session_.settings_presentation_origin();
  const ToolDescriptor *origin_tool = find_viewport_tool_descriptor(origin.target_id);
  if (!origin.target_id.empty() &&
      origin_tool != nullptr && !compatible(origin_tool)) {
    session_.record_settings_presentation_origin("select", origin.pane);
  }
}

void EditorModeService::synchronize_tools_after_publication() {
  // Structural publication may remove the final Edit participant. Its rollback
  // checkpoint must complete before tool owners or retained panes are changed.
  if (session_.history().transaction_active() ||
      session_.native_tools().has_pending_history_preview() ||
      session_.modeling().operation_preview_active() ||
      session_.modeling().transform_drag_active()) {
    return;
  }
  const CommandResult retired =
      session_.native_tools().retire_incompatible_tools(mode());
  if (retired.ok) {
    normalize_tools();
  }
}

void EditorModeService::restore_exact_state(EditorModeSnapshot &&state) noexcept {
  snapshot_ = std::move(state);
}

void EditorModeService::restore_history_state(
    const EditorModeSnapshot &state, bool exact_failure_compensation) {
  if (exact_failure_compensation) {
    snapshot_ = state;
    return;
  }
  if (state.generation != generation() || state.mode != mode()) {
    reconcile_scene_lifecycle();
    return;
  }
  if (state.component_kind == snapshot_.component_kind &&
      state.active_entity == snapshot_.active_entity &&
      state.active_uuid == snapshot_.active_uuid &&
      state.participants == snapshot_.participants &&
      state.object_results == snapshot_.object_results) {
    reconcile_scene_lifecycle();
    return;
  }
  const std::uint64_t previous_revision = revision();
  snapshot_ = state;
  snapshot_.revision = previous_revision;
  advance_revision();
  reconcile_scene_lifecycle();
}

void EditorModeService::set_active_participant(EntityId entity) {
  if (contains(entity) && snapshot_.active_entity != entity) {
    assign_active_entity(entity);
    advance_revision();
  }
}

void EditorModeService::assign_active_entity(EntityId entity) {
  snapshot_.active_entity = entity;
  snapshot_.active_uuid = entity == kInvalidEntityId
                              ? std::string{}
                              : session_.scene().uuid(entity);
}

void EditorModeService::synchronize_object_active_entity() {
  if (mode() != EditorMode::Object) {
    return;
  }
  const SceneDocument &scene = session_.scene();
  const EntityId selected_active = scene.primary_selection();
  const EntityId active = scene.exists(selected_active)
                              ? selected_active
                              : scene.entity_for_uuid(snapshot_.active_uuid);
  const std::string uuid = active == kInvalidEntityId ? std::string{}
                                                    : scene.uuid(active);
  if (snapshot_.active_entity != active || snapshot_.active_uuid != uuid) {
    assign_active_entity(active);
    advance_revision();
  }
}

void EditorModeService::record_component_kind(ModelingSelectionMode kind) {
  if (mode() == EditorMode::Edit && is_component_kind(kind) &&
      snapshot_.component_kind != kind) {
    snapshot_.component_kind = kind;
    advance_revision();
  }
}

void EditorModeService::remap_participants(std::span<const EntityId> consumed,
                                          EntityId survivor) {
  if (mode() != EditorMode::Edit) {
    return;
  }
  const bool consumed_member = std::any_of(
      consumed.begin(), consumed.end(), [this](EntityId entity) {
        return snapshot_.contains(entity);
      });
  if (!consumed_member) {
    return;
  }
  const bool active_consumed = std::find(consumed.begin(), consumed.end(),
                                         snapshot_.active_entity) != consumed.end();
  std::erase_if(snapshot_.participants, [&](const EditorModeParticipant &participant) {
    return std::find(consumed.begin(), consumed.end(), participant.entity) !=
           consumed.end();
  });
  if (session_.scene().exists(survivor) && !snapshot_.contains(survivor)) {
    EditorModeParticipant participant = participant_for(survivor);
    if (participant.modeling_object_id != kInvalidModelingObjectId) {
      admitted_uuids_.insert(participant.uuid);
      snapshot_.participants.push_back(std::move(participant));
    }
  }
  if (active_consumed && snapshot_.contains(survivor)) {
    assign_active_entity(survivor);
  }
  advance_revision();
  reconcile_scene_lifecycle();
}

void EditorModeService::register_object_result(EntityId entity) {
  if (mode() != EditorMode::Edit || !session_.scene().exists(entity) ||
      contains(entity)) {
    return;
  }
  const std::size_t previous_size = snapshot_.object_results.size();
  append_unique(snapshot_.object_results, entity);
  if (snapshot_.object_results.size() != previous_size) {
    advance_revision();
  }
}

void EditorModeService::reconcile_scene_lifecycle() {
  if (mode() != EditorMode::Edit) {
    synchronize_object_active_entity();
    return;
  }
  const bool operation_preview = session_.modeling().operation_preview_active();
  bool changed = false;
  for (EditorModeParticipant &participant : snapshot_.participants) {
    const EntityId live = session_.scene().entity_for_uuid(participant.uuid);
    // Structural previews temporarily suppress consumed meshes by removing
    // their rendered binding. Their live SDK identities remain participants
    // until commit remaps them; cancel restores the original binding.
    if (live == kInvalidEntityId ||
        (session_.scene().mesh(live) == nullptr && !operation_preview)) {
      changed = changed || participant.entity != kInvalidEntityId;
      participant.entity = kInvalidEntityId;
      continue;
    }
    const auto modeling_object = session_.modeling().modeling_object_for_entity(live);
    if (!modeling_object.has_value()) {
      changed = true;
      participant.entity = kInvalidEntityId;
      continue;
    }
    changed = changed || participant.entity != live ||
              participant.modeling_object_id != *modeling_object;
    if (participant.entity == snapshot_.active_entity) {
      assign_active_entity(live);
    }
    participant.entity = live;
    participant.modeling_object_id = *modeling_object;
  }
  std::erase_if(snapshot_.participants, [](const EditorModeParticipant &participant) {
    return participant.entity == kInvalidEntityId;
  });
  const std::size_t result_count = snapshot_.object_results.size();
  std::erase_if(snapshot_.object_results, [this](EntityId entity) {
    return !session_.scene().exists(entity);
  });
  changed = changed || result_count != snapshot_.object_results.size();
  if (snapshot_.participants.empty()) {
    snapshot_.mode = EditorMode::Object;
    assign_active_entity(snapshot_.object_results.empty()
                             ? kInvalidEntityId
                             : snapshot_.object_results.front());
    advance_revision(true);
  } else if (changed) {
    if (!snapshot_.contains(snapshot_.active_entity)) {
      assign_active_entity(snapshot_.participants.front().entity);
    }
    advance_revision();
  }
}

void EditorModeService::reset_for_document_replacement() {
  const std::uint64_t previous_revision = revision();
  const std::uint64_t previous_generation = generation();
  snapshot_ = {};
  snapshot_.revision = previous_revision;
  snapshot_.generation = previous_generation;
  snapshot_.object_results = session_.scene().selected_entities();
  assign_active_entity(session_.scene().primary_selection());
  advance_revision(true);
  admitted_uuids_.clear();
}

} // namespace quader
