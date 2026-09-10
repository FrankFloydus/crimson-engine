////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_context_menu_policy.hpp"

#include "api/quader_api_types.hpp"

namespace quader {

bool is_viewport_context_menu_click_gesture(
    const ViewportContextMenuOpenRequest &request) {
  if (request.modal_tool_owns_viewport || !request.right_press_tracked ||
      !request.right_released || !request.release_inside_viewport) {
    return false;
  }

  const QVec2 delta = request.release_screen - request.press_screen;
  const float release_distance_squared = delta.x * delta.x + delta.y * delta.y;
  const float drag_distance_squared =
      request.accumulated_drag_delta.x * request.accumulated_drag_delta.x +
      request.accumulated_drag_delta.y * request.accumulated_drag_delta.y;
  return release_distance_squared <= request.max_click_distance_squared &&
         drag_distance_squared <= request.max_click_distance_squared;
}

bool viewport_context_menu_has_available_actions(
    bool has_modeling_actions, bool has_object_actions, bool has_selection,
    bool has_material_under_mouse, bool has_fixed_command_actions) {
  return has_modeling_actions || has_object_actions || has_selection ||
         has_material_under_mouse || has_fixed_command_actions;
}

std::optional<ViewportContextMenuModelingSection>
viewport_context_menu_modeling_section(QuaderSelectionMode mode) {
  switch (mode) {
  case QuaderSelectionMode::Object:
    return ViewportContextMenuModelingSection::Mesh;
  case QuaderSelectionMode::Vertex:
    return ViewportContextMenuModelingSection::Vertex;
  case QuaderSelectionMode::Edge:
    return ViewportContextMenuModelingSection::Edge;
  case QuaderSelectionMode::Face:
    return ViewportContextMenuModelingSection::Face;
  case QuaderSelectionMode::Group:
    return std::nullopt;
  }
  return std::nullopt;
}

ViewportContextMenuActionBucket
viewport_context_menu_action_bucket(QuaderSelectionMode mode,
                                    std::string_view action_id) {
  if (action_id == "copy_pivot_from_active") {
    return mode == QuaderSelectionMode::Object
               ? ViewportContextMenuActionBucket::Object
               : ViewportContextMenuActionBucket::Suppressed;
  }
  if (action_id == "delete_selection" && (mode == QuaderSelectionMode::Object ||
                                          mode == QuaderSelectionMode::Group)) {
    return ViewportContextMenuActionBucket::Object;
  }
  return ViewportContextMenuActionBucket::Modeling;
}

ViewportContextMenuActionDisposition
viewport_context_menu_action_disposition(QuaderSelectionMode mode,
                                         std::string_view action_id) {
  if (action_id == "apply_active_material") {
    // The current material command is projected explicitly with a consistent
    // label for mesh and face selection, so do not duplicate the Face action
    // supplied by the modeling catalog.
    return ViewportContextMenuActionDisposition::Suppressed;
  }
  if (viewport_context_menu_action_bucket(mode, action_id) ==
      ViewportContextMenuActionBucket::Suppressed) {
    return ViewportContextMenuActionDisposition::Suppressed;
  }
  if (mode == QuaderSelectionMode::Group &&
      action_id == "duplicate_meshes") {
    return ViewportContextMenuActionDisposition::Suppressed;
  }

  switch (mode) {
  case QuaderSelectionMode::Object:
    if (action_id == "snap_meshes_to_floor" || action_id == "combine_mesh" ||
        action_id == "half_cut" || action_id == "create_outer_hull") {
      return ViewportContextMenuActionDisposition::Primary;
    }
    break;
  case QuaderSelectionMode::Vertex:
    if (action_id == "merge_vertices_at_center" ||
        action_id == "bevel_vertices" || action_id == "connect_vertices") {
      return ViewportContextMenuActionDisposition::Primary;
    }
    break;
  case QuaderSelectionMode::Edge:
    if (action_id == "bevel_edges" || action_id == "bridge_edges" ||
        action_id == "connect_edges" || action_id == "fill_hole") {
      return ViewportContextMenuActionDisposition::Primary;
    }
    break;
  case QuaderSelectionMode::Face:
    if (action_id == "thicken_faces" || action_id == "bridge_faces" ||
        action_id == "combine_faces" || action_id == "extract_faces") {
      return ViewportContextMenuActionDisposition::Primary;
    }
    break;
  case QuaderSelectionMode::Group:
    break;
  }

  return ViewportContextMenuActionDisposition::Catalog;
}

std::uint32_t viewport_context_menu_primary_rank(QuaderSelectionMode mode,
                                                 std::string_view action_id) {
  switch (mode) {
  case QuaderSelectionMode::Object:
    if (action_id == "snap_meshes_to_floor") {
      return 10U;
    }
    if (action_id == "combine_mesh") {
      return 20U;
    }
    if (action_id == "half_cut") {
      return 30U;
    }
    if (action_id == "create_outer_hull") {
      return 40U;
    }
    break;
  case QuaderSelectionMode::Vertex:
    if (action_id == "merge_vertices_at_center") {
      return 10U;
    }
    if (action_id == "bevel_vertices") {
      return 20U;
    }
    if (action_id == "connect_vertices") {
      return 30U;
    }
    break;
  case QuaderSelectionMode::Edge:
    if (action_id == "bevel_edges") {
      return 10U;
    }
    if (action_id == "bridge_edges") {
      return 20U;
    }
    if (action_id == "connect_edges") {
      return 30U;
    }
    if (action_id == "fill_hole") {
      return 40U;
    }
    break;
  case QuaderSelectionMode::Face:
    if (action_id == "thicken_faces") {
      return 10U;
    }
    if (action_id == "bridge_faces") {
      return 20U;
    }
    if (action_id == "combine_faces") {
      return 30U;
    }
    if (action_id == "extract_faces") {
      return 40U;
    }
    break;
  case QuaderSelectionMode::Group:
    break;
  }

  return 500U;
}

bool should_open_viewport_context_menu(
    const ViewportContextMenuOpenRequest &request) {
  return is_viewport_context_menu_click_gesture(request) &&
         (request.target_hit || request.has_target_independent_actions) &&
         request.has_actions;
}

bool should_preserve_viewport_context_menu_hover(
    const ViewportContextMenuHoverRequest &request) {
  if (!request.right_press_tracked || request.fly_hud_active) {
    return false;
  }

  const float drag_distance_squared =
      request.accumulated_drag_delta.x * request.accumulated_drag_delta.x +
      request.accumulated_drag_delta.y * request.accumulated_drag_delta.y;
  return drag_distance_squared <= request.max_click_distance_squared;
}

} // namespace quader
