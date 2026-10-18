////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/qt_viewport_quick_menu.hpp"
#include "viewport/viewport_input_router.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <QWidgetAction>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace {

struct QuickActionSnapshot final {
  QString text;
  QString object_name;
  QString submenu_object_name;
  bool enabled = false;
  bool separator = false;
  bool widget_action = false;
  bool widget_visible = false;
  bool widget_focusable = false;
  bool shortcut_empty = true;
  QString widget_text;
  std::vector<QuickActionSnapshot> children;
};

struct QuickMenuSnapshot final {
  bool opened = false;
  QString object_name;
  QString title;
  QString accessible_name;
  QPoint global_position;
  std::vector<QuickActionSnapshot> actions;
};

quader::LocalizationService loaded_localization() {
  quader::LocalizationService localization;
  EXPECT_TRUE(localization.load(std::filesystem::path(QUADER_SOURCE_DIR) /
                                    "resources" / "locales",
                                "en-US"));
  return localization;
}

QuickActionSnapshot snapshot_action(const QAction &action) {
  QuickActionSnapshot snapshot{
      .text = action.text(),
      .object_name = action.objectName(),
      .enabled = action.isEnabled(),
      .separator = action.isSeparator(),
      .shortcut_empty = action.shortcut().isEmpty(),
  };
  if (const auto *widget_action =
          qobject_cast<const QWidgetAction *>(&action)) {
    snapshot.widget_action = true;
    if (const auto *label =
            qobject_cast<const QLabel *>(widget_action->defaultWidget())) {
      snapshot.widget_visible = label->isVisible();
      snapshot.widget_focusable = label->focusPolicy() != Qt::NoFocus;
      snapshot.widget_text = label->text();
    }
  }
  if (const QMenu *submenu = action.menu(); submenu != nullptr) {
    snapshot.submenu_object_name = submenu->objectName();
    for (const QAction *child : submenu->actions()) {
      if (child != nullptr) {
        snapshot.children.push_back(snapshot_action(*child));
      }
    }
  }
  return snapshot;
}

QAction *find_action(QMenu &menu, const QString &object_name) {
  return menu.findChild<QAction *>(object_name, Qt::FindChildrenRecursively);
}

const QuickActionSnapshot *find_action(
    const std::vector<QuickActionSnapshot> &actions,
    const QString &object_name) {
  for (const QuickActionSnapshot &action : actions) {
    if (action.object_name == object_name) {
      return &action;
    }
    if (const QuickActionSnapshot *child =
            find_action(action.children, object_name)) {
      return child;
    }
  }
  return nullptr;
}

void snapshot_and_close(QuickMenuSnapshot &snapshot,
                        const QString &expected_menu_name) {
  auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
  if (menu == nullptr || menu->objectName() != expected_menu_name) {
    ADD_FAILURE() << "Expected viewport quick menu: "
                  << expected_menu_name.toStdString();
    if (QWidget *popup = QApplication::activePopupWidget()) {
      popup->close();
    }
    return;
  }
  snapshot.opened = true;
  snapshot.object_name = menu->objectName();
  snapshot.title = menu->title();
  snapshot.accessible_name = menu->accessibleName();
  snapshot.global_position = menu->pos();
  for (const QAction *action : menu->actions()) {
    if (action != nullptr) {
      snapshot.actions.push_back(snapshot_action(*action));
    }
  }
  menu->close();
}

bool accept_action(QMenu &menu, const QString &object_name) {
  QAction *action = find_action(menu, object_name);
  if (action == nullptr) {
    ADD_FAILURE() << "Expected quick-menu action: "
                  << object_name.toStdString();
    menu.close();
    return false;
  }
  menu.setActiveAction(action);
  QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
  QApplication::sendEvent(&menu, &accept);
  if (menu.isVisible()) {
    menu.close();
  }
  return true;
}

void prepare_viewport_owner(QWidget &owner) {
  owner.setFocusPolicy(Qt::StrongFocus);
  owner.resize(640, 480);
  owner.show();
  owner.activateWindow();
  owner.setFocus(Qt::ShortcutFocusReason);
  QCoreApplication::processEvents();
}

} // namespace

TEST(QtViewportQuickMenuTests,
     AddMenuHasExactHierarchyDisabledSpotlightAndCursorAnchor) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-add-quick-menu-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  quader::LocalizationService localization = loaded_localization();
  quader::QtViewportQuickMenuPresenter presenter;
  QWidget owner;
  prepare_viewport_owner(owner);

  const QPoint requested_global_position(120, 140);
  QuickMenuSnapshot snapshot;
  QTimer::singleShot(0, &owner, [&]() {
    snapshot_and_close(snapshot,
                       QStringLiteral("viewport_add_quick_menu"));
  });
  EXPECT_FALSE(presenter.show(
      owner, requested_global_position, quader::ViewportPaneId::Perspective,
      editor, input_router, localization));

  ASSERT_TRUE(snapshot.opened);
  EXPECT_EQ(snapshot.title, QStringLiteral("Add"));
  EXPECT_EQ(snapshot.accessible_name, QStringLiteral("Add"));
  EXPECT_EQ(snapshot.global_position, requested_global_position)
      << "QMenu::exec must receive the initiating viewport cursor position "
         "without a device-pixel rescale.";
  ASSERT_EQ(snapshot.actions.size(), 6u);

  const QuickActionSnapshot &title = snapshot.actions[0];
  EXPECT_EQ(title.object_name,
            QStringLiteral("viewport_add_quick_menu_title"));
  EXPECT_EQ(title.text, QStringLiteral("Add"));
  EXPECT_FALSE(title.enabled);
  EXPECT_FALSE(title.separator);
  EXPECT_TRUE(title.widget_action);
  EXPECT_TRUE(title.widget_visible);
  EXPECT_FALSE(title.widget_focusable);
  EXPECT_EQ(title.widget_text, QStringLiteral("Add"));
  EXPECT_TRUE(title.shortcut_empty);

  const QuickActionSnapshot &title_separator = snapshot.actions[1];
  EXPECT_EQ(title_separator.object_name,
            QStringLiteral("viewport_add_quick_menu_title_separator"));
  EXPECT_TRUE(title_separator.separator);
  EXPECT_TRUE(title_separator.shortcut_empty);

  const QuickActionSnapshot &mesh = snapshot.actions[2];
  EXPECT_EQ(mesh.text, QStringLiteral("Mesh"));
  EXPECT_EQ(mesh.submenu_object_name,
            QStringLiteral("viewport_add_mesh_submenu"));
  ASSERT_EQ(mesh.children.size(), 6u);
  const std::vector<QString> expected_mesh_ids{
      QStringLiteral("tools.activate.create_box"),
      QStringLiteral("tools.activate.create_cylinder"),
      QStringLiteral("tools.activate.create_sphere"),
      QStringLiteral("tools.activate.create_torus"),
      QStringLiteral("tools.activate.create_cone"),
      QStringLiteral("tools.activate.create_stairs"),
  };
  const std::vector<QString> expected_mesh_labels{
      QStringLiteral("Box"), QStringLiteral("Cylinder"),
      QStringLiteral("Sphere"), QStringLiteral("Torus"),
      QStringLiteral("Cone"), QStringLiteral("Stairs && Ramps"),
  };
  for (std::size_t index = 0; index < mesh.children.size(); ++index) {
    EXPECT_EQ(mesh.children[index].object_name, expected_mesh_ids[index]);
    EXPECT_EQ(mesh.children[index].text, expected_mesh_labels[index]);
    EXPECT_TRUE(mesh.children[index].enabled);
    EXPECT_TRUE(mesh.children[index].shortcut_empty);
  }

  EXPECT_EQ(snapshot.actions[3].object_name,
            QStringLiteral("tools.activate.create_curve"));
  EXPECT_EQ(snapshot.actions[3].text, QStringLiteral("Curve"));
  EXPECT_TRUE(snapshot.actions[3].enabled);
  EXPECT_TRUE(snapshot.actions[3].shortcut_empty);

  const QuickActionSnapshot &light = snapshot.actions[4];
  EXPECT_EQ(light.text, QStringLiteral("Light"));
  EXPECT_EQ(light.submenu_object_name,
            QStringLiteral("viewport_add_light_submenu"));
  ASSERT_EQ(light.children.size(), 3u);
  EXPECT_EQ(light.children[0].object_name,
            QStringLiteral("scene.create_point_light"));
  EXPECT_EQ(light.children[0].text, QStringLiteral("Point"));
  EXPECT_TRUE(light.children[0].enabled);
  EXPECT_TRUE(light.children[0].shortcut_empty);
  EXPECT_EQ(light.children[1].object_name,
            QStringLiteral("viewport_add_spotlight_nyi"));
  EXPECT_EQ(light.children[1].text, QStringLiteral("Spotlight (NYI)"));
  EXPECT_FALSE(light.children[1].enabled);
  EXPECT_TRUE(light.children[1].shortcut_empty);
  EXPECT_EQ(light.children[2].object_name,
            QStringLiteral("scene.create_directional_light"));
  EXPECT_EQ(light.children[2].text, QStringLiteral("Directional"));
  EXPECT_TRUE(light.children[2].enabled);
  EXPECT_TRUE(light.children[2].shortcut_empty);

  EXPECT_EQ(snapshot.actions[5].object_name,
            QStringLiteral("scene.create_empty"));
  EXPECT_EQ(snapshot.actions[5].text, QStringLiteral("Empty"));
  EXPECT_TRUE(snapshot.actions[5].enabled);
  EXPECT_TRUE(snapshot.actions[5].shortcut_empty);
  EXPECT_EQ(owner.focusWidget(), &owner)
      << "a valid cancellation must return focus to the initiating viewport";
}

TEST(QtViewportQuickMenuTests,
     AddMenuDispatchesSelectedCommandAndReturnsViewportFocus) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-add-dispatch-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  quader::LocalizationService localization = loaded_localization();
  quader::QtViewportQuickMenuPresenter presenter;
  QWidget owner;
  prepare_viewport_owner(owner);
  const auto entity_count = editor.query().entities().size();

  QTimer::singleShot(0, &owner, [&]() {
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    ASSERT_NE(menu, nullptr);
    (void)accept_action(*menu, QStringLiteral("scene.create_empty"));
  });

  EXPECT_TRUE(presenter.show(
      owner, QPoint(160, 180), quader::ViewportPaneId::Perspective, editor,
      input_router, localization));
  EXPECT_EQ(editor.query().entities().size(), entity_count + 1u);
  EXPECT_EQ(owner.focusWidget(), &owner);
}

TEST(QtViewportQuickMenuTests,
     ActiveCurveDraftDisablesFreshCurveEntryWithoutDiscardingDraft) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-add-disabled-curve-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(session.curve().begin_new_curve().ok);
  ASSERT_TRUE(session.curve().begin_point({1.0F, 2.0F, 3.0F}).ok);
  ASSERT_TRUE(session.curve().commit_drag().ok);
  ASSERT_EQ(session.curve().point_count(), 1u);

  quader::ViewportInputRouter input_router;
  quader::LocalizationService localization = loaded_localization();
  quader::QtViewportQuickMenuPresenter presenter;
  QWidget owner;
  prepare_viewport_owner(owner);
  QuickMenuSnapshot snapshot;
  QTimer::singleShot(0, &owner, [&]() {
    snapshot_and_close(snapshot,
                       QStringLiteral("viewport_add_quick_menu"));
  });
  EXPECT_FALSE(presenter.show(
      owner, QPoint(180, 200), quader::ViewportPaneId::Perspective, editor,
      input_router, localization));

  ASSERT_TRUE(snapshot.opened);
  const QuickActionSnapshot *curve = find_action(
      snapshot.actions, QStringLiteral("tools.activate.create_curve"));
  ASSERT_NE(curve, nullptr);
  EXPECT_FALSE(curve->enabled)
      << "a fresh Curve session must not be offered while doing so would "
         "discard the active draft";
  EXPECT_EQ(session.curve().point_count(), 1u);
  EXPECT_TRUE(session.curve().drawing());

  ASSERT_TRUE(session.curve().cancel().ok);
  const std::uint64_t command_status_revision =
      editor.commands().last_status_revision();
  QTimer::singleShot(0, &owner, [&]() {
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    ASSERT_NE(menu, nullptr);
    QAction *curve_action =
        find_action(*menu, QStringLiteral("tools.activate.create_curve"));
    ASSERT_NE(curve_action, nullptr);
    ASSERT_TRUE(curve_action->isEnabled());
    ASSERT_TRUE(session.curve().begin_new_curve().ok);
    ASSERT_TRUE(session.curve().begin_point({4.0F, 5.0F, 6.0F}).ok);
    ASSERT_TRUE(session.curve().commit_drag().ok);
    (void)accept_action(*menu,
                        QStringLiteral("tools.activate.create_curve"));
  });
  EXPECT_FALSE(presenter.show(
      owner, QPoint(190, 210), quader::ViewportPaneId::Perspective, editor,
      input_router, localization));
  EXPECT_EQ(editor.commands().last_status_revision(), command_status_revision)
      << "a command that becomes disabled inside QMenu::exec must not be "
         "dispatched";
  EXPECT_EQ(session.curve().point_count(), 1u);
  EXPECT_TRUE(session.curve().drawing());
}

TEST(QtViewportQuickMenuTests,
     RevisionAndSourcePaneChangesInsidePopupRejectSelectedIntent) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-quick-menu-fence-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  ASSERT_TRUE(session.viewport_workspace().set_layout(
      quader::ViewportLayoutId::FourQuad));
  ASSERT_TRUE(session.viewport_workspace().focus_pane(
      quader::ViewportPaneId::Top));
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Quick Menu Fence Box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  const auto entity_count = editor.query().entities().size();
  const auto command_status_revision = editor.commands().last_status_revision();

  quader::ViewportInputRouter input_router;
  quader::LocalizationService localization = loaded_localization();
  quader::QtViewportQuickMenuPresenter presenter;
  QWidget owner;
  prepare_viewport_owner(owner);

  QTimer::singleShot(0, &owner, [&]() {
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    ASSERT_NE(menu, nullptr);
    ASSERT_TRUE(editor.selection().clear().ok());
    (void)accept_action(*menu, QStringLiteral("scene.create_empty"));
  });
  EXPECT_FALSE(presenter.show(
      owner, QPoint(200, 220), quader::ViewportPaneId::Top, editor,
      input_router, localization));
  EXPECT_EQ(editor.query().entities().size(), entity_count);
  EXPECT_EQ(editor.commands().last_status_revision(), command_status_revision);
  EXPECT_EQ(editor.viewports().focused_pane(), quader::ViewportPaneId::Top);

  QTimer::singleShot(0, &owner, [&]() {
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    ASSERT_NE(menu, nullptr);
    ASSERT_TRUE(session.viewport_workspace().focus_pane(
        quader::ViewportPaneId::Perspective));
    (void)accept_action(*menu, QStringLiteral("scene.create_empty"));
  });
  EXPECT_FALSE(presenter.show(
      owner, QPoint(210, 230), quader::ViewportPaneId::Top, editor,
      input_router, localization));
  EXPECT_EQ(editor.query().entities().size(), entity_count);
  EXPECT_EQ(editor.commands().last_status_revision(), command_status_revision);
}

TEST(QtViewportQuickMenuTests,
     RuntimeInvalidationAndOwnerDestructionAreSafeNestedLoopFences) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-quick-menu-lifetime-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  quader::LocalizationService localization = loaded_localization();
  quader::QtViewportQuickMenuPresenter presenter;
  const auto entity_count = editor.query().entities().size();
  const auto command_status_revision = editor.commands().last_status_revision();

  QWidget owner;
  prepare_viewport_owner(owner);
  std::uint64_t runtime_generation = 9u;
  const quader::ViewportRuntimeBindingToken runtime_binding{
      .current_generation = &runtime_generation,
      .expected_generation = runtime_generation,
  };
  QTimer::singleShot(0, &owner, [&]() {
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    ASSERT_NE(menu, nullptr);
    ++runtime_generation;
    (void)accept_action(*menu, QStringLiteral("scene.create_empty"));
  });
  EXPECT_FALSE(presenter.show(
      owner, QPoint(220, 240), quader::ViewportPaneId::Perspective, editor,
      input_router, localization, runtime_binding));
  EXPECT_EQ(editor.query().entities().size(), entity_count);
  EXPECT_EQ(editor.commands().last_status_revision(), command_status_revision);

  QPointer<QWidget> destroyed_owner = new QWidget();
  destroyed_owner->setFocusPolicy(Qt::StrongFocus);
  destroyed_owner->resize(320, 240);
  destroyed_owner->show();
  QCoreApplication::processEvents();
  QTimer::singleShot(0, [&]() {
    QWidget *const popup = QApplication::activePopupWidget();
    delete destroyed_owner.data();
    if (popup != nullptr) {
      popup->close();
    }
  });
  EXPECT_FALSE(presenter.show(
      *destroyed_owner.data(), QPoint(240, 260),
      quader::ViewportPaneId::Perspective, editor, input_router, localization));
  EXPECT_TRUE(destroyed_owner.isNull());
}
