#pragma once

#include "quader/ui/UIPanel.h"

#include <filesystem>

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

private:
    bool outlinerVisible_ = true;
    bool inspectorVisible_ = true;
    bool consoleVisible_ = false;
};

} // namespace quader
