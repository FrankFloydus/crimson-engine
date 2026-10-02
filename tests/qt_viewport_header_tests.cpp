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
#include "ui/qt_application_style.hpp"
#include "ui/qt_viewport_control_strip.hpp"
#include "ui/qt_viewport_chrome_layout.hpp"
#include "ui/qt_viewport_panel.hpp"

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QFocusEvent>
#include <QImage>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QStyleOption>
#include <QLayout>
#include <QPushButton>
#include <QWindow>

#include <algorithm>
#include <array>
#include <vector>

using quader::ViewportPaneId;

namespace {

void click_native_widget(QWidget &widget) {
  QWidget *native_host = widget.nativeParentWidget();
  ASSERT_NE(native_host, nullptr);
  ASSERT_NE(native_host->windowHandle(), nullptr);
  const QPointF global = widget.mapToGlobal(widget.rect().center());
  const QPointF position = native_host->mapFromGlobal(global);
  QMouseEvent press(QEvent::MouseButtonPress, position, global,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(native_host->windowHandle(), &press);
  QMouseEvent release(QEvent::MouseButtonRelease, position, global,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(native_host->windowHandle(), &release);
  QCoreApplication::processEvents();
}

void send_native_key(QWidget &widget, int key) {
  QWidget *native_host = widget.window();
  ASSERT_NE(native_host->windowHandle(), nullptr);
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  QApplication::sendEvent(native_host->windowHandle(), &press);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QApplication::sendEvent(native_host->windowHandle(), &release);
  QCoreApplication::processEvents();
}

} // namespace

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
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  auto &workspace = session.viewport_workspace();
  ASSERT_TRUE(workspace.set_layout(quader::ViewportLayoutId::FourQuad));
  quader::QtViewportPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.attach_workspace(workspace);
  panel.set_startup_presentation_blocked(false);
  std::vector<QAction *> actions;
  constexpr std::array kNames{"tool_select", "tool_move", "tool_rotate", "tool_scale",
                             "toggle_transform_space", "backfaceSelection",
                             "snappingEnabled", "gridSnapping", "vertexSnapping",
                             "rotationSnapping", "viewportGrid", "viewportGizmos",
                             "viewportMeshGrid", "viewportOverlays"};
  constexpr std::array kIcons{"tool_select.png", "tool_move.png", "tool_rotate.png",
                             "tool_scale.png", "tool_transform_local.png",
                             "selection_backface.png", "snapping_master.png",
                             "snapping_grid.png", "snapping_vertex.png",
                             "snapping_rotation.png", "viewport_grid.png",
                             "viewport_gizmos.png", "viewport_mesh_grid.png",
                             "viewport_overlays.png"};
  for (std::size_t index = 0; index < kNames.size(); ++index) {
    const char *name = kNames[index];
    auto *action = new QAction(QString::fromUtf8(name), &panel);
    action->setObjectName(QString::fromUtf8(name));
    action->setCheckable(true);
    action->setIcon(QIcon(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons/") +
                          QString::fromUtf8(kIcons[index])));
    actions.push_back(action);
  }
  QAction lasso(QStringLiteral("Lasso Select"), &panel);
  lasso.setObjectName(QStringLiteral("tool_lasso_select"));
  lasso.setCheckable(true);
  lasso.setIcon(QIcon(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons/tool_lasso.png")));
  panel.set_control_actions(actions, &lasso);
  panel.refresh_control_actions(editor);
  panel.resize(1000, 700);
  panel.show();
  panel.refresh_workspace_layout();
  const auto popups = panel.findChildren<QWidget *>(QStringLiteral("environmentSettings"));
  ASSERT_EQ(popups.size(), 1);
  EXPECT_EQ(popups.front()->parentWidget(), &panel);
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(review_directory));
  const auto review = [&](const QString &suffix) {
    for (QToolButton *button : panel.findChildren<QToolButton *>()) {
      if (button->defaultAction() == nullptr) {
        continue;
      }
      button->defaultAction()->setToolTip(suffix);
      QEvent font_change(QEvent::FontChange);
      QCoreApplication::sendEvent(button, &font_change);
      QEvent style_change(QEvent::StyleChange);
      QCoreApplication::sendEvent(button, &style_change);
    }
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
      auto *layout = panel.findChild<QToolButton *>(QStringLiteral("viewportLayout_") + id);
      auto *camera_settings = panel.findChild<QToolButton *>(QStringLiteral("viewportCameraSettings_") + id);
      auto *environment = panel.findChild<QPushButton *>(QStringLiteral("viewportEnvironment_") + id);
      EXPECT_EQ(panel.findChild<QWidget *>(
                    QStringLiteral("viewportNavigationGizmo_") + id), nullptr);
      ASSERT_NE(header, nullptr);
      ASSERT_NE(surface, nullptr);
      ASSERT_NE(camera, nullptr);
      ASSERT_NE(display, nullptr);
      ASSERT_NE(layout, nullptr);
      ASSERT_NE(camera_settings, nullptr);
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
      for (QToolButton *button : header->findChildren<QToolButton *>(
               QString(), Qt::FindDirectChildrenOnly)) {
        EXPECT_EQ(button->minimumSize(), metrics.size(QSize(28, 28)));
        EXPECT_EQ(button->maximumSize(), metrics.size(QSize(28, 28)));
        if (!button->isVisible()) {
          continue;
        }
        EXPECT_EQ(button->size(), metrics.size(QSize(28, 28)));
        EXPECT_EQ(button->geometry().top(), metrics.px(4));
        EXPECT_EQ(header->height() - button->geometry().bottom() - 1,
                  metrics.px(4));
        EXPECT_TRUE(header->rect().contains(button->geometry()));
      }
      for (auto *button : {layout, camera_settings}) {
        EXPECT_EQ(button->parentWidget(), header);
        EXPECT_EQ(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
        EXPECT_TRUE(button->isVisible());
        EXPECT_TRUE(header->rect().contains(button->geometry()));
        EXPECT_GE(button->width(), button->minimumSizeHint().width());
        EXPECT_GE(button->height(), button->minimumSizeHint().height());
        EXPECT_FALSE(button->geometry().intersects(camera->geometry()));
        EXPECT_FALSE(button->geometry().intersects(display->geometry()));
      }
      EXPECT_FALSE(layout->geometry().intersects(camera_settings->geometry()));
      EXPECT_EQ(layout->popupMode(), QToolButton::InstantPopup);
      auto *layout_menu = panel.findChild<QMenu *>(QStringLiteral("viewportLayoutMenu_") + id);
      ASSERT_NE(layout_menu, nullptr);
      EXPECT_EQ(layout_menu->actions().size(), 8);
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
        EXPECT_LT(display->geometry().right(), layout->x());
        EXPECT_LT(layout->geometry().right(), camera_settings->x());
      } else {
        EXPECT_LT(display->geometry().right(), camera->x());
        EXPECT_LT(layout->geometry().right(), display->x());
        EXPECT_LT(camera_settings->geometry().right(), layout->x());
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

    auto *layout_menu = panel.findChild<QMenu *>(
        QStringLiteral("viewportLayoutMenu_perspective"));
    ASSERT_NE(layout_menu, nullptr);
    layout_menu->popup(panel.mapToGlobal(QPoint(metrics.px(8), metrics.px(36))));
    application.processEvents();
    ASSERT_TRUE(layout_menu->isVisible());
    for (auto *action : layout_menu->actions()) {
      EXPECT_TRUE(layout_menu->rect().contains(layout_menu->actionGeometry(action)));
    }
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(layout_menu->grab().save(QDir(review_directory).filePath(
          QStringLiteral("viewport-pane-layout-%1.png").arg(suffix))));
    }
    layout_menu->close();

    auto *camera_settings = panel.findChild<QToolButton *>(
        QStringLiteral("viewportCameraSettings_perspective"));
    ASSERT_NE(camera_settings, nullptr);
    click_native_widget(*camera_settings);
    auto *settings_popup = panel.findChild<QWidget *>(
        QStringLiteral("viewportCameraSettingsPopup_perspective"));
    ASSERT_NE(settings_popup, nullptr);
    ASSERT_TRUE(settings_popup->isVisible());
    auto *slider = settings_popup->findChild<QSlider *>(
        QStringLiteral("viewportFlySpeed_perspective"));
    ASSERT_NE(slider, nullptr);
    EXPECT_TRUE(settings_popup->rect().contains(slider->geometry()));
    EXPECT_GE(slider->width(), slider->minimumSizeHint().width());
    for (auto *label : settings_popup->findChildren<QLabel *>()) {
      EXPECT_TRUE(settings_popup->rect().contains(label->geometry()));
      EXPECT_GE(label->width(), label->minimumSizeHint().width());
    }
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(settings_popup->grab().save(QDir(review_directory).filePath(
          QStringLiteral("viewport-pane-camera-settings-%1.png").arg(suffix))));
    }
    send_native_key(*settings_popup, Qt::Key_Escape);
    EXPECT_FALSE(settings_popup->isVisible());
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

  QMenu *debug_menu = nullptr;
  for (auto *action : top_shading->menu()->actions()) {
    if (action->menu() != nullptr &&
        action->menu()->title() == QStringLiteral("Debug View")) {
      debug_menu = action->menu();
    }
  }
  ASSERT_NE(debug_menu, nullptr);
  QAction *uv = nullptr;
  for (auto *action : debug_menu->actions()) {
    if (action->text() == QStringLiteral("UV")) {
      uv = action;
    }
  }
  ASSERT_NE(uv, nullptr);
  uv->trigger();
  EXPECT_EQ(workspace.shading_mode(ViewportPaneId::Top),
            quader::ViewportShadingMode::Uv);
  EXPECT_EQ(workspace.shading_mode(ViewportPaneId::Perspective),
            perspective_shading);
  EXPECT_TRUE(uv->isChecked());
  EXPECT_FALSE(solid->isChecked());
  EXPECT_EQ(top_shading->text(), QStringLiteral("UV"));
}

TEST(QtViewportChromeLayoutTests,
     NativeLayoutAndCameraSettingsKeepSharedLayoutAndIndependentFlySpeed) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-pane-header-settings";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  auto &workspace = session.viewport_workspace();
  ASSERT_TRUE(editor.viewports().set_layout(quader::ViewportLayoutId::FourQuad).ok());
  ASSERT_TRUE(editor.viewports().set_fly_navigation_speed(ViewportPaneId::Perspective, 42.5).ok());
  ASSERT_TRUE(editor.viewports().set_fly_navigation_speed(ViewportPaneId::Top, 12.25).ok());
  const auto content_revision = session.scene().revision();
  const auto undo_count = session.history().undo_count();
  const auto redo_count = session.history().redo_count();
  const auto default_speed = workspace.default_fly_speed();
  const auto settings_origin = editor.tools().active_settings_presentation_origin();

  QWidget host;
  host.setAttribute(Qt::WA_DontShowOnScreen);
  host.resize(1250, 780);
  quader::QtViewportPanel panel(&host);
  panel.setGeometry(0, 0, 1000, 740);
  panel.attach_workspace(workspace);
  panel.set_startup_presentation_blocked(false);
  panel.refresh_control_actions(editor);
  QPushButton outside(QStringLiteral("Outside viewport"), &host);
  outside.setGeometry(1030, 730, 190, 30);
  host.show();
  QApplication::setActiveWindow(&host);
  application.processEvents();
  const auto process_input = [&]() {
    panel.process_input(editor, session.scene(), session.assets(),
                        session.modeling(), {}, session.native_tools(),
                        &session.point_placement(), 1.0 / 60.0);
  };
  const auto expect_content_unchanged = [&]() {
    EXPECT_EQ(session.scene().revision(), content_revision);
    EXPECT_EQ(session.history().undo_count(), undo_count);
    EXPECT_EQ(session.history().redo_count(), redo_count);
    EXPECT_EQ(editor.tools().active_settings_presentation_origin(), settings_origin);
    EXPECT_FLOAT_EQ(workspace.default_fly_speed(), default_speed);
    EXPECT_FLOAT_EQ(workspace.camera(ViewportPaneId::Perspective).default_fly_speed(), default_speed);
    EXPECT_FLOAT_EQ(workspace.camera(ViewportPaneId::Top).default_fly_speed(), default_speed);
  };
  auto *layout_button = panel.findChild<QToolButton *>(
      QStringLiteral("viewportLayout_perspective"));
  auto *layout_menu = panel.findChild<QMenu *>(
      QStringLiteral("viewportLayoutMenu_perspective"));
  ASSERT_NE(layout_button, nullptr);
  ASSERT_NE(layout_menu, nullptr);
  ASSERT_EQ(layout_button->popupMode(), QToolButton::InstantPopup);

  auto *single_layout = layout_menu->findChild<QAction *>(QStringLiteral("layout_single"));
  ASSERT_NE(single_layout, nullptr);
  const auto expect_shortcut_on_open = [&](const QString &shortcut) {
    const auto descriptor = editor.commands().descriptor(
        quader::quader_command::kViewportLayoutSingle);
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(QString::fromStdString(descriptor->shortcut), shortcut);
    const auto layout_before = editor.viewports().layout();
    bool observed_menu = false;
    QTimer::singleShot(0, [&]() {
      observed_menu = layout_menu->isVisible() &&
          QApplication::activePopupWidget() == layout_menu;
      if (observed_menu) {
        const QString suffix = shortcut.isEmpty() ? QString() : QStringLiteral("\t") + shortcut;
        EXPECT_EQ(single_layout->text(), QStringLiteral("Single View") + suffix);
        EXPECT_EQ(single_layout->isEnabled(), descriptor->enabled);
        EXPECT_TRUE(single_layout->shortcut().isEmpty());
        send_native_key(*layout_menu, Qt::Key_Escape);
      }
      layout_menu->close();
    });
    click_native_widget(*layout_button);
    EXPECT_TRUE(observed_menu);
    EXPECT_EQ(editor.viewports().layout(), layout_before);
  };
  expect_shortcut_on_open(QStringLiteral("Ctrl+1"));
  ASSERT_TRUE(editor.preferences().set_shortcut(
      quader::quader_command::kViewportLayoutSingle, "Ctrl+Alt+F23").ok());
  expect_shortcut_on_open(QStringLiteral("Ctrl+Alt+F23"));
  ASSERT_TRUE(editor.preferences().remove_shortcut(
      quader::quader_command::kViewportLayoutSingle).ok());
  expect_shortcut_on_open(QString());
  ASSERT_TRUE(editor.preferences().reset_shortcut(
      quader::quader_command::kViewportLayoutSingle).ok());

  for (const auto &option : quader::viewport_layout_options()) {
    const QString action_name = QStringLiteral("layout_") +
        QString::fromUtf8(quader::viewport_layout_stable_id(option.id).data());
    auto *choice = layout_menu->findChild<QAction *>(action_name);
    ASSERT_NE(choice, nullptr);
    bool observed_menu = false;
    QTimer::singleShot(0, [&]() {
      observed_menu = layout_menu->isVisible() &&
          QApplication::activePopupWidget() == layout_menu;
      if (observed_menu) {
        layout_menu->setActiveAction(choice);
        send_native_key(*layout_menu, Qt::Key_Return);
      }
      // Keep failures bounded if native activation does not dismiss the menu.
      layout_menu->close();
    });
    click_native_widget(*layout_button);
    ASSERT_TRUE(observed_menu);
    EXPECT_EQ(editor.viewports().layout(), option.id);
    panel.refresh_workspace_layout();
    for (const char *pane : {"perspective", "top", "front", "side"}) {
      auto *menu = panel.findChild<QMenu *>(
          QStringLiteral("viewportLayoutMenu_") + QString::fromUtf8(pane));
      ASSERT_NE(menu, nullptr);
      ASSERT_EQ(menu->actions().size(), 8);
      for (auto *action : menu->actions()) {
        EXPECT_EQ(action->isChecked(), action->objectName() == action_name)
            << pane << ": " << action->objectName().toStdString();
      }
    }
    EXPECT_DOUBLE_EQ(editor.viewports().fly_navigation_speed(ViewportPaneId::Perspective), 42.5);
    EXPECT_DOUBLE_EQ(editor.viewports().fly_navigation_speed(ViewportPaneId::Top), 12.25);
    expect_content_unchanged();
  }
  ASSERT_EQ(editor.viewports().layout(), quader::ViewportLayoutId::FourQuad);
  auto *camera_settings = panel.findChild<QToolButton *>(
      QStringLiteral("viewportCameraSettings_top"));
  auto *popup = panel.findChild<QWidget *>(
      QStringLiteral("viewportCameraSettingsPopup_top"));
  auto *container = panel.findChild<QWidget *>(
      QStringLiteral("viewportSurfaceContainer_top"));
  auto *border = panel.findChild<QWidget *>(
      QStringLiteral("viewportFocusedPaneFrame"));
  ASSERT_NE(camera_settings, nullptr);
  ASSERT_NE(popup, nullptr);
  ASSERT_NE(container, nullptr);
  ASSERT_NE(border, nullptr);
  auto *slider = popup->findChild<QSlider *>(QStringLiteral("viewportFlySpeed_top"));
  auto *value = popup->findChild<QLabel *>(QStringLiteral("viewportFlySpeedValue_top"));
  ASSERT_NE(slider, nullptr);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(popup->windowType(), Qt::Popup);
  EXPECT_EQ(slider->minimum(), 1);
  EXPECT_EQ(slider->maximum(), 1000);
  EXPECT_EQ(slider->singleStep(), 1);
  EXPECT_EQ(slider->pageStep(), 10);
  EXPECT_EQ(slider->accessibleName(), QStringLiteral("Fly Speed"));
  container->show();
  QFocusEvent surface_focus(QEvent::FocusIn, Qt::OtherFocusReason);
  QApplication::sendEvent(panel.surface(ViewportPaneId::Top), &surface_focus);

  click_native_widget(*camera_settings);
  ASSERT_TRUE(popup->isVisible());
  EXPECT_EQ(QApplication::activePopupWidget(), popup);
  EXPECT_TRUE(slider->hasFocus());
  EXPECT_EQ(slider->value(), 12);
  EXPECT_EQ(value->text(), popup->locale().toString(12.25, 'f', 2));
  EXPECT_DOUBLE_EQ(editor.viewports().fly_navigation_speed(ViewportPaneId::Top), 12.25);
  process_input();
  EXPECT_EQ(workspace.focused_pane(), ViewportPaneId::Top);
  EXPECT_TRUE(panel.operation_input_context().viewport_available);
  EXPECT_FALSE(border->isHidden());
  send_native_key(*popup, Qt::Key_Escape);
  EXPECT_FALSE(popup->isVisible());
  EXPECT_DOUBLE_EQ(editor.viewports().fly_navigation_speed(ViewportPaneId::Top), 12.25);
  expect_content_unchanged();

  click_native_widget(*camera_settings);
  ASSERT_TRUE(popup->isVisible());
  ASSERT_FALSE(popup->frameGeometry().contains(outside.mapToGlobal(outside.rect().center())));
  click_native_widget(outside);
  EXPECT_FALSE(popup->isVisible());
  EXPECT_DOUBLE_EQ(editor.viewports().fly_navigation_speed(ViewportPaneId::Top), 12.25);
  expect_content_unchanged();

  click_native_widget(*camera_settings);
  ASSERT_TRUE(popup->isVisible());
  send_native_key(*popup, Qt::Key_Right);
  EXPECT_EQ(slider->value(), 13);
  EXPECT_DOUBLE_EQ(editor.viewports().fly_navigation_speed(ViewportPaneId::Top), 13.0);
  EXPECT_DOUBLE_EQ(editor.viewports().fly_navigation_speed(ViewportPaneId::Perspective), 42.5);
  send_native_key(*popup, Qt::Key_End);
  EXPECT_DOUBLE_EQ(editor.viewports().fly_navigation_speed(ViewportPaneId::Top), 1000.0);
  send_native_key(*popup, Qt::Key_Home);
  EXPECT_DOUBLE_EQ(editor.viewports().fly_navigation_speed(ViewportPaneId::Top), 1.0);
  expect_content_unchanged();
  panel.abandon_interactions();
  EXPECT_FALSE(popup->isVisible());
  slider->setValue(80);
  EXPECT_DOUBLE_EQ(editor.viewports().fly_navigation_speed(ViewportPaneId::Top), 1.0);

  quader::SceneSession replacement;
  quader::QuaderEditorApi replacement_editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(replacement));
  ASSERT_TRUE(replacement_editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(replacement_editor.viewports().set_layout(quader::ViewportLayoutId::FourQuad).ok());
  ASSERT_TRUE(replacement_editor.viewports().set_fly_navigation_speed(ViewportPaneId::Top, 66.5).ok());
  panel.attach_workspace(workspace);
  panel.refresh_control_actions(editor);
  click_native_widget(*camera_settings);
  ASSERT_TRUE(popup->isVisible());
  panel.attach_workspace(replacement.viewport_workspace());
  EXPECT_FALSE(popup->isVisible());
  slider->setValue(90);
  EXPECT_DOUBLE_EQ(editor.viewports().fly_navigation_speed(ViewportPaneId::Top), 1.0);
  EXPECT_DOUBLE_EQ(replacement_editor.viewports().fly_navigation_speed(ViewportPaneId::Top), 66.5);
  panel.refresh_control_actions(replacement_editor);
  panel.refresh_workspace_layout();
  application.processEvents();
  click_native_widget(*camera_settings);
  ASSERT_TRUE(popup->isVisible());
  EXPECT_EQ(slider->value(), 67);
  EXPECT_DOUBLE_EQ(replacement_editor.viewports().fly_navigation_speed(ViewportPaneId::Top), 66.5);
  panel.abandon_interactions();
  EXPECT_FALSE(popup->isVisible());
  EXPECT_DOUBLE_EQ(editor.viewports().fly_navigation_speed(ViewportPaneId::Top), 1.0);
  expect_content_unchanged();
}

