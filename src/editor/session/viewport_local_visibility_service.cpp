////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/session/viewport_local_visibility_service.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

ViewportLocalVisibilityEntityId viewport_entity_id(EntityId entity) {
  return static_cast<ViewportLocalVisibilityEntityId>(entity);
}

std::vector<ViewportLocalVisibilityEntityId>
viewport_entity_ids(std::span<const EntityId> entities) {
  std::vector<ViewportLocalVisibilityEntityId> result;
  result.reserve(entities.size());
  for (const EntityId entity : entities) {
    result.push_back(viewport_entity_id(entity));
  }
  return result;
}

bool component_selection_mode(ModelingSelectionMode mode) {
  return mode == ModelingSelectionMode::Vertex ||
         mode == ModelingSelectionMode::Edge ||
         mode == ModelingSelectionMode::Face;
}

} // namespace

ViewportLocalVisibilityService::ViewportLocalVisibilityService(
    SceneDocument &scene, AssetRegistry &assets,
    ModelingSessionService &modeling, SceneInstanceService &instances,
    NativeViewportToolCoordinator &native_tools,
    ViewportWorkspaceService &workspace,
    ResolvedHoverClearer clear_resolved_hover)
    : scene_(scene), assets_(assets), modeling_(modeling),
      instances_(instances), native_tools_(native_tools),
      workspace_(workspace),
      clear_resolved_hover_(std::move(clear_resolved_hover)) {}

bool ViewportLocalVisibilityService::active() const {
  reconcile_scene_lifecycle();
  return workspace_.local_visibility_active();
}

bool ViewportLocalVisibilityService::can_enter() const {
  if (workspace_.local_visibility_active() || transient_interaction_active()) {
    return false;
  }

  const ModelingSelectionMode mode = modeling_.selection_mode();
  const SceneInstanceEditState edit_state = instances_.instance_edit_state();
  if (edit_state.active) {
    return scene_.is_layer_eligible(edit_state.occurrence) &&
           scene_.is_effectively_visible(edit_state.occurrence);
  }

  if (component_selection_mode(mode)) {
    return component_selection_can_enter();
  }

  for (const EntityId entity : scene_.selected_entities()) {
    if (scene_.is_layer_eligible(entity) &&
        scene_.is_effectively_visible(entity)) {
      return true;
    }
  }
  return false;
}

bool ViewportLocalVisibilityService::can_toggle() const {
  reconcile_scene_lifecycle();
  if (transient_interaction_active()) {
    return false;
  }
  return workspace_.local_visibility_active() || can_enter();
}

bool ViewportLocalVisibilityService::allows(
    ViewportLocalVisibilityEntityId entity) const {
  reconcile_scene_lifecycle();
  return workspace_.local_visibility_snapshot().allows(entity);
}

bool ViewportLocalVisibilityService::allows_selection_entity(
    ViewportLocalVisibilityEntityId entity) const {
  const EntityId scene_entity = static_cast<EntityId>(entity);
  const SceneInstanceEditState edit_state = instances_.instance_edit_state();
  if (edit_state.active &&
      scene_.containing_instance_definition(scene_entity) ==
          edit_state.definition) {
    return allows(edit_state.occurrence);
  }
  return allows(entity);
}

std::uint64_t ViewportLocalVisibilityService::revision() const {
  reconcile_scene_lifecycle();
  return workspace_.local_visibility_revision();
}

ViewportLocalVisibilitySnapshot
ViewportLocalVisibilityService::snapshot() const {
  reconcile_scene_lifecycle();
  return workspace_.local_visibility_snapshot();
}

void ViewportLocalVisibilityService::synchronize_scene_lifecycle() const {
  reconcile_scene_lifecycle();
}

ViewportLocalVisibilityMutation ViewportLocalVisibilityService::toggle() {
  reconcile_scene_lifecycle();
  if (transient_interaction_active()) {
    return {
        .ok = false,
        .changed = false,
        .message =
            "Local Visibility is unavailable during an active viewport "
            "tool, transform, or operation preview.",
    };
  }

  if (workspace_.local_visibility_active()) {
    const bool changed = workspace_.exit_local_visibility();
    if (changed) {
      clear_resolved_hover_();
      lifecycle_reconcile_valid_ = false;
    }
    return {
        .ok = true,
        .changed = changed,
        .message = "Local Visibility exited.",
    };
  }

  std::vector<EntityId> baseline;
  baseline.reserve(scene_.entities().size());
  for (const EntityId entity : scene_.entities()) {
    if (scene_.is_layer_eligible(entity)) {
      baseline.push_back(entity);
    }
  }

  std::vector<EntityId> members;
  const SceneInstanceEditState edit_state = instances_.instance_edit_state();
  const ModelingSelectionMode mode = modeling_.selection_mode();
  if (edit_state.active) {
    if (scene_.is_layer_eligible(edit_state.occurrence) &&
        scene_.is_effectively_visible(edit_state.occurrence)) {
      members.push_back(edit_state.occurrence);
    }
  } else if (component_selection_mode(mode)) {
    QDR_PROFILE_SCOPE("Local visibility collect component selection members");
    for (const EntityId entity : modeling_.selected_component_entities()) {
      if (scene_.is_layer_eligible(entity) &&
          scene_.is_effectively_visible(entity)) {
        members.push_back(entity);
      }
    }
  } else {
    std::vector<EntityId> selected_visible;
    for (const EntityId entity : scene_.selected_entities()) {
      if (scene_.is_layer_eligible(entity) &&
          scene_.is_effectively_visible(entity)) {
        selected_visible.push_back(entity);
      }
    }
    members = mode == ModelingSelectionMode::Group
                  ? scene_.layer_eligible_entities(selected_visible)
                  : std::move(selected_visible);
  }

  std::sort(members.begin(), members.end());
  members.erase(std::unique(members.begin(), members.end()), members.end());
  if (members.empty()) {
    return {
        .ok = false,
        .changed = false,
        .message = "Select at least one visible object to enter Local "
                   "Visibility.",
    };
  }

  std::optional<SceneBounds> frame_bounds;
  if (component_selection_mode(mode)) {
    frame_bounds = modeling_.selection_bounds();
    if (!frame_bounds.has_value() && edit_state.active) {
      frame_bounds =
          entity_world_bounds(scene_, assets_, edit_state.occurrence);
    }
  } else {
    frame_bounds = selected_transform_world_bounds(scene_, assets_);
  }

  const std::vector<ViewportLocalVisibilityEntityId> baseline_ids =
      viewport_entity_ids(baseline);
  const std::vector<ViewportLocalVisibilityEntityId> member_ids =
      viewport_entity_ids(members);
  const bool changed =
      workspace_.enter_local_visibility(baseline_ids, member_ids);
  if (changed) {
    clear_resolved_hover_();
    last_reconciled_scene_content_revision_ = scene_.content_revision();
    last_reconciled_workspace_revision_ =
        workspace_.local_visibility_revision();
    lifecycle_reconcile_valid_ = true;
  }
  if (changed && frame_bounds.has_value()) {
    workspace_.focus_local_visibility_bounds(frame_bounds->min,
                                             frame_bounds->max);
  }
  return {
      .ok = true,
      .changed = changed,
      .message = changed ? "Local Visibility entered."
                         : "Local Visibility unchanged.",
  };
}

void ViewportLocalVisibilityService::reset_for_document_replacement() {
  (void)workspace_.reset_local_visibility();
  lifecycle_reconcile_valid_ = false;
  component_availability_valid_ = false;
}

void ViewportLocalVisibilityService::reconcile_scene_lifecycle() const {
  const ViewportLocalVisibilitySnapshot current =
      workspace_.local_visibility_snapshot();
  if (!current.active()) {
    lifecycle_reconcile_valid_ = false;
    return;
  }

  const std::uint64_t scene_content_revision = scene_.content_revision();
  if (lifecycle_reconcile_valid_ &&
      last_reconciled_scene_content_revision_ == scene_content_revision &&
      last_reconciled_workspace_revision_ == current.revision()) {
    return;
  }

  std::unordered_set<ViewportLocalVisibilityEntityId> baseline(
      current.baseline_entities().begin(), current.baseline_entities().end());
  std::vector<ViewportLocalVisibilityEntityId> created_entities;
  for (const EntityId entity : scene_.entities()) {
    if (!scene_.is_layer_eligible(entity)) {
      continue;
    }
    const ViewportLocalVisibilityEntityId viewport_id =
        viewport_entity_id(entity);
    if (!baseline.contains(viewport_id) && !current.contains_member(viewport_id)) {
      created_entities.push_back(viewport_id);
    }
  }
  if (!created_entities.empty()) {
    (void)workspace_.add_local_visibility_members(created_entities);
  }

  const ViewportLocalVisibilitySnapshot reconciled =
      workspace_.local_visibility_snapshot();
  const bool has_live_member =
      std::any_of(reconciled.members().begin(), reconciled.members().end(),
                  [&](ViewportLocalVisibilityEntityId entity) {
                    return scene_.exists(static_cast<EntityId>(entity));
                  });
  if (!has_live_member) {
    if (workspace_.exit_local_visibility()) {
      clear_resolved_hover_();
    }
  }
  last_reconciled_scene_content_revision_ = scene_content_revision;
  last_reconciled_workspace_revision_ =
      workspace_.local_visibility_revision();
  lifecycle_reconcile_valid_ = workspace_.local_visibility_active();
}

bool ViewportLocalVisibilityService::transient_interaction_active() const {
  return modeling_.modal_tool_owns_viewport() ||
         modeling_.transform_drag_active() ||
         modeling_.tool_settings_target().operation_preview_active ||
         native_tools_.has_pending_tool();
}

bool ViewportLocalVisibilityService::component_selection_can_enter() const {
  const std::uint64_t selection_revision = modeling_.selection_revision();
  const std::uint64_t object_map_revision =
      modeling_.modeling_object_map_revision();
  const std::uint64_t visibility_revision = scene_.visibility_revision();
  const std::uint64_t layer_revision = scene_.layer_revision();
  if (component_availability_valid_ &&
      component_availability_selection_revision_ == selection_revision &&
      component_availability_object_map_revision_ == object_map_revision &&
      component_availability_visibility_revision_ == visibility_revision &&
      component_availability_layer_revision_ == layer_revision) {
    return component_availability_value_;
  }

  QDR_PROFILE_SCOPE("Local visibility rebuild component availability");
  component_availability_value_ = std::ranges::any_of(
      modeling_.selected_component_entities(), [&](const EntityId entity) {
        return scene_.is_layer_eligible(entity) &&
               scene_.is_effectively_visible(entity);
      });
  component_availability_selection_revision_ = selection_revision;
  component_availability_object_map_revision_ = object_map_revision;
  component_availability_visibility_revision_ = visibility_revision;
  component_availability_layer_revision_ = layer_revision;
  component_availability_valid_ = true;
  return component_availability_value_;
}

} // namespace quader
