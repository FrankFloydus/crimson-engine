////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace quader {

inline constexpr std::string_view kQuaderModelingCommandTargetPrefix = "modeling.";
inline constexpr std::string_view kQuaderToolActivationCommandTargetPrefix = "tools.activate.";
inline constexpr std::string_view kQuaderViewportSelectionModeTargetPrefix = "viewport.selection_mode.";
inline constexpr std::string_view kQuaderViewportGridPresetTargetPrefix = "viewport.grid_preset.";

inline std::string quader_prefixed_command_target(std::string_view prefix,
                                                  std::string_view id) {
  std::string target(prefix);
  target.append(id);
  return target;
}

inline std::string
quader_modeling_command_target_id(std::string_view action_id) {
  return quader_prefixed_command_target(kQuaderModelingCommandTargetPrefix,
                                        action_id);
}

inline std::string
quader_tool_activation_command_target_id(std::string_view tool_id) {
  if (tool_id == "uv_peel") {
    return quader_modeling_command_target_id("uv_peel");
  }
  return quader_prefixed_command_target(
      kQuaderToolActivationCommandTargetPrefix, tool_id);
}

inline std::string
quader_viewport_selection_mode_target_id(std::string_view mode_id) {
  return quader_prefixed_command_target(
      kQuaderViewportSelectionModeTargetPrefix, mode_id);
}

inline std::string quader_viewport_grid_preset_target_id(int preset) {
  return quader_prefixed_command_target(kQuaderViewportGridPresetTargetPrefix,
                                        std::to_string(preset));
}

inline std::optional<std::string_view>
quader_command_target_suffix(std::string_view target_id,
                             std::string_view prefix) {
  if (!target_id.starts_with(prefix)) {
    return std::nullopt;
  }
  const std::string_view suffix = target_id.substr(prefix.size());
  return suffix.empty() ? std::nullopt
                        : std::optional<std::string_view>{suffix};
}

inline std::optional<std::string_view>
quader_tool_id_from_activation_command_target(std::string_view target_id) {
  if (target_id == "modeling.uv_peel") {
    return "uv_peel";
  }
  return quader_command_target_suffix(target_id,
                                      kQuaderToolActivationCommandTargetPrefix);
}

inline std::optional<std::string_view>
quader_selection_mode_id_from_command_target(std::string_view target_id) {
  return quader_command_target_suffix(target_id,
                                      kQuaderViewportSelectionModeTargetPrefix);
}

inline std::optional<std::string_view>
quader_grid_preset_id_from_command_target(std::string_view target_id) {
  return quader_command_target_suffix(target_id,
                                      kQuaderViewportGridPresetTargetPrefix);
}

} // namespace quader
