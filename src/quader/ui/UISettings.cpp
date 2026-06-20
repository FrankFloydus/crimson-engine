#include "quader/ui/UISettings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>

namespace quader {
namespace {

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }).base();
    if (first >= last) {
        return {};
    }
    return {first, last};
}

bool parseBool(std::string value, bool fallback) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }
    return fallback;
}

std::filesystem::path localAppDataDirectory(const std::filesystem::path& executableDir) {
#ifdef _WIN32
    if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
        if (localAppData[0] != '\0') {
            return std::filesystem::path(localAppData) / "Quader";
        }
    }
#else
    if (const char* configHome = std::getenv("XDG_CONFIG_HOME")) {
        if (configHome[0] != '\0') {
            return std::filesystem::path(configHome) / "Quader";
        }
    }
#endif
    return executableDir;
}

} // namespace

UISettings UISettings::defaults() {
    return {};
}

UISettings UISettings::load(const std::filesystem::path& path) {
    UISettings settings = defaults();
    std::ifstream file(path);
    if (!file.is_open()) {
        return settings;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == '[') {
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (key == uiPanelKey(UIPanelId::Outliner)) {
            settings.outlinerVisible_ = parseBool(value, settings.outlinerVisible_);
        } else if (key == uiPanelKey(UIPanelId::Inspector)) {
            settings.inspectorVisible_ = parseBool(value, settings.inspectorVisible_);
        } else if (key == uiPanelKey(UIPanelId::Console)) {
            settings.consoleVisible_ = parseBool(value, settings.consoleVisible_);
        }
    }

    return settings;
}

UISettingsPaths UISettings::resolvePaths(const std::filesystem::path& executableDir) {
    UISettingsPaths paths;
    paths.directory = localAppDataDirectory(executableDir);
    paths.imguiIniFile = paths.directory / "quader_ui.ini";
    paths.settingsFile = paths.directory / "ui_settings.ini";
    paths.viewportSettingsFile = paths.directory / "viewport_settings.ini";
    return paths;
}

bool UISettings::save(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << "[Panels]\n";
    file << uiPanelKey(UIPanelId::Outliner) << '=' << (outlinerVisible_ ? "true" : "false") << '\n';
    file << uiPanelKey(UIPanelId::Inspector) << '=' << (inspectorVisible_ ? "true" : "false") << '\n';
    file << uiPanelKey(UIPanelId::Console) << '=' << (consoleVisible_ ? "true" : "false") << '\n';
    return true;
}

bool UISettings::isPanelVisible(UIPanelId id) const {
    switch (id) {
    case UIPanelId::Viewport:
        return true;
    case UIPanelId::Outliner:
        return outlinerVisible_;
    case UIPanelId::Inspector:
        return inspectorVisible_;
    case UIPanelId::Console:
        return consoleVisible_;
    }
    return true;
}

void UISettings::setPanelVisible(UIPanelId id, bool visible) {
    switch (id) {
    case UIPanelId::Viewport:
        break;
    case UIPanelId::Outliner:
        outlinerVisible_ = visible;
        break;
    case UIPanelId::Inspector:
        inspectorVisible_ = visible;
        break;
    case UIPanelId::Console:
        consoleVisible_ = visible;
        break;
    }
}

} // namespace quader
