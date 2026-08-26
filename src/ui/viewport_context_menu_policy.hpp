////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"

#include <optional>
#include <string_view>

namespace quader {

enum class QuaderSelectionMode;

/// Names the mode-owned modeling submenu shown by the viewport context menu.
enum class ViewportContextMenuModelingSection {
  Mesh,
  Vertex,
  Edge,
  Face,
};

/// Separates catalog actions by their presentation owner.
enum class ViewportContextMenuActionBucket {
  Modeling,
  Object,
  Suppressed,
};

/**
 * Stores the toolkit-neutral release policy inputs used by the viewport host
 * before it presents a context menu.
 */
struct ViewportContextMenuOpenRequest {
  bool modal_tool_owns_viewport = false;
  bool right_press_tracked = false;
  bool right_released = false;
  bool release_inside_viewport = false;
  bool target_hit = false;
  bool has_target_independent_actions = false;
  bool has_actions = false;
  QVec2 press_screen = {};
  QVec2 release_screen = {};
  QVec2 accumulated_drag_delta = {};
  float max_click_distance_squared = 16.0f;
};

/**
 * Stores the hover-preservation inputs for a pending viewport context click.
 */
struct ViewportContextMenuHoverRequest {
  bool right_press_tracked = false;
  bool fly_hud_active = false;
  QVec2 accumulated_drag_delta = {};
  float max_click_distance_squared = 16.0f;
};

[[nodiscard]] bool is_viewport_context_menu_click_gesture(
    const ViewportContextMenuOpenRequest &request);
[[nodiscard]] bool viewport_context_menu_has_available_actions(
    bool has_modeling_actions, bool has_object_actions, bool has_selection,
    bool has_material_under_mouse, bool has_fixed_command_actions);
[[nodiscard]] std::optional<ViewportContextMenuModelingSection>
viewport_context_menu_modeling_section(QuaderSelectionMode mode);
[[nodiscard]] ViewportContextMenuActionBucket
viewport_context_menu_action_bucket(QuaderSelectionMode mode,
                                    std::string_view action_id);
[[nodiscard]] bool should_open_viewport_context_menu(
    const ViewportContextMenuOpenRequest &request);
[[nodiscard]] bool should_preserve_viewport_context_menu_hover(
    const ViewportContextMenuHoverRequest &request);

} // namespace quader
