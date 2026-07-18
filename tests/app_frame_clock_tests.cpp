////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app_frame_clock.hpp"
#include "app/app_frame_presentation.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QMenu>
#include <QPaintEvent>
#include <QWidget>

#include <chrono>

using namespace quader;

namespace {

/**
 * Records whether frame submission stayed blocked inside a dialog's first
 * paint handler.
 */
class FirstPaintProbeDialog final : public QDialog {
public:
  explicit FirstPaintProbeDialog(AppFramePresentationGate &gate)
      : gate_(&gate) {}

  [[nodiscard]] bool gate_blocked_during_paint() const noexcept {
    return gate_blocked_during_paint_;
  }

protected:
  void paintEvent(QPaintEvent *event) override {
    gate_blocked_during_paint_ = gate_->blocks_frame_submission();
    QDialog::paintEvent(event);
  }

private:
  AppFramePresentationGate *gate_ = nullptr;
  bool gate_blocked_during_paint_ = false;
};

void deliver_first_paint(QWidget &widget) {
  QPaintEvent paint_event(widget.rect());
  QCoreApplication::sendEvent(&widget, &paint_event);
}

} // namespace

TEST(AppFrameClockTests, ProducesClampedFrameTimingAndMonotonicFrameIndex) {
    using Clock = AppFrameClock::Clock;

    AppFrameClock clock;
    const Clock::time_point start{};
    clock.reset(start);

    const FrameTiming first = clock.tick(start + std::chrono::milliseconds(16));
    EXPECT_EQ(first.frame_index, 0u);
    EXPECT_NEAR(first.raw_delta_seconds, 0.016, 0.000001);
    EXPECT_NEAR(first.delta_seconds, 0.016, 0.000001);
    EXPECT_NEAR(first.elapsed_seconds, 0.016, 0.000001);

    const FrameTiming clamped = clock.tick(start + std::chrono::seconds(2));
    EXPECT_EQ(clamped.frame_index, 1u);
    EXPECT_NEAR(clamped.raw_delta_seconds, 1.984, 0.000001);
    EXPECT_NEAR(clamped.delta_seconds, AppFrameClock::kMaxDeltaSeconds,
                0.000001);
    EXPECT_NEAR(clamped.elapsed_seconds, 2.0, 0.000001);
}

TEST(AppFramePresentationTests,
     PausesOnlyUntilEachTopLevelTransientFinishesItsFirstPaint) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-app-frame-presentation-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  AppFramePresentationGate gate(application);

  QWidget main_window;
  main_window.setAttribute(Qt::WA_DontShowOnScreen, true);
  main_window.show();
  EXPECT_FALSE(gate.blocks_frame_submission());

  QWidget child(&main_window);
  child.show();
  EXPECT_FALSE(gate.blocks_frame_submission());

  FirstPaintProbeDialog dialog(gate);
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.setModal(true);
  dialog.show();
  EXPECT_TRUE(gate.blocks_frame_submission());

  int submitted_frames = 0;
  const auto submit_if_ready = [&]() {
    if (!gate.blocks_frame_submission()) {
      ++submitted_frames;
    }
  };
  submit_if_ready();
  EXPECT_EQ(submitted_frames, 0);

  deliver_first_paint(dialog);
  EXPECT_TRUE(dialog.gate_blocked_during_paint());
  EXPECT_TRUE(gate.blocks_frame_submission());
  QCoreApplication::processEvents();
  EXPECT_FALSE(gate.blocks_frame_submission());
  EXPECT_TRUE(dialog.isVisible());
  submit_if_ready();
  EXPECT_EQ(submitted_frames, 1);

  QMenu popup;
  popup.setAttribute(Qt::WA_DontShowOnScreen, true);
  popup.addAction(QStringLiteral("Choice"));
  popup.show();
  EXPECT_TRUE(gate.blocks_frame_submission());
  deliver_first_paint(popup);
  EXPECT_TRUE(gate.blocks_frame_submission());
  QCoreApplication::processEvents();
  EXPECT_FALSE(gate.blocks_frame_submission());
  EXPECT_TRUE(popup.isVisible());
  popup.close();

  QDialog outer;
  outer.setAttribute(Qt::WA_DontShowOnScreen, true);
  QDialog inner;
  inner.setAttribute(Qt::WA_DontShowOnScreen, true);
  outer.show();
  inner.show();
  EXPECT_TRUE(gate.blocks_frame_submission());
  outer.close();
  EXPECT_TRUE(gate.blocks_frame_submission());
  inner.close();
  EXPECT_FALSE(gate.blocks_frame_submission());
}
