////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/session_preferences.hpp"
#include "viewport/viewport_settings.hpp"

#include <functional>
#include <filesystem>
#include <optional>
#include <string_view>

namespace quader {

class LocalizationService;
class UISettings;

/// Identifies the persistence and refresh phase of one Settings mutation.
enum class QtSettingsChangeKind {
  ViewportPreview,
  ViewportCommit,
  InterfacePreview,
  InterfaceCommit,
  RenderingPreview,
  RenderingCommit,
  SessionCommit,
  DefaultMaterialCommit,
  KeybindingsCommit,
};

/// Carries one Settings mutation phase and any value needed for live apply.
struct QtSettingsChange final {
  QtSettingsChangeKind kind = QtSettingsChangeKind::ViewportPreview;
  std::optional<float> interface_scale;
  std::optional<bool> vsync_enabled;
  std::optional<SessionPreferences> session_settings;
  std::optional<std::filesystem::path> default_material_texture;
};

/**
 * Routes retained Settings controls to their existing state owners while
 * keeping preview, commit, and rollback notifications explicit and testable.
 */
class QtSettingsController final {
public:
  using ChangeCallback = std::function<void(const QtSettingsChange &)>;

  void bind(UISettings &ui_settings, ViewportSettings &viewport_settings,
            LocalizationService &localization, ChangeCallback callback);

  bool preview_color(ViewportColorSettingId id, ViewportSettingColor value);
  bool rollback_color(ViewportColorSettingId id,
                      ViewportSettingColor initial_value);
  bool commit_color(ViewportColorSettingId id,
                    ViewportSettingColor initial_value);
  bool set_float(ViewportFloatSettingId id, float value);
  bool set_bool(ViewportBoolSettingId id, bool value);
  bool preview_interface_scale(float scale);
  bool preview_vsync_enabled(bool enabled);
  bool select_locale(std::string_view locale_id);

private:
  void notify(QtSettingsChange change) const;

  UISettings *ui_settings_ = nullptr;
  ViewportSettings *viewport_settings_ = nullptr;
  LocalizationService *localization_ = nullptr;
  ChangeCallback callback_;
};

} // namespace quader
