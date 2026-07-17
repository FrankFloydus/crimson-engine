////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_workspace_service.hpp"

#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <utility>

namespace quader {
namespace {

using LocalVisibilityEntityIds =
    std::vector<ViewportLocalVisibilityEntityId>;

LocalVisibilityEntityIds normalize_local_visibility_entity_ids(
    std::span<const ViewportLocalVisibilityEntityId> entities) {
  LocalVisibilityEntityIds normalized;
  normalized.reserve(entities.size());
  std::copy_if(entities.begin(), entities.end(), std::back_inserter(normalized),
               [](ViewportLocalVisibilityEntityId entity) {
                 return entity != kInvalidViewportLocalVisibilityEntityId;
               });
  std::ranges::sort(normalized);
  normalized.erase(std::unique(normalized.begin(), normalized.end()),
                   normalized.end());
  return normalized;
}

LocalVisibilityEntityIds union_local_visibility_entity_ids(
    std::span<const ViewportLocalVisibilityEntityId> first,
    std::span<const ViewportLocalVisibilityEntityId> second) {
  LocalVisibilityEntityIds combined;
  combined.reserve(first.size() + second.size());
  std::set_union(first.begin(), first.end(), second.begin(), second.end(),
                 std::back_inserter(combined));
  return combined;
}

} // namespace

void reset_viewport_camera_for_view(ViewportCameraController &camera,
                                    ViewportView view) {
  camera = ViewportCameraController{};
  const float focus_origin_distance =
      static_cast<float>(viewport_host_defaults().focus_origin_distance);
  switch (view) {
  case ViewportView::Top:
    camera.focus_origin(focus_origin_distance);
    camera.view_axis(ViewportViewAxis::Y, 1);
    break;
  case ViewportView::Bottom:
    camera.focus_origin(focus_origin_distance);
    camera.view_axis(ViewportViewAxis::Y, -1);
    break;
  case ViewportView::Front:
    camera.focus_origin(focus_origin_distance);
    camera.view_axis(ViewportViewAxis::Z, -1);
    break;
  case ViewportView::Back:
    camera.focus_origin(focus_origin_distance);
    camera.view_axis(ViewportViewAxis::Z, 1);
    break;
  case ViewportView::Side:
    camera.focus_origin(focus_origin_distance);
    camera.view_axis(ViewportViewAxis::X, 1);
    break;
  case ViewportView::Left:
    camera.focus_origin(focus_origin_distance);
    camera.view_axis(ViewportViewAxis::X, -1);
    break;
  case ViewportView::Perspective:
    break;
  }
}

ViewportView viewport_view_for_camera(const ViewportCameraController &camera) {
  if (camera.state().projection != ViewportProjection::Orthographic) {
    return ViewportView::Perspective;
  }

  const QVec3 forward = ViewportCameraAdapter::basis(camera).forward;
  const float abs_x = std::abs(forward.x);
  const float abs_y = std::abs(forward.y);
  const float abs_z = std::abs(forward.z);
  constexpr float kAxisDotThreshold = 0.985f;
  if (abs_y >= abs_x && abs_y >= abs_z && abs_y >= kAxisDotThreshold) {
    return forward.y < 0.0f ? ViewportView::Top : ViewportView::Bottom;
  }
  if (abs_z >= abs_x && abs_z >= abs_y && abs_z >= kAxisDotThreshold) {
    return forward.z > 0.0f ? ViewportView::Front : ViewportView::Back;
  }
  if (abs_x >= kAxisDotThreshold) {
    return forward.x < 0.0f ? ViewportView::Side : ViewportView::Left;
  }
  return ViewportView::Perspective;
}

ViewportWorkspaceService::ViewportWorkspaceService(
    ViewportWorkspaceLayoutPolicy layout_policy)
    : layout_policy_(layout_policy) {
  grid_preset_ = viewport_default_grid_preset();
  panes_[viewport_pane_index(ViewportPaneId::Perspective)].id =
      ViewportPaneId::Perspective;
  panes_[viewport_pane_index(ViewportPaneId::Top)].id = ViewportPaneId::Top;
  panes_[viewport_pane_index(ViewportPaneId::Front)].id = ViewportPaneId::Front;
  panes_[viewport_pane_index(ViewportPaneId::Side)].id = ViewportPaneId::Side;
  for (ViewportWorkspacePaneState &pane_state : panes_) {
    reset_viewport_camera_for_view(pane_state.camera,
                                   viewport_view_for_pane(pane_state.id));
  }
}

bool ViewportWorkspaceService::set_layout(ViewportLayoutId layout) {
  if (!supports_layout(layout)) {
    return false;
  }
  const bool layout_changed = layout_ != layout;
  const bool maximized_changed = focused_pane_maximized_;
  if (!layout_changed && !maximized_changed) {
    return false;
  }
  layout_ = layout;
  focused_pane_maximized_ = false;
  if (!focused_pane_is_visible()) {
    focused_pane_ = ViewportPaneId::Perspective;
  }
  return true;
}

bool ViewportWorkspaceService::set_layout_policy(
    ViewportWorkspaceLayoutPolicy policy) {
  if (layout_policy_ == policy) {
    return false;
  }
  layout_policy_ = policy;
  if (!supports_layout(layout_)) {
    layout_ = ViewportLayoutId::Single;
    focused_pane_maximized_ = false;
    focused_pane_ = ViewportPaneId::Perspective;
  }
  return true;
}

bool ViewportWorkspaceService::supports_layout(ViewportLayoutId layout) const {
  if (layout_policy_ == ViewportWorkspaceLayoutPolicy::SinglePaneOnly) {
    return layout == ViewportLayoutId::Single;
  }
  return true;
}

bool ViewportWorkspaceService::focus_pane(ViewportPaneId pane) {
  if (focused_pane_ == pane) {
    return false;
  }
  focused_pane_ = pane;
  return true;
}

bool ViewportWorkspaceService::set_focused_pane_maximized(bool maximized) {
  if (focused_pane_maximized_ == maximized) {
    return false;
  }
  focused_pane_maximized_ = maximized;
  return true;
}

bool ViewportWorkspaceService::toggle_focused_pane_maximized() {
  focused_pane_maximized_ = !focused_pane_maximized_;
  return true;
}

bool ViewportWorkspaceService::set_grid_preset(int preset) {
  const int clamped_preset = viewport_clamp_grid_preset(preset);
  if (grid_preset_ == clamped_preset) {
    return false;
  }
  grid_preset_ = clamped_preset;
  return true;
}

ViewportShadingMode
ViewportWorkspaceService::shading_mode(ViewportPaneId pane_id) const {
  return pane(pane_id).shading;
}

bool ViewportWorkspaceService::set_shading_mode(ViewportPaneId pane_id,
                                                ViewportShadingMode mode) {
  if (!viewport_shading_mode_is_valid(mode)) {
    return false;
  }
  ViewportWorkspacePaneState &pane_state = pane(pane_id);
  if (pane_state.shading == mode) {
    return false;
  }
  pane_state.shading = mode;
  return true;
}

ViewportView ViewportWorkspaceService::view(ViewportPaneId pane_id) const {
  return viewport_view_for_camera(pane(pane_id).camera);
}

bool ViewportWorkspaceService::set_view(ViewportPaneId pane_id,
                                        ViewportView view) {
  ViewportWorkspacePaneState &pane_state = pane(pane_id);
  if (viewport_view_for_camera(pane_state.camera) == view) {
    return false;
  }
  reset_viewport_camera_for_view(pane_state.camera, view);
  return true;
}

ViewportCameraController &
ViewportWorkspaceService::camera(ViewportPaneId pane_id) {
  return pane(pane_id).camera;
}

const ViewportCameraController &
ViewportWorkspaceService::camera(ViewportPaneId pane_id) const {
  return pane(pane_id).camera;
}

bool ViewportWorkspaceService::focus_origin(ViewportPaneId pane_id,
                                            float distance) {
  camera(pane_id).focus_origin(distance);
  return true;
}

bool ViewportWorkspaceService::focus_local_visibility_bounds(
    QVec3 min_bounds, QVec3 max_bounds, float margin) {
  if (!local_visibility_active()) {
    return false;
  }

  bool focused_any_viewport = false;
  for (const ViewportPanePlacement &placement : active_pane_placements()) {
    if (!pane_visible(placement.pane_id)) {
      continue;
    }
    camera(placement.pane_id).focus_bounds(min_bounds, max_bounds, margin);
    focused_any_viewport = true;
  }
  for (ViewportWorkspaceAuxiliaryViewportState &viewport_state :
       auxiliary_viewports_) {
    if (!viewport_state.visible_this_frame) {
      continue;
    }
    viewport_state.camera.focus_bounds(min_bounds, max_bounds, margin);
    focused_any_viewport = true;
  }
  return focused_any_viewport;
}

ViewportWorkspacePaneState &
ViewportWorkspaceService::pane(ViewportPaneId pane_id) {
  return panes_[viewport_pane_index(pane_id)];
}

const ViewportWorkspacePaneState &
ViewportWorkspaceService::pane(ViewportPaneId pane_id) const {
  return panes_[viewport_pane_index(pane_id)];
}

std::vector<ViewportPanePlacement>
ViewportWorkspaceService::active_pane_placements() const {
  if (focused_pane_maximized_) {
    return {ViewportPanePlacement{
        .pane_id = focused_pane_,
        .normalized_rect = {},
    }};
  }
  return layout_pane_placements();
}

std::vector<ViewportSplitHandle>
ViewportWorkspaceService::active_split_handles() const {
  if (focused_pane_maximized_) {
    return {};
  }
  return viewport_layout_split_handles(layout_, layout_state_);
}

void ViewportWorkspaceService::set_split_handle_position(
    ViewportSplitHandleId handle, float normalized_position) {
  layout_state_.set_handle_position(handle, normalized_position);
}

void ViewportWorkspaceService::clear_pane_visibility() {
  for (ViewportWorkspacePaneState &pane_state : panes_) {
    pane_state.visible_this_frame = false;
  }
  for (ViewportWorkspaceAuxiliaryViewportState &viewport_state :
       auxiliary_viewports_) {
    viewport_state.visible_this_frame = false;
  }
}

void ViewportWorkspaceService::set_pane_visible(ViewportPaneId pane_id,
                                                bool visible) {
  pane(pane_id).visible_this_frame = visible;
}

bool ViewportWorkspaceService::pane_visible(ViewportPaneId pane_id) const {
  return pane(pane_id).visible_this_frame;
}

bool ViewportWorkspaceService::set_pane_render_size(ViewportPaneId pane_id,
                                                    QVec2 size) {
  ViewportWorkspacePaneState &pane_state = pane(pane_id);
  if (pane_state.render_size.x == size.x &&
      pane_state.render_size.y == size.y) {
    return false;
  }
  pane_state.render_size = size;
  pane_state.camera.set_viewport_size(size.x, size.y);
  return true;
}

QVec2 ViewportWorkspaceService::pane_render_size(ViewportPaneId pane_id) const {
  return pane(pane_id).render_size;
}

ViewportWorkspaceAuxiliaryViewportState &
ViewportWorkspaceService::ensure_auxiliary_viewport(std::string stable_id,
                                                    std::uint8_t layer_index) {
  if (ViewportWorkspaceAuxiliaryViewportState *existing =
          auxiliary_viewport(stable_id)) {
    existing->layer_index = layer_index;
    return *existing;
  }

  ViewportWorkspaceAuxiliaryViewportState state;
  state.stable_id = std::move(stable_id);
  state.layer_index = layer_index;
  state.camera.focus_origin(
      static_cast<float>(viewport_host_defaults().focus_origin_distance));
  auxiliary_viewports_.push_back(std::move(state));
  return auxiliary_viewports_.back();
}

bool ViewportWorkspaceService::remove_auxiliary_viewport(
    std::string_view stable_id) {
  const auto found = std::find_if(
      auxiliary_viewports_.begin(), auxiliary_viewports_.end(),
      [stable_id](const ViewportWorkspaceAuxiliaryViewportState &state) {
        return state.stable_id == stable_id;
      });
  if (found == auxiliary_viewports_.end()) {
    return false;
  }
  std::erase_if(local_visibility_auxiliary_camera_backups_,
                [stable_id](const AuxiliaryViewportCameraBackup &backup) {
                  return backup.stable_id == stable_id;
                });
  auxiliary_viewports_.erase(found);
  return true;
}

ViewportWorkspaceAuxiliaryViewportState *
ViewportWorkspaceService::auxiliary_viewport(std::string_view stable_id) {
  const auto found = std::find_if(
      auxiliary_viewports_.begin(), auxiliary_viewports_.end(),
      [stable_id](const ViewportWorkspaceAuxiliaryViewportState &state) {
        return state.stable_id == stable_id;
      });
  return found == auxiliary_viewports_.end() ? nullptr : &*found;
}

const ViewportWorkspaceAuxiliaryViewportState *
ViewportWorkspaceService::auxiliary_viewport(std::string_view stable_id) const {
  const auto found = std::find_if(
      auxiliary_viewports_.begin(), auxiliary_viewports_.end(),
      [stable_id](const ViewportWorkspaceAuxiliaryViewportState &state) {
        return state.stable_id == stable_id;
      });
  return found == auxiliary_viewports_.end() ? nullptr : &*found;
}

ViewportView ViewportWorkspaceService::auxiliary_viewport_view(
    std::string_view stable_id) const {
  const ViewportWorkspaceAuxiliaryViewportState *state =
      auxiliary_viewport(stable_id);
  return state == nullptr ? ViewportView::Perspective
                          : viewport_view_for_camera(state->camera);
}

bool ViewportWorkspaceService::set_auxiliary_viewport_view(
    std::string_view stable_id, ViewportView view) {
  ViewportWorkspaceAuxiliaryViewportState *state =
      auxiliary_viewport(stable_id);
  if (state == nullptr) {
    return false;
  }
  if (viewport_view_for_camera(state->camera) == view) {
    return false;
  }
  reset_viewport_camera_for_view(state->camera, view);
  return true;
}

void ViewportWorkspaceService::set_auxiliary_viewport_visible(
    std::string_view stable_id, bool visible) {
  if (ViewportWorkspaceAuxiliaryViewportState *state =
          auxiliary_viewport(stable_id)) {
    state->visible_this_frame = visible;
  }
}

bool ViewportWorkspaceService::auxiliary_viewport_visible(
    std::string_view stable_id) const {
  const ViewportWorkspaceAuxiliaryViewportState *state =
      auxiliary_viewport(stable_id);
  return state != nullptr && state->visible_this_frame;
}

bool ViewportWorkspaceService::set_auxiliary_viewport_render_size(
    std::string_view stable_id, QVec2 size) {
  ViewportWorkspaceAuxiliaryViewportState *state =
      auxiliary_viewport(stable_id);
  if (state == nullptr) {
    return false;
  }
  if (state->render_size.x == size.x && state->render_size.y == size.y) {
    return false;
  }
  state->render_size = size;
  state->camera.set_viewport_size(size.x, size.y);
  return true;
}

bool ViewportWorkspaceService::enter_local_visibility(
    std::span<const ViewportLocalVisibilityEntityId> baseline_entities,
    std::span<const ViewportLocalVisibilityEntityId> members) {
  if (local_visibility_active()) {
    return false;
  }

  LocalVisibilityEntityIds normalized_members =
      normalize_local_visibility_entity_ids(members);
  if (normalized_members.empty()) {
    return false;
  }
  LocalVisibilityEntityIds normalized_baseline =
      normalize_local_visibility_entity_ids(baseline_entities);
  normalized_baseline = union_local_visibility_entity_ids(
      normalized_baseline, normalized_members);

  for (std::size_t index = 0u; index < panes_.size(); ++index) {
    local_visibility_pane_camera_backups_[index] = panes_[index].camera;
  }
  local_visibility_auxiliary_camera_backups_.clear();
  local_visibility_auxiliary_camera_backups_.reserve(
      auxiliary_viewports_.size());
  for (const ViewportWorkspaceAuxiliaryViewportState &viewport_state :
       auxiliary_viewports_) {
    local_visibility_auxiliary_camera_backups_.push_back({
        .stable_id = viewport_state.stable_id,
        .camera = viewport_state.camera,
    });
  }

  local_visibility_snapshot_ = ViewportLocalVisibilitySnapshot{
      true, next_local_visibility_revision(),
      std::make_shared<const LocalVisibilityEntityIds>(
          std::move(normalized_baseline)),
      std::make_shared<const LocalVisibilityEntityIds>(
          std::move(normalized_members)),
  };
  return true;
}

bool ViewportWorkspaceService::add_local_visibility_members(
    std::span<const ViewportLocalVisibilityEntityId> members) {
  if (!local_visibility_active()) {
    return false;
  }

  const LocalVisibilityEntityIds normalized_members =
      normalize_local_visibility_entity_ids(members);
  LocalVisibilityEntityIds combined_members = union_local_visibility_entity_ids(
      local_visibility_snapshot_.members(), normalized_members);
  if (combined_members.size() == local_visibility_snapshot_.members().size()) {
    return false;
  }

  local_visibility_snapshot_ = ViewportLocalVisibilitySnapshot{
      true, next_local_visibility_revision(),
      local_visibility_snapshot_.baseline_entities_,
      std::make_shared<const LocalVisibilityEntityIds>(
          std::move(combined_members)),
  };
  return true;
}

bool ViewportWorkspaceService::exit_local_visibility() {
  if (!local_visibility_active()) {
    return false;
  }

  restore_local_visibility_cameras();
  local_visibility_snapshot_ = ViewportLocalVisibilitySnapshot{
      false, next_local_visibility_revision(), nullptr, nullptr};
  local_visibility_auxiliary_camera_backups_.clear();
  return true;
}

bool ViewportWorkspaceService::reset_local_visibility() {
  return exit_local_visibility();
}

void ViewportWorkspaceService::restore_local_visibility_cameras() {
  for (std::size_t index = 0u; index < panes_.size(); ++index) {
    const QVec2 current_render_size = panes_[index].render_size;
    panes_[index].camera = local_visibility_pane_camera_backups_[index];
    panes_[index].camera.set_viewport_size(current_render_size.x,
                                           current_render_size.y);
  }

  for (const AuxiliaryViewportCameraBackup &backup :
       local_visibility_auxiliary_camera_backups_) {
    ViewportWorkspaceAuxiliaryViewportState *viewport_state =
        auxiliary_viewport(backup.stable_id);
    if (viewport_state == nullptr) {
      continue;
    }
    const QVec2 current_render_size = viewport_state->render_size;
    viewport_state->camera = backup.camera;
    viewport_state->camera.set_viewport_size(current_render_size.x,
                                             current_render_size.y);
  }
}

std::uint64_t ViewportWorkspaceService::next_local_visibility_revision() {
  return local_visibility_snapshot_.revision() + 1u;
}

bool ViewportWorkspaceService::focused_pane_is_visible() const {
  const std::vector<ViewportPanePlacement> placements =
      layout_pane_placements();
  return std::any_of(placements.begin(), placements.end(),
                     [&](const ViewportPanePlacement &placement) {
                       return placement.pane_id == focused_pane_;
                     });
}

std::vector<ViewportPanePlacement>
ViewportWorkspaceService::layout_pane_placements() const {
  return viewport_layout_placements(layout_, layout_state_);
}

} // namespace quader
