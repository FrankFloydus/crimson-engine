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
class UISettings;
class ViewportSettings;

/**
 * Qt Widgets presentation of Quader's interface and viewport preferences.
 * Controls edit a per-field draft. Apply merges and persists the draft without
 * closing, OK applies and closes, and every reject path discards it.
 */
class QtSettingsDialog final : public QDialog {
public:
  explicit QtSettingsDialog(QWidget *parent = nullptr);
  ~QtSettingsDialog() override;

  QtSettingsDialog(const QtSettingsDialog &) = delete;
  QtSettingsDialog &operator=(const QtSettingsDialog &) = delete;

  void set_context(UISettings &ui_settings, ViewportSettings &viewport_settings,
                   LocalizationService &localization,
                   std::function<void(QtSettingsChangeKind)> settings_changed);
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
