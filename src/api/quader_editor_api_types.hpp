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
    std::function<QuaderApiVoidResult(ViewportPaneId)> frameSelection;
    std::function<QuaderApiVoidResult(ViewportPaneId)> focusOrigin;
    std::function<QuaderApiVoidResult()> spawnWindow;
    std::function<QuaderApiVoidResult(ViewportPaneId, const std::filesystem::path&)> captureScreenshot;
};

/**
 * Represents a Quader Tools Runtime Hooks value used by the public Quader API surface.
 */
struct QuaderToolsRuntimeHooks {
    std::function<QuaderApiVoidResult(std::string_view)> activate;
    std::function<QuaderApiVoidResult()> cancelModalTool;
};

/**
 * Represents a Quader Preferences Runtime Hooks value used by the public Quader API surface.
 */
struct QuaderPreferencesRuntimeHooks {
    std::function<std::string()> locale;
    std::function<QuaderApiVoidResult(std::string)> setLocale;
    std::function<QuaderApiVoidResult()> save;
    std::function<bool()> navigationGizmoVisible;
    std::function<QuaderApiVoidResult(bool)> setNavigationGizmoVisible;
};

/**
 * Represents a Quader UI Runtime Hooks value used by the public Quader API surface.
 */
struct QuaderUIRuntimeHooks {
    std::function<bool(std::string_view)> isPanelVisible;
    std::function<QuaderApiVoidResult(std::string_view, bool)> setPanelVisible;
    std::function<QuaderApiVoidResult(std::string_view)> focusPanel;
    std::function<QuaderApiVoidResult(QuaderNotification)> notify;
};

} // namespace quader
