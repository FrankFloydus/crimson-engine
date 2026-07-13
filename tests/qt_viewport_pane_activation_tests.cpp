////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_viewport_pane_activation.hpp"
#include "ui/qt_viewport_panel.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QObject>
#include <QWidget>
#include <QWindow>

#include <array>
#include <cstddef>
#include <optional>

namespace {

/**
 * Models the visibility and geometry state of one embedded native pane.
 */
struct FakeNativePane {
  bool visible = false;
  bool geometry_synchronized_after_activation = false;
  std::size_t geometry_sync_count = 0u;
};

TEST(QtViewportPaneActivationTests,
     SingleToFourMakesEveryPanePresentableWithoutSplitMutation) {
  std::array<FakeNativePane, 4> panes{};
  const auto apply = [&panes](const std::array<bool, 4> &active) {
    quader::qt_apply_viewport_pane_activation(
        active,
        [&panes](std::size_t index, bool visible) {
          panes[index].visible = visible;
        },
        [&panes](std::size_t index) {
          ++panes[index].geometry_sync_count;
          panes[index].geometry_synchronized_after_activation =
              panes[index].visible;
        });
  };
  std::optional<std::array<bool, 4>> displayed;
  const auto refresh = [&apply, &displayed](
                           const std::array<bool, 4> &active) {
    if (displayed.has_value() && *displayed == active) {
      return;
    }
    displayed = active;
    apply(active);
  };

  refresh({true, false, false, false});
  EXPECT_TRUE(panes[0].geometry_synchronized_after_activation);
  for (std::size_t index = 1; index < panes.size(); ++index) {
    EXPECT_FALSE(panes[index].visible);
    EXPECT_EQ(panes[index].geometry_sync_count, 0u);
  }

  refresh({true, true, true, true});
  for (const FakeNativePane &pane : panes) {
    EXPECT_TRUE(pane.visible);
    EXPECT_TRUE(pane.geometry_synchronized_after_activation);
    EXPECT_GT(pane.geometry_sync_count, 0u);
  }

  const std::array<std::size_t, 4> before_cached_frame{
      panes[0].geometry_sync_count, panes[1].geometry_sync_count,
      panes[2].geometry_sync_count, panes[3].geometry_sync_count};
  refresh({true, true, true, true});
  // The unchanged layout takes the cached no-op path without a splitter
  // mutation. The one activation pass already left every pane presentable.
  for (std::size_t index = 0; index < panes.size(); ++index) {
    EXPECT_TRUE(panes[index].geometry_synchronized_after_activation);
    EXPECT_EQ(panes[index].geometry_sync_count, before_cached_frame[index]);
  }
}

TEST(QtViewportEventTargetPolicyTests,
     RetainedToolSettingsControlsStayInsideTheActiveTool) {
  QObject tool_settings;
  tool_settings.setObjectName(QStringLiteral("floating.tool_settings"));
  QObject previous_button(&tool_settings);
  previous_button.setObjectName(
      QStringLiteral("tool_settings.control.pivot.align.prev"));
  QObject next_button(&tool_settings);
  next_button.setObjectName(
      QStringLiteral("tool_settings.control.pivot.align.next"));
  QObject apply_button(&tool_settings);
  apply_button.setObjectName(QStringLiteral("tool_settings.apply"));

  EXPECT_TRUE(
      quader::qt_event_target_belongs_to_tool_settings(&tool_settings));
  EXPECT_TRUE(
      quader::qt_event_target_belongs_to_tool_settings(&previous_button));
  EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(&next_button));
  EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(&apply_button));

  QObject ordinary_dialog;
  ordinary_dialog.setObjectName(QStringLiteral("unrelated.dialog"));
  QObject ordinary_button(&ordinary_dialog);
  EXPECT_FALSE(
      quader::qt_event_target_belongs_to_tool_settings(&ordinary_dialog));
  EXPECT_FALSE(
      quader::qt_event_target_belongs_to_tool_settings(&ordinary_button));
  EXPECT_FALSE(quader::qt_event_target_belongs_to_tool_settings(nullptr));
}

TEST(QtViewportEventTargetPolicyTests,
     ToolSettingsNativeWindowHandleStaysInsideButOtherWindowsRemainOutside) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-tool-settings-window-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QWidget tool_settings;
  tool_settings.setObjectName(QStringLiteral("floating.tool_settings"));
  tool_settings.setAttribute(Qt::WA_DontShowOnScreen, true);
  (void)tool_settings.winId();
  QWindow *tool_settings_window = tool_settings.windowHandle();
  ASSERT_NE(tool_settings_window, nullptr);
  EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(
      tool_settings_window));

  QWidget unrelated_dialog;
  unrelated_dialog.setObjectName(QStringLiteral("unrelated.dialog"));
  unrelated_dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  (void)unrelated_dialog.winId();
  QWindow *unrelated_window = unrelated_dialog.windowHandle();
  ASSERT_NE(unrelated_window, nullptr);
  EXPECT_FALSE(
      quader::qt_event_target_belongs_to_tool_settings(unrelated_window));
}

} // namespace
