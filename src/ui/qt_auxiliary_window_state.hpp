////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QByteArray>

#include <filesystem>

namespace quader {

/**
 * Owns the persisted geometry and logical visibility of Qt auxiliary windows.
 * The main-window dock layout remains in QMainWindow's versioned binary state.
 */
struct QtAuxiliaryWindowState {
  QByteArray settings_dialog_geometry;
  bool settings_dialog_visible = false;
  QByteArray tool_settings_geometry;
  bool tool_settings_visible = false;

  [[nodiscard]] static QtAuxiliaryWindowState
  load(const std::filesystem::path &path);
  [[nodiscard]] bool save(const std::filesystem::path &path) const;
};

} // namespace quader
