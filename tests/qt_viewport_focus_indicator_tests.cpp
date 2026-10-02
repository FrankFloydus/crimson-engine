////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "support/qt_viewport_input_test_helpers.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <QApplication>
#include <QAction>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegion>
#include <QToolButton>
#include <QWindow>

namespace {

/// Detects redundant native border presentation work on unchanged frames.
class BorderEventCounter final : public QObject {
public:
  int changes = 0;
  int hides = 0;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::Hide) {
      ++hides;
    }
    switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::ZOrderChange:
    case QEvent::UpdateRequest:
      ++changes;
      break;
    default:
      break;
    }
    return QObject::eventFilter(watched, event);
  }
};

} // namespace

TEST(QtViewportFocusIndicatorTests, OneOrangeInsetBorderFollowsActivePaneInEveryLayout) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-viewport-focus-border";
  char *argv[] = {name, nullptr};
  QApplication app(argc, argv);
  quader::ViewportWorkspaceService workspace;
  QWidget host;
  host.setAttribute(Qt::WA_DontShowOnScreen);
  host.resize(1000, 800);
  quader::QtViewportPanel panel(&host);
  QLineEdit field(&host);
  field.setGeometry(810, 10, 150, 30);
  panel.resize(800, 600);
  panel.attach_workspace(workspace);
  panel.set_startup_presentation_blocked(false);
  host.show();
  QApplication::setActiveWindow(&host);
  QCoreApplication::processEvents();
  auto *border = panel.findChild<QWidget *>(QStringLiteral("viewportFocusedPaneFrame"));
  ASSERT_NE(border, nullptr);
  EXPECT_TRUE(border->testAttribute(Qt::WA_NativeWindow));
  EXPECT_TRUE(border->testAttribute(Qt::WA_TransparentForMouseEvents));
  EXPECT_EQ(border->focusPolicy(), Qt::NoFocus);

  for (const auto layout : {quader::ViewportLayoutId::Single,
                           quader::ViewportLayoutId::TwoHorizontal,
                           quader::ViewportLayoutId::TwoVertical,
                           quader::ViewportLayoutId::ThreePrimaryRight,
                           quader::ViewportLayoutId::ThreePrimaryLeft,
                           quader::ViewportLayoutId::ThreePrimaryBottom,
                           quader::ViewportLayoutId::ThreePrimaryTop,
                           quader::ViewportLayoutId::FourQuad}) {
    SCOPED_TRACE(static_cast<int>(layout));
    workspace.set_layout(layout);
    panel.refresh_workspace_layout();
    for (const auto &placement : workspace.active_pane_placements()) {
      auto *surface = panel.surface(placement.pane_id);
      ASSERT_NE(surface, nullptr);
      auto *container = panel.findChild<QWidget *>(
          QStringLiteral("viewportSurfaceContainer_%1").arg(QString::fromUtf8(
              quader::viewport_pane_stable_id(placement.pane_id).data())));
      ASSERT_NE(container, nullptr);
      container->show();
      container->setFocus();
      ASSERT_EQ(QApplication::focusWidget(), container->focusProxy());
      const QPointF point(40, 50);
      QEnterEvent hover(point, point, surface->mapToGlobal(point));
      quader::test_support::send_visible_viewport_event(surface, &hover);
      EXPECT_EQ(workspace.focused_pane(), placement.pane_id);
      EXPECT_FALSE(border->isHidden());
      auto *pane = panel.findChild<QWidget *>(
          QStringLiteral("viewportPane_%1").arg(QString::fromUtf8(
              quader::viewport_pane_stable_id(placement.pane_id).data())));
      ASSERT_NE(pane, nullptr);
      EXPECT_EQ(border->geometry(),
                QRect(container->mapTo(&panel, QPoint()), container->size()));
      EXPECT_GT(border->y(), pane->y());
      EXPECT_TRUE(border->mask().contains(QPoint(0, 0)));
      EXPECT_TRUE(border->mask().contains(QPoint(border->width() - 1, border->height() - 1)));
      EXPECT_FALSE(border->mask().contains(border->rect().center()));
    }
  }

  QImage image(border->size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  border->render(&painter);
  painter.end();
  EXPECT_EQ(image.pixelColor(0, 0), quader::qt_current_item_outline_color());
  EXPECT_EQ(image.pixelColor(image.width() - 1, image.height() - 1),
            quader::qt_current_item_outline_color());
  EXPECT_EQ(quader::qt_current_item_outline_color(), QColor(255, 143, 24));

  field.setFocus();
  EXPECT_TRUE(border->isHidden());
  auto *hover_surface = panel.surface(quader::ViewportPaneId::Perspective);
  const QPointF hover_point(40, 50);
  QEnterEvent editor_hover(hover_point, hover_point,
                           hover_surface->mapToGlobal(hover_point));
  quader::test_support::send_visible_viewport_event(hover_surface, &editor_hover);
  EXPECT_TRUE(field.hasFocus());
  EXPECT_TRUE(border->isHidden());
  ASSERT_TRUE(panel.focus_active_pane());
  EXPECT_FALSE(border->isHidden());
  hover_surface = panel.surface(quader::ViewportPaneId::Top);
  QEnterEvent pane_hover(hover_point, hover_point,
                         hover_surface->mapToGlobal(hover_point));
  quader::test_support::send_visible_viewport_event(hover_surface, &pane_hover);
  EXPECT_EQ(workspace.focused_pane(), quader::ViewportPaneId::Top);
  auto *top = panel.findChild<QWidget *>(QStringLiteral("viewportPane_top"));
  ASSERT_NE(top, nullptr);
  auto *top_content = panel.findChild<QWidget *>(
      QStringLiteral("viewportSurfaceContainer_top"));
  ASSERT_NE(top_content, nullptr);
  const auto content_bounds = [&] {
    return QRect(top_content->mapTo(&panel, QPoint()), top_content->size());
  };
  EXPECT_EQ(border->geometry(), content_bounds());
  EXPECT_GT(border->y(), top->y());
  EXPECT_FALSE(border->isHidden());

  workspace.set_focused_pane_maximized(true);
  panel.refresh_workspace_layout();
  EXPECT_EQ(border->geometry(), content_bounds());
  panel.resize(900, 650);
  EXPECT_EQ(border->geometry(), content_bounds());
  panel.set_startup_presentation_blocked(true);
  EXPECT_TRUE(border->isHidden());
  panel.set_startup_presentation_blocked(false);
  auto *container = panel.findChild<QWidget *>(
      QStringLiteral("viewportSurfaceContainer_%1").arg(QString::fromUtf8(
          quader::viewport_pane_stable_id(workspace.focused_pane()).data())));
  ASSERT_NE(container, nullptr);
  container->show();
  ASSERT_TRUE(panel.focus_active_pane());
  EXPECT_FALSE(border->isHidden());
  EXPECT_EQ(panel.findChildren<QWidget *>(QStringLiteral("viewportFocusedPaneFrame")).size(), 1);
  field.setFocus();
  EXPECT_TRUE(border->isHidden());
  EXPECT_TRUE(field.hasFocus());
}

TEST(QtViewportFocusIndicatorTests, UnchangedFramesDoNotRepeatBorderPresentation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-viewport-focus-border-retained";
  char *argv[] = {name, nullptr};
  QApplication app(argc, argv);
  quader::ViewportWorkspaceService workspace;
  quader::QtViewportPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen);
  panel.resize(800, 600);
  panel.attach_workspace(workspace);
  panel.set_startup_presentation_blocked(false);
  panel.show();
  QApplication::setActiveWindow(&panel);
  QCoreApplication::processEvents();
  auto *container = panel.findChild<QWidget *>(
      QStringLiteral("viewportSurfaceContainer_perspective"));
  ASSERT_NE(container, nullptr);
  container->show();
  ASSERT_TRUE(panel.focus_active_pane());
  QCoreApplication::processEvents();
  auto *border = panel.findChild<QWidget *>(QStringLiteral("viewportFocusedPaneFrame"));
  ASSERT_NE(border, nullptr);
  ASSERT_FALSE(border->isHidden());
  BorderEventCounter events;
  border->installEventFilter(&events);
  for (int frame = 0; frame < 64; ++frame) {
    panel.refresh_workspace_layout();
  }
  QCoreApplication::processEvents();
  EXPECT_EQ(events.changes, 0);
}

TEST(QtViewportFocusIndicatorTests,
     StripMouseKeyboardAndMenusKeepBorderAndHintContextStable) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-viewport-strip-focus";
  char *argv[] = {name, nullptr};
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  auto &workspace = session.viewport_workspace();
  constexpr auto kPane = quader::ViewportPaneId::Perspective;
  ASSERT_TRUE(workspace.set_layout(quader::ViewportLayoutId::FourQuad));
  auto expected_pane = kPane;

  QWidget host;
  host.setAttribute(Qt::WA_DontShowOnScreen);
  host.resize(1000, 700);
  quader::QtViewportPanel panel(&host);
  panel.resize(800, 600);
  panel.attach_workspace(workspace);
  panel.set_startup_presentation_blocked(false);
  QLineEdit field(&host);
  field.setGeometry(820, 10, 150, 30);
  QAction snap(QStringLiteral("Snapping"), &panel);
  snap.setObjectName(QStringLiteral("snappingEnabled"));
  snap.setCheckable(true);
  panel.set_control_actions({&snap});
  int dispatch_count = 0;
  panel.set_control_action_requested_callback(
      [&](quader::ViewportPaneId pane, QAction &) {
        ++dispatch_count;
        EXPECT_EQ(pane, expected_pane);
        const auto control = quader::QuaderViewportControl::SnappingEnabled;
        EXPECT_TRUE(editor.viewports().set_control(
            pane, control, !editor.viewports().control(pane, control)).ok());
      });
  host.show();
  QApplication::setActiveWindow(&host);
  QCoreApplication::processEvents();
  auto *surface = panel.surface(kPane);
  ASSERT_NE(surface, nullptr);
  auto *button = panel.findChild<QToolButton *>(
      QStringLiteral("viewportStrip_snappingEnabled"));
  auto *camera = panel.findChild<QPushButton *>(
      QStringLiteral("viewportCamera_perspective"));
  auto *border = panel.findChild<QWidget *>(
      QStringLiteral("viewportFocusedPaneFrame"));
  auto *container = panel.findChild<QWidget *>(
      QStringLiteral("viewportSurfaceContainer_perspective"));
  ASSERT_NE(button, nullptr);
  ASSERT_NE(camera, nullptr);
  ASSERT_NE(border, nullptr);
  ASSERT_NE(container, nullptr);
  ASSERT_NE(camera->menu(), nullptr);
  ASSERT_TRUE(button->isVisible());
  // Rendering normally reveals the native container after startup stabilizes.
  // This fixture exercises Qt focus without initializing the renderer.
  container->show();
  QCoreApplication::processEvents();
  ASSERT_TRUE(panel.focus_active_pane());
  ASSERT_EQ(QApplication::focusWidget(), container->focusProxy());
  QFocusEvent surface_focus(QEvent::FocusIn, Qt::OtherFocusReason);
  QApplication::sendEvent(surface, &surface_focus);
  const QPointF hover_point(40, 50);
  QEnterEvent hover(hover_point, hover_point, surface->mapToGlobal(hover_point));
  quader::test_support::send_visible_viewport_event(surface, &hover);
  const auto process_input = [&]() {
    panel.process_input(editor, session.scene(), session.assets(),
                        session.modeling(), {}, session.native_tools(),
                        &session.point_placement(), 1.0 / 60.0);
  };
  process_input();
  ASSERT_FALSE(border->isHidden());
  ASSERT_TRUE(panel.operation_input_context().viewport_available);
  const auto content_revision = session.scene().revision();
  const auto undo_count = session.history().undo_count();
  const auto settings_origin = editor.tools().active_settings_presentation_origin();
  BorderEventCounter border_events;
  border->installEventFilter(&border_events);
  const auto expect_stable = [&]() {
    process_input();
    EXPECT_EQ(workspace.focused_pane(), expected_pane);
    EXPECT_FALSE(border->isHidden());
    EXPECT_EQ(border_events.hides, 0);
    EXPECT_TRUE(panel.operation_input_context().viewport_available);
    EXPECT_EQ(session.scene().revision(), content_revision);
    EXPECT_EQ(session.history().undo_count(), undo_count);
    EXPECT_EQ(editor.tools().active_settings_presentation_origin(), settings_origin);
  };

  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(surface, &leave);
  QWidget *native_host = button->nativeParentWidget();
  ASSERT_NE(native_host, nullptr);
  ASSERT_NE(native_host->windowHandle(), nullptr);
  const QPointF global = button->mapToGlobal(button->rect().center());
  const QPointF position = native_host->mapFromGlobal(global);
  // Deliver through QWidgetWindow so Qt performs native hit testing and
  // mouse-focus transfer, rather than calling the button handler directly.
  QMouseEvent press(QEvent::MouseButtonPress, position, global,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(native_host->windowHandle(), &press);
  ASSERT_TRUE(button->hasFocus());
  QFocusEvent surface_blur(QEvent::FocusOut, Qt::MouseFocusReason);
  QApplication::sendEvent(surface, &surface_blur);
  expect_stable();
  EXPECT_EQ(dispatch_count, 0);
  QMouseEvent release(QEvent::MouseButtonRelease, position, global,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(native_host->windowHandle(), &release);
  expect_stable();
  EXPECT_TRUE(button->hasFocus());
  EXPECT_EQ(dispatch_count, 1);

  camera->setFocus(Qt::TabFocusReason);
  expect_stable();
  button->setFocus(Qt::TabFocusReason);
  expect_stable();
  QKeyEvent key_press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
  QApplication::sendEvent(button, &key_press);
  expect_stable();
  QKeyEvent key_release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
  QApplication::sendEvent(button, &key_release);
  expect_stable();
  EXPECT_EQ(dispatch_count, 2);
  EXPECT_TRUE(button->hasFocus());

  camera->setFocus(Qt::TabFocusReason);
  camera->menu()->popup(camera->mapToGlobal(QPoint(0, camera->height())));
  QCoreApplication::processEvents();
  ASSERT_TRUE(camera->menu()->isVisible());
  EXPECT_EQ(QApplication::activePopupWidget(), camera->menu());
  expect_stable();
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QApplication::sendEvent(camera->menu(), &escape);
  QCoreApplication::processEvents();
  EXPECT_FALSE(camera->menu()->isVisible());
  expect_stable();
  EXPECT_EQ(dispatch_count, 2);

  auto *top_frame = panel.findChild<QWidget *>(QStringLiteral("viewportPane_top"));
  auto *top_container = panel.findChild<QWidget *>(
      QStringLiteral("viewportSurfaceContainer_top"));
  ASSERT_NE(top_frame, nullptr);
  ASSERT_NE(top_container, nullptr);
  auto *top_button = top_frame->findChild<QToolButton *>(
      QStringLiteral("viewportStrip_snappingEnabled"));
  ASSERT_NE(top_button, nullptr);
  ASSERT_TRUE(top_button->isVisible());
  top_container->show();
  QCoreApplication::processEvents();
  QWidget *top_native_host = top_button->nativeParentWidget();
  ASSERT_NE(top_native_host, nullptr);
  ASSERT_NE(top_native_host->windowHandle(), nullptr);
  const QPointF top_global = top_button->mapToGlobal(top_button->rect().center());
  const QPointF top_position = top_native_host->mapFromGlobal(top_global);
  expected_pane = quader::ViewportPaneId::Top;
  QMouseEvent top_press(QEvent::MouseButtonPress, top_position, top_global,
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(top_native_host->windowHandle(), &top_press);
  ASSERT_TRUE(top_button->hasFocus());
  expect_stable();
  EXPECT_EQ(dispatch_count, 2);
  EXPECT_EQ(border->geometry(),
            QRect(top_container->mapTo(&panel, QPoint()), top_container->size()));
  QMouseEvent top_release(QEvent::MouseButtonRelease, top_position, top_global,
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(top_native_host->windowHandle(), &top_release);
  expect_stable();
  EXPECT_EQ(dispatch_count, 3);
  EXPECT_TRUE(top_button->hasFocus());

  field.setFocus(Qt::TabFocusReason);
  // This headless host supplies native focus transitions explicitly, including
  // the child revealed while testing the second pane's toolbar.
  auto *top_surface = panel.surface(quader::ViewportPaneId::Top);
  ASSERT_NE(top_surface, nullptr);
  QFocusEvent top_blur(QEvent::FocusOut, Qt::TabFocusReason);
  QApplication::sendEvent(top_surface, &top_blur);
  QApplication::sendEvent(top_surface, &leave);
  process_input();
  EXPECT_TRUE(field.hasFocus());
  EXPECT_TRUE(border->isHidden());
  EXPECT_FALSE(panel.operation_input_context().viewport_available);
  panel.abandon_interactions();
}
