#pragma once

#include "ui/UIPanel.h"

#include <filesystem>
#include <string>

namespace quader {

struct UISettingsPaths {
    std::filesystem::path directory;
    std::filesystem::path imguiIniFile;
    std::filesystem::path settingsFile;
    std::filesystem::path viewportSettingsFile;
};

class UISettings {
public:
    static UISettings defaults();
    static UISettings load(const std::filesystem::path& path);
    static UISettingsPaths resolvePaths(const std::filesystem::path& executableDir);

    bool save(const std::filesystem::path& path) const;

    bool isPanelVisible(UIPanelId id) const;
    void setPanelVisible(UIPanelId id, bool visible);
    bool navigationGizmoVisible() const { return navigationGizmoVisible_; }
    void setNavigationGizmoVisible(bool visible) { navigationGizmoVisible_ = visible; }
    const std::string& locale() const { return locale_; }
    void setLocale(std::string locale);

private:
    std::string locale_ = "en-US";
    bool outlinerVisible_ = true;
    bool inspectorVisible_ = true;
    bool consoleVisible_ = false;
    bool navigationGizmoVisible_ = true;
};

} // namespace quader
