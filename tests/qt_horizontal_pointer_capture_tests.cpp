////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_horizontal_pointer_capture.hpp"

#include <QApplication>
#include <QCursor>
#include <QWidget>

#include <memory>

namespace {

TEST(QtHorizontalPointerWrapTests, RepeatedWrapsPreserveTravelInBothDirections) {
  quader::QtHorizontalPointerWrap motion;
  const QRect bounds(0, 0, 101, 101);
  motion.start(QPointF(50, 50), bounds);
  double total = 0.0;
  for (int index = 0; index < 100; ++index) {
    const auto right = motion.move(QPointF(100, 50));
    ASSERT_TRUE(right.warp_target.has_value());
    EXPECT_EQ(*right.warp_target, QPoint(50, 50));
    total += right.delta_x;
    EXPECT_DOUBLE_EQ(motion.move(QPointF(*right.warp_target)).delta_x, 0.0);
  }
  EXPECT_DOUBLE_EQ(total, 5000.0);
  for (int index = 0; index < 100; ++index) {
    const auto left = motion.move(QPointF(0, 50));
    ASSERT_TRUE(left.warp_target.has_value());
    total += left.delta_x;
    EXPECT_DOUBLE_EQ(motion.move(QPointF(*left.warp_target)).delta_x, 0.0);
  }
  EXPECT_DOUBLE_EQ(total, 0.0);
}

TEST(QtHorizontalPointerWrapTests, DelayedWarpDoesNotRewindPhysicalMotion) {
  quader::QtHorizontalPointerWrap motion;
  motion.start(QPointF(50, 50), QRect(0, 0, 101, 101));
  ASSERT_TRUE(motion.move(QPointF(100, 50)).warp_target.has_value());
  EXPECT_DOUBLE_EQ(motion.move(QPointF(54, 50)).delta_x, 4.0);
  EXPECT_DOUBLE_EQ(motion.move(QPointF(50, 50)).delta_x, 0.0);
  EXPECT_DOUBLE_EQ(motion.move(QPointF(57, 50)).delta_x, 3.0);
}

TEST(QtHorizontalPointerWrapTests, CoalescedWarpEchoCannotStopFutureWrapping) {
  quader::QtHorizontalPointerWrap motion;
  motion.start(QPointF(50, 50), QRect(0, 0, 101, 101));
  ASSERT_TRUE(motion.move(QPointF(100, 50)).warp_target.has_value());
  for (int index = 0; index < 100; ++index) {
    EXPECT_DOUBLE_EQ(motion.move(QPointF(54, 50)).delta_x, 4.0);
    const auto sample = motion.move(QPointF(100, 50));
    EXPECT_DOUBLE_EQ(sample.delta_x, 46.0);
    ASSERT_TRUE(sample.warp_target.has_value());
    EXPECT_EQ(*sample.warp_target, QPoint(50, 50));
  }
  EXPECT_DOUBLE_EQ(motion.move(QPointF(50, 50)).delta_x, 0.0);
  EXPECT_DOUBLE_EQ(motion.move(QPointF(47, 50)).delta_x, -3.0);
}

TEST(QtHorizontalPointerWrapTests, StaleEdgeSamplesCannotAddPhantomTravel) {
  quader::QtHorizontalPointerWrap motion;
  motion.start(QPointF(50, 50), QRect(0, 0, 101, 101));
  const auto first = motion.move(QPointF(100, 50));
  EXPECT_DOUBLE_EQ(first.delta_x, 50.0);
  ASSERT_TRUE(first.warp_target.has_value());
  for (int index = 0; index < 100; ++index) {
    const auto stale = motion.move(QPointF(100 + index, 50));
    EXPECT_DOUBLE_EQ(stale.delta_x, 0.0);
    EXPECT_FALSE(stale.warp_target.has_value());
  }
  EXPECT_DOUBLE_EQ(motion.move(QPointF(54, 50)).delta_x, 4.0);
  EXPECT_DOUBLE_EQ(motion.move(QPointF(50, 50)).delta_x, 0.0);
  EXPECT_DOUBLE_EQ(motion.move(QPointF(52, 50)).delta_x, -2.0);
}

TEST(QtHorizontalPointerWrapTests, VerticalBoundaryRecentersWithoutNumericTravel) {
  quader::QtHorizontalPointerWrap motion;
  motion.start(QPointF(50, 50), QRect(0, 0, 101, 101));
  const auto vertical = motion.move(QPointF(50, 100));
  EXPECT_DOUBLE_EQ(vertical.delta_x, 0.0);
  ASSERT_TRUE(vertical.warp_target.has_value());
  EXPECT_EQ(*vertical.warp_target, QPoint(50, 50));
  EXPECT_DOUBLE_EQ(motion.move(QPointF(50, 50)).delta_x, 0.0);
  EXPECT_DOUBLE_EQ(motion.move(QPointF(44, 50)).delta_x, -6.0);
}

TEST(QtHorizontalPointerWrapTests, RestartDiscardsOldWarpAndCoordinateBaseline) {
  quader::QtHorizontalPointerWrap motion;
  motion.start(QPointF(50, 50), QRect(0, 0, 101, 101));
  ASSERT_TRUE(motion.move(QPointF(100, 50)).warp_target.has_value());
  motion.clear();
  motion.start(QPointF(48, 50), QRect());
  EXPECT_DOUBLE_EQ(motion.move(QPointF(50, 50)).delta_x, 2.0);
  const auto unbounded = motion.move(QPointF(10000, 50));
  EXPECT_DOUBLE_EQ(unbounded.delta_x, 9950.0);
  EXPECT_FALSE(unbounded.warp_target.has_value());
}

TEST(QtHorizontalPointerCaptureTests,
     HiddenWidgetTracksDeltasAndCleanupWithoutMovingDesktopCursor) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "horizontal-pointer-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  auto window = std::make_unique<QWidget>();
  window->setAttribute(Qt::WA_DontShowOnScreen);
  QWidget child(window.get());
  const QPoint desktop_cursor = QCursor::pos();
  quader::QtHorizontalPointerCapture capture;

  ASSERT_TRUE(capture.start(child, QPointF(10, 10), QPointF(5, 5)));
  EXPECT_TRUE(capture.is_active());
  EXPECT_DOUBLE_EQ(capture.delta(QPointF(10010, 10)), 10000.0);
  EXPECT_DOUBLE_EQ(capture.delta(QPointF(-10, 10)), -10020.0);
  EXPECT_EQ(QCursor::pos(), desktop_cursor);
  EXPECT_NE(QWidget::mouseGrabber(), &child);
  capture.stop();
  EXPECT_FALSE(capture.is_active());
  EXPECT_DOUBLE_EQ(capture.delta(QPointF(0, 0)), 0.0);
  EXPECT_EQ(QCursor::pos(), desktop_cursor);

  ASSERT_TRUE(capture.start(child, QPointF(100, 10), QPointF(5, 5)));
  EXPECT_DOUBLE_EQ(capture.delta(QPointF(101, 10)), 1.0);
  capture.stop();
}

TEST(QtHorizontalPointerCaptureTests, DestroyedHiddenReceiverIsSafeToStop) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "horizontal-pointer-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::QtHorizontalPointerCapture capture;
  auto widget = std::make_unique<QWidget>();
  widget->setAttribute(Qt::WA_DontShowOnScreen);
  ASSERT_TRUE(capture.start(*widget, QPointF(10, 10), QPointF(5, 5)));
  widget.reset();
  EXPECT_DOUBLE_EQ(capture.delta(QPointF(40, 10)), 0.0);
  capture.stop();
  EXPECT_FALSE(capture.is_active());
}

} // namespace
