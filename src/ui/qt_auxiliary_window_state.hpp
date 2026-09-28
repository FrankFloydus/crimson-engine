////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QByteArray>
#include <QString>

#include <filesystem>

namespace quader {

/**
 * Owns persisted Qt auxiliary-window geometry, logical visibility, and local
 * presentation preferences. The main-window dock layout remains in
 * QMainWindow's versioned binary state.
 */
struct QtAuxiliaryWindowState {
  QByteArray settings_dialog_geometry;
  bool settings_dialog_visible = false;
  QByteArray material_browser_geometry;
  bool material_browser_grid_mode = true;
  int material_browser_tile_size = 128;
  bool material_browser_sidebar_visible = true;
  int material_browser_sidebar_width = 200;
  bool asset_browser_grid_mode = true;
  int asset_browser_tile_size = 128;
  bool asset_browser_sidebar_visible = true;
  int asset_browser_sidebar_width = 200;
  bool asset_browser_layout_migrated = false;
  bool face_texture_browser_grid_mode = true;
  int face_texture_browser_tile_size = 64;
  bool face_texture_browser_sidebar_visible = true;
  int face_texture_browser_sidebar_width = 120;
  QByteArray face_texture_splitter_state;
  bool face_texture_layout_migrated = false;
  QByteArray standard_material_editor_geometry;
  int standard_material_preview_mesh = 0;
  int standard_material_preview_shading = 2;
  QString standard_material_preview_environment =
      QStringLiteral("lightroom_14b");
  bool standard_material_preview_background = false;

  [[nodiscard]] static QtAuxiliaryWindowState
  load(const std::filesystem::path &path);
  [[nodiscard]] bool save(const std::filesystem::path &path) const;
};

} // namespace quader
