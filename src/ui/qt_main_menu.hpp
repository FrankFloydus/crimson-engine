////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_command_ids.hpp"
#include "api/quader_viewport_layout_types.hpp"
#include "ui/ui_panel.hpp"
#include "viewport/viewport_settings.hpp"

#include <functional>
#include <memory>

class QMenuBar;

namespace quader {

class LocalizationService;
class ModelingSessionService;
class QuaderEditorApi;

/**
 * Long-lived owner references and shell callbacks used by the Qt menu bar.
 */
struct QtMainMenuContext {
  std::reference_wrapper<QuaderEditorApi> editor;
  std::reference_wrapper<ModelingSessionService> modeling;
  ViewportSettings &viewport_settings;
  LocalizationService &localization;
  std::function<void()> request_quit;
  std::function<void()> show_settings;
  std::function<void()> show_about;
  std::function<bool(UIPanelId)> panel_visible;
  std::function<void(UIPanelId, bool)> set_panel_visible;
  std::function<bool()> selection_toolbar_visible;
  std::function<void(bool)> set_selection_toolbar_visible;
  std::function<bool()> snapping_toolbar_visible;
  std::function<void(bool)> set_snapping_toolbar_visible;
  std::function<bool()> viewport_display_toolbar_visible;
  std::function<void(bool)> set_viewport_display_toolbar_visible;
  std::function<bool()> texture_toolbar_visible;
  std::function<void(bool)> set_texture_toolbar_visible;
  std::function<bool()> tool_toolbar_visible;
  std::function<void(bool)> set_tool_toolbar_visible;
  std::function<void()> viewport_settings_changed;
  std::function<void()> show_material_find_replace;
  std::function<bool()> material_find_replace_available;
  std::function<void()> show_autosave_recovery;
  std::function<bool()> autosave_recovery_available;
};

/**
 * Builds and refreshes the standard Qt menu bar without owning shortcuts.
 * Keyboard dispatch remains owned by QuaderKeybindingService.
 */
class QtMainMenu final {
public:
  QtMainMenu();
  ~QtMainMenu();

  QtMainMenu(const QtMainMenu &) = delete;
  QtMainMenu &operator=(const QtMainMenu &) = delete;

  void initialize(QMenuBar &menu_bar, QtMainMenuContext context);
  void rebind_runtime(QuaderEditorApi &editor,
                      ModelingSessionService &modeling) noexcept;
  void rebuild();
  void refresh();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
