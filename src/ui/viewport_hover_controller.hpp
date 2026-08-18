////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_router.hpp"
#include "api/quader_viewport_layout_types.hpp"

#include <cstdint>

namespace quader {

/**
 * Stores the Viewport Hover Work Key data contract used by the Qt editor presentation layer.
 */
struct ViewportHoverWorkKey {
    bool valid = false;
    ViewportPaneId pane_id = ViewportPaneId::Perspective;
    ViewportToolId active_tool = ViewportToolId::Select;
    ViewportSelectionMode selection_mode = ViewportSelectionMode::Object;
    ModelingBoxDragState box_drag_state = ModelingBoxDragState::Idle;
    int grid_preset = 0;
    bool grid_snap_enabled = true;
    QVec2 mouse_local = {};
    QVec2 viewport_size = {};
    QVec3 camera_eye = {};
    QVec3 camera_forward = {0.0f, 0.0f, -1.0f};
    QVec3 camera_right = {1.0f, 0.0f, 0.0f};
    QVec3 camera_up = {0.0f, 1.0f, 0.0f};
    ViewportOverlayProjection camera_projection =
        ViewportOverlayProjection::Perspective;
    float camera_orthographic_size = 24.0f;
    float camera_fov_degrees = 60.0f;
    float camera_near_plane = 0.05f;
    float camera_far_plane = 1000.0f;
    bool primary_modifier = false;
    bool shift_modifier = false;
    bool alt_modifier = false;
    bool gizmo_consumed = false;
    double vertex_radius = 0.3;
    double edge_radius = 0.2;
    bool select_backfaces = false;
    std::uint64_t scene_revision = 0;
    std::uint64_t asset_revision = 0;
    std::uint64_t local_visibility_revision = 0;
    std::uint64_t content_revision = 0;
    std::uint64_t selection_revision = 0;

    [[nodiscard]] bool matches(const ViewportHoverWorkKey& other) const;
};

/**
 * Stores the Viewport Hover Work Request data contract used by the Qt editor presentation layer.
 */
struct ViewportHoverWorkRequest {
  ViewportPaneId pane_id = ViewportPaneId::Perspective;
  ViewportToolId active_tool = ViewportToolId::Select;
  ViewportSelectionMode selection_mode = ViewportSelectionMode::Object;
  ModelingBoxDragState box_drag_state = ModelingBoxDragState::Idle;
  int grid_preset = 0;
  bool grid_snap_enabled = true;
  QVec2 mouse_local = {};
  QVec2 viewport_size = {};
  ViewportModifiers modifiers = {};
  bool gizmo_consumed = false;
  double vertex_radius = 0.3;
  double edge_radius = 0.2;
  bool select_backfaces = false;
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  std::uint64_t local_visibility_revision = 0;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
};

/**
 * Coordinates Viewport Hover Controller state transitions for the Qt editor presentation layer.
 */
class ViewportHoverController {
public:
  [[nodiscard]] ViewportHoverWorkKey
  make_key(const ViewportCameraController &camera,
           const ViewportHoverWorkRequest &request) const;

  [[nodiscard]] bool claim(const ViewportHoverWorkKey &key);
  void invalidate();

private:
  ViewportHoverWorkKey last_key_;
};

} // namespace quader
