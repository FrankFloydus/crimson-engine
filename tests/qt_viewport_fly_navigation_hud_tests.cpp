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
#include <QEvent>
#include <QLabel>
#include <QObject>
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

/// Counts retained QWidget presentation events observed by the HUD tests.
struct HudWidgetEventCounts {
  int move = 0;
  int resize = 0;
  int show = 0;
  int hide = 0;
  int z_order = 0;

  friend bool operator==(const HudWidgetEventCounts &,
                         const HudWidgetEventCounts &) = default;
};

/// Records geometry, visibility, and stacking events for one HUD widget.
class HudWidgetEventCounter final : public QObject {
public:
  [[nodiscard]] HudWidgetEventCounts counts() const { return counts_; }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (watched != nullptr && event != nullptr) {
      switch (event->type()) {
      case QEvent::Move:
        ++counts_.move;
        break;
      case QEvent::Resize:
        ++counts_.resize;
        break;
      case QEvent::Show:
      case QEvent::ShowToParent:
        ++counts_.show;
        break;
      case QEvent::Hide:
      case QEvent::HideToParent:
        ++counts_.hide;
        break;
      case QEvent::ZOrderChange:
        ++counts_.z_order;
        break;
      default:
        break;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  HudWidgetEventCounts counts_;
};

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
     RefreshPolicyRejectsIdenticalPresentationAndBounds) {
  const quader::QtViewportFlyNavigationHudPresentation active{
      .crosshair_visible = true,
      .speed_visible = true,
      .speed_label = "Speed 6.5",
  };
  EXPECT_FALSE(
      quader::qt_viewport_fly_navigation_hud_presentation_refresh_required(
          active, active));
  EXPECT_TRUE(
      quader::qt_viewport_fly_navigation_hud_presentation_refresh_required(
          active,
          quader::QtViewportFlyNavigationHudPresentation{
              .crosshair_visible = true,
              .speed_visible = true,
              .speed_label = "Speed 7.0",
          }));

  const QRect bounds{0, 0, 320, 200};
  EXPECT_FALSE(quader::qt_viewport_fly_navigation_hud_bounds_refresh_required(
      bounds, bounds));
  EXPECT_TRUE(quader::qt_viewport_fly_navigation_hud_bounds_refresh_required(
      bounds, QRect{0, 0, 321, 200}));
}

TEST(QtViewportFlyNavigationHudTests,
     StableCallsDoNotRepeatWidgetGeometryVisibilityOrZOrderWork) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-fly-navigation-hud-refresh-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QWidget parent;
  parent.resize(320, 200);
  parent.setAttribute(Qt::WA_DontShowOnScreen, true);
  parent.show();
  quader::QtViewportFlyNavigationHud hud(
      parent, quader::ViewportPaneId::Perspective);

  QWidget *crosshair = parent.findChild<QWidget *>(
      QStringLiteral("viewportFlyCrosshair_perspective"));
  QLabel *speed_label = parent.findChild<QLabel *>(
      QStringLiteral("viewportFlySpeed_perspective"));
  ASSERT_NE(crosshair, nullptr);
  ASSERT_NE(speed_label, nullptr);

  HudWidgetEventCounter events;
  crosshair->installEventFilter(&events);
  speed_label->installEventFilter(&events);
  const quader::QtViewportFlyNavigationHudPresentation active{
      .crosshair_visible = true,
      .speed_visible = true,
      .speed_label = "Speed 6.5",
  };
  hud.present(active);
  QCoreApplication::processEvents();

  const HudWidgetEventCounts active_counts = events.counts();
  EXPECT_GT(active_counts.show, 0);
  EXPECT_GT(active_counts.z_order, 0);
  for (int repetition = 0; repetition < 64; ++repetition) {
    hud.present(active);
    hud.set_bounds(parent.rect());
  }
  QCoreApplication::processEvents();
  EXPECT_EQ(events.counts(), active_counts);

  hud.present(true, false, 6.5f, nullptr);
  QCoreApplication::processEvents();
  const HudWidgetEventCounts raw_input_counts = events.counts();
  for (int repetition = 0; repetition < 64; ++repetition) {
    hud.present(true, false, 6.5f, nullptr);
  }
  QCoreApplication::processEvents();
  EXPECT_EQ(events.counts(), raw_input_counts);

  quader::QtViewportFlyNavigationHudPresentation relabeled = active;
  relabeled.speed_label = "Speed 12.5";
  hud.present(relabeled);
  QCoreApplication::processEvents();
  const HudWidgetEventCounts relabeled_counts = events.counts();
  EXPECT_EQ(relabeled_counts.show, active_counts.show);
  EXPECT_EQ(relabeled_counts.hide, active_counts.hide);
  EXPECT_EQ(relabeled_counts.z_order, active_counts.z_order);
  for (int repetition = 0; repetition < 64; ++repetition) {
    hud.present(relabeled);
  }
  QCoreApplication::processEvents();
  EXPECT_EQ(events.counts(), relabeled_counts);

  hud.hide();
  QCoreApplication::processEvents();
  const HudWidgetEventCounts hidden_counts = events.counts();
  EXPECT_GT(hidden_counts.hide, relabeled_counts.hide);
  for (int repetition = 0; repetition < 64; ++repetition) {
    hud.hide();
  }
  QCoreApplication::processEvents();
  EXPECT_EQ(events.counts(), hidden_counts);

  hud.present(true, false, 6.5f, nullptr);
  QCoreApplication::processEvents();
  EXPECT_GT(events.counts().show, hidden_counts.show);
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
