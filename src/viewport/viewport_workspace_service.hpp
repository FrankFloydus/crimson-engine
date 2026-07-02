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

#include <array>
#include <cstdint>
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
};

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
  bool focus_origin(ViewportPaneId pane, float distance = 64.0f);

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

private:
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
  std::vector<ViewportWorkspaceAuxiliaryViewportState> auxiliary_viewports_;
};

} // namespace quader
