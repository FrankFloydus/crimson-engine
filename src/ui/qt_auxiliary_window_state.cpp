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

  settings.beginGroup(QStringLiteral("MaterialBrowser"));
  state.material_browser_geometry =
      settings.value(QStringLiteral("geometry")).toByteArray();
  state.material_browser_grid_mode =
      settings.value(QStringLiteral("gridMode"), true).toBool();
  state.material_browser_tile_size =
      settings.value(QStringLiteral("tileSize"), 128).toInt();
  state.material_browser_sidebar_visible =
      settings.value(QStringLiteral("sidebarVisible"), true).toBool();
  state.material_browser_sidebar_width =
      settings.value(QStringLiteral("sidebarWidth"), 200).toInt();
  settings.endGroup();

  // Preserve useful Material Browser preferences without inheriting its
  // floating geometry; the Asset Browser is part of the saved dock layout.
  settings.beginGroup(QStringLiteral("AssetBrowser"));
  state.asset_browser_grid_mode = settings.value(
      QStringLiteral("gridMode"), state.material_browser_grid_mode).toBool();
  state.asset_browser_tile_size = settings.value(
      QStringLiteral("tileSize"), state.material_browser_tile_size).toInt();
  state.asset_browser_sidebar_visible = settings.value(
      QStringLiteral("sidebarVisible"), state.material_browser_sidebar_visible).toBool();
  state.asset_browser_sidebar_width = settings.value(
      QStringLiteral("sidebarWidth"), state.material_browser_sidebar_width).toInt();
  state.asset_browser_layout_migrated =
      settings.value(QStringLiteral("layoutMigrated"), false).toBool();
  settings.endGroup();

  settings.beginGroup(QStringLiteral("FaceTexture"));
  state.face_texture_browser_grid_mode =
      settings.value(QStringLiteral("gridMode"), true).toBool();
  state.face_texture_browser_tile_size =
      settings.value(QStringLiteral("tileSize"), 64).toInt();
  state.face_texture_browser_sidebar_visible =
      settings.value(QStringLiteral("sidebarVisible"), true).toBool();
  state.face_texture_browser_sidebar_width =
      settings.value(QStringLiteral("sidebarWidth"), 120).toInt();
  state.face_texture_splitter_state =
      settings.value(QStringLiteral("splitterState")).toByteArray();
  state.face_texture_layout_migrated =
      settings.value(QStringLiteral("layoutMigrated"), false).toBool();
  settings.endGroup();

  settings.beginGroup(QStringLiteral("StandardMaterialEditor"));
  state.standard_material_editor_geometry =
      settings.value(QStringLiteral("geometry")).toByteArray();
  state.standard_material_preview_mesh =
      settings.value(QStringLiteral("previewMesh"), 0).toInt();
  state.standard_material_preview_shading =
      settings.value(QStringLiteral("previewShading"), 2).toInt();
  state.standard_material_preview_environment =
      settings
          .value(QStringLiteral("previewEnvironment"),
                 QStringLiteral("lightroom_14b"))
          .toString();
  state.standard_material_preview_background =
      settings.value(QStringLiteral("previewBackground"), false).toBool();
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

  settings.beginGroup(QStringLiteral("MaterialBrowser"));
  settings.setValue(QStringLiteral("geometry"), material_browser_geometry);
  settings.setValue(QStringLiteral("gridMode"), material_browser_grid_mode);
  settings.setValue(QStringLiteral("tileSize"), material_browser_tile_size);
  settings.setValue(QStringLiteral("sidebarVisible"), material_browser_sidebar_visible);
  settings.setValue(QStringLiteral("sidebarWidth"), material_browser_sidebar_width);
  settings.endGroup();

  settings.beginGroup(QStringLiteral("AssetBrowser"));
  settings.setValue(QStringLiteral("gridMode"), asset_browser_grid_mode);
  settings.setValue(QStringLiteral("tileSize"), asset_browser_tile_size);
  settings.setValue(QStringLiteral("sidebarVisible"), asset_browser_sidebar_visible);
  settings.setValue(QStringLiteral("sidebarWidth"), asset_browser_sidebar_width);
  settings.setValue(QStringLiteral("layoutMigrated"), asset_browser_layout_migrated);
  settings.endGroup();

  settings.beginGroup(QStringLiteral("FaceTexture"));
  settings.setValue(QStringLiteral("gridMode"), face_texture_browser_grid_mode);
  settings.setValue(QStringLiteral("tileSize"), face_texture_browser_tile_size);
  settings.setValue(QStringLiteral("sidebarVisible"), face_texture_browser_sidebar_visible);
  settings.setValue(QStringLiteral("sidebarWidth"), face_texture_browser_sidebar_width);
  settings.setValue(QStringLiteral("splitterState"), face_texture_splitter_state);
  settings.setValue(QStringLiteral("layoutMigrated"), face_texture_layout_migrated);
  settings.endGroup();

  settings.beginGroup(QStringLiteral("StandardMaterialEditor"));
  settings.setValue(QStringLiteral("geometry"),
                    standard_material_editor_geometry);
  settings.setValue(QStringLiteral("previewMesh"),
                    standard_material_preview_mesh);
  settings.setValue(QStringLiteral("previewShading"),
                    standard_material_preview_shading);
  settings.setValue(QStringLiteral("previewEnvironment"),
                    standard_material_preview_environment);
  settings.setValue(QStringLiteral("previewBackground"),
                    standard_material_preview_background);
  settings.endGroup();
  settings.sync();
  return settings.status() == QSettings::NoError;
}

} // namespace quader
