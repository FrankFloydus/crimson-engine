////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_settings_controller.hpp"

#include <QDialog>

#include <functional>
#include <memory>

class QShowEvent;

namespace quader {

class LocalizationService;
class QuaderCommandsApi;
class QtUiScaleMetrics;
class UISettings;
class ViewportSettings;

/**
 * Qt Widgets presentation of task-oriented editor preference categories.
 * Controls edit a per-field draft. Apply merges and persists the draft without
 * closing, OK applies and closes, and every reject path discards it.
 * Interface scale and display synchronization preview immediately; rejection
 * restores their last applied values.
 * Keybinding edits are public-API mutations that persist immediately.
 */
class QtSettingsDialog final : public QDialog {
public:
  explicit QtSettingsDialog(QWidget *parent = nullptr);
  ~QtSettingsDialog() override;

  QtSettingsDialog(const QtSettingsDialog &) = delete;
  QtSettingsDialog &operator=(const QtSettingsDialog &) = delete;

  /** Replacing only runtime command/callback owners preserves the active draft. */
  void set_context(UISettings &ui_settings, ViewportSettings &viewport_settings,
                   LocalizationService &localization,
                   QuaderCommandsApi &commands,
                   QtSettingsController::ChangeCallback settings_changed,
                   std::function<bool()> persist_interface_settings = {},
                   std::filesystem::path settings_directory = {},
                   std::filesystem::path built_in_default_texture = {},
                   std::function<std::optional<std::filesystem::path>()>
                       default_texture_picker = {});
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  /** Reconciles only the interface-scale draft after an external API commit. */
  void synchronize_interface_scale();
  /** Reconciles only the VSync draft after an external API commit. */
  void synchronize_vsync_enabled();
  /** Reconciles only history/autosave drafts after an external API commit. */
  void synchronize_session_settings();
  void synchronize_default_material_texture();
  void refresh();
  void accept() override;
  void reject() override;

protected:
  void showEvent(QShowEvent *event) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
