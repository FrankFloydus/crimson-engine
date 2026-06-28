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

inline std::string quaderPrefixedCommandTarget(std::string_view prefix, std::string_view id) {
    std::string target(prefix);
    target.append(id);
    return target;
}

inline std::string quaderModelingCommandTargetId(std::string_view actionId) {
    return quaderPrefixedCommandTarget(kQuaderModelingCommandTargetPrefix, actionId);
}

inline std::string quaderToolActivationCommandTargetId(std::string_view toolId) {
    return quaderPrefixedCommandTarget(kQuaderToolActivationCommandTargetPrefix, toolId);
}

inline std::string quaderViewportSelectionModeTargetId(std::string_view modeId) {
    return quaderPrefixedCommandTarget(kQuaderViewportSelectionModeTargetPrefix, modeId);
}

inline std::string quaderViewportGridPresetTargetId(int preset) {
    return quaderPrefixedCommandTarget(kQuaderViewportGridPresetTargetPrefix, std::to_string(preset));
}

inline std::optional<std::string_view> quaderCommandTargetSuffix(
        std::string_view targetId,
        std::string_view prefix) {
    if (!targetId.starts_with(prefix)) {
        return std::nullopt;
    }
    const std::string_view suffix = targetId.substr(prefix.size());
    return suffix.empty() ? std::nullopt : std::optional<std::string_view>{suffix};
}

inline std::optional<std::string_view> quaderToolIdFromActivationCommandTarget(std::string_view targetId) {
    return quaderCommandTargetSuffix(targetId, kQuaderToolActivationCommandTargetPrefix);
}

inline std::optional<std::string_view> quaderSelectionModeIdFromCommandTarget(std::string_view targetId) {
    return quaderCommandTargetSuffix(targetId, kQuaderViewportSelectionModeTargetPrefix);
}

inline std::optional<std::string_view> quaderGridPresetIdFromCommandTarget(std::string_view targetId) {
    return quaderCommandTargetSuffix(targetId, kQuaderViewportGridPresetTargetPrefix);
}

} // namespace quader
