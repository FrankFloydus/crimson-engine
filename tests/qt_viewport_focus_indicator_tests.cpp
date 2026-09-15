////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_application_theme.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <QApplication>
#include <QEnterEvent>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QRegion>

namespace {

/// Detects redundant native border presentation work on unchanged frames.
class BorderEventCounter final : public QObject {
public:
  int changes = 0;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
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
      ASSERT_EQ(QApplication::focusWidget(), container);
      const QPointF point(40, 50);
      QEnterEvent hover(point, point, surface->mapToGlobal(point));
      QApplication::sendEvent(surface, &hover);
      EXPECT_EQ(workspace.focused_pane(), placement.pane_id);
      EXPECT_FALSE(border->isHidden());
      auto *pane = panel.findChild<QWidget *>(
          QStringLiteral("viewportPane_%1").arg(QString::fromUtf8(
              quader::viewport_pane_stable_id(placement.pane_id).data())));
      ASSERT_NE(pane, nullptr);
      EXPECT_EQ(border->geometry(), pane->geometry());
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
  QApplication::sendEvent(hover_surface, &editor_hover);
  EXPECT_TRUE(field.hasFocus());
  EXPECT_TRUE(border->isHidden());
  ASSERT_TRUE(panel.focus_active_pane());
  EXPECT_FALSE(border->isHidden());
  hover_surface = panel.surface(quader::ViewportPaneId::Top);
  QEnterEvent pane_hover(hover_point, hover_point,
                         hover_surface->mapToGlobal(hover_point));
  QApplication::sendEvent(hover_surface, &pane_hover);
  EXPECT_EQ(workspace.focused_pane(), quader::ViewportPaneId::Top);
  auto *top = panel.findChild<QWidget *>(QStringLiteral("viewportPane_top"));
  ASSERT_NE(top, nullptr);
  EXPECT_EQ(border->geometry(), top->geometry());
  EXPECT_FALSE(border->isHidden());

  workspace.set_focused_pane_maximized(true);
  panel.refresh_workspace_layout();
  EXPECT_EQ(border->geometry(), panel.rect());
  panel.resize(900, 650);
  EXPECT_EQ(border->geometry(), panel.rect());
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
