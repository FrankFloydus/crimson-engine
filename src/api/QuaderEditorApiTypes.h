#pragma once

#include "api/QuaderApiResult.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportLayout.h"

#include <filesystem>
#include <functional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

enum class QuaderShadingMode {
    Material,
    Solid,
    Wireframe,
    XRay,
};

enum class QuaderNotificationLevel {
    Info,
    Warning,
    Error,
};

struct QuaderProjectMetadata {
    std::uint64_t id = 0;
    std::string name;
};

struct QuaderCommandDescriptor {
    std::string id;
    std::string label;
    std::string shortcut;
    bool enabled = true;
};

struct QuaderNotification {
    QuaderNotificationLevel level = QuaderNotificationLevel::Info;
    std::string message;
};

struct QuaderViewportRuntimeHooks {
    std::function<QuaderApiVoidResult(ViewportPaneId)> frameSelection;
    std::function<QuaderApiVoidResult(ViewportPaneId)> focusOrigin;
    std::function<QuaderApiVoidResult(ViewportPaneId, const std::filesystem::path&)> captureScreenshot;
};

struct QuaderToolsRuntimeHooks {
    std::function<QuaderApiVoidResult(std::string_view)> activate;
    std::function<QuaderApiVoidResult()> cancelModalTool;
};

struct QuaderPreferencesRuntimeHooks {
    std::function<std::string()> locale;
    std::function<QuaderApiVoidResult(std::string)> setLocale;
    std::function<QuaderApiVoidResult()> save;
    std::function<bool()> navigationGizmoVisible;
    std::function<QuaderApiVoidResult(bool)> setNavigationGizmoVisible;
};

struct QuaderUIRuntimeHooks {
    std::function<bool(std::string_view)> isPanelVisible;
    std::function<QuaderApiVoidResult(std::string_view, bool)> setPanelVisible;
    std::function<QuaderApiVoidResult(std::string_view)> focusPanel;
    std::function<QuaderApiVoidResult(QuaderNotification)> notify;
};

} // namespace quader
