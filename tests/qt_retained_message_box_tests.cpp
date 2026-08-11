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
#include <QLayout>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

using namespace quader;

TEST(QtRetainedMessageBoxTests,
     ReusesNativeWindowLayoutAndButtonsAcrossPresentations) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-retained-message-box-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setStyle(QStringLiteral("Fusion"));

  QWidget owner;
  owner.resize(640, 480);
  owner.show();
  QCoreApplication::processEvents();

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
  const WId retained_window_id = message_box.winId();

  QTimer::singleShot(0, &message_box, [&]() {
    EXPECT_EQ(message_box.winId(), retained_window_id);
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
  EXPECT_EQ(message_box.winId(), retained_window_id);

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
    EXPECT_EQ(message_box.winId(), retained_window_id);
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
  EXPECT_EQ(message_box.winId(), retained_window_id);
  EXPECT_EQ(message_box.layout(), retained_layout);
}
