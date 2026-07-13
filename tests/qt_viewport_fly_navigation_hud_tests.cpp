////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "core/localization_service.hpp"
#include "ui/qt_viewport_fly_navigation_hud.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QPoint>
#include <QRegion>
#include <QString>
#include <QWidget>

#include <filesystem>
#include <string>

namespace {

std::filesystem::path source_root() {
#ifdef QUADER_SOURCE_DIR
  return std::filesystem::path(QUADER_SOURCE_DIR);
#else
  return std::filesystem::current_path();
#endif
}

} // namespace

TEST(QtViewportFlyNavigationHudTests, InactiveNavigationHidesEveryHudElement) {
  const quader::QtViewportFlyNavigationHudPresentation presentation =
      quader::qt_viewport_fly_navigation_hud_presentation(
          false, false, 8.0f, nullptr);

  EXPECT_FALSE(presentation.crosshair_visible);
  EXPECT_FALSE(presentation.speed_visible);
  EXPECT_TRUE(presentation.speed_label.empty());
}

TEST(QtViewportFlyNavigationHudTests,
     DefaultSpeedKeepsCrosshairWithoutRedundantResetHint) {
  const quader::QtViewportFlyNavigationHudPresentation presentation =
      quader::qt_viewport_fly_navigation_hud_presentation(
          true, true, 4.0f, nullptr);

  EXPECT_TRUE(presentation.crosshair_visible);
  EXPECT_FALSE(presentation.speed_visible);
  EXPECT_TRUE(presentation.speed_label.empty());
}

TEST(QtViewportFlyNavigationHudTests,
     ChangedSpeedUsesLocalizedMiddleMouseResetHint) {
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(source_root() / "resources" / "locales",
                                "en-US"));

  quader::QtViewportFlyNavigationHudPresentation presentation =
      quader::qt_viewport_fly_navigation_hud_presentation(
          true, false, 6.5f, &localization);
  EXPECT_TRUE(presentation.crosshair_visible);
  EXPECT_TRUE(presentation.speed_visible);
  EXPECT_NE(presentation.speed_label.find("Speed 6.5"), std::string::npos);
  EXPECT_NE(presentation.speed_label.find("Middle Mouse resets"),
            std::string::npos);
  EXPECT_EQ(presentation.speed_label.find("R resets"), std::string::npos);

  ASSERT_TRUE(localization.select_locale("it-IT"));
  presentation = quader::qt_viewport_fly_navigation_hud_presentation(
      true, false, 6.5f, &localization);
  EXPECT_NE(presentation.speed_label.find("Velocità 6.5"),
            std::string::npos);
  EXPECT_NE(presentation.speed_label.find("Mouse centrale ripristina"),
            std::string::npos);
}

TEST(QtViewportFlyNavigationHudTests,
     NativeCrosshairMaskExposesViewportBetweenOutlinedArms) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-fly-navigation-hud-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QWidget parent;
  parent.resize(200, 160);
  parent.setAttribute(Qt::WA_DontShowOnScreen, true);
  quader::QtViewportFlyNavigationHud hud(
      parent, quader::ViewportPaneId::Perspective);

  QWidget *crosshair = parent.findChild<QWidget *>(
      QStringLiteral("viewportFlyCrosshair_perspective"));
  ASSERT_NE(crosshair, nullptr);
  EXPECT_TRUE(crosshair->testAttribute(Qt::WA_NativeWindow));

  const QRegion mask = crosshair->mask();
  ASSERT_FALSE(mask.isEmpty());
  EXPECT_FALSE(mask.contains(QPoint{0, 0}));
  EXPECT_FALSE(mask.contains(QPoint{14, 14}));
  EXPECT_TRUE(mask.contains(QPoint{7, 14}));
  EXPECT_TRUE(mask.contains(QPoint{21, 14}));
  EXPECT_TRUE(mask.contains(QPoint{14, 7}));
  EXPECT_TRUE(mask.contains(QPoint{14, 21}));
  EXPECT_LT(mask.boundingRect().width(), crosshair->width());
  EXPECT_LT(mask.boundingRect().height(), crosshair->height());
}
