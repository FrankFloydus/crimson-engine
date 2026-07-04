////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/host_viewport_contract_adapter.hpp"

#include "api/quader_command_ids.hpp"
#include "core/public_command_targets.hpp"
#include "editor/commands/modeling_command_tool_catalog.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"

#include <editor/metadata/host_contract.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace quader {
namespace {

std::string viewport_shortcut_label(bool primary, bool shift, bool alt,
                                    std::string_view key) {
  std::string shortcut;
  if (primary) {
    shortcut += "Ctrl+";
  }
  if (shift) {
    shortcut += "Shift+";
  }
  if (alt) {
    shortcut += "Alt+";
  }
  shortcut += key;
  return shortcut;
}

ViewportShortcut viewport_target_shortcut(
    ViewportShortcutKind kind,
    ViewportShortcutContext context = ViewportShortcutContext::Viewport) {
  return {
      .kind = kind,
      .context = context,
  };
}

ViewportShortcut viewport_mirror_target_shortcut(ViewportMirrorAxis axis) {
  return {
      .kind = ViewportShortcutKind::MirrorAxis,
      .context = ViewportShortcutContext::Viewport,
      .mirror_axis = axis,
  };
}

ViewportShortcut viewport_view_target_shortcut(ViewportView view) {
  return {
      .kind = ViewportShortcutKind::SetView,
      .context = ViewportShortcutContext::Viewport,
      .view = view,
  };
}

std::optional<std::string> viewport_modeling_command_target_id_for_host_target(
    std::string_view target_id) {
  for (const CommandTool *tool : modeling_command_tools()) {
    if (tool == nullptr) {
      continue;
    }
    if (tool->id() == target_id ||
        tool->host_shortcut_target_id() == target_id) {
      return tool->public_command_target_id();
    }
  }
  return std::nullopt;
}

std::optional<std::string> viewport_command_target_id_for_shortcut_descriptor(
    const ViewportShortcutTargetDescriptor &descriptor) {
  if (!descriptor.contract_target_id.empty()) {
    if (std::optional<std::string> modeling_target =
            viewport_modeling_command_target_id_for_host_target(
                descriptor.contract_target_id)) {
      return modeling_target;
    }
  }
  if (!descriptor.command_target_id.empty()) {
    return std::string(descriptor.command_target_id);
  }
  return std::nullopt;
}

} // namespace

bool viewport_host_contract_available() { return true; }

std::string viewport_input_action_target_id(std::string_view suffix) {
  std::string target_id("viewport.");
  target_id.append(suffix);
  return target_id;
}

std::string viewport_selection_mode_target_id(std::string_view mode) {
  return quader_viewport_selection_mode_target_id(mode);
}

std::string viewport_grid_preset_target_id(int preset) {
  return quader_viewport_grid_preset_target_id(preset);
}

std::span<const ViewportShortcutTargetDescriptor>
viewport_shortcut_target_catalog() {
  static const std::array<ViewportShortcutTargetDescriptor, 28> kDescriptors{{
      {"new_document", quader_command::kProjectNew.string(),
       viewport_target_shortcut(ViewportShortcutKind::None,
                                ViewportShortcutContext::Global)},
      {"open_document", quader_command::kProjectOpen.string(),
       viewport_target_shortcut(ViewportShortcutKind::None,
                                ViewportShortcutContext::Global)},
      {"save_document", quader_command::kProjectSave.string(),
       viewport_target_shortcut(ViewportShortcutKind::None,
                                ViewportShortcutContext::Global)},
      {"save_as_document", quader_command::kProjectSaveAs.string(),
       viewport_target_shortcut(ViewportShortcutKind::None,
                                ViewportShortcutContext::Global)},
      {"select_all",
       {},
       viewport_target_shortcut(ViewportShortcutKind::SelectAll,
                                ViewportShortcutContext::Global)},
      {"copy_meshes",
       {},
       viewport_target_shortcut(ViewportShortcutKind::Copy,
                                ViewportShortcutContext::Global)},
      {"duplicate_selection", quader_command::kDuplicateSelection.string(),
       viewport_target_shortcut(ViewportShortcutKind::Duplicate,
                                ViewportShortcutContext::Global)},
      {"invert_selection",
       {},
       viewport_target_shortcut(ViewportShortcutKind::InvertSelection,
                                ViewportShortcutContext::Global)},
      {"repeat_last_action",
       {},
       viewport_target_shortcut(ViewportShortcutKind::RepeatLastAction,
                                ViewportShortcutContext::Global)},
      {"paste_meshes",
       {},
       viewport_target_shortcut(ViewportShortcutKind::Paste,
                                ViewportShortcutContext::Global)},
      {"undo", quader_command::kEditUndo.string(),
       viewport_target_shortcut(ViewportShortcutKind::Undo,
                                ViewportShortcutContext::Global)},
      {"redo", quader_command::kEditRedo.string(),
       viewport_target_shortcut(ViewportShortcutKind::Redo,
                                ViewportShortcutContext::Global)},
      {"delete", quader_command::kDeleteSelection.string(),
       viewport_target_shortcut(ViewportShortcutKind::DeleteSelection,
                                ViewportShortcutContext::Global)},
      {"merge_vertices",
       {},
       viewport_target_shortcut(ViewportShortcutKind::MergeActive)},
      {"merge_vertices_at_center",
       {},
       viewport_target_shortcut(ViewportShortcutKind::MergeCenter)},
      {"apply_active_material",
       {},
       viewport_target_shortcut(ViewportShortcutKind::ApplyMaterial)},
      {"invert_mesh",
       {},
       viewport_target_shortcut(ViewportShortcutKind::FlipMeshNormals)},
      {"focus_all", quader_command::kViewportFocusAll.string(),
       viewport_target_shortcut(ViewportShortcutKind::FocusDocument)},
      {"focus_selection", quader_command::kViewportFrameSelection.string(), {}},
      {"focus_origin", quader_command::kViewportFocusOrigin.string(),
       viewport_target_shortcut(ViewportShortcutKind::FocusOrigin)},
      {"mirror_axis_x",
       {},
       viewport_mirror_target_shortcut(ViewportMirrorAxis::X)},
      {"mirror_axis_y",
       {},
       viewport_mirror_target_shortcut(ViewportMirrorAxis::Y)},
      {"mirror_axis_z",
       {},
       viewport_mirror_target_shortcut(ViewportMirrorAxis::Z)},
      {"flip_horizontal",
       {},
       viewport_target_shortcut(ViewportShortcutKind::FlipHorizontal)},
      {"flip_vertical",
       {},
       viewport_target_shortcut(ViewportShortcutKind::FlipVertical)},
      {"", "viewport.cancel_active_tool",
       viewport_target_shortcut(ViewportShortcutKind::CancelActiveTool)},
      {"", "viewport.toggle_maximized",
       viewport_target_shortcut(ViewportShortcutKind::ToggleMaximized)},
      {"", "viewport.delete_popup",
       viewport_target_shortcut(ViewportShortcutKind::DeletePopup)},
  }};
  return kDescriptors;
}

std::vector<ViewportInputActionBindingDescriptor>
viewport_input_action_binding_catalog() {
  return {
      {
          .target_id = viewport_input_action_target_id("navigation.orbit"),
          .shortcut = "MiddleMouseDrag",
          .action = ViewportInputActionKind::Orbit,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.orbit_snap"),
          .shortcut = "Alt+MiddleMouseDrag",
          .action = ViewportInputActionKind::OrbitSnap,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.pan"),
          .shortcut = "Shift+MiddleMouseDrag",
          .action = ViewportInputActionKind::Pan,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.fly"),
          .shortcut = "RightMouseDrag",
          .action = ViewportInputActionKind::Fly,
      },
      {
          .target_id =
              viewport_input_action_target_id("navigation.reset_fly_speed"),
          .shortcut = "MiddleMouse",
          .action = ViewportInputActionKind::ResetFlySpeed,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.cancel"),
          .shortcut = "Escape",
          .when = "viewportFocus && !textInputActive && navigation.active",
          .weight = 30,
          .action = ViewportInputActionKind::CancelNavigation,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.zoom"),
          .shortcut = "Wheel",
          .action = ViewportInputActionKind::Zoom,
      },
      {
          .target_id =
              viewport_input_action_target_id("navigation.zoom_to_cursor"),
          .shortcut = "Ctrl+Wheel",
          .action = ViewportInputActionKind::ZoomToCursor,
      },
      {
          .target_id =
              viewport_input_action_target_id("navigation.fly_forward"),
          .shortcut = "W",
          .when = "viewportFocus && !textInputActive && navigation.flyActive",
          .action = ViewportInputActionKind::FlyForward,
      },
      {
          .target_id =
              viewport_input_action_target_id("navigation.fly_backward"),
          .shortcut = "S",
          .when = "viewportFocus && !textInputActive && navigation.flyActive",
          .action = ViewportInputActionKind::FlyBackward,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.fly_right"),
          .shortcut = "D",
          .when = "viewportFocus && !textInputActive && navigation.flyActive",
          .action = ViewportInputActionKind::FlyRight,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.fly_left"),
          .shortcut = "A",
          .when = "viewportFocus && !textInputActive && navigation.flyActive",
          .action = ViewportInputActionKind::FlyLeft,
      },
      {
          .target_id = viewport_input_action_target_id("modal_tool.finalize"),
          .shortcut = "Space",
          .when = "viewportFocus && !textInputActive && modalTool.active && "
                  "!navigation.active",
          .weight = 30,
          .action = ViewportInputActionKind::ModalToolFinalize,
      },
      {
          .target_id = viewport_input_action_target_id("modal_tool.finalize"),
          .shortcut = "Enter",
          .when = "viewportFocus && !textInputActive && modalTool.active && "
                  "!navigation.active",
          .weight = 30,
          .action = ViewportInputActionKind::ModalToolFinalize,
      },
      {
          .target_id = viewport_input_action_target_id("modal_tool.mode_cycle"),
          .shortcut = "F",
          .when = "viewportFocus && !textInputActive && modalTool.active && "
                  "!navigation.active",
          .weight = 30,
          .action = ViewportInputActionKind::ModalToolModeCycle,
      },
  };
}

bool viewport_input_action_for_target_id(std::string_view target_id,
                                         ViewportInputActionKind &action) {
  for (const ViewportInputActionBindingDescriptor &descriptor :
       viewport_input_action_binding_catalog()) {
    if (descriptor.target_id == target_id) {
      action = descriptor.action;
      return true;
    }
  }
  return false;
}

std::vector<ViewportShortcutBindingDescriptor>
viewport_shortcut_binding_catalog() {
  std::vector<ViewportShortcutBindingDescriptor> bindings;
  for (const quader_host_contract::HostShortcutDescriptor &descriptor :
       quader_host_contract::shortcut_catalog()) {
    bindings.push_back({
        .target_id = std::string(descriptor.target_id),
        .shortcut = viewport_shortcut_label(descriptor.primary_modifier,
                                            descriptor.shift, descriptor.alt,
                                            descriptor.key),
    });
  }
  return bindings;
}

std::vector<ViewportGridShortcutBindingDescriptor>
viewport_grid_shortcut_binding_catalog() {
  std::vector<ViewportGridShortcutBindingDescriptor> bindings;
  const auto presets = quader_host_contract::grid_preset_catalog();
  bindings.reserve(presets.size());
  for (std::size_t index = 0u; index < presets.size(); ++index) {
    if (presets[index].shortcut.empty()) {
      continue;
    }
    bindings.push_back({
        .preset = static_cast<int>(index + 1u),
        .shortcut = std::string(presets[index].shortcut),
    });
  }
  return bindings;
}

std::optional<ViewportView>
viewport_view_for_command_target_id(std::string_view target_id) {
  if (target_id == quader_command::kViewportViewPerspective.string()) {
    return ViewportView::Perspective;
  }
  if (target_id == quader_command::kViewportViewTop.string()) {
    return ViewportView::Top;
  }
  if (target_id == quader_command::kViewportViewBottom.string()) {
    return ViewportView::Bottom;
  }
  if (target_id == quader_command::kViewportViewLeft.string()) {
    return ViewportView::Left;
  }
  if (target_id == quader_command::kViewportViewRight.string()) {
    return ViewportView::Side;
  }
  if (target_id == quader_command::kViewportViewFront.string()) {
    return ViewportView::Front;
  }
  if (target_id == quader_command::kViewportViewBack.string()) {
    return ViewportView::Back;
  }
  return std::nullopt;
}

std::optional<std::string_view>
viewport_command_target_id_for_view(ViewportView view) {
  switch (view) {
  case ViewportView::Perspective:
    return quader_command::kViewportViewPerspective.string();
  case ViewportView::Top:
    return quader_command::kViewportViewTop.string();
  case ViewportView::Bottom:
    return quader_command::kViewportViewBottom.string();
  case ViewportView::Left:
    return quader_command::kViewportViewLeft.string();
  case ViewportView::Side:
    return quader_command::kViewportViewRight.string();
  case ViewportView::Front:
    return quader_command::kViewportViewFront.string();
  case ViewportView::Back:
    return quader_command::kViewportViewBack.string();
  }
  return std::nullopt;
}

ViewportHostDefaults viewport_host_defaults() {
  const quader_host_contract::HostViewportDefaultsDescriptor defaults =
      quader_host_contract::viewport_defaults();
  return {
      .map_units_per_world_unit = defaults.map_units_per_world_unit,
      .default_grid_map_units = defaults.default_grid_map_units,
      .default_orthographic_size = defaults.default_orthographic_size,
      .focus_origin_distance = defaults.focus_origin_distance,
      .default_fov_degrees = defaults.default_fov_degrees,
      .orbit_radians_per_pixel = defaults.orbit_radians_per_pixel,
      .fly_look_radians_per_pixel = defaults.fly_look_radians_per_pixel,
      .fly_base_speed = defaults.fly_base_speed,
      .fly_fast_multiplier = defaults.fly_fast_multiplier,
      .fly_slow_multiplier = defaults.fly_slow_multiplier,
      .selection_box_drag_threshold_pixels =
          defaults.selection_box_drag_threshold_pixels,
  };
}

std::string_view viewport_tool_contract_id(ViewportToolId tool) {
  const ToolDescriptor *descriptor = find_viewport_tool_descriptor(tool);
  return descriptor != nullptr ? descriptor->id : std::string_view{};
}

bool viewport_tool_has_host_contract(ViewportToolId tool) {
  const std::string_view id = viewport_tool_contract_id(tool);
  return !id.empty() &&
         quader_host_contract::find_tool_descriptor(id) != nullptr;
}

bool viewport_tool_has_native_controller(ViewportToolId tool) {
  return find_viewport_tool_descriptor(tool) != nullptr &&
         viewport_tool_has_host_contract(tool);
}

bool viewport_tool_allows_selection_fallback(ViewportToolId tool) {
  const ToolDescriptor *descriptor = find_viewport_tool_descriptor(tool);
  return descriptor != nullptr && descriptor->allows_selection_fallback;
}

int viewport_default_grid_preset() {
  const auto presets = quader_host_contract::grid_preset_catalog();
  for (std::size_t index = 0; index < presets.size(); ++index) {
    if (presets[index].default_preset) {
      return static_cast<int>(index + 1U);
    }
  }

  const double default_map_units =
      quader_host_contract::viewport_defaults().default_grid_map_units;
  for (std::size_t index = 0; index < presets.size(); ++index) {
    if (std::abs(presets[index].map_units - default_map_units) <= 0.0001) {
      return static_cast<int>(index + 1U);
    }
  }
  return 6;
}

int viewport_clamp_grid_preset(int preset) {
  const auto presets = quader_host_contract::grid_preset_catalog();
  const int max_preset =
      presets.empty() ? 10 : static_cast<int>(presets.size());
  return std::clamp(preset, 1, max_preset);
}

float viewport_grid_world_size_from_contract(int preset) {
  const auto presets = quader_host_contract::grid_preset_catalog();
  if (!presets.empty()) {
    const int clamped_preset = viewport_clamp_grid_preset(preset);
    return static_cast<float>(
        presets[static_cast<std::size_t>(clamped_preset - 1)].world_units);
  }
  const int clamped_preset = viewport_clamp_grid_preset(preset);
  const float map_units =
      std::pow(2.0f, static_cast<float>(clamped_preset - 1));
  return map_units / 32.0f;
}

bool viewport_tool_from_contract_id(std::string_view id, ViewportToolId &tool) {
  if (const ToolDescriptor *descriptor = find_viewport_tool_descriptor(id)) {
    tool = descriptor->tool;
    return true;
  }
  if (const quader_host_contract::HostActionDescriptor *descriptor =
          quader_host_contract::find_action_descriptor(id);
      descriptor != nullptr &&
      descriptor->kind == quader_host_contract::HostActionKind::Tool &&
      !descriptor->tool_id.empty()) {
    return viewport_tool_from_contract_id(descriptor->tool_id, tool);
  }
  return false;
}

bool viewport_selection_mode_from_contract_id(std::string_view id,
                                              ViewportSelectionMode &mode) {
  if (const std::optional<quader_host_contract::HostSelectionMode> host_mode =
          host_selection_mode_from_id(id)) {
    mode = host_selection_mode_to_viewport(*host_mode);
    return true;
  }
  return false;
}

std::optional<std::string>
viewport_command_target_id_for_contract_target(std::string_view target_id) {
  for (const ViewportShortcutTargetDescriptor &descriptor :
       viewport_shortcut_target_catalog()) {
    if (!descriptor.contract_target_id.empty() &&
        descriptor.contract_target_id == target_id) {
      return viewport_command_target_id_for_shortcut_descriptor(descriptor);
    }
  }

  ViewportToolId tool = ViewportToolId::Select;
  if (viewport_tool_from_contract_id(target_id, tool)) {
    return quader_tool_activation_command_target_id(
        viewport_tool_contract_id(tool));
  }

  ViewportSelectionMode mode = ViewportSelectionMode::Object;
  if (viewport_selection_mode_from_contract_id(target_id, mode)) {
    return viewport_selection_mode_target_id(viewport_selection_mode_id(mode));
  }

  return std::nullopt;
}

bool viewport_shortcut_from_contract_target(std::string_view target_id,
                                            ViewportShortcut &shortcut) {
  for (const ViewportShortcutTargetDescriptor &descriptor :
       viewport_shortcut_target_catalog()) {
    if (!descriptor.contract_target_id.empty() &&
        descriptor.contract_target_id == target_id) {
      shortcut = descriptor.shortcut;
      return true;
    }
  }

  ViewportToolId tool = ViewportToolId::Select;
  if (viewport_tool_from_contract_id(target_id, tool)) {
    shortcut.kind = ViewportShortcutKind::ActivateTool;
    shortcut.context = ViewportShortcutContext::Viewport;
    shortcut.tool = tool;
    return true;
  }

  ViewportSelectionMode mode = ViewportSelectionMode::Object;
  if (viewport_selection_mode_from_contract_id(target_id, mode)) {
    shortcut.kind = ViewportShortcutKind::SetSelectionMode;
    shortcut.context = ViewportShortcutContext::Viewport;
    shortcut.selection_mode = mode;
    return true;
  }

  return false;
}

bool viewport_shortcut_for_command_target_id(std::string_view target_id,
                                             ViewportShortcut &shortcut) {
  if (const std::optional<ViewportView> view =
          viewport_view_for_command_target_id(target_id)) {
    shortcut = viewport_view_target_shortcut(*view);
    return true;
  }

  for (const ViewportShortcutTargetDescriptor &descriptor :
       viewport_shortcut_target_catalog()) {
    const std::optional<std::string> command_target =
        viewport_command_target_id_for_shortcut_descriptor(descriptor);
    if (command_target && *command_target == target_id) {
      shortcut = descriptor.shortcut;
      return true;
    }
  }

  if (const std::optional<std::string_view> tool_id =
          quader_tool_id_from_activation_command_target(target_id)) {
    ViewportToolId tool = ViewportToolId::Select;
    if (!viewport_tool_from_contract_id(*tool_id, tool)) {
      return false;
    }
    shortcut.kind = ViewportShortcutKind::ActivateTool;
    shortcut.context = ViewportShortcutContext::Viewport;
    shortcut.tool = tool;
    return true;
  }

  if (const std::optional<std::string_view> selection_mode_id =
          quader_selection_mode_id_from_command_target(target_id)) {
    ViewportSelectionMode mode = ViewportSelectionMode::Object;
    if (!viewport_selection_mode_from_contract_id(*selection_mode_id, mode)) {
      return false;
    }
    shortcut.kind = ViewportShortcutKind::SetSelectionMode;
    shortcut.context = ViewportShortcutContext::Viewport;
    shortcut.selection_mode = mode;
    return true;
  }

  if (const std::optional<std::string_view> grid_preset_id =
          quader_grid_preset_id_from_command_target(target_id)) {
    const std::string preset_text(*grid_preset_id);
    try {
      shortcut.kind = ViewportShortcutKind::SetGridPreset;
      shortcut.context = ViewportShortcutContext::Viewport;
      shortcut.grid_preset = std::stoi(preset_text);
      return true;
    } catch (...) {
      return false;
    }
  }

  return false;
}

bool viewport_target_requires_router_dispatch(std::string_view target_id) {
  return viewport_view_for_command_target_id(target_id).has_value() ||
         target_id == quader_command::kViewportFocusAll.string() ||
         target_id == quader_command::kViewportFocusOrigin.string() ||
         target_id == "viewport.cancel_active_tool" ||
         target_id == "viewport.toggle_maximized" ||
         target_id == "viewport.delete_popup" ||
         quader_tool_id_from_activation_command_target(target_id).has_value() ||
         quader_selection_mode_id_from_command_target(target_id).has_value() ||
         quader_grid_preset_id_from_command_target(target_id).has_value();
}

bool viewport_shortcuts_match(const ViewportShortcut &lhs,
                              const ViewportShortcut &rhs) {
  if (lhs.kind != rhs.kind || lhs.context != rhs.context) {
    return false;
  }

  switch (lhs.kind) {
  case ViewportShortcutKind::ActivateTool:
    return lhs.tool == rhs.tool;
  case ViewportShortcutKind::SetSelectionMode:
    return lhs.selection_mode == rhs.selection_mode;
  case ViewportShortcutKind::SetGridPreset:
    return lhs.grid_preset == rhs.grid_preset;
  case ViewportShortcutKind::SetView:
    return lhs.view == rhs.view;
  case ViewportShortcutKind::MirrorAxis:
    return lhs.mirror_axis == rhs.mirror_axis;
  case ViewportShortcutKind::None:
  case ViewportShortcutKind::FocusDocument:
  case ViewportShortcutKind::FocusOrigin:
  case ViewportShortcutKind::ToggleMaximized:
  case ViewportShortcutKind::CancelActiveTool:
  case ViewportShortcutKind::DeletePopup:
  case ViewportShortcutKind::DeleteSelection:
  case ViewportShortcutKind::SelectAll:
  case ViewportShortcutKind::InvertSelection:
  case ViewportShortcutKind::Copy:
  case ViewportShortcutKind::Paste:
  case ViewportShortcutKind::Duplicate:
  case ViewportShortcutKind::Undo:
  case ViewportShortcutKind::Redo:
  case ViewportShortcutKind::RepeatLastAction:
  case ViewportShortcutKind::MergeActive:
  case ViewportShortcutKind::MergeCenter:
  case ViewportShortcutKind::FlipMeshNormals:
  case ViewportShortcutKind::FlipHorizontal:
  case ViewportShortcutKind::FlipVertical:
  case ViewportShortcutKind::ApplyMaterial:
    return true;
  }
  return false;
}

std::optional<std::string> viewport_command_target_id_for_router_shortcut(
    const ViewportShortcut &shortcut) {
  if (shortcut.kind == ViewportShortcutKind::None) {
    return std::nullopt;
  }
  for (const ViewportShortcutTargetDescriptor &descriptor :
       viewport_shortcut_target_catalog()) {
    const std::optional<std::string> command_target =
        viewport_command_target_id_for_shortcut_descriptor(descriptor);
    if (!command_target ||
        viewport_target_requires_router_dispatch(*command_target) ||
        !viewport_shortcuts_match(shortcut, descriptor.shortcut)) {
      continue;
    }
    return *command_target;
  }
  return std::nullopt;
}

std::optional<std::string>
viewport_operation_preview_command_target_id_for_router_shortcut(
    const ViewportShortcut &shortcut) {
  switch (shortcut.kind) {
  case ViewportShortcutKind::ToggleMaximized:
    return std::string(quader_command::kModelingApplyActivePreview.string());
  case ViewportShortcutKind::CancelActiveTool:
    return std::string(quader_command::kModelingCancelActivePreview.string());
  default:
    return std::nullopt;
  }
}

bool viewport_router_shortcut_for_target_id(std::string_view target_id,
                                            ViewportShortcut &shortcut) {
  return viewport_target_requires_router_dispatch(target_id) &&
         viewport_shortcut_for_command_target_id(target_id, shortcut);
}

} // namespace quader
