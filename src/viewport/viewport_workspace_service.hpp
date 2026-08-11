////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_layout.hpp"
#include "viewport/viewport_local_visibility.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Enumerates ViewportShadingMode values used by the viewport layer.
 */
enum class ViewportShadingMode {
    Wireframe,
    Shaded,
    Rendered,
    DiffuseOnly,
    SpecularOnly,
    Albedo,
    Roughness,
    Metalness,
    Normal,
};

[[nodiscard]] constexpr bool
viewport_shading_mode_is_valid(ViewportShadingMode mode) {
  switch (mode) {
  case ViewportShadingMode::Wireframe:
  case ViewportShadingMode::Shaded:
  case ViewportShadingMode::Rendered:
  case ViewportShadingMode::DiffuseOnly:
  case ViewportShadingMode::SpecularOnly:
  case ViewportShadingMode::Albedo:
  case ViewportShadingMode::Roughness:
  case ViewportShadingMode::Metalness:
  case ViewportShadingMode::Normal:
    return true;
  }
  return false;
}

[[nodiscard]] constexpr bool
viewport_shading_is_debug_view(ViewportShadingMode mode) {
  switch (mode) {
  case ViewportShadingMode::DiffuseOnly:
  case ViewportShadingMode::SpecularOnly:
  case ViewportShadingMode::Albedo:
  case ViewportShadingMode::Roughness:
  case ViewportShadingMode::Metalness:
  case ViewportShadingMode::Normal:
    return true;
  case ViewportShadingMode::Wireframe:
  case ViewportShadingMode::Shaded:
  case ViewportShadingMode::Rendered:
    return false;
  }
  return false;
}

[[nodiscard]] constexpr bool
viewport_shading_uses_rendered_materials(ViewportShadingMode mode) {
  return mode == ViewportShadingMode::Rendered ||
         viewport_shading_is_debug_view(mode);
}

[[nodiscard]] constexpr bool
viewport_shading_uses_scene_lighting(ViewportShadingMode mode) {
  return mode == ViewportShadingMode::Rendered ||
         mode == ViewportShadingMode::DiffuseOnly ||
         mode == ViewportShadingMode::SpecularOnly;
}

[[nodiscard]] constexpr bool
viewport_shading_shows_light_gizmos(ViewportShadingMode mode) {
  return mode == ViewportShadingMode::Shaded ||
         mode == ViewportShadingMode::Rendered;
}

/**
 * Enumerates Viewport Workspace Layout Policy values used by the viewport layer.
 */
enum class ViewportWorkspaceLayoutPolicy {
    // Host policy for integrations that expose only one embedded viewport surface.
    SinglePaneOnly,
    MultiPane,
};

/**
 * Stores the Viewport Workspace Pane State data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportWorkspacePaneState {
    ViewportPaneId id = ViewportPaneId::Perspective;
    ViewportShadingMode shading = ViewportShadingMode::Shaded;
    ViewportCameraController camera;
    QVec2 render_size = {1.0f, 1.0f};
    bool visible_this_frame = false;
};

/**
 * Stores state for a runtime-created viewport surface that is not part of the embedded pane layout.
 */
struct ViewportWorkspaceAuxiliaryViewportState {
  std::string stable_id;
  std::uint8_t layer_index = 0u;
  ViewportCameraController camera;
  QVec2 render_size = {1.0f, 1.0f};
  bool visible_this_frame = false;
};

void reset_viewport_camera_for_view(ViewportCameraController &camera,
                                    ViewportView view);
[[nodiscard]] ViewportView
viewport_view_for_camera(const ViewportCameraController &camera);

/**
 * Coordinates Viewport Workspace Service responsibilities for the viewport input, layout, and camera layer.
 */
class ViewportWorkspaceService final {
public:
  explicit ViewportWorkspaceService(
      ViewportWorkspaceLayoutPolicy layout_policy =
          ViewportWorkspaceLayoutPolicy::MultiPane);

  [[nodiscard]] ViewportLayoutId layout() const { return layout_; }
  bool set_layout(ViewportLayoutId layout);
  [[nodiscard]] ViewportWorkspaceLayoutPolicy layout_policy() const {
    return layout_policy_;
  }
  bool set_layout_policy(ViewportWorkspaceLayoutPolicy policy);
  [[nodiscard]] bool supports_layout(ViewportLayoutId layout) const;

  [[nodiscard]] ViewportPaneId focused_pane() const { return focused_pane_; }
  bool focus_pane(ViewportPaneId pane);
  [[nodiscard]] bool focused_pane_maximized() const {
    return focused_pane_maximized_;
  }
  bool set_focused_pane_maximized(bool maximized);
  bool toggle_focused_pane_maximized();

  [[nodiscard]] int grid_preset() const { return grid_preset_; }
  bool set_grid_preset(int preset);

  [[nodiscard]] ViewportShadingMode shading_mode(ViewportPaneId pane) const;
  bool set_shading_mode(ViewportPaneId pane, ViewportShadingMode mode);

  [[nodiscard]] ViewportView view(ViewportPaneId pane) const;
  bool set_view(ViewportPaneId pane, ViewportView view);

  [[nodiscard]] ViewportCameraController &camera(ViewportPaneId pane);
  [[nodiscard]] const ViewportCameraController &
  camera(ViewportPaneId pane) const;
  [[nodiscard]] ViewportCameraPose persisted_perspective_camera_pose() const;
  [[nodiscard]] std::uint64_t
  persisted_perspective_camera_pose_signature() const;
  bool restore_persisted_perspective_camera_pose(
      const std::optional<ViewportCameraPose> &camera_pose);
  bool set_default_fly_speed(float speed);
  [[nodiscard]] float default_fly_speed() const {
    return default_fly_speed_;
  }
  bool focus_origin(ViewportPaneId pane, float distance = 64.0f);
  bool focus_local_visibility_bounds(QVec3 min_bounds, QVec3 max_bounds,
                                     float margin = 1.25f);

  [[nodiscard]] ViewportWorkspacePaneState &pane(ViewportPaneId pane);
  [[nodiscard]] const ViewportWorkspacePaneState &
  pane(ViewportPaneId pane) const;

  [[nodiscard]] std::vector<ViewportPanePlacement>
  active_pane_placements() const;
  [[nodiscard]] std::vector<ViewportSplitHandle> active_split_handles() const;
  void set_split_handle_position(ViewportSplitHandleId handle,
                                 float normalized_position);

  void clear_pane_visibility();
  void set_pane_visible(ViewportPaneId pane, bool visible);
  [[nodiscard]] bool pane_visible(ViewportPaneId pane) const;
  bool set_pane_render_size(ViewportPaneId pane, QVec2 size);
  [[nodiscard]] QVec2 pane_render_size(ViewportPaneId pane) const;

  [[nodiscard]] ViewportWorkspaceAuxiliaryViewportState &
  ensure_auxiliary_viewport(std::string stable_id, std::uint8_t layer_index);
  bool remove_auxiliary_viewport(std::string_view stable_id);
  [[nodiscard]] ViewportWorkspaceAuxiliaryViewportState *
  auxiliary_viewport(std::string_view stable_id);
  [[nodiscard]] const ViewportWorkspaceAuxiliaryViewportState *
  auxiliary_viewport(std::string_view stable_id) const;
  [[nodiscard]] std::size_t auxiliary_viewport_count() const {
    return auxiliary_viewports_.size();
  }
  [[nodiscard]] ViewportView
  auxiliary_viewport_view(std::string_view stable_id) const;
  bool set_auxiliary_viewport_view(std::string_view stable_id,
                                   ViewportView view);
  void set_auxiliary_viewport_visible(std::string_view stable_id, bool visible);
  [[nodiscard]] bool
  auxiliary_viewport_visible(std::string_view stable_id) const;
  bool set_auxiliary_viewport_render_size(std::string_view stable_id,
                                          QVec2 size);

  [[nodiscard]] bool local_visibility_active() const {
    return local_visibility_snapshot_.active();
  }
  [[nodiscard]] std::uint64_t local_visibility_revision() const {
    return local_visibility_snapshot_.revision();
  }
  [[nodiscard]] ViewportLocalVisibilitySnapshot
  local_visibility_snapshot() const {
    return local_visibility_snapshot_;
  }
  bool enter_local_visibility(
      std::span<const ViewportLocalVisibilityEntityId> baseline_entities,
      std::span<const ViewportLocalVisibilityEntityId> members);
  bool add_local_visibility_members(
      std::span<const ViewportLocalVisibilityEntityId> members);
  bool exit_local_visibility();
  bool reset_local_visibility();

private:
  /// Captures one pre-entry auxiliary viewport camera by stable identity.
  struct AuxiliaryViewportCameraBackup {
    std::string stable_id;
    ViewportCameraController camera;
  };

  void restore_local_visibility_cameras();
  [[nodiscard]] std::uint64_t next_local_visibility_revision();
  [[nodiscard]] bool focused_pane_is_visible() const;
  [[nodiscard]] std::vector<ViewportPanePlacement>
  layout_pane_placements() const;

  std::array<ViewportWorkspacePaneState, kViewportPaneCount> panes_;
  ViewportLayoutState layout_state_;
  ViewportLayoutId layout_ = ViewportLayoutId::Single;
  ViewportWorkspaceLayoutPolicy layout_policy_ =
      ViewportWorkspaceLayoutPolicy::MultiPane;
  ViewportPaneId focused_pane_ = ViewportPaneId::Perspective;
  bool focused_pane_maximized_ = false;
  int grid_preset_ = 0;
  float default_fly_speed_ = kDefaultViewportFlyNavigationSpeed;
  std::vector<ViewportWorkspaceAuxiliaryViewportState> auxiliary_viewports_;
  ViewportLocalVisibilitySnapshot local_visibility_snapshot_;
  std::array<ViewportCameraController, kViewportPaneCount>
      local_visibility_pane_camera_backups_;
  std::vector<AuxiliaryViewportCameraBackup>
      local_visibility_auxiliary_camera_backups_;
};

} // namespace quader
