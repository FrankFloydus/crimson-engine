////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_input_types.hpp"
#include "viewport/viewport_navigation_defaults.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Represents a Viewport Host Defaults value used by the viewport input, layout, and camera layer.
 */
struct ViewportHostDefaults {
  double map_units_per_world_unit = 32.0;
  double default_grid_map_units = 32.0;
  double default_orthographic_size = 24.0;
  double focus_origin_distance = 64.0;
  double default_fov_degrees = 60.0;
  double orbit_radians_per_pixel = 0.006;
  double fly_look_radians_per_pixel = 0.004;
  double fly_base_speed = kDefaultViewportFlyNavigationSpeed;
  double fly_fast_multiplier = 4.0;
  double fly_slow_multiplier = 0.25;
  double selection_box_drag_threshold_pixels = 4.0;
};

/**
 * Represents one viewport shortcut binding projected from editor metadata into viewport terms.
 */
struct ViewportShortcutBindingDescriptor {
  std::string target_id;
  std::string shortcut;
};

/**
 * Represents one grid preset shortcut binding projected from editor metadata into viewport terms.
 */
struct ViewportGridShortcutBindingDescriptor {
    int preset = 0;
    std::string shortcut;
};

/**
 * Projects one canonical host grid-size preset into viewport-owned terms.
 */
struct ViewportGridPresetDescriptor {
  int preset = 0;
  double map_units = 1.0;
  double world_units = 1.0 / 32.0;
  bool is_default = false;
};

/// Classifies the semantic viewport state that owns an input action.
enum class ViewportInputBindingContext {
  Viewport,
  Navigation,
  FlyNavigation,
  ModalTool,
  LinearArrayTool,
  RadialArrayTool,
};

/**
 * Represents one semantic viewport input action binding target.
 */
struct ViewportInputActionBindingDescriptor {
  std::string target_id;
  std::string_view action_id;
  std::string_view label;
  std::string_view localization_key;
  ViewportInputBindingContext context =
      ViewportInputBindingContext::Navigation;
  std::string shortcut;
  std::string_view when;
  int weight = 20;
  ViewportInputActionKind action = ViewportInputActionKind::None;
};

/**
 * Represents one host shortcut target mapped to command and viewport execution terms.
 */
struct ViewportShortcutTargetDescriptor {
  std::string_view contract_target_id;
  std::string_view command_target_id;
  ViewportShortcut shortcut;
};

bool viewport_host_contract_available();
std::string viewport_input_action_target_id(std::string_view suffix);
std::string viewport_selection_mode_target_id(std::string_view mode);
std::string viewport_grid_preset_target_id(int preset);
std::span<const ViewportShortcutTargetDescriptor>
viewport_shortcut_target_catalog();
std::vector<ViewportInputActionBindingDescriptor>
viewport_input_action_binding_catalog();
bool viewport_input_action_for_target_id(std::string_view target_id,
                                         ViewportInputActionKind &action);
std::vector<ViewportShortcutBindingDescriptor>
viewport_shortcut_binding_catalog();
std::vector<ViewportGridShortcutBindingDescriptor>
viewport_grid_shortcut_binding_catalog();
std::vector<ViewportGridPresetDescriptor> viewport_grid_preset_catalog();
std::optional<std::string>
viewport_command_target_id_for_contract_target(std::string_view target_id);
std::optional<ViewportView>
viewport_view_for_command_target_id(std::string_view target_id);
std::optional<std::string_view>
viewport_command_target_id_for_view(ViewportView view);
ViewportHostDefaults viewport_host_defaults();
std::string_view viewport_tool_contract_id(ViewportToolId tool);
bool viewport_tool_has_host_contract(ViewportToolId tool);
bool viewport_tool_has_native_controller(ViewportToolId tool);
bool viewport_tool_allows_selection_fallback(ViewportToolId tool);
int viewport_default_grid_preset();
int viewport_clamp_grid_preset(int preset);
float viewport_grid_world_size_from_contract(int preset);
bool viewport_tool_from_contract_id(std::string_view id, ViewportToolId &tool);
bool viewport_selection_mode_from_contract_id(std::string_view id,
                                              ViewportSelectionMode &mode);
bool viewport_shortcut_from_contract_target(std::string_view target_id,
                                            ViewportShortcut &shortcut);
bool viewport_shortcut_for_command_target_id(std::string_view target_id,
                                             ViewportShortcut &shortcut);
bool viewport_target_requires_router_dispatch(std::string_view target_id);
bool viewport_shortcuts_match(const ViewportShortcut &lhs,
                              const ViewportShortcut &rhs);
std::optional<std::string> viewport_command_target_id_for_router_shortcut(
    const ViewportShortcut &shortcut);
std::optional<std::string>
viewport_operation_preview_command_target_id_for_router_shortcut(
    const ViewportShortcut &shortcut);
bool viewport_router_shortcut_for_target_id(std::string_view target_id,
                                            ViewportShortcut &shortcut);
bool viewport_router_shortcut_for_target_id(std::string_view target_id,
                                            int current_grid_preset,
                                            ViewportShortcut &shortcut);

} // namespace quader
