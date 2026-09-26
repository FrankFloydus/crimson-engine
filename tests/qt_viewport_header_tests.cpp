////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_application_theme.hpp"
#include "ui/qt_application_style.hpp"
#include "ui/qt_viewport_control_strip.hpp"
#include "ui/qt_viewport_chrome_layout.hpp"
#include "ui/qt_viewport_panel.hpp"

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QImage>
#include <QIcon>
#include <QMenu>
#include <QToolButton>
#include <QStyleOption>
#include <QLayout>
#include <QPushButton>

#include <algorithm>
#include <array>
#include <vector>

using quader::ViewportPaneId;

TEST(QtViewportChromeLayoutTests,
     KeepsUnifiedDisplayMenuAndToolbarInsideEveryPaneWidth) {
  constexpr int kMenuWidth = 110;
  constexpr int kInset = 6;

  for (int pane_width = 0; pane_width <= 400; ++pane_width) {
    const quader::QtViewportChromeLayout layout =
        quader::qt_viewport_chrome_layout(pane_width, kMenuWidth, kInset);
    EXPECT_GE(layout.toolbar_x, 0) << pane_width;
    EXPECT_GE(layout.toolbar_width, 0) << pane_width;
    EXPECT_LE(layout.toolbar_x + layout.toolbar_width, pane_width)
        << pane_width;
    if (pane_width > 0) {
      EXPECT_GT(layout.menu_width, 0) << pane_width;
    }
  }
}

TEST(QtViewportChromeLayoutTests,
     UsesReferenceMinimumDisplayMenuWidthWhenThePaneCanAccommodateIt) {
  constexpr int kMenuWidth = 110;
  constexpr int kInset = 6;

  const quader::QtViewportChromeLayout wide =
      quader::qt_viewport_chrome_layout(400, kMenuWidth, kInset);
  EXPECT_EQ(wide.menu_width, quader::kQtViewportDisplayMenuMinimumWidth);
  EXPECT_EQ(wide.toolbar_width, quader::kQtViewportDisplayMenuMinimumWidth);
  EXPECT_EQ(wide.toolbar_x,
            400 - kInset - quader::kQtViewportDisplayMenuMinimumWidth);

  const quader::QtViewportChromeLayout long_label =
      quader::qt_viewport_chrome_layout(400, 180, kInset);
  EXPECT_EQ(long_label.menu_width, 180);

  const quader::QtViewportChromeLayout narrow =
      quader::qt_viewport_chrome_layout(40, kMenuWidth, kInset);
  EXPECT_EQ(narrow.menu_width, 28);
  EXPECT_EQ(narrow.toolbar_width, 28);
  EXPECT_EQ(narrow.toolbar_x, kInset);
}

TEST(QtViewportChromeLayoutTests,
     AcceptsAnExplicitScaledMenuMinimumWithoutScalingPaneCoordinates) {
  const quader::QtUiScaleMetrics metrics{2.0F, 1U};
  const int inset = metrics.px(6);
  const int minimum_width =
      metrics.px(quader::kQtViewportDisplayMenuMinimumWidth);
  const quader::QtViewportChromeLayout layout =
      quader::qt_viewport_chrome_layout(800, 220, inset, minimum_width);

  EXPECT_EQ(layout.menu_width, 296);
  EXPECT_EQ(layout.toolbar_width, 296);
  EXPECT_EQ(layout.toolbar_x, 800 - inset - 296);
}

TEST(QtViewportChromeLayoutTests,
     PlacesEnvironmentImmediatelyLeftOfTheDisplayMenuWithRightInset) {
  const quader::QtViewportChromeLayout layout =
      quader::qt_viewport_chrome_layout(800, 180, 6, 148, 120, 4);

  EXPECT_EQ(layout.menu_width, 180);
  EXPECT_EQ(layout.environment_width, 120);
  EXPECT_EQ(layout.menu_spacing, 4);
  EXPECT_EQ(layout.toolbar_width, 304);
  EXPECT_EQ(layout.toolbar_x, 800 - 6 - 304);
}

TEST(QtViewportChromeLayoutTests,
     ShrinksBothMenusAndTheirSpacingWithoutOverflowInNarrowPanes) {
  for (int pane_width = 1; pane_width <= 400; ++pane_width) {
    const quader::QtViewportChromeLayout layout =
        quader::qt_viewport_chrome_layout(pane_width, 180, 6, 148, 120, 4);
    EXPECT_GE(layout.toolbar_x, 0) << pane_width;
    EXPECT_GT(layout.menu_width, 0) << pane_width;
    EXPECT_GE(layout.environment_width, 0) << pane_width;
    EXPECT_GE(layout.menu_spacing, 0) << pane_width;
    EXPECT_LE(layout.menu_spacing, 4) << pane_width;
    EXPECT_EQ(layout.toolbar_width,
              layout.menu_width + layout.environment_width + layout.menu_spacing)
        << pane_width;
    EXPECT_LE(layout.toolbar_x + layout.toolbar_width, pane_width) << pane_width;
    if (pane_width >= 14) {
      EXPECT_GT(layout.environment_width, 0) << pane_width;
      EXPECT_EQ(layout.toolbar_x + layout.toolbar_width, pane_width - 6)
          << pane_width;
    }
  }

  const auto narrow =
      quader::qt_viewport_chrome_layout(40, 180, 6, 148, 120, 4);
  EXPECT_EQ(narrow.toolbar_width, 28);
  EXPECT_EQ(narrow.toolbar_x, 6);
  EXPECT_EQ(narrow.menu_spacing, 4);
  EXPECT_EQ(narrow.environment_width, 9);
  EXPECT_EQ(narrow.menu_width, 15);
}

TEST(QtViewportChromeLayoutTests,
     ZeroWidthPanesHideBothMenusAndZeroEnvironmentWidthKeepsSingleMenuLayout) {
  const auto empty =
      quader::qt_viewport_chrome_layout(0, 180, 6, 148, 120, 4);
  EXPECT_EQ(empty.toolbar_x, 0);
  EXPECT_EQ(empty.toolbar_width, 0);
  EXPECT_EQ(empty.menu_width, 0);
  EXPECT_EQ(empty.environment_width, 0);
  EXPECT_EQ(empty.menu_spacing, 0);

  for (int pane_width : {0, 1, 14, 40, 400}) {
    const auto original =
        quader::qt_viewport_chrome_layout(pane_width, 180, 6);
    const auto explicit_single =
        quader::qt_viewport_chrome_layout(pane_width, 180, 6, 148, 0, 4);
    EXPECT_EQ(explicit_single.toolbar_x, original.toolbar_x);
    EXPECT_EQ(explicit_single.toolbar_width, original.toolbar_width);
    EXPECT_EQ(explicit_single.menu_width, original.menu_width);
    EXPECT_EQ(explicit_single.environment_width, 0);
    EXPECT_EQ(explicit_single.menu_spacing, 0);
  }
}

TEST(QtViewportChromeLayoutTests,
     EveryPaneReservesHeaderAndIndependentCameraAndShadingMenus) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-pane-header-layout";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(QStringLiteral("Fusion"));
  const QPalette native_palette = application.palette();
  quader::apply_qt_obsidian_application_theme(native_palette);
  quader::QtUiScaleApplicationController scale_controller(application);
  quader::ViewportWorkspaceService workspace;
  ASSERT_TRUE(workspace.set_layout(quader::ViewportLayoutId::FourQuad));
  quader::QtViewportPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.attach_workspace(workspace);
  panel.set_startup_presentation_blocked(false);
  std::vector<QAction *> actions;
  constexpr std::array names{"tool_select", "tool_move", "tool_rotate", "tool_scale",
                             "toggle_transform_space", "backfaceSelection",
                             "snappingEnabled", "gridSnapping", "vertexSnapping",
                             "rotationSnapping", "viewportGrid", "viewportGizmos",
                             "viewportMeshGrid", "viewportOverlays"};
  constexpr std::array icons{"tool_select.png", "tool_move.png", "tool_rotate.png",
                             "tool_scale.png", "tool_transform_local.png",
                             "selection_backface.png", "snapping_master.png",
                             "snapping_grid.png", "snapping_vertex.png",
                             "snapping_rotation.png", "viewport_grid.png",
                             "viewport_gizmos.png", "viewport_mesh_grid.png",
                             "viewport_overlays.png"};
  for (std::size_t index = 0; index < names.size(); ++index) {
    const char *name = names[index];
    auto *action = new QAction(QString::fromUtf8(name), &panel);
    action->setObjectName(QString::fromUtf8(name));
    action->setCheckable(true);
    action->setIcon(QIcon(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons/") +
                          QString::fromUtf8(icons[index])));
    actions.push_back(action);
  }
  QAction lasso(QStringLiteral("Lasso Select"), &panel);
  lasso.setObjectName(QStringLiteral("tool_lasso_select"));
  lasso.setCheckable(true);
  lasso.setIcon(QIcon(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons/tool_lasso.png")));
  panel.set_control_actions(actions, &lasso);
  panel.resize(1000, 700);
  panel.show();
  panel.refresh_workspace_layout();
  const auto popups = panel.findChildren<QWidget *>(QStringLiteral("environmentSettings"));
  ASSERT_EQ(popups.size(), 1);
  EXPECT_EQ(popups.front()->parentWidget(), &panel);
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(review_directory));
  const auto review = [&](const QString &suffix) {
    for (int pass = 0; pass < 4; ++pass) {
      panel.refresh_workspace_layout();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      application.processEvents();
    }
    const auto &metrics = scale_controller.metrics();
    for (const char *pane : {"perspective", "top", "front", "side"}) {
      SCOPED_TRACE(pane);
      const QString id = QString::fromUtf8(pane);
      auto *header = panel.findChild<QWidget *>(QStringLiteral("viewportToolbar_") + id);
      auto *surface = panel.findChild<QWidget *>(QStringLiteral("viewportSurfaceContainer_") + id);
      auto *camera = panel.findChild<QPushButton *>(QStringLiteral("viewportCamera_") + id);
      auto *display = panel.findChild<QPushButton *>(QStringLiteral("viewportShading_") + id);
      auto *environment = panel.findChild<QPushButton *>(QStringLiteral("viewportEnvironment_") + id);
      EXPECT_EQ(panel.findChild<QWidget *>(
                    QStringLiteral("viewportNavigationGizmo_") + id), nullptr);
      ASSERT_NE(header, nullptr);
      ASSERT_NE(surface, nullptr);
      ASSERT_NE(camera, nullptr);
      ASSERT_NE(display, nullptr);
      ASSERT_NE(environment, nullptr);
      auto *selection = header->findChild<QToolButton *>(QStringLiteral("viewportStrip_tool_select"));
      ASSERT_NE(selection, nullptr);
      ASSERT_NE(selection->defaultAction()->menu(), nullptr);
      EXPECT_EQ(selection->popupMode(), QToolButton::DelayedPopup);
      EXPECT_EQ(selection->defaultAction()->menu()->actions().size(), 2);
      auto *local = header->findChild<QToolButton *>(QStringLiteral("viewportStrip_toggle_transform_space"));
      auto *backface = header->findChild<QToolButton *>(QStringLiteral("viewportStrip_backfaceSelection"));
      ASSERT_NE(local, nullptr);
      ASSERT_NE(backface, nullptr);
      EXPECT_EQ(header->layout()->indexOf(backface), header->layout()->indexOf(local) + 1);
      EXPECT_EQ(header->geometry(), QRect(0, 0, header->parentWidget()->width(), metrics.px(36)));
      EXPECT_EQ(surface->geometry(), QRect(0, header->height(), header->width(),
          header->parentWidget()->height() - header->height()));
      EXPECT_EQ(camera->parentWidget(), header);
      EXPECT_EQ(display->parentWidget(), header);
      EXPECT_TRUE(camera->isVisible());
      EXPECT_TRUE(display->isVisible());
      EXPECT_TRUE(header->rect().contains(camera->geometry()));
      EXPECT_TRUE(header->rect().contains(display->geometry()));
      EXPECT_FALSE(camera->geometry().intersects(display->geometry()));
      EXPECT_GE(camera->width(), camera->minimumSizeHint().width());
      EXPECT_GE(display->width(), display->minimumSizeHint().width());
      ASSERT_NE(camera->menu(), nullptr);
      ASSERT_NE(display->menu(), nullptr);
      EXPECT_NE(camera->menu(), display->menu());
      EXPECT_EQ(camera->menu()->actions().size(), 7);
      EXPECT_LT(camera->font().pointSizeF(), camera->menu()->font().pointSizeF());
      for (const auto *action : display->menu()->actions()) {
        if (action->menu() != nullptr) EXPECT_NE(action->menu()->title(), QStringLiteral("Camera"));
      }
      if (panel.layoutDirection() == Qt::LeftToRight) {
        EXPECT_LT(camera->geometry().right(), display->x());
      } else {
        EXPECT_LT(display->geometry().right(), camera->x());
      }
      if (!environment->isHidden()) {
        EXPECT_TRUE(header->rect().contains(environment->geometry()));
        EXPECT_FALSE(environment->geometry().intersects(camera->geometry()));
      } else {
        auto *overflow = header->findChild<QMenu *>(QStringLiteral("viewportStripOverflowMenu"));
        ASSERT_NE(overflow, nullptr);
        const auto overflow_actions = overflow->actions();
        EXPECT_TRUE(std::any_of(overflow_actions.cbegin(), overflow_actions.cend(),
            [](const QAction *action) { return action->text() == QStringLiteral("Environment"); }));
      }
    }
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(panel.grab().save(QDir(review_directory).filePath(
          QStringLiteral("viewport-pane-headers-%1.png").arg(suffix))));
    }
    auto *selection_menu = panel.findChild<QMenu *>(
        QStringLiteral("viewportSelectionToolMenu_perspective"));
    ASSERT_NE(selection_menu, nullptr);
    selection_menu->popup(panel.mapToGlobal(QPoint(metrics.px(8), metrics.px(36))));
    application.processEvents();
    ASSERT_TRUE(selection_menu->isVisible());
    ASSERT_EQ(selection_menu->actions().size(), 2);
    const QRect box_row = selection_menu->actionGeometry(selection_menu->actions()[0]);
    const QRect lasso_row = selection_menu->actionGeometry(selection_menu->actions()[1]);
    EXPECT_TRUE(selection_menu->rect().contains(box_row));
    EXPECT_TRUE(selection_menu->rect().contains(lasso_row));
    EXPECT_FALSE(box_row.intersects(lasso_row));
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(selection_menu->grab().save(QDir(review_directory).filePath(
          QStringLiteral("viewport-pane-selection-%1.png").arg(suffix))));
    }
    selection_menu->close();
  };
  int cycle = 0;
  QSize base_minimum;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    scale_controller.apply_factor(factor);
    panel.apply_ui_scale(scale_controller.metrics());
    panel.resize(scale_controller.metrics().px(1100), scale_controller.metrics().px(700));
    review(QStringLiteral("%1-%2-wide").arg(qRound(factor * 100)).arg(cycle));
    panel.resize(scale_controller.metrics().px(560), scale_controller.metrics().px(500));
    review(QStringLiteral("%1-%2-narrow").arg(qRound(factor * 100)).arg(cycle));
    panel.resize(300, 200);
    review(QStringLiteral("%1-%2-minimum").arg(qRound(factor * 100)).arg(cycle));
    const QSize minimum = panel.minimumSize();
    EXPECT_GE(panel.width(), minimum.width());
    EXPECT_GE(panel.height(), minimum.height());
    if (cycle == 0) {
      base_minimum = minimum;
    } else if (cycle == 1) {
      EXPECT_GT(minimum.width(), base_minimum.width());
    } else {
      EXPECT_EQ(minimum, base_minimum);
    }
    for (const float split : {0.15F, 0.85F}) {
      workspace.set_split_handle_position(quader::ViewportSplitHandleId::FourVertical, split);
      workspace.set_split_handle_position(quader::ViewportSplitHandleId::FourHorizontal, split);
      review(QStringLiteral("%1-%2-split-%3").arg(qRound(factor * 100)).arg(cycle).arg(qRound(split * 100)));
      EXPECT_EQ(panel.minimumSize(), minimum)
          << "split ratios cannot inflate the workspace minimum";
    }
    workspace.set_split_handle_position(quader::ViewportSplitHandleId::FourVertical, 0.5F);
    workspace.set_split_handle_position(quader::ViewportSplitHandleId::FourHorizontal, 0.5F);
    ++cycle;
  }
  panel.setLayoutDirection(Qt::RightToLeft);
  review(QStringLiteral("rtl"));
  panel.setLayoutDirection(Qt::LeftToRight);
  quader::apply_qt_application_theme_for_contrast_preference(native_palette, Qt::ContrastPreference::HighContrast);
  review(QStringLiteral("native-high-contrast"));

  auto *top_camera = panel.findChild<QPushButton *>(QStringLiteral("viewportCamera_top"));
  auto *top_shading = panel.findChild<QPushButton *>(QStringLiteral("viewportShading_top"));
  ASSERT_NE(top_camera, nullptr);
  ASSERT_NE(top_shading, nullptr);
  const auto perspective_view = workspace.view(ViewportPaneId::Perspective);
  const auto perspective_shading = workspace.shading_mode(ViewportPaneId::Perspective);
  QAction *front = nullptr;
  for (auto *action : top_camera->menu()->actions()) {
    if (action->text() == QStringLiteral("Front")) front = action;
  }
  ASSERT_NE(front, nullptr);
  front->trigger();
  EXPECT_EQ(workspace.focused_pane(), ViewportPaneId::Top);
  EXPECT_EQ(workspace.view(ViewportPaneId::Top), quader::ViewportView::Front);
  EXPECT_EQ(workspace.view(ViewportPaneId::Perspective), perspective_view);
  QAction *solid = nullptr;
  for (auto *action : top_shading->menu()->actions()) {
    if (action->text() == QStringLiteral("Solid")) solid = action;
  }
  ASSERT_NE(solid, nullptr);
  solid->trigger();
  EXPECT_EQ(workspace.shading_mode(ViewportPaneId::Top), quader::ViewportShadingMode::Solid);
  EXPECT_EQ(workspace.shading_mode(ViewportPaneId::Perspective), perspective_shading);
  EXPECT_EQ(top_camera->text(), QStringLiteral("Front"));
  EXPECT_EQ(top_shading->text(), QStringLiteral("Solid"));
}

