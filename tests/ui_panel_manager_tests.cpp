////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/ui_continuous_edit_tracker.hpp"
#include "ui/qt_active_material_panel.hpp"
#include "ui/ui_panel_manager.hpp"
#include "ui/ui_settings.hpp"

#include <array>
#include <span>

#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QWidget>

namespace quader {
namespace {

TEST(UIContinuousEditTrackerTests, ReusesKeyWithinOneControlAndEntityScope) {
  UIContinuousEditTracker tracker;
  const std::array<EntityId, 2> selection{17, 23};
  const std::span<const EntityId> scope(selection);

  const std::string first =
      tracker.ensure_merge_key("environment.intensity", 17, scope);
  const std::string repeated =
      tracker.ensure_merge_key("environment.intensity", 17, scope);

  EXPECT_FALSE(first.empty());
  EXPECT_EQ(repeated, first);
}

TEST(UIContinuousEditTrackerTests, ChangesKeyWhenScopeChangesOrAfterReset) {
  UIContinuousEditTracker tracker;
  const std::array<EntityId, 2> selection{17, 23};
  const std::array<EntityId, 2> reordered_selection{23, 17};

  const std::string first =
      tracker.ensure_merge_key("environment.intensity", 17, selection);
  const std::string changed_control =
      tracker.ensure_merge_key("environment.rotation", 17, selection);
  const std::string changed_primary =
      tracker.ensure_merge_key("environment.rotation", 23, selection);
  const std::string changed_selection = tracker.ensure_merge_key(
      "environment.rotation", 23, reordered_selection);
  tracker.reset();
  const std::string after_reset = tracker.ensure_merge_key(
      "environment.rotation", 23, reordered_selection);

  EXPECT_NE(changed_control, first);
  EXPECT_NE(changed_primary, changed_control);
  EXPECT_NE(changed_selection, changed_primary);
  EXPECT_NE(after_reset, changed_selection);
}

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
  EXPECT_EQ(kUiDockPanelIds.size(), 6U);
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
  EXPECT_TRUE(manager.is_presented(UIPanelId::ToolSettings));
  EXPECT_TRUE(manager.focus(UIPanelId::ToolSettings));
  QCoreApplication::processEvents();
  EXPECT_EQ(window.focusWidget(), focus_target);

  UISettings requested = UISettings::defaults();
  requested.set_panel_visible(UIPanelId::ToolSettings, false);
  manager.apply_settings(requested);
  EXPECT_FALSE(manager.is_visible(UIPanelId::ToolSettings));
  EXPECT_FALSE(manager.is_presented(UIPanelId::ToolSettings));

  requested.set_panel_visible(UIPanelId::ToolSettings, true);
  manager.apply_settings(requested);
  EXPECT_TRUE(manager.is_visible(UIPanelId::ToolSettings));
  EXPECT_TRUE(manager.is_presented(UIPanelId::ToolSettings));

  UISettings persisted = UISettings::defaults();
  persisted.set_panel_visible(UIPanelId::ToolSettings, false);
  manager.write_settings(persisted);
  EXPECT_TRUE(persisted.is_panel_visible(UIPanelId::ToolSettings));
}

TEST(UIPanelManagerTests, ActiveMaterialDockCanCloseReopenFloatAndFocusBrowse) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-active-material-dock-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(QStringLiteral("Fusion"));

  QMainWindow window;
  auto *dock = new QDockWidget(&window);
  dock->setObjectName(QStringLiteral("dock_active_material"));
  auto *panel = new QtActiveMaterialPanel(dock);
  panel->set_presentation(std::nullopt, true);
  dock->setWidget(panel);
  window.addDockWidget(Qt::RightDockWidgetArea, dock);
  window.show();
  window.activateWindow();
  QCoreApplication::processEvents();

  UIPanelManager manager;
  manager.add(UIPanelId::ActiveMaterial, *dock);
  std::optional<bool> last_presented;
  int presentation_changes = 0;
  manager.set_presentation_changed_callback(
      [&](UIPanelId id, bool presented) {
        EXPECT_EQ(id, UIPanelId::ActiveMaterial);
        last_presented = presented;
        ++presentation_changes;
      });
  EXPECT_TRUE(manager.is_presented(UIPanelId::ActiveMaterial));
  ASSERT_TRUE(manager.focus(UIPanelId::ActiveMaterial));
  QCoreApplication::processEvents();
  auto *browse = panel->findChild<QPushButton *>(
      QStringLiteral("activeMaterial.browse"));
  ASSERT_NE(browse, nullptr);
  EXPECT_EQ(window.focusWidget(), browse);

  dock->close();
  EXPECT_FALSE(manager.is_visible(UIPanelId::ActiveMaterial));
  EXPECT_EQ(last_presented, std::optional<bool>(false));
  UISettings settings = UISettings::defaults();
  manager.write_settings(settings);
  EXPECT_FALSE(settings.is_panel_visible(UIPanelId::ActiveMaterial));
  ASSERT_TRUE(manager.set_visible(UIPanelId::ActiveMaterial, true));
  QCoreApplication::processEvents();
  EXPECT_TRUE(manager.is_presented(UIPanelId::ActiveMaterial));
  EXPECT_EQ(last_presented, std::optional<bool>(true));
  const int stable_presentation_changes = presentation_changes;
  EXPECT_FALSE(manager.set_visible(UIPanelId::ActiveMaterial, true));
  QCoreApplication::processEvents();
  EXPECT_EQ(presentation_changes, stable_presentation_changes);

  dock->setFloating(true);
  QCoreApplication::processEvents();
  const QByteArray floating_layout = window.saveState(1);
  dock->setFloating(false);
  ASSERT_TRUE(window.restoreState(floating_layout, 1));
  QCoreApplication::processEvents();
  EXPECT_TRUE(dock->isFloating());
  EXPECT_TRUE(manager.is_visible(UIPanelId::ActiveMaterial));
  manager.apply_settings(settings);
  EXPECT_FALSE(dock->isVisible());
  EXPECT_FALSE(manager.is_presented(UIPanelId::ActiveMaterial));
}

} // namespace
} // namespace quader
