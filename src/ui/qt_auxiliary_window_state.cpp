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

  settings.beginGroup(QStringLiteral("MaterialBrowser"));
  state.material_browser_geometry =
      settings.value(QStringLiteral("geometry")).toByteArray();
  state.material_browser_grid_mode =
      settings.value(QStringLiteral("gridMode"), true).toBool();
  state.material_browser_tile_size =
      settings.value(QStringLiteral("tileSize"), 128).toInt();
  settings.endGroup();

  settings.beginGroup(QStringLiteral("StandardMaterialEditor"));
  state.standard_material_editor_geometry =
      settings.value(QStringLiteral("geometry")).toByteArray();
  state.standard_material_preview_mesh =
      settings.value(QStringLiteral("previewMesh"), 0).toInt();
  state.standard_material_preview_shading =
      settings.value(QStringLiteral("previewShading"), 2).toInt();
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

  settings.beginGroup(QStringLiteral("MaterialBrowser"));
  settings.setValue(QStringLiteral("geometry"), material_browser_geometry);
  settings.setValue(QStringLiteral("gridMode"), material_browser_grid_mode);
  settings.setValue(QStringLiteral("tileSize"), material_browser_tile_size);
  settings.endGroup();

  settings.beginGroup(QStringLiteral("StandardMaterialEditor"));
  settings.setValue(QStringLiteral("geometry"),
                    standard_material_editor_geometry);
  settings.setValue(QStringLiteral("previewMesh"),
                    standard_material_preview_mesh);
  settings.setValue(QStringLiteral("previewShading"),
                    standard_material_preview_shading);
  settings.endGroup();
  settings.sync();
  return settings.status() == QSettings::NoError;
}

} // namespace quader
