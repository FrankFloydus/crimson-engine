////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app_frame_clock.hpp"
#include "app/app_frame_presentation.hpp"
#include "ui/qt_unsaved_confirmation_dialog.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QMenu>
#include <QPaintEvent>
#include <QPushButton>
#include <QTimer>
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

/// Records the first real paint while allowing the modal loop to continue.
class FirstPaintEventProbe final : public QObject {
public:
  explicit FirstPaintEventProbe(AppFramePresentationGate &gate)
      : gate_(&gate) {}

  [[nodiscard]] bool paint_seen() const noexcept { return paint_seen_; }
  [[nodiscard]] bool gate_blocked_during_paint() const noexcept {
    return gate_blocked_during_paint_;
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    (void)watched;
    if (!paint_seen_ && event != nullptr &&
        event->type() == QEvent::Paint) {
      paint_seen_ = true;
      gate_blocked_during_paint_ = gate_->blocks_frame_submission();
    }
    return false;
  }

private:
  AppFramePresentationGate *gate_ = nullptr;
  bool paint_seen_ = false;
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

TEST(QtUnsavedConfirmationDialogTests,
     ReusesNativeDialogAndButtonsWhileRefreshingEachPresentation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-unsaved-confirmation-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setStyle(QStringLiteral("Fusion"));

  QWidget main_window;
  main_window.resize(640, 480);
  main_window.show();
  QCoreApplication::processEvents();

  QtUnsavedConfirmationDialog confirmation(&main_window);
  QMessageBox *const retained_dialog = &confirmation;
  QPushButton *const save_button = confirmation.findChild<QPushButton *>(
      QStringLiteral("unsavedConfirmation.save"));
  QPushButton *const discard_button = confirmation.findChild<QPushButton *>(
      QStringLiteral("unsavedConfirmation.discard"));
  QPushButton *const cancel_button = confirmation.findChild<QPushButton *>(
      QStringLiteral("unsavedConfirmation.cancel"));
  ASSERT_NE(save_button, nullptr);
  ASSERT_NE(discard_button, nullptr);
  ASSERT_NE(cancel_button, nullptr);

  confirmation.prewarm();
  EXPECT_FALSE(confirmation.isVisible());
  EXPECT_TRUE(confirmation.testAttribute(Qt::WA_WState_Created));
  EXPECT_EQ(confirmation.presented_icon(), QMessageBox::Warning);

  const QtUnsavedConfirmationPresentation project_presentation{
      .object_name = QStringLiteral("project_unsaved_confirmation"),
      .title = QStringLiteral("Unsaved Changes"),
      .message = QStringLiteral("Save the project before closing?"),
      .save_label = QStringLiteral("Save Project"),
      .discard_label = QStringLiteral("Discard Project"),
      .cancel_label = QStringLiteral("Keep Editing"),
  };
  QTimer::singleShot(0, &confirmation, [&]() {
    EXPECT_EQ(confirmation.windowTitle(), project_presentation.title);
    EXPECT_EQ(confirmation.text(), project_presentation.message);
    EXPECT_EQ(save_button->text(), project_presentation.save_label);
    EXPECT_EQ(discard_button->text(), project_presentation.discard_label);
    EXPECT_EQ(cancel_button->text(), project_presentation.cancel_label);
    EXPECT_EQ(confirmation.defaultButton(), save_button);
    EXPECT_EQ(confirmation.escapeButton(), cancel_button);
    discard_button->click();
  });
  EXPECT_EQ(confirmation.present(project_presentation),
            QtUnsavedConfirmationDecision::Discard);

  const QtUnsavedConfirmationPresentation scene_presentation{
      .object_name = QStringLiteral("scene_unsaved_confirmation"),
      .title = QStringLiteral("Unsaved Scene"),
      .message = QStringLiteral("A different scene is dirty."),
      .save_label = QStringLiteral("Save Scene"),
      .discard_label = QStringLiteral("Discard Scene"),
      .cancel_label = QStringLiteral("Cancel"),
  };
  QTimer::singleShot(0, &confirmation, [&]() {
    EXPECT_EQ(&confirmation, retained_dialog);
    EXPECT_EQ(confirmation.objectName(), scene_presentation.object_name);
    EXPECT_EQ(confirmation.windowTitle(), scene_presentation.title);
    EXPECT_EQ(confirmation.text(), scene_presentation.message);
    EXPECT_EQ(save_button->text(), scene_presentation.save_label);
    EXPECT_EQ(discard_button->text(), scene_presentation.discard_label);
    EXPECT_EQ(cancel_button->text(), scene_presentation.cancel_label);
    EXPECT_EQ(confirmation.defaultButton(), save_button);
    EXPECT_EQ(confirmation.escapeButton(), cancel_button);
    EXPECT_EQ(confirmation.findChild<QPushButton *>(
                  QStringLiteral("unsavedConfirmation.save")),
              save_button);
    EXPECT_EQ(confirmation.findChild<QPushButton *>(
                  QStringLiteral("unsavedConfirmation.discard")),
              discard_button);
    EXPECT_EQ(confirmation.findChild<QPushButton *>(
                  QStringLiteral("unsavedConfirmation.cancel")),
              cancel_button);
    cancel_button->click();
  });
  EXPECT_EQ(confirmation.present(scene_presentation),
            QtUnsavedConfirmationDecision::Cancel);
}

TEST(AppFramePresentationTests,
     RealMessageBoxPaintReleasesFramesWhileModalRemainsVisible) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-real-message-box-presentation-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setStyle(QStringLiteral("Fusion"));
  AppFramePresentationGate gate(application);

  QWidget main_window;
  main_window.resize(640, 480);
  main_window.show();
  QCoreApplication::processEvents();
  ASSERT_FALSE(gate.blocks_frame_submission());

  QtUnsavedConfirmationDialog confirmation(&main_window);
  confirmation.prewarm();
  FirstPaintEventProbe paint_probe(gate);
  confirmation.installEventFilter(&paint_probe);
  QPushButton *const cancel_button = confirmation.findChild<QPushButton *>(
      QStringLiteral("unsavedConfirmation.cancel"));
  ASSERT_NE(cancel_button, nullptr);

  int submitted_frames = 0;
  bool resumed_while_visible = false;
  QTimer frame_surrogate;
  frame_surrogate.setInterval(1);
  QObject::connect(&frame_surrogate, &QTimer::timeout, &confirmation, [&]() {
    if (!paint_probe.paint_seen() || gate.blocks_frame_submission()) {
      return;
    }
    resumed_while_visible = confirmation.isVisible();
    if (resumed_while_visible) {
      ++submitted_frames;
    }
    cancel_button->click();
  });
  frame_surrogate.start();
  QTimer::singleShot(2000, &confirmation,
                     [cancel_button]() { cancel_button->click(); });

  EXPECT_EQ(confirmation.present(QtUnsavedConfirmationPresentation{
                .object_name =
                    QStringLiteral("project_unsaved_confirmation"),
                .title = QStringLiteral("Unsaved Changes"),
                .message = QStringLiteral("Save before closing?"),
                .save_label = QStringLiteral("Save"),
                .discard_label = QStringLiteral("Discard"),
                .cancel_label = QStringLiteral("Cancel"),
            }),
            QtUnsavedConfirmationDecision::Cancel);
  frame_surrogate.stop();

  EXPECT_TRUE(paint_probe.paint_seen());
  EXPECT_TRUE(paint_probe.gate_blocked_during_paint());
  EXPECT_TRUE(resumed_while_visible);
  EXPECT_EQ(submitted_frames, 1);
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
