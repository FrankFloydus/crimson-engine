////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/ui_panel_manager.hpp"
#include "ui/ui_settings.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QLineEdit>
#include <QMainWindow>
#include <QWidget>

namespace quader {
namespace {

TEST(UIPanelManagerTests, ExplicitVisibilityTransitionIgnoresPhysicalNoise) {
  const UIPanelVisibilityTransition unchanged_visible =
      ui_panel_visibility_transition(true, true);
  EXPECT_TRUE(unchanged_visible.visible);
  EXPECT_FALSE(unchanged_visible.changed);

  const UIPanelVisibilityTransition hidden =
      ui_panel_visibility_transition(true, false);
  EXPECT_FALSE(hidden.visible);
  EXPECT_TRUE(hidden.changed);

  const UIPanelVisibilityTransition unchanged_hidden =
      ui_panel_visibility_transition(hidden.visible, false);
  EXPECT_FALSE(unchanged_hidden.visible);
  EXPECT_FALSE(unchanged_hidden.changed);

  const UIPanelVisibilityTransition restored =
      ui_panel_visibility_transition(unchanged_hidden.visible, true);
  EXPECT_TRUE(restored.visible);
  EXPECT_TRUE(restored.changed);
}

TEST(UIPanelManagerTests, ToolSettingsUsesTheSharedDockPanelContract) {
  bool found_tool_settings = false;
  for (UIPanelId id : kUiDockPanelIds) {
    found_tool_settings = found_tool_settings || id == UIPanelId::ToolSettings;
  }

  EXPECT_TRUE(found_tool_settings);
  EXPECT_EQ(kUiDockPanelIds.size(), 5U);
  EXPECT_STREQ(ui_panel_stable_id(UIPanelId::ToolSettings), "tool_settings");
  EXPECT_STREQ(ui_panel_localization_key(UIPanelId::ToolSettings),
               "ui.panel.tool_settings");
  EXPECT_TRUE(ui_panel_default_visible(UIPanelId::ToolSettings));
}

TEST(UIPanelManagerTests, FocusUsesThePanelContentsQtFocusProxy) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-panel-manager-focus-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  QMainWindow window;
  auto *dock = new QDockWidget(&window);
  auto *content = new QWidget(dock);
  auto *decoy = new QLineEdit(content);
  auto *focus_target = new QLineEdit(content);
  decoy->setGeometry(0, 0, 120, 24);
  focus_target->setGeometry(0, 32, 120, 24);
  content->setFocusProxy(focus_target);
  dock->setWidget(content);
  window.addDockWidget(Qt::LeftDockWidgetArea, dock);
  window.show();
  window.activateWindow();
  window.raise();
  QCoreApplication::processEvents();
  decoy->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(window.focusWidget(), decoy)
      << "the test must begin away from the panel focus proxy";

  UIPanelManager manager;
  manager.add(UIPanelId::ToolSettings, *dock);
  EXPECT_TRUE(manager.focus(UIPanelId::ToolSettings));
  QCoreApplication::processEvents();
  EXPECT_EQ(window.focusWidget(), focus_target);

  UISettings requested = UISettings::defaults();
  requested.set_panel_visible(UIPanelId::ToolSettings, false);
  manager.apply_settings(requested);
  EXPECT_FALSE(manager.is_visible(UIPanelId::ToolSettings));

  requested.set_panel_visible(UIPanelId::ToolSettings, true);
  manager.apply_settings(requested);
  EXPECT_TRUE(manager.is_visible(UIPanelId::ToolSettings));

  UISettings persisted = UISettings::defaults();
  persisted.set_panel_visible(UIPanelId::ToolSettings, false);
  manager.write_settings(persisted);
  EXPECT_TRUE(persisted.is_panel_visible(UIPanelId::ToolSettings));
}

} // namespace
} // namespace quader
