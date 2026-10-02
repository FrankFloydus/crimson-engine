////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_retained_message_box.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QLayout>
#include <QPalette>
#include <QPixmap>
#include <QProxyStyle>
#include <QPushButton>
#include <QScopeGuard>
#include <QTimer>
#include <QWidget>

using namespace quader;

namespace {

class PaletteMessageIconStyle final : public QProxyStyle {
public:
  PaletteMessageIconStyle() : QProxyStyle(QStringLiteral("fusion")) {}

  QIcon standardIcon(StandardPixmap standard_pixmap,
                     const QStyleOption *option,
                     const QWidget *widget) const override {
    switch (standard_pixmap) {
    case SP_MessageBoxCritical:
    case SP_MessageBoxWarning:
    case SP_MessageBoxQuestion:
    case SP_MessageBoxInformation: {
      QPixmap pixmap(24, 24);
      pixmap.fill(QApplication::palette().color(QPalette::Text));
      return QIcon(pixmap);
    }
    default:
      return QProxyStyle::standardIcon(standard_pixmap, option, widget);
    }
  }
};

} // namespace

TEST(QtRetainedMessageBoxTests,
     ReusesNativeWindowLayoutAndButtonsAcrossPresentations) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const bool native_siblings_disabled = QCoreApplication::testAttribute(
      Qt::AA_DontCreateNativeWidgetSiblings);
  const auto restore_native_siblings = qScopeGuard([native_siblings_disabled]() {
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings,
                                   native_siblings_disabled);
  });
  QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
  int argc = 1;
  char application_name[] = "quader-retained-message-box-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setStyle(QStringLiteral("Fusion"));
  QtUiScaleApplicationController scale_controller(application);

  QWidget owner;
  owner.resize(640, 480);
  QWidget sibling(&owner);
  sibling.setGeometry(10, 10, 120, 80);
  owner.show();
  QCoreApplication::processEvents();
  ASSERT_NE(owner.internalWinId(), WId{});
  ASSERT_EQ(sibling.internalWinId(), WId{});

  QtRetainedMessageBox message_box(&owner);
  QPushButton *const primary = message_box.retained_button(
      QtRetainedMessageBoxButton::Primary);
  QPushButton *const secondary = message_box.retained_button(
      QtRetainedMessageBoxButton::Secondary);
  QPushButton *const tertiary = message_box.retained_button(
      QtRetainedMessageBoxButton::Tertiary);
  ASSERT_NE(primary, nullptr);
  ASSERT_NE(secondary, nullptr);
  ASSERT_NE(tertiary, nullptr);
  const QtRetainedMessageBoxPresentation first{
      .icon = QMessageBox::Critical,
      .object_name = QStringLiteral("first_message_box"),
      .title = QStringLiteral("First title"),
      .text = QStringLiteral("First text"),
      .informative_text = QStringLiteral("First details"),
      .buttons =
          {
              {
                  .button = QtRetainedMessageBoxButton::Primary,
                  .object_name = QStringLiteral("first.primary"),
                  .label = QStringLiteral("Proceed"),
                  .role = QMessageBox::AcceptRole,
              },
              {
                  .button = QtRetainedMessageBoxButton::Secondary,
                  .object_name = QStringLiteral("first.secondary"),
                  .label = QStringLiteral("Remove"),
                  .role = QMessageBox::DestructiveRole,
              },
              {
                  .button = QtRetainedMessageBoxButton::Tertiary,
                  .object_name = QStringLiteral("first.tertiary"),
                  .label = QStringLiteral("Cancel"),
                  .role = QMessageBox::RejectRole,
              },
          },
      .default_button = QtRetainedMessageBoxButton::Primary,
      .escape_button = QtRetainedMessageBoxButton::Tertiary,
      .close_button = QtRetainedMessageBoxButton::Tertiary,
  };
  ASSERT_TRUE(message_box.set_presentation(first));
  message_box.prewarm();
  EXPECT_FALSE(message_box.isVisible());
  EXPECT_TRUE(message_box.testAttribute(Qt::WA_WState_Created));
  QLayout *const retained_layout = message_box.layout();
  ASSERT_NE(retained_layout, nullptr);
  const WId retained_window_id = message_box.internalWinId();
  ASSERT_NE(retained_window_id, WId{});
  const auto expect_native_isolation = [&]() {
    EXPECT_EQ(message_box.internalWinId(), retained_window_id);
    EXPECT_EQ(sibling.internalWinId(), WId{});
  };
  expect_native_isolation();
  auto *const icon_label = message_box.findChild<QLabel *>(
      QStringLiteral("qt_msgboxex_icon_label"));
  ASSERT_NE(icon_label, nullptr);
  ASSERT_FALSE(icon_label->pixmap().isNull());
  const QSizeF unscaled_icon_size =
      icon_label->pixmap().deviceIndependentSize();
  ASSERT_TRUE(scale_controller.apply_factor(2.0F));
  message_box.apply_ui_scale(scale_controller.metrics());
  expect_native_isolation();
  EXPECT_EQ(message_box.layout(), retained_layout);
  EXPECT_GT(icon_label->pixmap().deviceIndependentSize().width(),
            unscaled_icon_size.width());
  const qint64 scaled_icon_key = icon_label->pixmap().cacheKey();
  EXPECT_FALSE(scale_controller.apply_factor(2.0F));
  message_box.apply_ui_scale(scale_controller.metrics());
  EXPECT_EQ(icon_label->pixmap().cacheKey(), scaled_icon_key)
      << "one scale revision must not churn retained standard icons";
  ASSERT_TRUE(scale_controller.apply_factor(1.0F));
  message_box.apply_ui_scale(scale_controller.metrics());
  EXPECT_EQ(icon_label->pixmap().deviceIndependentSize(), unscaled_icon_size);
  expect_native_isolation();

  QTimer::singleShot(0, &message_box, [&]() {
    expect_native_isolation();
    EXPECT_EQ(message_box.objectName(), first.object_name);
    EXPECT_EQ(message_box.windowTitle(), first.title);
    EXPECT_EQ(message_box.text(), first.text);
    EXPECT_EQ(message_box.informativeText(), first.informative_text);
    EXPECT_EQ(message_box.presented_icon(), QMessageBox::Critical);
    EXPECT_EQ(message_box.defaultButton(), primary);
    EXPECT_EQ(message_box.escapeButton(), tertiary);
    EXPECT_EQ(message_box.buttonRole(primary), QMessageBox::AcceptRole);
    EXPECT_EQ(message_box.buttonRole(secondary),
              QMessageBox::DestructiveRole);
    EXPECT_EQ(message_box.buttonRole(tertiary), QMessageBox::RejectRole);
    secondary->click();
  });
  EXPECT_EQ(message_box.present(first),
            QtRetainedMessageBoxButton::Secondary);
  EXPECT_TRUE(message_box.testAttribute(Qt::WA_WState_Created));
  expect_native_isolation();

  const QtRetainedMessageBoxPresentation second{
      .icon = QMessageBox::Question,
      .object_name = QStringLiteral("second_message_box"),
      .title = QStringLiteral("Second title"),
      .text = QStringLiteral("Second text"),
      .buttons =
          {
              {
                  .button = QtRetainedMessageBoxButton::Primary,
                  .object_name = QStringLiteral("second.primary"),
                  .label = QStringLiteral("Inspect"),
                  .role = QMessageBox::HelpRole,
              },
              {
                  .button = QtRetainedMessageBoxButton::Secondary,
                  .object_name = QStringLiteral("second.secondary"),
                  .label = QStringLiteral("Keep Editing"),
                  .role = QMessageBox::RejectRole,
              },
          },
      .default_button = QtRetainedMessageBoxButton::Secondary,
      .escape_button = QtRetainedMessageBoxButton::Secondary,
      .close_button = QtRetainedMessageBoxButton::Secondary,
  };
  QTimer::singleShot(0, &message_box, [&]() {
    expect_native_isolation();
    EXPECT_EQ(message_box.layout(), retained_layout);
    EXPECT_EQ(message_box.retained_button(
                  QtRetainedMessageBoxButton::Primary),
              primary);
    EXPECT_EQ(message_box.retained_button(
                  QtRetainedMessageBoxButton::Secondary),
              secondary);
    EXPECT_EQ(message_box.retained_button(
                  QtRetainedMessageBoxButton::Tertiary),
              tertiary);
    EXPECT_EQ(message_box.objectName(), second.object_name);
    EXPECT_EQ(message_box.presented_icon(), QMessageBox::Question);
    EXPECT_EQ(message_box.buttonRole(primary), QMessageBox::HelpRole);
    EXPECT_EQ(message_box.buttonRole(secondary), QMessageBox::RejectRole);
    EXPECT_EQ(message_box.defaultButton(), secondary);
    EXPECT_EQ(message_box.escapeButton(), secondary);
    EXPECT_FALSE(tertiary->isVisible());
    message_box.close();
  });
  EXPECT_EQ(message_box.present(second),
            QtRetainedMessageBoxButton::Secondary);
  EXPECT_TRUE(message_box.testAttribute(Qt::WA_WState_Created));
  expect_native_isolation();
  EXPECT_EQ(message_box.layout(), retained_layout);
}

TEST(QtRetainedMessageBoxTests,
     RefreshesRetainedStyleIconsAcrossApplicationPaletteChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-retained-message-box-palette-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(new PaletteMessageIconStyle);

  QPalette palette = QApplication::palette();
  palette.setColor(QPalette::Text, QColor(211, 37, 53));
  QApplication::setPalette(palette);

  QtRetainedMessageBox message_box;
  ASSERT_TRUE(message_box.set_presentation(
      QtRetainedMessageBoxPresentation{
          .icon = QMessageBox::Critical,
          .object_name = QStringLiteral("palette_message_box"),
          .title = QStringLiteral("Palette"),
          .text = QStringLiteral("Palette-aware icon"),
          .buttons =
              {{
                  .button = QtRetainedMessageBoxButton::Primary,
                  .object_name = QStringLiteral("palette_message_box.ok"),
                  .label = QStringLiteral("OK"),
                  .role = QMessageBox::AcceptRole,
              }},
          .default_button = QtRetainedMessageBoxButton::Primary,
          .escape_button = QtRetainedMessageBoxButton::Primary,
          .close_button = QtRetainedMessageBoxButton::Primary,
      }));
  message_box.prewarm();
  auto *const icon_label = message_box.findChild<QLabel *>(
      QStringLiteral("qt_msgboxex_icon_label"));
  ASSERT_NE(icon_label, nullptr);
  ASSERT_FALSE(icon_label->pixmap().isNull());
  const WId retained_window_id = message_box.winId();
  QLayout *const retained_layout = message_box.layout();
  const qint64 original_icon_key = icon_label->pixmap().cacheKey();
  EXPECT_EQ(icon_label->pixmap().toImage().pixelColor(0, 0),
            QColor(211, 37, 53));

  palette.setColor(QPalette::Text, QColor(29, 197, 83));
  QApplication::setPalette(palette);
  QCoreApplication::processEvents();

  ASSERT_FALSE(icon_label->pixmap().isNull());
  EXPECT_NE(icon_label->pixmap().cacheKey(), original_icon_key);
  EXPECT_EQ(icon_label->pixmap().toImage().pixelColor(0, 0),
            QColor(29, 197, 83));
  EXPECT_EQ(message_box.winId(), retained_window_id);
  EXPECT_EQ(message_box.layout(), retained_layout);
}
