////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/interface_scale_policy.hpp"
#include "core/session_preferences.hpp"
#include "ui/ui_panel.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace quader {

/**
 * Represents UI settings paths used by the Qt Widgets editor shell.
 */
struct UISettingsPaths {
    std::filesystem::path directory;
    std::filesystem::path qt_window_state_file;
    std::filesystem::path qt_auxiliary_window_state_file;
    std::filesystem::path settings_file;
    std::filesystem::path viewport_settings_file;
};

/**
 * Owns backend-neutral editor UI preferences.
 */
class UISettings {
public:
    static constexpr float kMinimumInterfaceScale = kInterfaceScaleMinimum;
    static constexpr float kMaximumInterfaceScale = kInterfaceScaleMaximum;
    static constexpr float kDefaultInterfaceScale = kInterfaceScaleDefault;

    static UISettings defaults();
    static UISettings load(const std::filesystem::path& path);
    static UISettingsPaths
    resolve_paths(const std::filesystem::path &executable_dir);

    bool save(const std::filesystem::path& path) const;

    bool is_panel_visible(UIPanelId id) const;
    void set_panel_visible(UIPanelId id, bool visible);
    const std::string& locale() const { return locale_; }
    void set_locale(std::string locale);
    float interface_scale() const { return interface_scale_; }
    void set_interface_scale(float scale);
    bool vsync_enabled() const { return vsync_enabled_; }
    void set_vsync_enabled(bool enabled) { vsync_enabled_ = enabled; }
    const SessionPreferences &session_settings() const {
      return session_settings_;
    }
    /// Rejects an invalid group without changing any stored preference.
    bool set_session_settings(SessionPreferences settings);
    const std::filesystem::path &default_material_texture() const {
      return default_material_texture_;
    }
    void set_default_material_texture(std::filesystem::path path) {
      default_material_texture_ = std::move(path);
    }

  private:
    std::string locale_ = "en-US";
    float interface_scale_ = kDefaultInterfaceScale;
    bool vsync_enabled_ = true;
    SessionPreferences session_settings_;
    std::filesystem::path default_material_texture_;
    bool outliner_visible_ = ui_panel_default_visible(UIPanelId::Outliner);
    bool inspector_visible_ = ui_panel_default_visible(UIPanelId::Inspector);
    bool console_visible_ = ui_panel_default_visible(UIPanelId::Console);
    bool layers_visible_ = ui_panel_default_visible(UIPanelId::Layers);
    bool face_texture_visible_ =
        ui_panel_default_visible(UIPanelId::FaceTexture);
    bool asset_browser_visible_ =
        ui_panel_default_visible(UIPanelId::AssetBrowser);
};

} // namespace quader
