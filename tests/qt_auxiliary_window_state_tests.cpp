////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_auxiliary_window_state.hpp"
#include "ui/ui_settings.hpp"

#include <filesystem>

namespace {

std::filesystem::path auxiliary_state_test_path() {
  return std::filesystem::temp_directory_path() /
         "quader_qt_auxiliary_window_state.ini";
}

} // namespace

TEST(QtAuxiliaryWindowStateTests,
     MissingStateDefaultsBothAuxiliaryWindowsToClosed) {
  const std::filesystem::path path = auxiliary_state_test_path();
  std::filesystem::remove(path);

  const quader::QtAuxiliaryWindowState state =
      quader::QtAuxiliaryWindowState::load(path);
  EXPECT_TRUE(state.settings_dialog_geometry.isEmpty());
  EXPECT_FALSE(state.settings_dialog_visible);
  EXPECT_TRUE(state.tool_settings_geometry.isEmpty());
  EXPECT_FALSE(state.tool_settings_visible);
}

TEST(QtAuxiliaryWindowStateTests, RoundTripsGeometryAndLogicalVisibility) {
  const std::filesystem::path path = auxiliary_state_test_path();
  std::filesystem::remove(path);

  quader::QtAuxiliaryWindowState state;
  state.settings_dialog_geometry = QByteArray("settings-geometry");
  state.settings_dialog_visible = true;
  state.tool_settings_geometry = QByteArray("tool-settings-geometry");
  state.tool_settings_visible = true;
  ASSERT_TRUE(state.save(path));

  const quader::QtAuxiliaryWindowState loaded =
      quader::QtAuxiliaryWindowState::load(path);
  EXPECT_EQ(loaded.settings_dialog_geometry, state.settings_dialog_geometry);
  EXPECT_TRUE(loaded.settings_dialog_visible);
  EXPECT_EQ(loaded.tool_settings_geometry, state.tool_settings_geometry);
  EXPECT_TRUE(loaded.tool_settings_visible);

  std::filesystem::remove(path);
}

TEST(QtAuxiliaryWindowStateTests, UISettingsOwnsAStableAuxiliaryStatePath) {
  const quader::UISettingsPaths paths = quader::UISettings::resolve_paths(
      std::filesystem::temp_directory_path());

  EXPECT_EQ(paths.qt_auxiliary_window_state_file.parent_path(),
            paths.directory);
  EXPECT_EQ(paths.qt_auxiliary_window_state_file.filename(),
            "qt_auxiliary_windows.ini");
  EXPECT_NE(paths.qt_auxiliary_window_state_file,
            paths.qt_window_state_file);
}
