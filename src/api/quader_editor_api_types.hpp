////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_viewport_layout_types.hpp"

#include <filesystem>
#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Enumerates QuaderShadingMode values used by the api layer.
 */
enum class QuaderShadingMode {
    Material,
    Solid,
    Wireframe,
    XRay,
};

/**
 * Enumerates QuaderNotificationLevel values used by the api layer.
 */
enum class QuaderNotificationLevel {
    Info,
    Warning,
    Error,
};

/**
 * Represents a Quader Project Metadata value used by the public Quader API surface.
 */
struct QuaderProjectMetadata {
    std::uint64_t id = 0;
    std::string name;
};

/**
 * Represents Quader Project Runtime Hooks value used by the native shell.
 */
struct QuaderProjectRuntimeHooks {
  std::function<std::optional<std::filesystem::path>()> choose_open_path;
  std::function<std::optional<std::filesystem::path>()> choose_save_as_path;
};

/**
 * Stores the Quader Command Descriptor data contract used by the public Quader API surface.
 */
struct QuaderCommandDescriptor {
    std::string id;
    std::string label;
    std::string shortcut;
    bool enabled = true;
};

/**
 * Represents a Quader Notification value used by the public Quader API surface.
 */
struct QuaderNotification {
    QuaderNotificationLevel level = QuaderNotificationLevel::Info;
    std::string message;
};

/**
 * Represents a Quader Viewport Runtime Hooks value used by the public Quader API surface.
 */
struct QuaderViewportRuntimeHooks {
  std::function<QuaderApiVoidResult(ViewportPaneId)> frame_selection;
  std::function<QuaderApiVoidResult(ViewportPaneId)> focus_origin;
  std::function<QuaderApiVoidResult()> spawn_window;
  std::function<QuaderApiVoidResult(ViewportPaneId,
                                    const std::filesystem::path &)>
      capture_screenshot;
};

/**
 * Represents a Quader Tools Runtime Hooks value used by the public Quader API surface.
 */
struct QuaderToolsRuntimeHooks {
    std::function<QuaderApiVoidResult(std::string_view)> activate;
    std::function<QuaderApiVoidResult()> cancel_modal_tool;
};

/**
 * Represents a Quader Preferences Runtime Hooks value used by the public Quader API surface.
 */
struct QuaderPreferencesRuntimeHooks {
    std::function<std::string()> locale;
    std::function<QuaderApiVoidResult(std::string)> set_locale;
    std::function<QuaderApiVoidResult()> save;
    std::function<bool()> navigation_gizmo_visible;
    std::function<QuaderApiVoidResult(bool)> set_navigation_gizmo_visible;
};

/**
 * Represents a Quader UI Runtime Hooks value used by the public Quader API surface.
 */
struct QuaderUIRuntimeHooks {
  std::function<bool(std::string_view)> is_panel_visible;
  std::function<QuaderApiVoidResult(std::string_view, bool)> set_panel_visible;
  std::function<QuaderApiVoidResult(std::string_view)> focus_panel;
  std::function<QuaderApiVoidResult(QuaderNotification)> notify;
};

} // namespace quader
