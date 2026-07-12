////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_auxiliary_window_state.hpp"

#include <QSettings>
#include <QString>

#include <system_error>

namespace quader {
namespace {

QString qsettings_path(const std::filesystem::path &path) {
#if defined(_WIN32)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromStdString(path.string());
#endif
}

} // namespace

QtAuxiliaryWindowState
QtAuxiliaryWindowState::load(const std::filesystem::path &path) {
  QtAuxiliaryWindowState state;
  if (path.empty()) {
    return state;
  }

  QSettings settings(qsettings_path(path), QSettings::IniFormat);
  settings.beginGroup(QStringLiteral("SettingsDialog"));
  state.settings_dialog_geometry =
      settings.value(QStringLiteral("geometry")).toByteArray();
  state.settings_dialog_visible =
      settings.value(QStringLiteral("visible"), false).toBool();
  settings.endGroup();

  settings.beginGroup(QStringLiteral("ToolSettingsWindow"));
  state.tool_settings_geometry =
      settings.value(QStringLiteral("geometry")).toByteArray();
  state.tool_settings_visible =
      settings.value(QStringLiteral("visible"), false).toBool();
  settings.endGroup();
  return state;
}

bool QtAuxiliaryWindowState::save(const std::filesystem::path &path) const {
  if (path.empty()) {
    return false;
  }

  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  QSettings settings(qsettings_path(path), QSettings::IniFormat);
  settings.beginGroup(QStringLiteral("SettingsDialog"));
  settings.setValue(QStringLiteral("geometry"), settings_dialog_geometry);
  settings.setValue(QStringLiteral("visible"), settings_dialog_visible);
  settings.endGroup();

  settings.beginGroup(QStringLiteral("ToolSettingsWindow"));
  settings.setValue(QStringLiteral("geometry"), tool_settings_geometry);
  settings.setValue(QStringLiteral("visible"), tool_settings_visible);
  settings.endGroup();
  settings.sync();
  return settings.status() == QSettings::NoError;
}

} // namespace quader
