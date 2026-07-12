////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QWidget>

#include <memory>

namespace quader {

class LocalizationService;
class QuaderEditorApi;

/**
 * Presents the active public tool-settings target in a stock Qt tool window.
 * Call refresh from the host UI update path so activation, value changes, and
 * preview completion are reflected immediately.
 */
class QtToolSettingsWindow final : public QWidget {
public:
  explicit QtToolSettingsWindow(QWidget *parent = nullptr);
  ~QtToolSettingsWindow() override;

  QtToolSettingsWindow(const QtToolSettingsWindow &) = delete;
  QtToolSettingsWindow &operator=(const QtToolSettingsWindow &) = delete;

  void bind(QuaderEditorApi &editor);
  void unbind();
  void set_localization(const LocalizationService &localization);
  void refresh();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
