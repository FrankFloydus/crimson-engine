////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/ui_panel.hpp"

#include <filesystem>
#include <string>

namespace quader {

/**
 * Represents an UI Settings Paths value used by the ImGui editor presentation layer.
 */
struct UISettingsPaths {
    std::filesystem::path directory;
    std::filesystem::path imgui_ini_file;
    std::filesystem::path settings_file;
    std::filesystem::path viewport_settings_file;
};

/**
 * Owns UI Settings behavior within the ImGui editor presentation layer.
 */
class UISettings {
public:
    static UISettings defaults();
    static UISettings load(const std::filesystem::path& path);
    static UISettingsPaths
    resolve_paths(const std::filesystem::path &executable_dir);

    bool save(const std::filesystem::path& path) const;

    bool is_panel_visible(UIPanelId id) const;
    void set_panel_visible(UIPanelId id, bool visible);
    bool navigation_gizmo_visible() const { return navigation_gizmo_visible_; }
    void set_navigation_gizmo_visible(bool visible) {
      navigation_gizmo_visible_ = visible;
    }
    const std::string& locale() const { return locale_; }
    void set_locale(std::string locale);

  private:
    std::string locale_ = "en-US";
    bool outliner_visible_ = ui_panel_default_visible(UIPanelId::Outliner);
    bool inspector_visible_ = ui_panel_default_visible(UIPanelId::Inspector);
    bool console_visible_ = ui_panel_default_visible(UIPanelId::Console);
    bool navigation_gizmo_visible_ = true;
};

} // namespace quader
