////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_project_home_page.hpp"
#include "ui/qt_scene_workspace.hpp"
#include "ui/qt_selection_toolbar.hpp"
#include "ui/qt_tool_toolbar.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMetaObject>
#include <QModelIndex>
#include <QItemSelectionModel>
#include <QLabel>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleOption>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>

#include <optional>
#include <vector>

namespace {

void send_mouse_event(QWidget &target, QEvent::Type type, QPoint position,
                      Qt::MouseButton button, Qt::MouseButtons buttons) {
  const QPoint global = target.mapToGlobal(position);
  QMouseEvent event(type, QPointF(position), QPointF(global), button, buttons,
                    Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &event);
}

void click_widget(QWidget &target, QPoint position) {
  send_mouse_event(target, QEvent::MouseButtonPress, position,
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(target, QEvent::MouseButtonRelease, position,
                   Qt::LeftButton, Qt::NoButton);
}

void send_key(QWidget &target, int key) {
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &press);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &release);
}

int expect_persistent_button_faces(QToolBar &toolbar,
                                   int expected_minimum_height) {
  int action_button_count = 0;
  for (QToolButton *button : toolbar.findChildren<QToolButton *>()) {
    QAction *action = button->defaultAction();
    if (action == nullptr || !action->isCheckable()) {
      continue;
    }
    ++action_button_count;
    EXPECT_FALSE(button->autoRaise());
    EXPECT_TRUE(button->testAttribute(Qt::WA_Hover));
    EXPECT_TRUE(button->hasMouseTracking());
    EXPECT_EQ(button->focusPolicy(), Qt::StrongFocus);
    EXPECT_GE(button->minimumWidth(), 40);
    EXPECT_EQ(button->minimumHeight(), expected_minimum_height);
    EXPECT_FALSE(button->isDown());
    EXPECT_EQ(button->accessibleName(), action->text());
  }
  return action_button_count;
}

QImage render_toolbar_button_face(QToolButton &button,
                                  QStyle::State state) {
  QImage image(QSize(32, 32), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  QStyleOption option;
  option.rect = image.rect();
  option.palette = button.palette();
  option.state = state;
  button.style()->drawPrimitive(QStyle::PE_PanelButtonCommand, &option,
                                &painter, &button);
  return image;
}

} // namespace

TEST(QtProjectWorkspaceTests,
     EditorToolbarsKeepNativeButtonFacesVisibleAtRest) {
  int argc = 1;
  char application_name[] = "quader_toolbar_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  quader::QtSelectionToolbar selection_toolbar;
  quader::QtToolToolbar tool_toolbar;

  EXPECT_EQ(selection_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(tool_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(selection_toolbar, 30), 5);
  EXPECT_EQ(expect_persistent_button_faces(tool_toolbar, 40), 12);
  for (QToolButton *button :
       selection_toolbar.findChildren<QToolButton *>()) {
    if (button->defaultAction() != nullptr &&
        button->defaultAction()->isCheckable()) {
      EXPECT_EQ(button->maximumHeight(), 30);
    }
  }

  QToolButton *button = nullptr;
  for (QToolButton *candidate : tool_toolbar.findChildren<QToolButton *>()) {
    if (candidate->defaultAction() != nullptr &&
        candidate->defaultAction()->isCheckable()) {
      button = candidate;
      break;
    }
  }
  ASSERT_NE(button, nullptr);
  const QStyle::State default_state =
      QStyle::State_Active | QStyle::State_Enabled | QStyle::State_Raised;
  const QImage default_face =
      render_toolbar_button_face(*button, default_state);
  const QImage hover_face = render_toolbar_button_face(
      *button, default_state | QStyle::State_MouseOver);
  const QImage selected_face = render_toolbar_button_face(
      *button, default_state | QStyle::State_On);
  const QImage pressed_face = render_toolbar_button_face(
      *button, QStyle::State_Active | QStyle::State_Enabled |
                   QStyle::State_Sunken);

  EXPECT_NE(default_face, hover_face);
  EXPECT_NE(default_face, selected_face);
  EXPECT_NE(hover_face, selected_face);
  EXPECT_NE(default_face, pressed_face);

  QAction *action = button->defaultAction();
  action->setChecked(true);
  EXPECT_TRUE(action->isChecked());
  EXPECT_FALSE(button->isDown());
  action->setToolTip(QStringLiteral("Selection Tool [Q]"));
  EXPECT_EQ(button->accessibleDescription(),
            QStringLiteral("Selection Tool [Q]"));
}

TEST(QtProjectWorkspaceTests,
     SceneLabelsUseShortestUniqueParentsAndDirtyAsterisk) {
  const std::vector<quader::QtSceneTabItem> scenes{
      {.session_id = 1,
       .name = QStringLiteral("Main"),
       .project_relative_path = QStringLiteral("Scenes/A/Main.qdr")},
      {.session_id = 2,
       .name = QStringLiteral("Main"),
       .project_relative_path = QStringLiteral("Archive/A/Main.qdr"),
       .dirty = true},
      {.session_id = 3,
       .name = QStringLiteral("Props"),
       .project_relative_path = QStringLiteral("Scenes/Props.qdr")},
  };

  const std::vector<QString> labels =
      quader::qt_scene_tab_display_labels(scenes);

  ASSERT_EQ(labels.size(), 3u);
  EXPECT_EQ(labels[0], QStringLiteral("Scenes/A/Main"));
  EXPECT_EQ(labels[1], QStringLiteral("Archive/A/Main *"));
  EXPECT_EQ(labels[2], QStringLiteral("Props"));
}

TEST(QtProjectWorkspaceTests,
     DuplicateUnsavedNamesUseStableOrdinalDisambiguation) {
  const std::vector<quader::QtSceneTabItem> scenes{
      {.session_id = 11, .name = QStringLiteral("Untitled Scene")},
      {.session_id = 12,
       .name = QStringLiteral("Untitled Scene"),
       .dirty = true},
  };

  const std::vector<QString> labels =
      quader::qt_scene_tab_display_labels(scenes);

  ASSERT_EQ(labels.size(), 2u);
  EXPECT_EQ(labels[0], QStringLiteral("Untitled Scene (1)"));
  EXPECT_EQ(labels[1], QStringLiteral("Untitled Scene (2) *"));
}

TEST(QtProjectWorkspaceTests,
     HomeAndWorkspaceDispatchPresentationIntentWithoutMutatingModels) {
  int argc = 1;
  char application_name[] = "quader_ui_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  quader::QtProjectHomePage home;
  int new_project_requests = 0;
  int open_project_requests = 0;
  QString recent_manifest;
  QString removed_manifest;
  home.set_new_project_requested_callback(
      [&new_project_requests]() { ++new_project_requests; });
  home.set_open_project_requested_callback(
      [&open_project_requests]() { ++open_project_requests; });
  home.set_open_recent_project_requested_callback(
      [&recent_manifest](QString path) { recent_manifest = std::move(path); });
  home.set_remove_recent_project_requested_callback(
      [&removed_manifest](QString path) {
        removed_manifest = std::move(path);
      });
  const std::vector<quader::QtRecentProjectItem> recent_projects{
      {.name = QStringLiteral("Available"),
       .manifest_path = QStringLiteral("C:/Projects/Available/Available.qproj")},
      {.name = QStringLiteral("Missing"),
       .manifest_path = QStringLiteral("C:/Projects/Missing/Missing.qproj"),
       .available = false},
  };
  home.set_recent_projects(recent_projects);
  home.resize(900, 600);
  home.show();
  QCoreApplication::processEvents();

  EXPECT_EQ(home.findChild<QLabel *>(QStringLiteral("projectHome.title")),
            nullptr);
  EXPECT_NE(home.findChild<QLabel *>(
                QStringLiteral("projectHome.recentProjects.label")),
            nullptr);

  home.findChild<QPushButton *>(
          QStringLiteral("projectHome.newProject"))
      ->click();
  home.findChild<QPushButton *>(
          QStringLiteral("projectHome.openProject"))
      ->click();
  auto *recent_view = home.findChild<QTableView *>(
      QStringLiteral("projectHome.recentProjects.view"));
  ASSERT_NE(recent_view, nullptr);
  EXPECT_TRUE(recent_view->model()->flags(
                  recent_view->model()->index(1, 0)) & Qt::ItemIsEnabled)
      << "missing projects must remain selectable so they can be removed";
  const QModelIndex available_index = recent_view->model()->index(0, 0);
  const QRect available_rect = recent_view->visualRect(available_index);
  ASSERT_TRUE(available_rect.isValid());
  click_widget(*recent_view->viewport(), available_rect.center());
  EXPECT_EQ(recent_view->currentIndex().row(), 0);
  home.set_recent_projects(recent_projects);
  EXPECT_EQ(recent_view->currentIndex().row(), 0)
      << "an unchanged app refresh must preserve the selected recent project";
  send_key(*recent_view, Qt::Key_Return);
  EXPECT_EQ(new_project_requests, 1);
  EXPECT_EQ(open_project_requests, 1);
  EXPECT_EQ(recent_manifest,
            QStringLiteral("C:/Projects/Available/Available.qproj"));
  recent_manifest.clear();
  const QModelIndex missing_index = recent_view->model()->index(1, 0);
  const QRect missing_rect = recent_view->visualRect(missing_index);
  ASSERT_TRUE(missing_rect.isValid());
  click_widget(*recent_view->viewport(), missing_rect.center());
  EXPECT_EQ(recent_view->currentIndex().row(), 1);
  send_key(*recent_view, Qt::Key_Return);
  EXPECT_TRUE(recent_manifest.isEmpty());
  auto *remove_recent = home.findChild<QPushButton *>(
      QStringLiteral("projectHome.removeRecent"));
  ASSERT_NE(remove_recent, nullptr);
  ASSERT_TRUE(remove_recent->isEnabled());
  remove_recent->click();
  EXPECT_EQ(removed_manifest,
            QStringLiteral("C:/Projects/Missing/Missing.qproj"));

  QWidget viewport;
  quader::QtSceneWorkspace workspace;
  workspace.set_viewport_widget(&viewport);
  std::optional<quader::QtSceneSessionId> activated;
  std::optional<quader::QtSceneSessionId> closed;
  int new_scene_requests = 0;
  int open_scene_requests = 0;
  workspace.set_new_scene_requested_callback(
      [&new_scene_requests]() { ++new_scene_requests; });
  workspace.set_open_scene_requested_callback(
      [&open_scene_requests]() { ++open_scene_requests; });
  workspace.set_activate_scene_requested_callback(
      [&activated](quader::QtSceneSessionId id) { activated = id; });
  workspace.set_close_scene_requested_callback(
      [&closed](quader::QtSceneSessionId id) { closed = id; });
  int exit_instance_edit_requests = 0;
  workspace.set_exit_instance_edit_requested_callback(
      [&exit_instance_edit_requests]() { ++exit_instance_edit_requests; });

  EXPECT_TRUE(workspace.set_scene_tabs(
      {{.session_id = 41, .name = QStringLiteral("One")},
       {.session_id = 42,
        .name = QStringLiteral("Two"),
        .project_relative_path = QStringLiteral("Scenes/Two.qdr")}},
      41));
  EXPECT_EQ(workspace.tab_bar().tabData(0).toULongLong(), 41u);
  EXPECT_EQ(workspace.tab_bar().tabData(1).toULongLong(), 42u);
  workspace.tab_bar().setCurrentIndex(1);
  EXPECT_EQ(activated, 42u);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      &workspace.tab_bar(), "tabCloseRequested", Qt::DirectConnection,
      Q_ARG(int, 1)));
  EXPECT_EQ(closed, 42u);

  QWidget *breadcrumb_bar = workspace.findChild<QWidget *>(
      QStringLiteral("sceneWorkspace.instanceBreadcrumb"));
  auto *scene_crumb = workspace.findChild<QToolButton *>(
      QStringLiteral("sceneWorkspace.instanceBreadcrumb.scene"));
  auto *instance_crumb = workspace.findChild<QLabel *>(
      QStringLiteral("sceneWorkspace.instanceBreadcrumb.instance"));
  ASSERT_NE(breadcrumb_bar, nullptr);
  ASSERT_NE(scene_crumb, nullptr);
  ASSERT_NE(instance_crumb, nullptr);
  EXPECT_TRUE(breadcrumb_bar->isHidden());
  const int unchanged_tab_count = workspace.tab_bar().count();
  workspace.set_instance_edit_breadcrumb(quader::QtInstanceEditBreadcrumb{
      .scene_name = QStringLiteral("Two"),
      .instance_name = QStringLiteral("Cylinder Instance"),
  });
  EXPECT_FALSE(breadcrumb_bar->isHidden());
  EXPECT_EQ(workspace.tab_bar().count(), unchanged_tab_count)
      << "instance editing adds a lower row rather than replacing scene tabs";
  EXPECT_EQ(scene_crumb->text(), QStringLiteral("Two"));
  EXPECT_EQ(instance_crumb->text(), QStringLiteral("Cylinder Instance"));
  EXPECT_FALSE(scene_crumb->accessibleName().isEmpty());
  EXPECT_FALSE(instance_crumb->accessibleName().isEmpty());
  scene_crumb->click();
  EXPECT_EQ(exit_instance_edit_requests, 1);
  workspace.set_instance_edit_breadcrumb(std::nullopt);
  EXPECT_TRUE(breadcrumb_bar->isHidden());

  auto *new_scene = workspace.findChild<QToolButton *>(
      QStringLiteral("sceneWorkspace.newScene"));
  ASSERT_NE(new_scene, nullptr);
  new_scene->click();
  EXPECT_EQ(new_scene_requests, 1);

  EXPECT_TRUE(workspace.set_scene_tabs({}));
  auto *open_scene = workspace.findChild<QPushButton *>(
      QStringLiteral("sceneWorkspace.empty.openScene"));
  ASSERT_NE(open_scene, nullptr);
  open_scene->click();
  EXPECT_EQ(open_scene_requests, 1);

  workspace.set_viewport_widget(nullptr);
}
